#include "loopback_http_server.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <string>
#include <system_error>
#include <utility>

namespace luil::testing {
    namespace {
        // 머리의 상한이다. 밖에서 오는 값이라 상한이 없으면 끝없이 보내는 상대
        // 하나가 test 프로세스의 메모리를 다 먹는다.
        constexpr std::size_t max_header_bytes { 64 * 1024 };
        // 몸통의 상한이다. 같은 이유이고, 우리가 받아 볼 일이 있는 크기보다 훨씬 넉넉하다.
        constexpr std::size_t max_body_bytes { 16 * 1024 * 1024 };
        // recv가 막히는 길이의 상한이다. stop()은 shutdown으로 곧바로 깨우지만,
        // 아무것도 보내지 않는 상대에 걸린 연결이 이 간격마다 stop 깃발을 다시 본다.
        constexpr std::chrono::milliseconds receive_timeout { 50 };
        // 받아들이는 thread가 stop 깃발을 다시 보는 간격이다.
        constexpr std::chrono::microseconds accept_poll { 50 * 1000 };
        constexpr std::size_t transfer_block_bytes { 64 * 1024 };

        [[nodiscard]] char lowered(const char character) noexcept
        {
            return (character >= 'A' && character <= 'Z') ? static_cast<char>(character - 'A' + 'a') : character;
        }

        [[nodiscard]] std::string to_lower(const std::string_view text)
        {
            std::string lower {};
            lower.reserve(text.size());
            for (const char character : text)
                lower.push_back(lowered(character));
            return lower;
        }

        // 앞뒤의 공백과 탭을 뗀다 (머리 값의 규칙이다).
        [[nodiscard]] std::string_view trimmed(std::string_view text) noexcept
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                text.remove_suffix(1);
            return text;
        }

        [[nodiscard]] bool contains_token(const std::string_view text, const std::string_view lower_case_token)
        {
            return to_lower(text).find(lower_case_token) != std::string::npos;
        }

        // status에 맞는 표준 문구다. 모르는 값은 "Unknown"이다 — 문구 자체를 보는
        // 축은 없고, 상태 줄에 무언가는 있어야 한다.
        [[nodiscard]] std::string_view reason_for(const int status) noexcept
        {
            switch (status)
            {
            case 100:
                return "Continue";
            case 200:
                return "OK";
            case 201:
                return "Created";
            case 204:
                return "No Content";
            case 301:
                return "Moved Permanently";
            case 302:
                return "Found";
            case 303:
                return "See Other";
            case 304:
                return "Not Modified";
            case 307:
                return "Temporary Redirect";
            case 308:
                return "Permanent Redirect";
            case 400:
                return "Bad Request";
            case 401:
                return "Unauthorized";
            case 403:
                return "Forbidden";
            case 404:
                return "Not Found";
            case 408:
                return "Request Timeout";
            case 413:
                return "Content Too Large";
            case 431:
                return "Request Header Fields Too Large";
            case 500:
                return "Internal Server Error";
            case 503:
                return "Service Unavailable";
            default:
                return "Unknown";
            }
        }

        [[nodiscard]] std::string hex_of(std::size_t value)
        {
            if (value == 0)
                return "0";
            static constexpr char digits[] { "0123456789abcdef" };
            std::string text {};
            while (value > 0)
            {
                text.push_back(digits[value & 0xFu]);
                value >>= 4;
            }
            std::reverse(text.begin(), text.end());
            return text;
        }

        // 다 보내거나 실패할 때까지 민다. send는 청한 만큼을 다 받아 주지 않는다.
        [[nodiscard]] bool send_all(const std::uintptr_t client, const void* data, const std::size_t size) noexcept
        {
            const char* cursor { static_cast<const char*>(data) };
            std::size_t remaining { size };
            while (remaining > 0)
            {
                const std::size_t block { std::min<std::size_t>(remaining, transfer_block_bytes) };
                const int sent { loopback_socket::send(client, cursor, block) };
                if (sent <= 0)
                    return false;
                cursor += sent;
                remaining -= static_cast<std::size_t>(sent);
            }
            return true;
        }

        [[nodiscard]] bool send_text(const std::uintptr_t client, const std::string_view text) noexcept
        {
            return send_all(client, text.data(), text.size());
        }

        // 흔한 끝맺음이다. 보낸 것을 마저 흘려보내고 닫는다.
        void close_gracefully(const std::uintptr_t client) noexcept
        {
            loopback_socket::shutdown_send(client);
            loopback_socket::close(client);
        }

