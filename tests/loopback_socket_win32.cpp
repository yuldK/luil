#include "loopback_socket.h"

// winsock2.h는 언제나 windows.h보다 먼저다. 늦게 들어가면 windows.h가 이미 끌어온
// winsock 1과 이름이 겹쳐 수백 줄짜리 재정의 오류가 난다 — 그래서 이 파일이 winsock
// 헤더를 아는 유일한 자리이고, 여기서도 맨 앞이다.
#include <winsock2.h>

#include <windows.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <limits>

namespace luil::testing::loopback_socket {
    namespace {
        [[nodiscard]] SOCKET native(const std::uintptr_t socket) noexcept
        {
            return static_cast<SOCKET>(socket);
        }

        [[nodiscard]] int clamped_size(const std::size_t size) noexcept
        {
            return static_cast<int>(std::min<std::size_t>(size, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        }

        [[nodiscard]] bool loopback_address(const std::uint16_t port, sockaddr_in& address) noexcept
        {
            address = {};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            // inet_addr가 아니라 inet_pton이다 — 앞엣것은 폐기 표시가 붙어 /WX에서 오류다.
            return inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1;
        }
    } // namespace

    bool startup() noexcept
    {
        WSADATA winsock {};
        return WSAStartup(MAKEWORD(2, 2), &winsock) == 0;
    }

    void cleanup() noexcept
    {
        WSACleanup();
    }

    std::uintptr_t bind_loopback(const bool listen, std::uint16_t& port) noexcept
    {
        const SOCKET opened { socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) };
        if (opened == INVALID_SOCKET)
            return loopback_no_socket;

        // 포트 0은 "빈 것을 아무거나"라는 뜻이다. 실제로 무엇을 잡았는지는
        // getsockname으로 되묻는다.
        sockaddr_in address {};
        if (loopback_address(0, address) == false || bind(opened, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address))) != 0 || (listen && ::listen(opened, SOMAXCONN) != 0))
        {
            closesocket(opened);
            return loopback_no_socket;
        }

        sockaddr_in bound {};
        int bound_size { static_cast<int>(sizeof(bound)) };
        if (getsockname(opened, reinterpret_cast<sockaddr*>(&bound), &bound_size) != 0)
        {
            closesocket(opened);
            return loopback_no_socket;
        }
        port = ntohs(bound.sin_port);
        return static_cast<std::uintptr_t>(opened);
    }

    std::uintptr_t connect_loopback(const std::uint16_t port) noexcept
    {
        const SOCKET opened { socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) };
        if (opened == INVALID_SOCKET)
            return loopback_no_socket;

        sockaddr_in address {};
        if (loopback_address(port, address) == false || connect(opened, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address))) != 0)
        {
            closesocket(opened);
            return loopback_no_socket;
        }
        return static_cast<std::uintptr_t>(opened);
    }

    bool wait_readable(const std::uintptr_t socket, const std::chrono::microseconds limit, bool& failed) noexcept
    {
        fd_set readable {};
        FD_ZERO(&readable);
        FD_SET(native(socket), &readable);
        timeval timeout {};
        timeout.tv_sec = static_cast<long>(limit.count() / 1'000'000);
        timeout.tv_usec = static_cast<long>(limit.count() % 1'000'000);
        const int ready { select(0, &readable, nullptr, nullptr, &timeout) };
        failed = ready < 0;
        return ready > 0;
    }

    std::uintptr_t accept(const std::uintptr_t socket, bool& transient) noexcept
    {
        const SOCKET client { ::accept(native(socket), nullptr, nullptr) };
        if (client != INVALID_SOCKET)
        {
            transient = false;
            return static_cast<std::uintptr_t>(client);
        }
        const int error { WSAGetLastError() };
        transient = error == WSAEWOULDBLOCK || error == WSAECONNRESET || error == WSAEINTR;
        return loopback_no_socket;
    }

    void set_receive_timeout(const std::uintptr_t socket, const std::chrono::milliseconds limit) noexcept
    {
        DWORD receive_timeout { static_cast<DWORD>(limit.count()) };
        setsockopt(native(socket), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receive_timeout), static_cast<int>(sizeof(receive_timeout)));
    }

    receive_result receive(const std::uintptr_t socket, void* data, const std::size_t size) noexcept
    {
        const int received { recv(native(socket), static_cast<char*>(data), clamped_size(size), 0) };
        if (received > 0)
            return receive_result { receive_status::data, static_cast<std::size_t>(received) };
        if (received == 0)
            return receive_result { receive_status::closed, 0 };

        const int error { WSAGetLastError() };
        if (error == WSAETIMEDOUT)
            return receive_result { receive_status::timed_out, 0 };
        if (error == WSAECONNRESET || error == WSAECONNABORTED)
            return receive_result { receive_status::reset, 0 };
        return receive_result { receive_status::failed, 0 };
    }

    int send(const std::uintptr_t socket, const void* data, const std::size_t size) noexcept
    {
        return ::send(native(socket), static_cast<const char*>(data), clamped_size(size), 0);
    }

    void shutdown_send(const std::uintptr_t socket) noexcept
    {
        shutdown(native(socket), SD_SEND);
    }

    void shutdown_both(const std::uintptr_t socket) noexcept
    {
        shutdown(native(socket), SD_BOTH);
    }

    void close(const std::uintptr_t socket) noexcept
    {
        closesocket(native(socket));
    }

    void close_abortively(const std::uintptr_t socket) noexcept
    {
        linger option {};
        option.l_onoff = static_cast<u_short>(1);
        option.l_linger = static_cast<u_short>(0);
        setsockopt(native(socket), SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&option), static_cast<int>(sizeof(option)));
        closesocket(native(socket));
    }
} // namespace luil::testing::loopback_socket
