#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace luil::testing {
    // socket 손잡이가 없다는 표시다 (winsock의 INVALID_SOCKET과 같은 값이다).
    //
    // 이 헤더는 winsock2.h를 들이지 않는다. winsock2.h는 반드시 windows.h보다
    // 먼저 들어가야 하는데, 이 도우미를 쓰는 test가 어떤 순서로 무엇을 include할지
    // 여기서 정할 수 없다 — 손잡이는 폭이 같은 정수로 들고 다니고 winsock 헤더는
    // .cpp 하나에 가둔다 (`SOCKET`은 `UINT_PTR`이다).
    inline constexpr std::uintptr_t loopback_no_socket { static_cast<std::uintptr_t>(-1) };

    // 서버가 받은 요청 하나다.
    struct loopback_request
    {
        std::string method {};
        // 받은 그대로의 경로 + 질의다 (예: "/api/status?x=1"). 풀지 않는다 —
        // 클라이언트가 무엇을 실제로 보냈는지가 이 축이 묻는 것이라, 여기서
        // 정규화하면 그 차이가 지워진다.
        std::string target {};
        // 이름은 소문자로, 값은 앞뒤 공백을 떼어 **도착한 차례대로** 담는다.
        // 차례를 지키는 이유는 클라이언트가 머리를 몇 번 어떤 순서로 붙였는지도
        // 검증 대상이기 때문이다 (같은 이름이 두 번 올 수 있어 map이 아니다).
        std::vector<std::pair<std::string, std::string>> headers {};
        // Content-Length로 잘라 읽은 몸통이다. chunked 요청 몸통은 범위 밖이다.
        std::vector<std::uint8_t> body {};

        // 그 이름의 첫 머리다. 이름은 **이미 소문자**로 넣어야 한다.
        [[nodiscard]] std::optional<std::string> header(std::string_view lower_case_name) const;
    };

    // 몸통의 길이를 상대에게 알리는 방식이다.
    //  - `close_delimited`는 길이를 알리지 않고 끊는 것으로 끝을 삼는다.
    //    HTTP/1.0 시절의 방식이라 클라이언트가 "끊김"과 "끝"을 구별하지 못한다 —
    //    그 자리를 일부러 만들어 보는 갈래다.
    enum class loopback_framing
    {
        content_length,
        chunked,
        close_delimited,
    };

    // 서버가 돌려줄 답이다. 전부 test가 적어 두는 값이라 기본값은 가장 흔한 답이다.
    struct loopback_response
    {
        int status { 200 };
        // 비면 status에 맞는 표준 문구를 서버가 채운다.
        std::string reason {};
        // 적은 그대로 나간다. Content-Length·Transfer-Encoding은 framing을 보고
        // 서버가 따로 붙이고, "Connection: close"는 언제나 붙는다.
        std::vector<std::pair<std::string, std::string>> headers {};
        std::vector<std::uint8_t> body {};
        loopback_framing framing { loopback_framing::content_length };
        // 상태 줄을 쓰기 전에 이만큼 쉰다 (timeout 축). stop()이 이 잠을 깨운다.
        std::chrono::milliseconds delay_before_headers { 0 };
        // chunked일 때 덩어리 사이에서 이만큼 쉰다 (느린 몸통 축).
        std::chrono::milliseconds delay_between_body_chunks { 0 };
        // chunked일 때 덩어리 크기다. 0이면 몸통 전체가 한 덩어리다.
        std::size_t chunk_size { 0 };
        // 머리를 보낸 뒤 몸통을 이만큼만 보내고 소켓을 거칠게 끊는다.
        //  - 세 framing 모두에 걸린다. content_length에서 **알린 길이는 그대로**
        //    전체 크기다 — 그래야 클라이언트가 "덜 왔다"를 알아챌 수 있다.
        //  - chunked에서는 보낸 **몸통 바이트** 수로 센다. 마지막 덩어리는 실제
        //    보낸 만큼의 길이로 적히고, 끝 표시(0 덩어리)는 나가지 않는다.
        std::optional<std::size_t> truncate_body_after {};
        // 요청은 다 읽고 답은 영영 하지 않는다. stop()이 이 연결을 끝낸다.
        bool hang_forever { false };
    };

    // 글을 몸통 바이트로 옮긴다. canned 응답이 대개 글이라 이 한 줄이 반복된다.
    [[nodiscard]] std::vector<std::uint8_t> loopback_bytes(std::string_view text);

    // test 실행 파일 안에 서는 HTTP/1.1 서버다 (127.0.0.1의 빈 포트 하나).
    //
    // 네트워크 test가 인터넷에 기대면 CI가 오프라인일 때 죽고, 이어지더라도 상대
    // 서버의 사정에 따라 답이 달라져 결정적이지 않다. 그래서 답을 test가 적어 두고
    // 그것만 내주는 서버를 같은 프로세스 안에 세운다.
    //  - **Winsock 위에 직접 쓴다.** 검증 대상이 HTTP 클라이언트이므로 서버를 같은
    //    라이브러리로 세우면 둘이 함께 틀려도 test는 통과한다 — 서로 다른 층이어야
    //    한쪽의 잘못이 드러난다.
    //  - **한 연결에 요청 하나다.** 언제나 "Connection: close"를 붙이고 답한 뒤
    //    끊는다. keep-alive는 범위 밖이고, keep-alive로 연 클라이언트는 그저
    //    끊기는 것을 본다.
    //  - **던지지 않는다.** 서지 못하면 `port()`가 0이다 (`load_image_file`이
    //    빈 이미지를 답하는 것과 같은 규약이다). thread 안에서 튀어나온 예외는
    //    그 연결 하나만 버리고 삼킨다 — thread 밖으로 나가면 std::terminate다.
    //  - **thread**: 받아들이는 thread 하나 + 연결마다 thread 하나다. `set_handler`·
    //    `requests()`·`stop()`은 어느 thread에서 불러도 된다.
    class loopback_http_server
    {
    public:
        // WSAStartup부터 listen까지 하고 받아들이는 thread를 띄운다.
        // 어디서든 실패하면 조용히 선 채로 아무것도 하지 않는다 — `port()`가 0이다.
        loopback_http_server();
        loopback_http_server(const loopback_http_server&) = delete;
        loopback_http_server(loopback_http_server&&) = delete;
        loopback_http_server& operator=(const loopback_http_server&) = delete;
        loopback_http_server& operator=(loopback_http_server&&) = delete;
        ~loopback_http_server();

        // 실제로 잡은 포트다. 0이면 서지 못한 것이다.
        // 포트를 고정하지 않는 이유는 test가 병렬로 돌기 때문이다 — 고정 포트는
        // 같은 기계에서 도는 다른 test와 부딪힌다.
        [[nodiscard]] std::uint16_t port() const noexcept;
        // "http://127.0.0.1:<port>" 뒤에 받은 글을 그대로 붙인다.
        // 앞의 빗금까지가 부르는 쪽의 몫이다 (붙이지 않으면 붙이지 않은 채로 나간다).
        [[nodiscard]] std::u8string url(std::string_view path_and_query) const;
        // 요청 하나마다 부를 함수다. 비어 있으면 기본값으로 되돌린다.
        // 갈아 끼우는 것은 언제든 안전하다 — 이미 답하는 중인 연결은 옛 함수로 끝난다.
        void set_handler(std::function<loopback_response(const loopback_request&)> handler);
        // 지금까지 받은 요청 전부의 복사본이다.
        [[nodiscard]] std::vector<loopback_request> requests() const;
        [[nodiscard]] std::size_t request_count() const noexcept;
        // 아직 살아 있는 연결 thread 수다 (누수·종료 검증의 자리다).
        [[nodiscard]] std::size_t open_connections() const noexcept;
        // 듣기를 접고, 살아 있는 연결을 전부 깨워 끝내고, thread를 모두 거둔다.
        // 여러 번 불러도 된다 (소멸자가 다시 부른다).
        //  - **기다리는 자리는 전부 깨진다.** 잠(`delay_*`)도 `hang_forever`도
        //    조건 변수 위에 있고, recv에 막힌 연결은 shutdown이 푼다.
        void stop() noexcept;

    private:
        struct connection
        {
            // 연결 thread가 끝내면서 `loopback_no_socket`으로 비운다.
            // 비우는 것도 stop()이 읽는 것도 connections_mutex_ 아래다 — 그래야
            // 이미 닫힌 손잡이에 stop()이 손대지 않는다.
            std::uintptr_t socket { loopback_no_socket };
            std::thread worker {};
            std::atomic<bool> finished { false };
        };

        void accept_loop();
        void serve_and_finish(connection& record) noexcept;
        // 답을 다 쓴 뒤 **거칠게** 끊어야 하면 참이다.
        bool serve_connection(std::uintptr_t client);
        bool write_response(std::uintptr_t client, const loopback_response& response);
        [[nodiscard]] std::uintptr_t take_socket(connection& record);
        // connections_mutex_를 쥔 채로 부른다.
        void reap_finished_connections();
        [[nodiscard]] bool stopping() const noexcept;
        // stop()이 오면 참으로, 시간이 다하면 거짓으로 돌아온다.
        [[nodiscard]] bool wait_for_stop(std::chrono::milliseconds delay);
        void wait_until_stop();

        std::uintptr_t listen_socket_ { loopback_no_socket };
        std::uint16_t port_ { 0 };
        bool winsock_ready_ { false };
        std::atomic<bool> stopping_ { false };
        std::atomic<bool> stopped_ { false };
        std::atomic<std::size_t> request_count_ { 0 };
        std::atomic<std::size_t> live_connections_ { 0 };
        mutable std::mutex stop_mutex_ {};
        std::condition_variable stop_signal_ {};
        mutable std::mutex handler_mutex_ {};
        std::function<loopback_response(const loopback_request&)> handler_ {};
        mutable std::mutex requests_mutex_ {};
        std::vector<loopback_request> requests_ {};
        mutable std::mutex connections_mutex_ {};
        // 항목의 주소가 흔들리면 연결 thread가 쥔 참조가 무너진다 — 그래서 손잡이다.
        std::vector<std::unique_ptr<connection>> connections_ {};
        std::thread accept_thread_ {};
    };

    // 아무도 듣지 않는 포트 하나를 사는 동안 붙잡고 있는 값이다.
    //
    // **묶기만 하고 듣지 않는다.** 그 포트로 오는 SYN에는 RST가 돌아오므로 연결이
    // 언제나 거절되고, 묶어 두었으니 test가 도는 동안 다른 프로그램이 그 번호를
    // 가져가 답을 뒤집는 일도 없다.
    //  - 서버를 세웠다 멈추는 흉내와 다른 점이 그 둘이다. 멈춘 포트는 곧바로 남의
    //    것이 될 수 있고, 그때 test는 "거절"이 아니라 남의 답을 본다.
    //  - **던지지 않는다.** 서지 못하면 `port()`가 0이다 (`loopback_http_server`와
    //    같은 규약이다).
    class loopback_dead_port
    {
    public:
        loopback_dead_port();
        loopback_dead_port(const loopback_dead_port&) = delete;
        loopback_dead_port(loopback_dead_port&&) = delete;
        loopback_dead_port& operator=(const loopback_dead_port&) = delete;
        loopback_dead_port& operator=(loopback_dead_port&&) = delete;
        ~loopback_dead_port();

        // 붙잡은 포트다. 0이면 잡지 못한 것이다.
        [[nodiscard]] std::uint16_t port() const noexcept;
        // "http://127.0.0.1:<port>" 뒤에 받은 글을 그대로 붙인다.
        [[nodiscard]] std::u8string url(std::string_view path_and_query) const;

    private:
        std::uintptr_t socket_ { loopback_no_socket };
        std::uint16_t port_ { 0 };
        bool winsock_ready_ { false };
    };
} // namespace luil::testing
