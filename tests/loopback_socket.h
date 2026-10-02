#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace luil::testing {
    // socket 손잡이가 없다는 표시다 (winsock의 INVALID_SOCKET과 같은 값이다).
    //
    // 손잡이는 폭이 같은 정수로 들고 다닌다 (`SOCKET`은 `UINT_PTR`이고 POSIX는 `int`다).
    // 소켓 헤더는 플랫폼마다 .cpp 하나에 가둔다 — winsock2.h는 반드시 windows.h보다 먼저
    // 들어가야 하는데, 이 도우미를 쓰는 test가 어떤 순서로 무엇을 include할지 여기서 정할
    // 수 없다.
    inline constexpr std::uintptr_t loopback_no_socket { static_cast<std::uintptr_t>(-1) };

    // loopback 서버와 그 test가 쓰는 TCP 소켓 몇 가지다 (Winsock과 POSIX 두 벌).
    namespace loopback_socket {
        // 받기 한 번의 결과다.
        enum class receive_status
        {
            data,
            // 상대가 곱게 닫았다.
            closed,
            // 받기 시간 상한이 지났다. 다시 돌아볼 일이다.
            timed_out,
            // 상대가 RST로 끊었다.
            reset,
            failed,
        };

        struct receive_result
        {
            receive_status status { receive_status::failed };
            std::size_t size { 0 };
        };

        // 프로세스의 소켓 층을 연다 (Winsock은 `WSAStartup`, POSIX는 할 일이 없다).
        // 참이면 짝으로 `cleanup`을 부른다.
        [[nodiscard]] bool startup() noexcept;
        void cleanup() noexcept;

        // 127.0.0.1의 빈 포트에 묶은 TCP 소켓이다. `listen`이 참이면 듣기까지 한다.
        // 실패하면 `loopback_no_socket`이다.
        [[nodiscard]] std::uintptr_t bind_loopback(bool listen, std::uint16_t& port) noexcept;
        // 127.0.0.1의 그 포트에 잇는다. 실패하면 `loopback_no_socket`이다.
        [[nodiscard]] std::uintptr_t connect_loopback(std::uint16_t port) noexcept;

        // 들어온 연결을 기다린다. 참이면 받아들일 것이 있다. 시간이 다하면 거짓이고
        // `failed`도 거짓이다.
        [[nodiscard]] bool wait_readable(std::uintptr_t socket, std::chrono::microseconds limit, bool& failed) noexcept;
        // 연결 하나를 받는다. 실패하면 `loopback_no_socket`이고, 다시 해 볼 실패면
        // `transient`가 참이다.
        [[nodiscard]] std::uintptr_t accept(std::uintptr_t socket, bool& transient) noexcept;
        void set_receive_timeout(std::uintptr_t socket, std::chrono::milliseconds limit) noexcept;

        [[nodiscard]] receive_result receive(std::uintptr_t socket, void* data, std::size_t size) noexcept;
        // 보낸 바이트 수다. 0 이하면 실패다. 상대가 사라진 소켓에 보내도 프로세스가
        // 죽지 않는다 (POSIX의 SIGPIPE를 막는다).
        [[nodiscard]] int send(std::uintptr_t socket, const void* data, std::size_t size) noexcept;

        // 보내는 쪽을 닫는다. 보낸 것을 마저 흘려보내는 끝맺음의 앞 절반이다.
        void shutdown_send(std::uintptr_t socket) noexcept;
        // 손잡이를 살려 둔 채 막힌 받기·보내기를 푼다.
        void shutdown_both(std::uintptr_t socket) noexcept;
        void close(std::uintptr_t socket) noexcept;
        // RST를 던지고 닫는다 (SO_LINGER {켬, 0초}).
        void close_abortively(std::uintptr_t socket) noexcept;
    } // namespace loopback_socket
} // namespace luil::testing