        // 거친 끝맺음이다. RST를 던져 상대가 **끊겼다**를 알게 한다.
        //
        // SO_LINGER를 {켬, 0초}로 두면 닫기가 정상 종료 절차를 건너뛰고 곧바로 RST를
        // 보낸다. 이것이 요점이다 — 그냥 닫으면 상대는 깨끗한 EOF를 보고 "몸통이 여기서
        // 끝났다"로 읽어, close_delimited로 온전히 받은 것과 구별하지 못한다. 몸통 중간에
        // 끊기는 자리를 흉내 내려면 오류여야 한다.
        void close_abortively(const std::uintptr_t client) noexcept
        {
            loopback_socket::close_abortively(client);
        }

        // "http://127.0.0.1:<port>" 뒤에 받은 글을 그대로 붙인다. 서 있는 서버와
        // 죽은 포트가 같은 글을 짓는다 — 두 벌이면 한쪽만 고쳐지는 자리다.
        [[nodiscard]] std::u8string loopback_url_text(const std::uint16_t port, const std::string_view path_and_query)
        {
            std::string text { "http://127.0.0.1:" };
            text += std::to_string(port);
            text += path_and_query;
            // 글자는 이미 ASCII라 UTF-8 바이트가 그대로다 — 옮기는 것은 형뿐이다.
            return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
        }

