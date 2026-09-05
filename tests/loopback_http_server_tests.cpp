#include "loopback_http_server.h"

// winsock2.h는 언제나 windows.h보다 먼저다 (도우미의 .cpp와 같은 이유다).
#include <winsock2.h>

#include <windows.h>
#include <ws2tcpip.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    // 이름을 그대로 쓴다. 이 파일은 처음부터 끝까지 이 도우미 하나만 다루므로
    // 자리마다 붙는 `luil::testing::`가 읽는 것을 가린다.
    using luil::testing::loopback_bytes;
    using luil::testing::loopback_framing;
    using luil::testing::loopback_http_server;
    using luil::testing::loopback_request;
    using luil::testing::loopback_response;

    // **HTTP 클라이언트를 하나도 쓰지 않고** 서버를 몬다.
    //
    // 이 축이 잠그려는 것은 "서버가 바이트를 옳게 낸다"이다. WinHTTP로 몰면
    // 서버가 틀린 프레이밍을 내도 WinHTTP가 너그럽게 받아 주는 만큼 test가
    // 눈을 감고, 나중에 그 서버로 WinHTTP를 검증하면 두 잘못이 서로를 가린다.
    // 그래서 여기서는 바이트를 손으로 보내고 손으로 읽는다.
    class winsock_scope
    {
    public:
        winsock_scope() noexcept
        {
            WSADATA winsock {};
            ready_ = WSAStartup(MAKEWORD(2, 2), &winsock) == 0;
        }

        winsock_scope(const winsock_scope&) = delete;
        winsock_scope(winsock_scope&&) = delete;
        winsock_scope& operator=(const winsock_scope&) = delete;
        winsock_scope& operator=(winsock_scope&&) = delete;

        ~winsock_scope()
        {
            if (ready_)
                WSACleanup();
        }

    private:
        bool ready_ { false };
    };

    class raw_client
    {
    public:
        explicit raw_client(const std::uint16_t port)
        {
            socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (socket_ == INVALID_SOCKET)
                return;
            // 서버가 답하지 못하는 결함이 생기면 test가 영영 매달리는 대신 여기서
            // 끝난다 (Catch2의 120초 timeout보다 훨씬 먼저다).
            DWORD receive_timeout { 5000 };
            setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receive_timeout), static_cast<int>(sizeof(receive_timeout)));

            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1 || connect(socket_, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address))) != 0)
            {
                closesocket(socket_);
                socket_ = INVALID_SOCKET;
            }
        }

        raw_client(const raw_client&) = delete;
        raw_client(raw_client&&) = delete;
        raw_client& operator=(const raw_client&) = delete;
        raw_client& operator=(raw_client&&) = delete;

        ~raw_client()
        {
            if (socket_ != INVALID_SOCKET)
                closesocket(socket_);
        }

        [[nodiscard]] bool connected() const noexcept
        {
            return socket_ != INVALID_SOCKET;
        }

        bool write(const std::string_view bytes)
        {
            if (socket_ == INVALID_SOCKET)
                return false;
            std::size_t remaining { bytes.size() };
            const char* cursor { bytes.data() };
            while (remaining > 0)
            {
                const int sent { send(socket_, cursor, static_cast<int>(remaining), 0) };
                if (sent <= 0)
                    return false;
                cursor += sent;
                remaining -= static_cast<std::size_t>(sent);
            }
            return true;
        }

        // 표시가 나올 때까지 읽는다. 스트림이 먼저 끝나면 거짓이다.
        bool read_until(const std::string_view marker)
        {
            while (received_.find(marker) == std::string::npos)
            {
                if (!pump())
                    return false;
            }
            return true;
        }

        void read_to_close()
        {
            while (pump())
                continue;
        }

        // 연결이 열린 뒤 받은 바이트 전부다 (여러 번 읽어도 쌓인다).
        [[nodiscard]] const std::string& received() const noexcept
        {
            return received_;
        }

        // 곱게 닫힌 것이 아니라 RST로 끊겼는가.
        [[nodiscard]] bool aborted() const noexcept
        {
            return aborted_;
        }

    private:
        bool pump()
        {
            if (socket_ == INVALID_SOCKET || closed_)
                return false;
            std::array<char, 4096> block {};
            const int received { recv(socket_, block.data(), static_cast<int>(block.size()), 0) };
            if (received > 0)
            {
                received_.append(block.data(), static_cast<std::size_t>(received));
                return true;
            }
            closed_ = true;
            if (received < 0)
            {
                const int error { WSAGetLastError() };
                aborted_ = error == WSAECONNRESET || error == WSAECONNABORTED;
            }
            return false;
        }

        winsock_scope scope_ {};
        SOCKET socket_ { INVALID_SOCKET };
        std::string received_ {};
        bool aborted_ { false };
        bool closed_ { false };
    };

    struct response_parts
    {
        // 마지막 머리 줄의 CRLF까지다 (빈 줄은 들어 있지 않다).
        std::string head {};
        std::string body {};
    };

    [[nodiscard]] response_parts split_response(const std::string_view text)
    {
        const std::size_t end { text.find("\r\n\r\n") };
        if (end == std::string_view::npos)
            return response_parts { std::string { text }, std::string {} };
        return response_parts { std::string { text.substr(0, end + 2) }, std::string { text.substr(end + 4) } };
    }

    [[nodiscard]] bool head_contains(const std::string_view head, const std::string_view line)
    {
        return head.find(line) != std::string_view::npos;
    }

    // chunked 몸통을 푼다. 프레이밍이 조금이라도 어긋나면 빈 값이다 —
    // "풀렸다"가 곧 "옳게 썼다"라야 이 축이 값을 한다.
    [[nodiscard]] std::optional<std::string> dechunked(std::string_view body)
    {
        std::string result {};
        while (true)
        {
            const std::size_t end { body.find("\r\n") };
            if (end == std::string_view::npos)
                return std::nullopt;
            const std::string_view digits { body.substr(0, end) };
            std::size_t size { 0 };
            const std::from_chars_result parsed { std::from_chars(digits.data(), digits.data() + digits.size(), size, 16) };
            if (parsed.ec != std::errc {} || parsed.ptr != digits.data() + digits.size())
                return std::nullopt;
            body.remove_prefix(end + 2);
            if (size == 0)
                return result;
            if (body.size() < size + 2)
                return std::nullopt;
            result.append(body.substr(0, size));
            body.remove_prefix(size + 2);
        }
    }

    [[nodiscard]] std::string as_text(const std::vector<std::uint8_t>& bytes)
    {
        return std::string { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

    [[nodiscard]] std::u8string as_u8(const std::string_view text)
    {
        return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
    }

    // 서버 쪽 상태는 다른 thread에서 바뀌므로 값 하나를 곧바로 단언하면 흔들린다.
    [[nodiscard]] bool wait_until_true(const std::function<bool()>& predicate, const std::chrono::milliseconds limit)
    {
        const std::chrono::steady_clock::time_point deadline { std::chrono::steady_clock::now() + limit };
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
                return true;
            std::this_thread::sleep_for(5ms);
        }
        return predicate();
    }
} // namespace

