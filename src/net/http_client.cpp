#include "luil/net/http_client.h"

#include "luil/messaging/channel.h"
#include "net/http_request_context.h"
#include "net/http_url.h"
#include "net/winhttp_error.h"
#include "win32/utf8.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace luil::net {
    namespace {
        using namespace std::chrono_literals;

        // pump가 할 일이 없을 때 자는 상한이다. `stopping_`을 늦지 않게 보기 위한
        // 값이고 `app_host::logic_loop`의 250 ms와 같은 이유다.
        constexpr std::chrono::milliseconds pump_idle_wait { 250 };
        // 완료가 끊이지 않을 때도 마감·회차를 이만큼마다는 훑는다.
        constexpr std::chrono::milliseconds pump_sweep_interval { 50 };

        std::atomic<std::uint64_t> request_counter { 0 };

        // 표와 문맥 번호를 함께 내는 계수기다.
        //
        // **프로세스에 하나**인 이유는 번호 표(`register_request`)가 하나이기
        // 때문이다 — client가 여럿이어도 번호가 겹치지 않아야 콜백이 자기 문맥을
        // 찾는다. 0은 "받아들여지지 않았다"는 뜻이라 1부터 준다.
        [[nodiscard]] std::uint64_t issue_request_id() noexcept
        {
            return request_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        [[nodiscard]] bool header_text_is_safe(const std::u8string_view text) noexcept
        {
            for (const char8_t character : text)
                if (character == u8'\r' || character == u8'\n' || character == 0)
                    return false;
            return true;
        }

        // 헤더 이름은 토큰 문자만이다 (RFC 9110). 밖에서 온 값이 그대로 실리는 앱이
        // 부르는 사고가 헤더 주입이라, 선을 건드리기 전에 여기서 막는다.
        [[nodiscard]] bool header_name_is_token(const std::u8string_view name) noexcept
        {
            if (name.empty())
                return false;

            constexpr std::u8string_view punctuation { u8"!#$%&'*+-.^_`|~" };
            for (const char8_t character : name)
            {
                const bool letter { (character >= u8'a' && character <= u8'z') || (character >= u8'A' && character <= u8'Z') };
                const bool digit { character >= u8'0' && character <= u8'9' };
                if (letter == false && digit == false && punctuation.find(character) == std::u8string_view::npos)
                    return false;
            }
            return true;
        }

        [[nodiscard]] std::wstring method_text(const http_method method)
        {
            // 동사는 ASCII뿐이라 코드 단위를 그대로 넓힌다 (변환기를 부를 일이 아니다).
            const std::u8string_view name { http_method_name(method) };
            std::wstring wide {};
            wide.reserve(name.size());
            for (const char8_t character : name)
                wide.push_back(static_cast<wchar_t>(character));
            return wide;
        }

        [[nodiscard]] std::u8string count_text(const std::size_t value)
        {
            if (value == 0)
                return std::u8string { u8"0" };

            std::u8string text {};
            std::size_t rest { value };
            while (rest != 0)
            {
                text.insert(text.begin(), static_cast<char8_t>(static_cast<std::size_t>(u8'0') + rest % 10u));
                rest /= 10u;
            }
            return text;
        }

        // 되풀이를 그만두게 하는 오류인가 (영원히 같은 답이 올 것들이다).
        [[nodiscard]] bool failure_is_permanent(const http_error_kind kind) noexcept
        {
            return kind == http_error_kind::invalid_url || kind == http_error_kind::unsupported_scheme || kind == http_error_kind::invalid_header;
        }

        [[nodiscard]] bool redirect_has_same_origin(const request_context& current, const wchar_t* url, std::size_t length) noexcept
        {
            if (url == nullptr || length == 0 || length > http_header_limit_bytes)
                return false;
            if (url[length - 1] == L'\0')
                --length;
            if (length == 0)
                return false;

            URL_COMPONENTS components {};
            components.dwStructSize = static_cast<DWORD>(sizeof(components));
            components.dwHostNameLength = static_cast<DWORD>(-1);
            if (WinHttpCrackUrl(url, static_cast<DWORD>(length), 0, &components) == FALSE || components.lpszHostName == nullptr || components.dwHostNameLength == 0)
                return false;
            if (current.origin_host.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) || components.dwHostNameLength > static_cast<DWORD>(std::numeric_limits<int>::max()))
                return false;

            const bool secure { components.nScheme == INTERNET_SCHEME_HTTPS };
            return (components.nScheme == INTERNET_SCHEME_HTTP || secure) && secure == current.origin_secure && components.nPort == current.origin_port
                && CompareStringOrdinal(current.origin_host.c_str(), static_cast<int>(current.origin_host.size()), components.lpszHostName, static_cast<int>(components.dwHostNameLength), TRUE)
                == CSTR_EQUAL;
        }

        [[nodiscard]] bool clear_redirect_headers(request_context& current, unsigned long& code)
        {
            const HINTERNET live { borrow_handle(current) };
            if (live == nullptr)
            {
                code = static_cast<unsigned long>(ERROR_WINHTTP_OPERATION_CANCELLED);
                return false;
            }

            for (const http_header& header : current.request.headers)
            {
                if (http_header_crosses_origins(header.name))
                    continue;
                // 검증된 헤더 이름은 ASCII 토큰이다. 같은 이름이 여러 번 있으면
                // 그 횟수만큼 지워, WinHTTP가 중복 줄 하나씩 지워도 남지 않게 한다.
                std::wstring removal {};
                removal.reserve(header.name.size() + 1);
                for (const char8_t character : header.name)
                    removal.push_back(static_cast<wchar_t>(character));
                removal.push_back(L':');

                if (WinHttpAddRequestHeaders(live, removal.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_REPLACE) == FALSE)
                {
                    code = GetLastError();
                    if (code != static_cast<unsigned long>(ERROR_WINHTTP_HEADER_NOT_FOUND))
                        return false;
                }
            }
            return true;
        }

        void read_next_chunk(request_context& current)
        {
            // 손잡이를 잠금 안에서 꺼내 잠금 **밖에서** 쓴다. 그 사이 다른 thread가
            // 닫으면 WinHTTP가 잘못된 손잡이로 거절하고, 그 실패는 아래에서 답이 된다.
            const HINTERNET live { borrow_handle(current) };
            if (live == nullptr)
                return;

            set_phase(current, request_phase::reading);
            if (WinHttpReadData(live, current.chunk.data(), static_cast<DWORD>(current.chunk.size()), nullptr) == FALSE)
                finish_and_close(current, make_winhttp_error(GetLastError(), current.secure_flags));
        }

        // 알림 하나에 전이 하나다. 여기서 파싱도 디코딩도 큰 할당도 대기도 하지 않는다.
        void dispatch_status(const std::uint64_t id, const DWORD status, LPVOID information, const DWORD length)
        {
            if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
            {
                close_request(id);
                return;
            }

            const std::shared_ptr<request_context> owned { find_request(id) };
            if (owned == nullptr)
                return;

            request_context& current { *owned };
            switch (status)
            {
            case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE: {
                const HINTERNET live { borrow_handle(current) };
                if (live == nullptr)
                    return;

                set_phase(current, request_phase::receiving);
                if (WinHttpReceiveResponse(live, nullptr) == FALSE)
                    finish_and_close(current, make_winhttp_error(GetLastError(), current.secure_flags));
                return;
            }
            case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: {
                http_error failure {};
                if (capture_response_head(current, failure) == false)
                {
                    finish_and_close(current, failure);
                    return;
                }
                // 몸이 오지 않는 답이다 — HEAD와 1xx·204·304는 `Content-Length`가 무엇이라
                // 적혀 있든 그것은 오지 않을 몸의 크기라 상한을 볼 자리가 아니다.
                const bool informational { current.status_code >= 100 && current.status_code < 200 };
                const bool bodiless { current.request.method == http_method::head || current.status_code == 204 || current.status_code == 304 || informational };
                // 압축되지 않은 답의 길이가 상한을 넘으면 한 바이트도 읽지 않는다.
                // 압축된 답에서는 헤더의 길이와 우리가 받을 몸의 길이가 다르다.
                const bool content_encoded { current.request.decompress && find_header(current.headers, u8"content-encoding").empty() == false };
                if (bodiless == false && content_encoded == false && current.content_length_hint > current.request.max_body_bytes)
                {
                    finish_and_close(current, make_http_error(http_error_kind::body_too_large, u8"The response Content-Length exceeds max_body_bytes.", 0));
                    return;
                }
                if (bodiless == false && content_encoded == false)
                    current.body.reserve(std::min(current.content_length_hint, http_body_reserve_limit_bytes));
                read_next_chunk(current);
                return;
            }
            case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: {
                if (length == 0)
                {
                    // 몸이 끝났다. 걸쇠에 빈 결말을 적고 닫으면 `HANDLE_CLOSING`이 답을 낸다.
                    finish_and_close(current, http_error {});
                    return;
                }
                if (information == nullptr)
                {
                    finish_and_close(current, make_http_error(http_error_kind::connection_lost, u8"The response body arrived without a buffer.", 0));
                    return;
                }
                const std::span<const std::uint8_t> chunk { static_cast<const std::uint8_t*>(information), static_cast<std::size_t>(length) };
                if (append_body(current, chunk) == false)
                {
                    finish_and_close(current, make_http_error(http_error_kind::body_too_large, u8"The response body exceeds max_body_bytes.", 0));
                    return;
                }
                read_next_chunk(current);
                return;
            }
            case WINHTTP_CALLBACK_STATUS_REDIRECT: {
                ++current.redirects;
                if (current.redirects > current.request.max_redirects)
                {
                    finish_and_close(current, make_http_error(http_error_kind::too_many_redirects, u8"The response redirected more times than max_redirects allows.", 0));
                    return;
                }
                capture_redirect_url(current, static_cast<const wchar_t*>(information), static_cast<std::size_t>(length));
                if (current.redirect_headers_cleared == false && current.request.headers.empty() == false
                    && redirect_has_same_origin(current, static_cast<const wchar_t*>(information), static_cast<std::size_t>(length)) == false)
                {
                    unsigned long code { 0 };
                    if (clear_redirect_headers(current, code) == false)
                    {
                        finish_and_close(current, make_winhttp_error(code, current.secure_flags));
                        return;
                    }
                    current.redirect_headers_cleared = true;
                }
                return;
            }
            case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE: {
                // 깃발만 적는다 — 끝내는 것은 뒤따르는 `REQUEST_ERROR`다.
                if (information != nullptr && length >= sizeof(DWORD))
                    current.secure_flags |= *static_cast<const DWORD*>(information);
                return;
            }
            case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR: {
                unsigned long code { static_cast<unsigned long>(ERROR_WINHTTP_INTERNAL_ERROR) };
                if (information != nullptr && length >= sizeof(WINHTTP_ASYNC_RESULT))
                    code = static_cast<const WINHTTP_ASYNC_RESULT*>(information)->dwError;
                // 취소로 온 것이면 걸쇠에 이미 적힌 사유가 이긴다 (걸쇠가 스스로 그렇게 한다).
                finish_and_close(current, make_winhttp_error(code, current.secure_flags));
                return;
            }
            default:
                return;
            }
        }

        void CALLBACK status_callback(HINTERNET handle, const DWORD_PTR context, const DWORD status, LPVOID information, const DWORD length)
        {
            static_cast<void>(handle);
            // 연결·세션 손잡이의 알림이다 (문맥을 박지 않았다).
            if (context == 0)
                return;

            try
            {
                dispatch_status(static_cast<std::uint64_t>(context), status, information, length);
            }
            catch (...)
            {
                // 콜백 밖으로 나가는 예외는 곧 terminate다. 삼키되 그 요청을 여기서
                // 접는다 — 마감이 없는 요청(`total_timeout == 0`)은 마감 훑기라는 그물도
                // 없어, 접지 않으면 영영 답하지 않는다.
                if (const std::shared_ptr<request_context> stranded { find_request(static_cast<std::uint64_t>(context)) }; stranded != nullptr)
                    finish_and_close(*stranded, make_http_error(http_error_kind::system_error, u8"The status callback failed unexpectedly.", 0));
            }
        }
    } // namespace

    // WinHTTP 세션 하나와 pump thread 하나를 든 실체다.
    //
    // 공개 클래스가 손잡이 타입을 드러내지 않으려고 struct를 이 파일 안에 둔다
    // (`renderer`가 같은 자리에 선다).
    struct http_client::engine final : request_owner
    {
        struct schedule_record
        {
            http_ticket ticket {};
            http_request request {};
            http_heartbeat schedule {};
            std::chrono::steady_clock::time_point next {};
            std::uint64_t rounds { 0 };
            // 지금 날아가 있는 회차의 문맥 번호다 (없으면 0).
            std::uint64_t in_flight_id { 0 };
        };

        struct pending_round
        {
            http_request request {};
            http_ticket ticket {};
            std::uint64_t sequence { 0 };
            std::uint64_t id { 0 };
        };

        using connection_key = std::tuple<std::wstring, unsigned short, bool>;

        // 수락 시 ticket 수를 제한한다. 합성 취소까지 그 자리를 쓰므로 완료
        // 큐에서 이미 수락한 응답을 버리지 않아도 전체 대기 수가 제한된다.
        explicit engine(http_client_config configuration)
            : configuration_ { std::move(configuration) }
            , completions_ { messaging::channel_options { std::numeric_limits<std::size_t>::max(), messaging::overflow_policy::reject_newest, {} } }
        {}

        engine(const engine&) = delete;
        engine(engine&&) = delete;
        engine& operator=(const engine&) = delete;
        engine& operator=(engine&&) = delete;

        ~engine() override
        {
            static_cast<void>(stop());
            // 표를 쥔 콜백이 지금 없음을 보장하는 관문이다 — 이 뒤로 우리 memory는 없다.
            wait_for_request_lookups();
        }

        [[nodiscard]] bool open(std::u8string& error)
        {
            auto agent { win32::utf8_to_utf16(configuration_.user_agent) };
            if (agent.value.has_value() == false)
            {
                error = u8"The user agent is not valid UTF-8.";
                return false;
            }

            const DWORD access { configuration_.use_system_proxy ? static_cast<DWORD>(WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY) : static_cast<DWORD>(WINHTTP_ACCESS_TYPE_NO_PROXY) };
            session_ = WinHttpOpen(agent.value->c_str(), access, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
            if (session_ == nullptr && access == static_cast<DWORD>(WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY) && GetLastError() == static_cast<unsigned long>(ERROR_INVALID_PARAMETER))
            {
                // 자동 proxy 값이 없는 OS가 있다. 그때는 시스템 설정을 그대로 쓴다.
                session_ = WinHttpOpen(agent.value->c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
            }
            if (session_ == nullptr)
            {
                error = make_winhttp_error(GetLastError(), 0).message;
                return false;
            }

            // TLS는 1.2·1.3만이다. 인증서 검증을 끄는 손잡이는 어디에도 없다.
            DWORD protocols { WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 };
            if (WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, static_cast<DWORD>(sizeof(protocols))) == FALSE)
            {
                protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
                static_cast<void>(WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, static_cast<DWORD>(sizeof(protocols))));
            }

            // 세션에 한 번 건다 — 자식 손잡이가 물려받는다.
            // `HANDLES`는 만들어짐·닫힘 둘 다인데, 만들어짐은 문맥을 박기 전에 오므로
            // 번호 0으로 와 그냥 버려진다 (닫힘 하나만 받는 깃발이 없다).
            constexpr DWORD notifications { WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES | WINHTTP_CALLBACK_FLAG_SECURE_FAILURE | WINHTTP_CALLBACK_FLAG_REDIRECT };
            if (WinHttpSetStatusCallback(session_, status_callback, notifications, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
            {
                error = make_winhttp_error(GetLastError(), 0).message;
                static_cast<void>(WinHttpCloseHandle(session_));
                session_ = nullptr;
                return false;
            }

            transcode_ = configuration_.transcode ? configuration_.transcode : codepage_text_transcoder();
            pump_ = std::thread { [this] { pump(); } };
            pump_id_ = pump_.get_id();
            return true;
        }

        [[nodiscard]] http_ticket send(http_request request, std::u8string& error)
        {
            if (stopping_.load())
            {
                error = u8"client stopped";
                return {};
            }

            if (reserve_ticket(error) == false)
                return {};
            const std::uint64_t id { issue_request_id() };
            const http_ticket ticket { id };
            http_error failure {};
            if (start_request(std::move(request), ticket, 0, id, failure) == false)
            {
                pending_tickets_.fetch_sub(1);
                error = failure.message;
                return {};
            }
            return ticket;
        }

        [[nodiscard]] http_ticket start_heartbeat(http_request request, http_heartbeat schedule, std::u8string& error)
        {
            if (stopping_.load())
            {
                error = u8"client stopped";
                return {};
            }

            // URL·헤더는 **먼저** 본다 — 잘못된 요청은 표를 받지 못한다.
            http_error failure {};
            std::wstring headers {};
            if (parse_http_url(request.url, failure).has_value() == false || build_request_headers(request, headers, failure) == false)
            {
                error = failure.message;
                return {};
            }

            if (schedule.interval < http_minimum_heartbeat_interval)
                schedule.interval = http_minimum_heartbeat_interval;

            if (reserve_ticket(error) == false)
                return {};

            const http_ticket ticket { issue_request_id() };
            schedule_record record {};
            record.ticket = ticket;
            record.request = std::move(request);
            record.schedule = schedule;
            record.next = std::chrono::steady_clock::now() + (schedule.immediate ? 0ms : schedule.interval);
            {
                std::lock_guard lock { schedules_mutex_ };
                if (stopping_.load())
                {
                    pending_tickets_.fetch_sub(1);
                    error = u8"client stopped";
                    return {};
                }
                schedules_.emplace(ticket.value, std::move(record));
            }
            return ticket;
        }

        void cancel(const http_ticket ticket) noexcept
        {
            if (ticket.value == 0)
                return;

            bool heartbeat { false };
            schedule_record dropped {};
            {
                std::lock_guard lock { schedules_mutex_ };
                const auto entry { schedules_.find(ticket.value) };
                if (entry != schedules_.end())
                {
                    heartbeat = true;
                    dropped = std::move(entry->second);
                    schedules_.erase(entry);
                }
            }

            // 회차가 하나도 나가 있지 않은 되풀이는 답할 것이 없으니 마지막 답을 짓는다.
            //
            // 회차 번호가 **적혀 있으면 짓지 않는다.** 그 회차의 완료가 이미 채널에
            // 있거나(손잡이는 닫혔는데 pump가 아직 못 봤다), 막 띄우는 중이거나
            // (`start_request`가 실패하면 `fire_schedules`가 합성 답을 낸다), 아래에서
            // 우리가 접는다 — 어느 길이든 답 하나가 오고, 되풀이표가 비었으므로
            // `handle_completion`이 그것을 마지막으로 찍는다. 여기서도 지으면 둘이 된다.
            if (heartbeat && dropped.in_flight_id == 0)
            {
                post_synthetic(dropped.ticket, dropped.rounds, dropped.request, cancelled_error());
                return;
            }

            const std::uint64_t target { heartbeat ? dropped.in_flight_id : ticket.value };
            if (const std::shared_ptr<request_context> live { borrow_request(target) }; live != nullptr)
                finish_and_close(*live, cancelled_error());
        }

        void cancel_all() noexcept
        {
            std::vector<schedule_record> dropped {};
            {
                std::lock_guard lock { schedules_mutex_ };
                for (auto& entry : schedules_)
                    dropped.push_back(std::move(entry.second));
                schedules_.clear();
            }
            for (const schedule_record& record : dropped)
                if (record.in_flight_id == 0)
                    post_synthetic(record.ticket, record.rounds, record.request, cancelled_error());
            abort_all(cancelled_error());
        }

        // 아직 답이 나가지 않은 요청 수다 — 선을 탄 것뿐 아니라 받아 두고 아직 풀지·
        // 건네지 않은 것까지다 (http_client.h의 계약). `max_in_flight` 검사는 선을 탄
        // 것만 세므로 `in_flight_.size()`를 따로 본다.
        [[nodiscard]] std::size_t in_flight() const noexcept
        {
            return outstanding_.load();
        }

        // 거짓이면 예산을 넘긴 것이다 — 부르는 쪽이 engine을 일부러 흘려보낸다.
        [[nodiscard]] bool stop() noexcept
        {
            // `deliver` 안에서 부르면 자기 자신을 join하게 된다 (http_client.h의 계약).
            assert(std::this_thread::get_id() != pump_id_ && "http_client::stop must not be called from deliver");
            // 둘이 함께 부르면 뒤엣것은 앞엣것이 **끝날 때까지** 기다린다. 곧바로
            // 돌아가면 "돌아온 뒤에는 deliver가 불리지 않는다"가 그쪽에게는 거짓이 된다.
            std::lock_guard serialize { stop_mutex_ };
            if (stopped_.load())
                return true;

            // 1. 새 요청을 거절한다.
            stopping_.store(true);
            // 2. 되풀이표를 비우고 pump를 **먼저** 멈춘다.
            //    pump가 서야 `fire_schedules`가 새 회차를 띄울 길이 없다 — 아래에서
            //    "지도가 비었다"를 본 뒤 회차 하나가 끼어들면 engine이 사라진 뒤에도
            //    WinHTTP가 그 문맥을 본다. 배달도 여기서 끝난다.
            {
                std::lock_guard lock { schedules_mutex_ };
                schedules_.clear();
            }
            delivering_.store(false);
            completions_.close();
            if (pump_.joinable())
                pump_.join();
            outstanding_.store(0);

            // 3. 날아가 있는 요청을 전부 접고 손잡이가 다 놓이기를 기다린다.
            abort_all(make_http_error(http_error_kind::stopped, u8"The client was stopped.", 0));
            bool closed_in_time { false };
            {
                std::unique_lock lock { in_flight_mutex_ };
                closed_in_time = in_flight_empty_.wait_for(lock, configuration_.stop_budget, [this] { return in_flight_.empty(); });
            }
            stopped_.store(true);

            // 콜백 해제가 **기다림 뒤**여야 한다 — 먼저 풀어도 이미 만들어진 요청
            // 손잡이에서 콜백이 떨어지지 않고, 도는 중인 콜백을 기다려 주지도 않는다.
            if (closed_in_time == false)
            {
                // 예산을 넘겼다. 이 뒤로 이 객체는 부술 수 없다 — 부수는 자리가
                // `leaked()`를 보고 일부러 흘려보낸다.
                leaked_.store(true);
                return false;
            }

            if (session_ != nullptr)
                static_cast<void>(WinHttpSetStatusCallback(session_, nullptr, 0, 0));
            {
                std::lock_guard lock { connections_mutex_ };
                for (const auto& entry : connections_)
                    static_cast<void>(WinHttpCloseHandle(entry.second));
                connections_.clear();
            }
            if (session_ != nullptr)
            {
                static_cast<void>(WinHttpCloseHandle(session_));
                session_ = nullptr;
            }
            return true;
        }

        [[nodiscard]] bool leaked() const noexcept
        {
            return leaked_.load();
        }

        void on_request_closed(const std::uint64_t id) noexcept override
        {
            std::shared_ptr<request_context> closing {};
            {
                std::lock_guard lock { in_flight_mutex_ };
                const auto entry { in_flight_.find(id) };
                if (entry == in_flight_.end())
                    return;
                // 사본이다 — 완료를 짓기 전에 지우면 마지막 참조가 사라진다.
                closing = entry->second;
            }

            http_completion completion { make_completion(*closing) };
            {
                std::lock_guard lock { in_flight_mutex_ };
                static_cast<void>(in_flight_.erase(id));
                static_cast<void>(completions_.post(std::move(completion)));
                // 알림도 **잠금 안**이다. 밖으로 나가는 순간 `stop()`이 지도가 빈 것을
                // 보고 engine을 부술 수 있어, 그 뒤로는 조건 변수조차 만질 수 없다.
                in_flight_empty_.notify_all();
            }
        }

    private:
        [[nodiscard]] static http_error cancelled_error()
        {
            return make_http_error(http_error_kind::cancelled, u8"The request was cancelled.", 0);
        }

        [[nodiscard]] std::shared_ptr<request_context> borrow_request(const std::uint64_t id) const
        {
            std::lock_guard lock { in_flight_mutex_ };
            const auto entry { in_flight_.find(id) };
            return entry == in_flight_.end() ? nullptr : entry->second;
        }

        // 살아 있는 문맥 전부에 사유를 걸고 닫는다. 닫기는 지도 잠금 **밖**이다.
        void abort_all(const http_error& reason) noexcept
        {
            std::vector<std::shared_ptr<request_context>> live {};
            {
                std::lock_guard lock { in_flight_mutex_ };
                live.reserve(in_flight_.size());
                for (const auto& entry : in_flight_)
                    live.push_back(entry.second);
            }
            for (const std::shared_ptr<request_context>& context : live)
                finish_and_close(*context, reason);
        }

        [[nodiscard]] static bool build_request_headers(const http_request& request, std::wstring& block, http_error& failure)
        {
            std::u8string text {};
            for (const http_header& header : request.headers)
            {
                if (header_name_is_token(header.name) == false || header_text_is_safe(header.value) == false)
                {
                    failure = make_http_error(http_error_kind::invalid_header, u8"A request header name or value is not allowed.", 0);
                    return false;
                }
                text += header.name;
                text += u8": ";
                text += header.value;
                text += u8"\r\n";
            }

            // 몸의 형식은 `content_type`이 말하고, 비어 있으면 앱이 `headers`에 직접
            // 적은 Content-Type을 존중한다. 둘 다 없을 때만 기본값이다 — 앱이 적은
            // 줄을 기본값으로 덮으면 앱은 그 사실을 알 길이 없다.
            if (request.body.empty() == false && (request.content_type.empty() == false || find_header(request.headers, u8"content-type").empty()))
            {
                std::u8string_view content_type { request.content_type };
                if (content_type.empty())
                    content_type = u8"application/octet-stream";
                if (header_text_is_safe(content_type) == false)
                {
                    failure = make_http_error(http_error_kind::invalid_header, u8"The request content type is not allowed.", 0);
                    return false;
                }
                text += u8"Content-Type: ";
                text += content_type;
                text += u8"\r\n";
            }

            block.clear();
            if (text.empty())
                return true;

            auto wide { win32::utf8_to_utf16(text) };
            if (wide.value.has_value() == false)
            {
                failure = make_http_error(http_error_kind::invalid_header, u8"A request header is not valid UTF-8.", wide.error.has_value() ? wide.error->native_error : 0ul);
                return false;
            }
            block = std::move(*wide.value);
            return true;
        }

        [[nodiscard]] HINTERNET acquire_connection(const std::wstring& host, const unsigned short port, const bool secure, http_error& failure)
        {
            const connection_key key { host, port, secure };
            {
                std::lock_guard lock { connections_mutex_ };
                const auto entry { connections_.find(key) };
                if (entry != connections_.end())
                    return entry->second;
            }

            // 잠금 밖에서 연다. 손잡이는 **축출하지 않는다** — 살아 있는 요청이 붙어
            // 있을 수 있고 keep-alive 소켓 풀이 여기 매달린다. 전부 `stop()`이 닫는다.
            const HINTERNET opened { WinHttpConnect(session_, host.c_str(), port, 0) };
            if (opened == nullptr)
            {
                failure = make_winhttp_error(GetLastError(), 0);
                return nullptr;
            }

            std::lock_guard lock { connections_mutex_ };
            const auto inserted { connections_.emplace(key, opened) };
            if (inserted.second == false)
            {
                static_cast<void>(WinHttpCloseHandle(opened));
                return inserted.first->second;
            }
            return opened;
        }

        // 문맥을 지도에서·표에서 지운다. 손잡이가 서지 않아 `HANDLE_CLOSING`이 오지
        // 않는 실패 경로의 자리다 — 이 자리를 빠뜨리면 항목이 영영 남는다.
        void drop_request(const std::uint64_t id) noexcept
        {
            forget_request(id);
            std::lock_guard lock { in_flight_mutex_ };
            if (in_flight_.erase(id) != 0)
                outstanding_.fetch_sub(1);
            in_flight_empty_.notify_all();
        }

        void remember_deadline(const request_context& context)
        {
            if (context.has_deadline == false)
                return;

            std::lock_guard lock { deadlines_mutex_ };
            static_cast<void>(deadlines_.emplace(context.deadline, context.id));
        }

        // 요청 하나를 실제로 띄운다. 거짓이면 손잡이가 서기 전에 실패한 것이라
        // 답이 오지 않는다 (`send`가 그대로 동기 실패로 답한다).
        [[nodiscard]] bool start_request(http_request request, const http_ticket ticket, const std::uint64_t sequence, const std::uint64_t id, http_error& failure)
        {
            const auto target { parse_http_url(request.url, failure) };
            if (target.has_value() == false)
                return false;

            std::wstring header_block {};
            if (build_request_headers(request, header_block, failure) == false)
                return false;

            if (request.body.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()))
            {
                failure = make_http_error(http_error_kind::body_too_large, u8"The request body is too large to send.", 0);
                return false;
            }

            auto context { std::make_shared<request_context>() };
            context->owner = this;
            context->id = id;
            context->ticket = ticket;
            context->sequence = sequence;
            context->started = std::chrono::steady_clock::now();
            if (request.total_timeout > 0ms)
            {
                context->has_deadline = true;
                context->deadline = context->started + request.total_timeout;
            }
            context->final_url = request.url;
            context->origin_secure = target->secure;
            context->origin_host = target->host;
            context->origin_port = target->port;
            context->upload = std::move(request.body);
            context->request = std::move(request);

            // 지도에 **먼저** 넣는다 — 콜백은 `WinHttpSendRequest`가 돌아오기 전에
            // 같은 thread에서 올 수 있다.
            //  - `stopping_`을 지도 잠금 안에서 다시 본다. `send`의 첫 검사와 이 사이에
            //    `stop()`이 지나갔으면 그 `abort_all`은 이 문맥을 못 봤다 — 여기서
            //    거절해야 정지가 기다릴 것 없이 끝난다.
            {
                // 회차 예약의 취소와 문맥 등록을 같은 관문으로 묶는다. 취소가
                // 먼저면 합성 완료를 내고, 등록이 먼저면 cancel이 문맥을 찾는다.
                std::unique_lock schedule_lock { schedules_mutex_, std::defer_lock };
                if (sequence != 0)
                {
                    schedule_lock.lock();
                    if (schedules_.contains(ticket.value) == false)
                    {
                        failure = cancelled_error();
                        return false;
                    }
                }
                std::lock_guard lock { in_flight_mutex_ };
                if (stopping_.load())
                {
                    failure = make_http_error(http_error_kind::stopped, u8"client stopped", 0);
                    return false;
                }
                if (in_flight_.size() >= configuration_.max_in_flight)
                {
                    failure = make_http_error(http_error_kind::system_error, u8"too many requests in flight (" + count_text(configuration_.max_in_flight) + u8")", 0);
                    return false;
                }
                in_flight_.emplace(id, context);
                outstanding_.fetch_add(1);
            }
            register_request(id, context);

            const HINTERNET connection { acquire_connection(target->host, target->port, target->secure, failure) };
            if (connection == nullptr)
            {
                drop_request(id);
                return false;
            }

            const std::wstring verb { method_text(context->request.method) };
            const DWORD open_flags { target->secure ? static_cast<DWORD>(WINHTTP_FLAG_SECURE) : 0u };
            const HINTERNET handle { WinHttpOpenRequest(connection, verb.c_str(), target->target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, open_flags) };
            if (handle == nullptr)
            {
                failure = make_winhttp_error(GetLastError(), 0);
                drop_request(id);
                return false;
            }

            // 손잡이가 서자마자 번호를 박는다. 이 두 줄이 없으면 `SendRequest` 전에
            // 닫는 실패 경로에서 `HANDLE_CLOSING`이 번호를 달고 오지 않아 항목이 영영 남는다.
            DWORD_PTR context_value { static_cast<DWORD_PTR>(id) };
            if (WinHttpSetOption(handle, WINHTTP_OPTION_CONTEXT_VALUE, &context_value, static_cast<DWORD>(sizeof(context_value))) == FALSE)
            {
                failure = make_winhttp_error(GetLastError(), 0);
                static_cast<void>(WinHttpCloseHandle(handle));
                drop_request(id);
                return false;
            }

            // 손잡이를 문맥에 걸되 단계는 `preparing` 그대로다. 이 단계에서는 취소·정지·
            // 마감이 걸쇠만 적고 손잡이를 닫지 않는다 (`finish_and_close`) — 꾸미는 중인
            // 손잡이는 이 thread만의 것이고, 다 꾸민 뒤 걸쇠를 보고 우리가 닫는다.
            {
                std::lock_guard lock { context->guard };
                context->handle = handle;
            }
            // 꾸미기가 끝났거나 도중에 틀렸을 때 손잡이의 임자를 넘기는 한 자리다.
            // 단계를 올린 뒤에 닫아야 `finish_and_close`가 실제로 손잡이를 잡는다.
            const auto settle = [&context](const http_error& outcome) {
                set_phase(*context, request_phase::sending);
                finish_and_close(*context, outcome);
            };

            static_cast<void>(WinHttpSetTimeouts(handle, timeout_value(context->request.connect_timeout), timeout_value(context->request.connect_timeout), timeout_value(context->request.send_timeout),
                timeout_value(context->request.receive_timeout)));

            // 몸이 있으면 정책과 무관하게 따라가지 않는다 — WinHTTP가 재지정된 요청에
            // 몸을 다시 실어 주지 않아 몸 없는 요청이 조용히 대신 나간다.
            DWORD policy { WINHTTP_OPTION_REDIRECT_POLICY_NEVER };
            if (context->upload.empty() && context->request.redirects == http_redirect_policy::follow)
                policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
            static_cast<void>(WinHttpSetOption(handle, WINHTTP_OPTION_REDIRECT_POLICY, &policy, static_cast<DWORD>(sizeof(policy))));
            // WinHTTP 자신의 상한(기본 10)을 우리 것보다 하나 크게 둔다. 그래야
            // `max_redirects`가 10을 넘어도 통하고, 넘겼을 때 먼저 걸리는 것이 우리
            // 계수기라 `too_many_redirects`의 뜻이 한 자리에서 정해진다.
            DWORD automatic_redirects { static_cast<DWORD>(std::max(context->request.max_redirects, 0) + 1) };
            static_cast<void>(WinHttpSetOption(handle, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &automatic_redirects, static_cast<DWORD>(sizeof(automatic_redirects))));

            // proxy를 쓰지 않는 client는 요청마다 직접 연결을 다시 못박는다. 세션을
            // `NO_PROXY`로 열어도 WinHTTP는 요청마다 프로세스 밖 proxy 해석 RPC를
            // 띄우고, 그 RPC가 아직 바인딩 중일 때 손잡이를 닫으면 WinHTTP의 취소
            // 경로가 RPCRT4 안에서 접근 위반이나 `RPC_NT_INTERNAL_ERROR`로 프로세스를
            // 끝낸다. `send` 직후의 `cancel`·`stop`은 정상적인 사용이라 피할 수 없고,
            // 요청 손잡이에 적은 직접 연결은 그 해석 자체를 건너뛴다.
            if (configuration_.use_system_proxy == false)
            {
                WINHTTP_PROXY_INFO direct { WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr };
                static_cast<void>(WinHttpSetOption(handle, WINHTTP_OPTION_PROXY, &direct, static_cast<DWORD>(sizeof(direct))));
            }

            if (context->request.decompress)
            {
                // OS가 지원하지 않으면 조용히 꺼지고 요청은 그대로 성공한다.
                DWORD decompression { WINHTTP_DECOMPRESSION_FLAG_ALL };
                static_cast<void>(WinHttpSetOption(handle, WINHTTP_OPTION_DECOMPRESSION, &decompression, static_cast<DWORD>(sizeof(decompression))));
            }

            // 헤더는 `WinHttpSendRequest`의 인자가 아니라 이 함수로 붙인다 — 글을
            // 동기적으로 복사해 가므로 수명 문제가 아예 없다.
            if (header_block.empty() == false
                && WinHttpAddRequestHeaders(handle, header_block.c_str(), static_cast<DWORD>(header_block.size()), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) == FALSE)
            {
                settle(make_winhttp_error(GetLastError(), 0));
                return true;
            }

            remember_deadline(*context);

            // 꾸미는 동안 취소·정지가 지나갔으면 선을 건드리지 않고 접는다 — POST가
            // 취소된 뒤에 서버에 닿는 일이 없게 한다.
            bool latched { false };
            {
                std::lock_guard lock { context->guard };
                latched = context->outcome_latched;
            }
            if (latched)
            {
                settle(http_error {});
                return true;
            }

            const DWORD upload_size { static_cast<DWORD>(context->upload.size()) };
            LPVOID upload { context->upload.empty() ? WINHTTP_NO_REQUEST_DATA : context->upload.data() };
            if (WinHttpSendRequest(handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, upload, upload_size, upload_size, context_value) == FALSE)
            {
                // 비동기에서 `FALSE`는 "콜백이 오지 않는다"는 뜻이다. 그 자리에서 접는다.
                settle(make_winhttp_error(GetLastError(), 0));
                return true;
            }

            // 손잡이의 임자를 넘긴다. 보내는 사이 취소·정지·마감이 지나갔으면 걸쇠에
            // 적혀 있다 — 이제 닫는다 (`HANDLE_CLOSING`이 답을 낸다). 걸쇠가 비어
            // 있으면 이 뒤로는 누구든 닫을 수 있다.
            {
                std::lock_guard lock { context->guard };
                context->phase = request_phase::sending;
                latched = context->outcome_latched;
            }
            if (latched)
                finish_and_close(*context, http_error {});
            return true;
        }

        [[nodiscard]] static int timeout_value(const std::chrono::milliseconds limit) noexcept
        {
            if (limit <= 0ms)
                return 0;
            if (limit.count() > static_cast<std::chrono::milliseconds::rep>(std::numeric_limits<int>::max()))
                return std::numeric_limits<int>::max();
            return static_cast<int>(limit.count());
        }

        [[nodiscard]] bool reserve_ticket(std::u8string& error)
        {
            std::size_t pending { pending_tickets_.load() };
            while (pending < configuration_.max_pending_responses)
                if (pending_tickets_.compare_exchange_weak(pending, pending + 1))
                    return true;
            error = u8"too many responses awaiting delivery";
            return false;
        }

        void post_synthetic(const http_ticket ticket, const std::uint64_t sequence, const http_request& request, const http_error& error) noexcept
        {
            http_completion completion {};
            completion.ticket = ticket;
            completion.sequence = sequence;
            completion.final_url = request.url;
            completion.parse = request.parse;
            completion.error = error;
            outstanding_.fetch_add(1);
            if (completions_.post(std::move(completion)) != messaging::post_result::posted)
                outstanding_.fetch_sub(1);
        }

        void pump()
        {
            auto swept { std::chrono::steady_clock::now() };
            while (stopping_.load() == false)
            {
                messaging::envelope<http_completion> entry {};
                const messaging::receive_status status { completions_.receive_wait(entry, next_wait()) };
                if (status == messaging::receive_status::closed)
                    break;
                if (status == messaging::receive_status::received)
                {
                    // 완료를 우선 비운다 — 다만 완료가 끊이지 않아도 마감과 회차가
                    // 굶지 않게 이만큼마다 한 번은 훑는다.
                    handle_completion(std::move(entry.payload));
                    if (std::chrono::steady_clock::now() - swept < pump_sweep_interval)
                        continue;
                }
                swept = std::chrono::steady_clock::now();
                expire_deadlines();
                fire_schedules();
            }
        }

        [[nodiscard]] std::chrono::milliseconds next_wait()
        {
            const auto now { std::chrono::steady_clock::now() };
            auto limit { now + pump_idle_wait };
            {
                std::lock_guard lock { deadlines_mutex_ };
                if (deadlines_.empty() == false)
                    limit = std::min(limit, deadlines_.begin()->first);
            }
            {
                std::lock_guard lock { schedules_mutex_ };
                for (const auto& entry : schedules_)
                    if (entry.second.in_flight_id == 0)
                        limit = std::min(limit, entry.second.next);
            }
            if (limit <= now)
                return 0ms;
            // 자투리에서 0 ms가 나와 헛도는 것을 막는다.
            return std::chrono::duration_cast<std::chrono::milliseconds>(limit - now) + 1ms;
        }

        void handle_completion(http_completion completion)
        {
            bool last { true };
            {
                std::lock_guard lock { schedules_mutex_ };
                const auto entry { schedules_.find(completion.ticket.value) };
                if (entry != schedules_.end())
                {
                    schedule_record& record { entry->second };
                    record.in_flight_id = 0;
                    const bool exhausted { record.schedule.max_rounds != 0 && record.rounds >= record.schedule.max_rounds };
                    last = exhausted || failure_is_permanent(completion.error.kind);
                    if (last)
                        schedules_.erase(entry);
                }
            }

            const http_ticket ticket { completion.ticket };
            deliver_safely(finish(std::move(completion), last));
            // 답이 나갔다 — 이제야 "아직 답이 나가지 않은" 수에서 빠진다.
            outstanding_.fetch_sub(1);
            // **재무장은 배달한 뒤다.** 그래서 회차가 겹칠 수 없고, 앱이 회차 n을 본
            // 뒤에야 n+1이 나간다.
            if (last == false)
                rearm_schedule(ticket);
            else
                pending_tickets_.fetch_sub(1);
        }

        [[nodiscard]] http_response finish(http_completion completion, const bool last)
        {
            http_response response {};
            response.ticket = completion.ticket;
            response.sequence = completion.sequence;
            response.last = last;
            response.status_code = completion.status_code;
            response.reason = std::move(completion.reason);
            response.final_url = std::move(completion.final_url);
            response.headers = std::move(completion.headers);
            response.content_type = parse_media_type(find_header(response.headers, u8"content-type"));
            response.error = std::move(completion.error);
            response.elapsed = completion.elapsed;
            if (response.error.empty())
                response.body = decode_http_body(std::move(completion.body), response.content_type, completion.parse, transcode_);
            return response;
        }

        void deliver_safely(http_response response) noexcept
        {
            if (delivering_.load() == false || configuration_.deliver == nullptr)
                return;

            try
            {
                configuration_.deliver(std::move(response));
            }
            catch (...)
            {
                // 계약은 던지지 않는 것이다. 던지면 삼키고 그 답만 버린다 — thread
                // 진입 함수를 벗어난 예외는 곧 terminate다.
            }
        }

        void rearm_schedule(const http_ticket ticket)
        {
            std::lock_guard lock { schedules_mutex_ };
            const auto entry { schedules_.find(ticket.value) };
            if (entry == schedules_.end())
                return;
            entry->second.next = std::chrono::steady_clock::now() + entry->second.schedule.interval;
        }

        void expire_deadlines()
        {
            const auto now { std::chrono::steady_clock::now() };
            std::vector<std::uint64_t> expired {};
            {
                std::lock_guard lock { deadlines_mutex_ };
                auto entry { deadlines_.begin() };
                while (entry != deadlines_.end() && entry->first <= now)
                {
                    expired.push_back(entry->second);
                    entry = deadlines_.erase(entry);
                }
            }

            // 이미 끝난 요청의 항목은 지도에서 찾히지 않는다 — 번호가 겹치지 않으므로
            // 남은 항목이 남의 요청을 끊는 일은 없다.
            for (const std::uint64_t id : expired)
            {
                const std::shared_ptr<request_context> live { borrow_request(id) };
                if (live != nullptr)
                    finish_and_close(*live, make_http_error(http_error_kind::timed_out, u8"The request exceeded total_timeout.", 0));
            }
        }

        void fire_schedules()
        {
            std::vector<pending_round> ready {};
            const auto now { std::chrono::steady_clock::now() };
            {
                std::lock_guard lock { schedules_mutex_ };
                for (auto& entry : schedules_)
                {
                    schedule_record& record { entry.second };
                    if (record.in_flight_id != 0 || record.next > now)
                        continue;

                    // 번호를 **잠금 안에서** 먼저 발번한다 — 그래야 다른 thread의
                    // `cancel`이 날아가는 중인 회차를 놓치지 않는다.
                    pending_round round {};
                    round.id = issue_request_id();
                    round.ticket = record.ticket;
                    round.request = record.request;
                    ++record.rounds;
                    round.sequence = record.rounds;
                    record.in_flight_id = round.id;
                    ready.push_back(std::move(round));
                }
            }

            for (pending_round& round : ready)
            {
                http_error failure {};
                if (start_request(round.request, round.ticket, round.sequence, round.id, failure) == false)
                    post_synthetic(round.ticket, round.sequence, round.request, failure);
            }
        }

        http_client_config configuration_ {};
        http_text_transcoder transcode_ {};
        HINTERNET session_ { nullptr };

        std::atomic<bool> stopping_ { false };
        std::atomic<bool> stopped_ { false };
        std::atomic<bool> delivering_ { true };
        // `stop()`이 예산을 넘겨 살아 있을지 모르는 콜백이 아직 이 객체를 볼 수 있다.
        std::atomic<bool> leaked_ { false };
        // 받아 두고 아직 답을 건네지 않은 요청 수다 (`in_flight()`의 답).
        std::atomic<std::size_t> outstanding_ { 0 };
        std::atomic<std::size_t> pending_tickets_ { 0 };
        std::mutex stop_mutex_ {};

        mutable std::mutex in_flight_mutex_ {};
        std::condition_variable in_flight_empty_ {};
        std::unordered_map<std::uint64_t, std::shared_ptr<request_context>> in_flight_ {};

        std::mutex connections_mutex_ {};
        std::map<connection_key, HINTERNET> connections_ {};

        std::mutex schedules_mutex_ {};
        std::map<std::uint64_t, schedule_record> schedules_ {};

        std::mutex deadlines_mutex_ {};
        std::multimap<std::chrono::steady_clock::time_point, std::uint64_t> deadlines_ {};

        messaging::channel<http_completion> completions_;
        std::thread pump_ {};
        std::thread::id pump_id_ {};
    };

    http_client::http_client(std::unique_ptr<engine> owned) noexcept
        : engine_ { std::move(owned) }
    {}

    http_client::~http_client()
    {
        stop();
        // `stop()`이 예산을 넘겼으면 살아 있을지 모르는 콜백이 `in_flight_`와 완료
        // 채널을 만지므로 **일부러 흘려보낸다** — 푸는 것은 use-after-free이고, 새는
        // 것은 곧 끝나는 프로세스의 몇 KB다 (http-client-design.md). 부수는
        // 자리가 여기 하나라 `stop()`을 둘이 함께 불러도 풀 손잡이를 다투지 않는다.
        if (engine_ != nullptr && engine_->leaked())
            static_cast<void>(engine_.release());
    }

    std::unique_ptr<http_client> http_client::create(http_client_config configuration, std::u8string& error)
    {
        auto owned { std::make_unique<engine>(std::move(configuration)) };
        if (owned->open(error) == false)
            return nullptr;
        return std::unique_ptr<http_client> { new http_client { std::move(owned) } };
    }

    http_ticket http_client::send(http_request request, std::u8string& error)
    {
        if (engine_ == nullptr)
        {
            error = u8"client stopped";
            return {};
        }
        return engine_->send(std::move(request), error);
    }

    http_ticket http_client::start_heartbeat(http_request request, http_heartbeat schedule, std::u8string& error)
    {
        if (engine_ == nullptr)
        {
            error = u8"client stopped";
            return {};
        }
        return engine_->start_heartbeat(std::move(request), schedule, error);
    }

    void http_client::cancel(const http_ticket ticket) noexcept
    {
        if (engine_ != nullptr)
            engine_->cancel(ticket);
    }

    void http_client::cancel_all() noexcept
    {
        if (engine_ != nullptr)
            engine_->cancel_all();
    }

    std::size_t http_client::in_flight() const noexcept
    {
        return engine_ == nullptr ? 0u : engine_->in_flight();
    }

    void http_client::stop() noexcept
    {
        // 예산을 넘긴 경우의 처분은 소멸자가 한다 — 여기서 손잡이를 풀면 둘이 함께
        // 부를 때 그 풀기가 다툰다.
        if (engine_ != nullptr)
            static_cast<void>(engine_->stop());
    }
} // namespace luil::net