        [[nodiscard]] loopback_response not_found_response()
        {
            loopback_response response {};
            response.status = 404;
            response.headers.emplace_back("Content-Type", "text/plain; charset=utf-8");
            response.body = loopback_bytes("not found");
            return response;
        }
    } // namespace

    std::optional<std::string> loopback_request::header(const std::string_view lower_case_name) const
    {
        for (const auto& [name, value] : headers)
        {
            if (name == lower_case_name)
                return value;
        }
        return std::nullopt;
    }

    std::vector<std::uint8_t> loopback_bytes(const std::string_view text)
    {
        const auto* first { reinterpret_cast<const std::uint8_t*>(text.data()) };
        return std::vector<std::uint8_t> { first, first + text.size() };
    }

    loopback_http_server::loopback_http_server()
    {
        handler_ = [](const loopback_request&) { return not_found_response(); };

        if (loopback_socket::startup() == false)
            return;
        sockets_ready_ = true;

        std::uint16_t port { 0 };
        const std::uintptr_t listener { loopback_socket::bind_loopback(true, port) };
        if (listener == loopback_no_socket)
            return;

        port_ = port;
        listen_socket_ = listener;
        accept_thread_ = std::thread { [this] { accept_loop(); } };
    }

    loopback_http_server::~loopback_http_server()
    {
        stop();
    }

    std::uint16_t loopback_http_server::port() const noexcept
    {
        return port_;
    }

    std::u8string loopback_http_server::url(const std::string_view path_and_query) const
    {
        return loopback_url_text(port_, path_and_query);
    }

    void loopback_http_server::set_handler(std::function<loopback_response(const loopback_request&)> handler)
    {
        std::lock_guard<std::mutex> lock { handler_mutex_ };
        if (handler)
            handler_ = std::move(handler);
        else
            handler_ = [](const loopback_request&) { return not_found_response(); };
    }

    std::vector<loopback_request> loopback_http_server::requests() const
    {
        std::lock_guard<std::mutex> lock { requests_mutex_ };
        return requests_;
    }

    std::size_t loopback_http_server::request_count() const noexcept
    {
        return request_count_.load(std::memory_order_acquire);
    }

    std::size_t loopback_http_server::open_connections() const noexcept
    {
        return live_connections_.load(std::memory_order_acquire);
    }

    void loopback_http_server::stop() noexcept
    {
        if (stopped_.exchange(true, std::memory_order_acq_rel))
            return;

        {
            std::lock_guard<std::mutex> lock { stop_mutex_ };
            stopping_.store(true, std::memory_order_release);
        }
        stop_signal_.notify_all();

        try
        {
            // 받아들이는 thread를 먼저 거둔다. 그래야 아래에서 목록을 훑는 동안
            // 새 연결이 끼어들지 않는다.
            if (accept_thread_.joinable())
                accept_thread_.join();

            std::vector<std::unique_ptr<connection>> pending {};
            {
                std::lock_guard<std::mutex> lock { connections_mutex_ };
                for (const auto& record : connections_)
                {
                    // 닫기가 아니라 shutdown이다. 손잡이는 그 연결의
                    // thread만 닫는다 — 남이 닫으면 그 사이 recv에 들어간 thread가
                    // **재활용된 다른 손잡이**를 만질 수 있다. shutdown은 손잡이를
                    // 살려 둔 채 막힌 recv·send만 푼다.
                    if (record->socket != loopback_no_socket)
                        loopback_socket::shutdown_both(record->socket);
                }
                pending.swap(connections_);
            }
            // 자물쇠 밖에서 거둔다. 연결 thread가 끝내면서 connections_mutex_를
            // 한 번 더 쥐므로, 쥔 채로 join하면 서로 기다린다.
            for (const auto& record : pending)
            {
                if (record->worker.joinable())
                    record->worker.join();
            }
            pending.clear();
        }
        catch (...)
        {
            // join이 던지는 자리는 이미 되돌릴 수 없다. noexcept를 지키려 삼킨다.
        }

        if (listen_socket_ != loopback_no_socket)
        {
            loopback_socket::close(listen_socket_);
            listen_socket_ = loopback_no_socket;
        }
        if (sockets_ready_)
        {
            loopback_socket::cleanup();
            sockets_ready_ = false;
        }
    }

    void loopback_http_server::accept_loop()
    {
        const std::uintptr_t listener { listen_socket_ };
        while (!stopping())
        {
            // 기다렸다 받는 이유는 accept가 막히면 stop 깃발을 다시 볼 수 없기
            // 때문이다. 듣는 손잡이를 밖에서 닫아 깨우는 길도 있지만, 그러면
            // 여기서 이미 accept 안에 들어간 손잡이가 사라진다.
            bool failed { false };
            if (loopback_socket::wait_readable(listener, accept_poll, failed) == false)
            {
                if (failed)
                    return;
                continue;
            }

            bool transient { false };
            const std::uintptr_t client { loopback_socket::accept(listener, transient) };
            if (client == loopback_no_socket)
            {
                if (transient)
                    continue;
                return;
            }

            loopback_socket::set_receive_timeout(client, receive_timeout);

            std::lock_guard<std::mutex> lock { connections_mutex_ };
            // 끝난 연결을 여기서 거둔다. 안 그러면 요청 수만큼 thread 객체가 쌓인다
            // (데모는 한 번 뜨는 동안 수백 번 부른다).
            reap_finished_connections();
            connections_.push_back(std::make_unique<connection>());
            connection& record { *connections_.back() };
            record.socket = client;
            live_connections_.fetch_add(1, std::memory_order_release);
            record.worker = std::thread { [this, &record] { serve_and_finish(record); } };
        }
    }

    void loopback_http_server::serve_and_finish(connection& record) noexcept
    {
        const std::uintptr_t client { record.socket };
        bool hard_close { false };
        try
        {
            hard_close = serve_connection(client);
        }
        catch (...)
        {
            // thread 밖으로 나가면 std::terminate다. 이 연결 하나만 버린다.
        }

        const std::uintptr_t owned { take_socket(record) };
        if (owned != loopback_no_socket)
        {
            if (hard_close)
                close_abortively(owned);
            else
                close_gracefully(owned);
        }

        live_connections_.fetch_sub(1, std::memory_order_release);
        // 이 뒤로 record를 만지지 않는다. 표시를 보는 쪽이 곧 join하고 지운다.
        record.finished.store(true, std::memory_order_release);
    }

    bool loopback_http_server::serve_connection(const std::uintptr_t client)
    {
        std::string buffer {};
        std::size_t header_end { std::string::npos };
        while (true)
        {
            header_end = buffer.find("\r\n\r\n");
            if (header_end != std::string::npos)
                break;
            if (buffer.size() >= max_header_bytes)
            {
                loopback_response too_large {};
                too_large.status = 431;
                write_response(client, too_large);
                return false;
            }
            if (stopping())
                return false;

            std::array<char, 4096> block {};
            const loopback_socket::receive_result received { loopback_socket::receive(client, block.data(), block.size()) };
            if (received.status == loopback_socket::receive_status::data)
            {
                buffer.append(block.data(), received.size);
                continue;
            }
            // 시간 초과만 다시 돌아본다. 곱게 닫힌 것도 끊긴 것도 끝이다.
            if (received.status != loopback_socket::receive_status::timed_out)
                return false;
        }

        // 마지막 머리 줄도 CRLF로 끝나도록 잘라 둔다.
        const std::string_view head { std::string_view { buffer }.substr(0, header_end + 2) };
        const std::size_t line_end { head.find("\r\n") };
        if (line_end == std::string_view::npos)
            return false;

        const std::string_view request_line { head.substr(0, line_end) };
        const std::size_t first_space { request_line.find(' ') };
        const std::size_t second_space { first_space == std::string_view::npos ? std::string_view::npos : request_line.find(' ', first_space + 1) };
        if (first_space == std::string_view::npos || second_space == std::string_view::npos)
        {
            loopback_response bad_request {};
            bad_request.status = 400;
            write_response(client, bad_request);
            return false;
        }

        loopback_request request {};
        request.method = std::string { request_line.substr(0, first_space) };
        request.target = std::string { request_line.substr(first_space + 1, second_space - first_space - 1) };

        std::size_t cursor { line_end + 2 };
        while (cursor < head.size())
        {
            const std::size_t end { head.find("\r\n", cursor) };
            if (end == std::string_view::npos)
                break;
            const std::string_view line { head.substr(cursor, end - cursor) };
            cursor = end + 2;
            // 이름 없는 줄과 이어 쓴 줄(앞이 공백인 옛 문법)은 버린다. 뒤엣것은
            // HTTP/1.1에서 이미 폐기되었고, 살려 두면 앞 공백이 이름에 섞여 든다.
            if (line.empty() || line.front() == ' ' || line.front() == '\t')
                continue;
            const std::size_t colon { line.find(':') };
            if (colon == std::string_view::npos || colon == 0)
                continue;
            request.headers.emplace_back(to_lower(line.substr(0, colon)), std::string { trimmed(line.substr(colon + 1)) });
        }

        std::size_t content_length { 0 };
        if (const std::optional<std::string> announced { request.header("content-length") }; announced.has_value())
        {
            const char* first { announced->data() };
            const char* last { first + announced->size() };
            const std::from_chars_result parsed { std::from_chars(first, last, content_length) };
            if (parsed.ec != std::errc {} || parsed.ptr != last)
                content_length = 0;
        }
        if (content_length > max_body_bytes)
        {
            loopback_response too_large {};
            too_large.status = 413;
            write_response(client, too_large);
            return false;
        }

        // WinHTTP는 몸통이 커지면 머리만 먼저 보내고 이 답을 기다린다. 답하지
        // 않으면 몸통이 오지 않아 요청 전체가 시간 초과로 끝난다.
        if (const std::optional<std::string> expectation { request.header("expect") }; expectation.has_value() && contains_token(*expectation, "100-continue"))
        {
            if (!send_text(client, "HTTP/1.1 100 Continue\r\n\r\n"))
                return false;
        }

        const std::string_view leftover { std::string_view { buffer }.substr(header_end + 4) };
        const std::size_t taken { std::min(content_length, leftover.size()) };
        request.body = loopback_bytes(leftover.substr(0, taken));
        while (request.body.size() < content_length)
        {
            if (stopping())
                return false;
            std::array<char, 4096> block {};
            const std::size_t wanted { std::min<std::size_t>(block.size(), content_length - request.body.size()) };
            const loopback_socket::receive_result received { loopback_socket::receive(client, block.data(), wanted) };
            if (received.status == loopback_socket::receive_status::data)
            {
                const auto* first { reinterpret_cast<const std::uint8_t*>(block.data()) };
                request.body.insert(request.body.end(), first, first + received.size);
                continue;
            }
            if (received.status != loopback_socket::receive_status::timed_out)
                return false;
        }

        // handler보다 먼저 적는다. handler가 무엇을 하든(막히든 던지든) 무엇이
        // 왔는지는 남아야 test가 그것을 읽을 수 있다.
        {
            std::lock_guard<std::mutex> lock { requests_mutex_ };
            requests_.push_back(request);
            request_count_.store(requests_.size(), std::memory_order_release);
        }

        std::function<loopback_response(const loopback_request&)> handler {};
        {
            std::lock_guard<std::mutex> lock { handler_mutex_ };
            handler = handler_;
        }
        loopback_response response {};
        if (handler)
            response = handler(request);

        if (response.hang_forever)
        {
            wait_until_stop();
            return false;
        }
        return write_response(client, response);
    }

    bool loopback_http_server::write_response(const std::uintptr_t client, const loopback_response& response)
    {
        if (response.delay_before_headers.count() > 0 && wait_for_stop(response.delay_before_headers))
            return false;

        std::string head { "HTTP/1.1 " };
        head += std::to_string(response.status);
        head += ' ';
        head += response.reason.empty() ? std::string { reason_for(response.status) } : response.reason;
        head += "\r\n";
        for (const auto& [name, value] : response.headers)
        {
            head += name;
            head += ": ";
            head += value;
            head += "\r\n";
        }
        switch (response.framing)
        {
        case loopback_framing::content_length:
            // 자르더라도 **알리는 길이는 전체**다. 그래야 덜 온 것이 오류로 보인다.
            head += "Content-Length: ";
            head += std::to_string(response.body.size());
            head += "\r\n";
            break;
        case loopback_framing::chunked:
            head += "Transfer-Encoding: chunked\r\n";
            break;
        case loopback_framing::close_delimited:
            break;
        }
        // 한 연결에 요청 하나다 — 언제나 알린다.
        head += "Connection: close\r\n\r\n";
        if (!send_text(client, head))
            return false;

        const std::size_t budget { response.truncate_body_after.value_or(response.body.size()) };
        const bool truncating { response.truncate_body_after.has_value() };

        if (response.framing != loopback_framing::chunked)
        {
            const std::size_t count { std::min(budget, response.body.size()) };
            if (count > 0 && !send_all(client, response.body.data(), count))
                return false;
            return truncating;
        }

        std::size_t sent { 0 };
        const std::size_t step { response.chunk_size == 0 ? response.body.size() : response.chunk_size };
        while (sent < response.body.size() && sent < budget)
        {
            if (sent > 0 && response.delay_between_body_chunks.count() > 0 && wait_for_stop(response.delay_between_body_chunks))
                return false;
            const std::size_t size { std::min(std::min(step, response.body.size() - sent), budget - sent) };
            if (size == 0)
                break;
            std::string chunk_head { hex_of(size) };
            chunk_head += "\r\n";
            if (!send_text(client, chunk_head) || !send_all(client, response.body.data() + sent, size) || !send_text(client, "\r\n"))
                return false;
            sent += size;
        }
        // 자르는 중이면 끝 표시를 보내지 않는다. 보내면 온전한 몸통이 되어 버린다.
        if (truncating)
            return true;
        // 끝 표시를 보내고 곱게 닫는다. 못 보냈다면 상대가 이미 사라진 것이라
        // 역시 할 일이 없다.
        static_cast<void>(send_text(client, "0\r\n\r\n"));
        return false;
    }

    std::uintptr_t loopback_http_server::take_socket(connection& record)
    {
        std::lock_guard<std::mutex> lock { connections_mutex_ };
        const std::uintptr_t owned { record.socket };
        record.socket = loopback_no_socket;
        return owned;
    }

    void loopback_http_server::reap_finished_connections()
    {
        for (auto iterator = connections_.begin(); iterator != connections_.end();)
        {
            connection& record { **iterator };
            if (!record.finished.load(std::memory_order_acquire))
            {
                ++iterator;
                continue;
            }
            if (record.worker.joinable())
                record.worker.join();
            iterator = connections_.erase(iterator);
        }
    }

    bool loopback_http_server::stopping() const noexcept
    {
        return stopping_.load(std::memory_order_acquire);
    }

    bool loopback_http_server::wait_for_stop(const std::chrono::milliseconds delay)
    {
        // sleep_for가 아니라 조건 변수다. 그냥 자면 stop()이 그 잠이 끝날 때까지
        // 기다려야 하고, 몇 초짜리 지연을 심은 test 하나가 종료를 그만큼 늘린다.
        std::unique_lock<std::mutex> lock { stop_mutex_ };
        return stop_signal_.wait_for(lock, delay, [this] { return stopping_.load(std::memory_order_acquire); });
    }

    void loopback_http_server::wait_until_stop()
    {
        std::unique_lock<std::mutex> lock { stop_mutex_ };
        stop_signal_.wait(lock, [this] { return stopping_.load(std::memory_order_acquire); });
    }

    loopback_dead_port::loopback_dead_port()
    {
        if (loopback_socket::startup() == false)
            return;
        sockets_ready_ = true;

        // **listen을 부르지 않는 것이 이 클래스의 전부다.** 듣는 자리가 없는 포트는
        // 연결을 RST로 거절한다.
        std::uint16_t port { 0 };
        const std::uintptr_t held { loopback_socket::bind_loopback(false, port) };
        if (held == loopback_no_socket)
            return;

        port_ = port;
        socket_ = held;
    }

    loopback_dead_port::~loopback_dead_port()
    {
        if (socket_ != loopback_no_socket)
        {
            loopback_socket::close(socket_);
            socket_ = loopback_no_socket;
        }
        if (sockets_ready_)
        {
            loopback_socket::cleanup();
            sockets_ready_ = false;
        }
    }

    std::uint16_t loopback_dead_port::port() const noexcept
    {
        return port_;
    }

    std::u8string loopback_dead_port::url(const std::string_view path_and_query) const
    {
        return loopback_url_text(port_, path_and_query);
    }
} // namespace luil::testing