TEST_CASE("A loopback server answers with a content-length body and records the request", "[net][loopback]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request& request) {
        loopback_response response {};
        response.headers.emplace_back("Content-Type", "application/json");
        response.body = loopback_bytes(request.method == "POST" ? "{ \"ok\": true }" : "{ }");
        return response;
    });

    raw_client client { server.port() };
    REQUIRE(client.connected());
    REQUIRE(client.write("POST /api/echo?x=1 HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello"));
    client.read_to_close();
    REQUIRE(!client.aborted());

    const response_parts parts { split_response(client.received()) };
    REQUIRE(head_contains(parts.head, "HTTP/1.1 200 OK\r\n"));
    REQUIRE(head_contains(parts.head, "Content-Type: application/json\r\n"));
    REQUIRE(head_contains(parts.head, "Content-Length: 14\r\n"));
    REQUIRE(head_contains(parts.head, "Connection: close\r\n"));
    REQUIRE(parts.body == "{ \"ok\": true }");

    const std::vector<loopback_request> received { server.requests() };
    REQUIRE(server.request_count() == 1);
    REQUIRE(received.size() == 1);
    REQUIRE(received[0].method == "POST");
    REQUIRE(received[0].target == "/api/echo?x=1");
    // 이름은 소문자로, 차례는 온 그대로다.
    REQUIRE(received[0].headers.size() == 3);
    REQUIRE(received[0].headers[0].first == "host");
    REQUIRE(received[0].headers[0].second == "127.0.0.1");
    REQUIRE(received[0].headers[1].first == "content-type");
    REQUIRE(received[0].header("content-type").value_or("") == "text/plain");
    REQUIRE(received[0].header("content-length").value_or("") == "5");
    REQUIRE(!received[0].header("x-absent").has_value());
    REQUIRE(as_text(received[0].body) == "hello");

    // 답한 연결은 스스로 사라진다 (thread도 소켓도 남지 않는다).
    REQUIRE(wait_until_true([&server] { return server.open_connections() == 0; }, 2000ms));
}

TEST_CASE("A chunked loopback response splits the body into chunks", "[net][loopback]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response {};
        response.framing = loopback_framing::chunked;
        response.chunk_size = 4;
        response.delay_between_body_chunks = 10ms;
        response.body = loopback_bytes("abcdefghij");
        return response;
    });

    raw_client client { server.port() };
    REQUIRE(client.connected());
    const std::chrono::steady_clock::time_point start { std::chrono::steady_clock::now() };
    REQUIRE(client.write("GET /chunked HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"));
    client.read_to_close();
    const std::chrono::steady_clock::duration elapsed { std::chrono::steady_clock::now() - start };

    const response_parts parts { split_response(client.received()) };
    REQUIRE(head_contains(parts.head, "Transfer-Encoding: chunked\r\n"));
    REQUIRE(!head_contains(parts.head, "Content-Length:"));
    // 4 + 4 + 2로 잘리고 0 덩어리로 끝난다.
    REQUIRE(parts.body == "4\r\nabcd\r\n4\r\nefgh\r\n2\r\nij\r\n0\r\n\r\n");

    const std::optional<std::string> decoded { dechunked(parts.body) };
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "abcdefghij");
    // 덩어리가 셋이면 사이는 둘이다.
    REQUIRE(elapsed >= 20ms);
}

TEST_CASE("A close-delimited loopback response ends with a graceful close", "[net][loopback]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response {};
        response.framing = loopback_framing::close_delimited;
        response.delay_before_headers = 30ms;
        response.body = loopback_bytes("plain and closed");
        return response;
    });

    raw_client client { server.port() };
    REQUIRE(client.connected());
    const std::chrono::steady_clock::time_point start { std::chrono::steady_clock::now() };
    REQUIRE(client.write("GET /closed HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"));
    client.read_to_close();
    const std::chrono::steady_clock::duration elapsed { std::chrono::steady_clock::now() - start };

    const response_parts parts { split_response(client.received()) };
    REQUIRE(!head_contains(parts.head, "Content-Length:"));
    REQUIRE(!head_contains(parts.head, "Transfer-Encoding:"));
    REQUIRE(parts.body == "plain and closed");
    // 길이를 알리지 않았으니 끝은 곱게 닫히는 것뿐이다 — RST가 아니다.
    REQUIRE(!client.aborted());
    REQUIRE(elapsed >= 30ms);
}

TEST_CASE("A loopback server answers 100-continue before it reads the body", "[net][loopback]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request& request) {
        loopback_response response {};
        response.body = request.body;
        return response;
    });

    raw_client client { server.port() };
    REQUIRE(client.connected());
    REQUIRE(client.write("POST /upload HTTP/1.1\r\nHost: 127.0.0.1\r\nExpect: 100-continue\r\nContent-Length: 11\r\n\r\n"));

    const std::string_view continue_line { "HTTP/1.1 100 Continue\r\n\r\n" };
    REQUIRE(client.read_until("\r\n\r\n"));
    REQUIRE(client.received() == continue_line);
    // 몸통이 아직 가지 않았으므로 요청은 기록되기 전이다.
    REQUIRE(server.request_count() == 0);

    REQUIRE(client.write("hello world"));
    client.read_to_close();
    const response_parts parts { split_response(std::string_view { client.received() }.substr(continue_line.size())) };
    REQUIRE(head_contains(parts.head, "HTTP/1.1 200 OK\r\n"));
    REQUIRE(head_contains(parts.head, "Content-Length: 11\r\n"));
    REQUIRE(parts.body == "hello world");

    const std::vector<loopback_request> received { server.requests() };
    REQUIRE(received.size() == 1);
    REQUIRE(as_text(received[0].body) == "hello world");
}

TEST_CASE("A truncated content-length loopback response cuts the connection inside the body", "[net][loopback]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    const std::string body { std::string(197, 'A') + "END" };
    server.set_handler([body](const loopback_request&) {
        loopback_response response {};
        response.body = loopback_bytes(body);
        response.truncate_body_after = 16;
        return response;
    });

    raw_client client { server.port() };
    REQUIRE(client.connected());
    REQUIRE(client.write("GET /cut HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"));
    client.read_to_close();

    // 끊긴 자리가 오류로 보여야 한다. 곱게 닫으면 close_delimited로 온전히 받은
    // 것과 구별되지 않는다.
    REQUIRE(client.aborted());
    // 알린 길이만큼은 결코 오지 않는다. RST가 먼저 닿으면 이미 도착한 바이트까지
    // 함께 버려지므로(그것도 정상적인 결말이다) 받은 것이 비어 있을 수도 있다.
    REQUIRE(client.received().find("END") == std::string::npos);
    if (!client.received().empty())
    {
        const response_parts parts { split_response(client.received()) };
        // 자르더라도 알리는 길이는 전체 그대로다.
        REQUIRE(head_contains(parts.head, "Content-Length: 200\r\n"));
        REQUIRE(parts.body.size() <= 16);
    }
}

TEST_CASE("A truncated chunked loopback response leaves the frame unterminated", "[net][loopback]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    const std::string body { std::string(56, 'A') + "END" };
    server.set_handler([body](const loopback_request&) {
        loopback_response response {};
        response.framing = loopback_framing::chunked;
        response.chunk_size = 8;
        // 덩어리 사이를 벌려 **머리와 첫 덩어리가 확실히 먼저 닿게** 한다.
        // 붙여 보내면 뒤따르는 RST가 아직 읽지 않은 바이트를 함께 버려, 무엇이
        // 왔는지 단언할 수 있는 것이 하나도 남지 않는다 (위의 축이 그 자리다).
        response.delay_between_body_chunks = 40ms;
        response.body = loopback_bytes(body);
        response.truncate_body_after = 16;
        return response;
    });

    raw_client client { server.port() };
    REQUIRE(client.connected());
    REQUIRE(client.write("GET /cut-chunked HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"));
    client.read_to_close();

    REQUIRE(client.aborted());
    const response_parts parts { split_response(client.received()) };
    REQUIRE(head_contains(parts.head, "Transfer-Encoding: chunked\r\n"));
    // 첫 덩어리는 온전히 왔고, 끝 표시는 오지 않았다.
    REQUIRE(parts.body.starts_with("8\r\nAAAAAAAA\r\n"));
    REQUIRE(parts.body.find("0\r\n\r\n") == std::string::npos);
    REQUIRE(client.received().find("END") == std::string::npos);
    // 끝나지 않은 프레임이라 푸는 데 실패해야 한다.
    REQUIRE(!dechunked(parts.body).has_value());
}

TEST_CASE("stop() releases a loopback connection that hangs forever", "[net][loopback]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);
    server.set_handler([](const loopback_request&) {
        loopback_response response {};
        response.hang_forever = true;
        return response;
    });

    std::atomic<bool> finished { false };
    const auto client_main = [port = server.port(), &finished] {
        raw_client client { port };
        if (client.connected() && client.write("GET /hang HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"))
            client.read_to_close();
        finished.store(true, std::memory_order_release);
    };
    std::thread client_thread { client_main };

    REQUIRE(wait_until_true([&server] { return server.request_count() == 1; }, 5000ms));
    REQUIRE(server.open_connections() == 1);
    REQUIRE(!finished.load(std::memory_order_acquire));

    const std::chrono::steady_clock::time_point start { std::chrono::steady_clock::now() };
    server.stop();
    const std::chrono::steady_clock::duration elapsed { std::chrono::steady_clock::now() - start };
    client_thread.join();

    // 잠도 무한 대기도 조건 변수 위에 있어 stop()이 곧바로 깬다.
    REQUIRE(elapsed < 1000ms);
    REQUIRE(finished.load(std::memory_order_acquire));
    REQUIRE(server.open_connections() == 0);
    // 여러 번 불러도 된다 (소멸자가 다시 부른다).
    server.stop();
}

TEST_CASE("The default loopback handler answers 404", "[net][loopback]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);

    raw_client client { server.port() };
    REQUIRE(client.connected());
    REQUIRE(client.write("GET /missing HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"));
    client.read_to_close();

    const response_parts parts { split_response(client.received()) };
    REQUIRE(head_contains(parts.head, "HTTP/1.1 404 Not Found\r\n"));
    REQUIRE(head_contains(parts.head, "Content-Type: text/plain; charset=utf-8\r\n"));
    REQUIRE(head_contains(parts.head, "Content-Length: 9\r\n"));
    REQUIRE(parts.body == "not found");
    REQUIRE(server.requests().size() == 1);
    REQUIRE(server.requests()[0].target == "/missing");
}

TEST_CASE("A loopback url carries the ephemeral port", "[net][loopback]")
{
    loopback_http_server server {};
    REQUIRE(server.port() != 0);

    std::string expected { "http://127.0.0.1:" };
    expected += std::to_string(server.port());
    expected += "/api/status?x=1";
    REQUIRE(server.url("/api/status?x=1") == as_u8(expected));
}
