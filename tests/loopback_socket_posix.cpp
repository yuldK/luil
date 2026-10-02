#include "loopback_socket.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <limits>

namespace luil::testing::loopback_socket {
    namespace {
        [[nodiscard]] int native(const std::uintptr_t socket) noexcept
        {
            return static_cast<int>(socket);
        }

        [[nodiscard]] bool loopback_address(const std::uint16_t port, sockaddr_in& address) noexcept
        {
            address = {};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            return inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1;
        }
    } // namespace

    bool startup() noexcept
    {
        return true;
    }

    void cleanup() noexcept
    {}

    std::uintptr_t bind_loopback(const bool listen, std::uint16_t& port) noexcept
    {
        // SO_REUSEADDR를 두지 않는다. POSIX에서는 뜻이 Windows와 달라, 묶기만 하고 듣지 않는
        // 죽은 포트의 거절 보장이 흐려진다.
        const int opened { socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP) };
        if (opened < 0)
            return loopback_no_socket;

        sockaddr_in address {};
        if (loopback_address(0, address) == false || bind(opened, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || (listen && ::listen(opened, SOMAXCONN) != 0))
        {
            ::close(opened);
            return loopback_no_socket;
        }

        sockaddr_in bound {};
        socklen_t bound_size { sizeof(bound) };
        if (getsockname(opened, reinterpret_cast<sockaddr*>(&bound), &bound_size) != 0)
        {
            ::close(opened);
            return loopback_no_socket;
        }
        port = ntohs(bound.sin_port);
        return static_cast<std::uintptr_t>(opened);
    }

    std::uintptr_t connect_loopback(const std::uint16_t port) noexcept
    {
        const int opened { socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP) };
        if (opened < 0)
            return loopback_no_socket;

        sockaddr_in address {};
        if (loopback_address(port, address) == false || connect(opened, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        {
            ::close(opened);
            return loopback_no_socket;
        }
        return static_cast<std::uintptr_t>(opened);
    }

    bool wait_readable(const std::uintptr_t socket, const std::chrono::microseconds limit, bool& failed) noexcept
    {
        pollfd entry {};
        entry.fd = native(socket);
        entry.events = POLLIN;
        const int ready { poll(&entry, 1, static_cast<int>(limit.count() / 1000)) };
        failed = ready < 0 && errno != EINTR;
        return ready > 0;
    }

    std::uintptr_t accept(const std::uintptr_t socket, bool& transient) noexcept
    {
        const int client { accept4(native(socket), nullptr, nullptr, SOCK_CLOEXEC) };
        if (client >= 0)
        {
            transient = false;
            return static_cast<std::uintptr_t>(client);
        }
        transient = errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED || errno == EINTR;
        return loopback_no_socket;
    }

    void set_receive_timeout(const std::uintptr_t socket, const std::chrono::milliseconds limit) noexcept
    {
        timeval timeout {};
        timeout.tv_sec = static_cast<time_t>(limit.count() / 1000);
        timeout.tv_usec = static_cast<suseconds_t>((limit.count() % 1000) * 1000);
        setsockopt(native(socket), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }

    receive_result receive(const std::uintptr_t socket, void* data, const std::size_t size) noexcept
    {
        const ssize_t received { recv(native(socket), data, size, 0) };
        if (received > 0)
            return receive_result { receive_status::data, static_cast<std::size_t>(received) };
        if (received == 0)
            return receive_result { receive_status::closed, 0 };
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return receive_result { receive_status::timed_out, 0 };
        if (errno == ECONNRESET || errno == ECONNABORTED)
            return receive_result { receive_status::reset, 0 };
        return receive_result { receive_status::failed, 0 };
    }

    int send(const std::uintptr_t socket, const void* data, const std::size_t size) noexcept
    {
        const std::size_t block { std::min<std::size_t>(size, static_cast<std::size_t>(std::numeric_limits<int>::max())) };
        // MSG_NOSIGNAL이 없으면 끊긴 상대에 보낼 때 SIGPIPE가 test 프로세스를 끝낸다.
        return static_cast<int>(::send(native(socket), data, block, MSG_NOSIGNAL));
    }

    void shutdown_send(const std::uintptr_t socket) noexcept
    {
        shutdown(native(socket), SHUT_WR);
    }

    void shutdown_both(const std::uintptr_t socket) noexcept
    {
        shutdown(native(socket), SHUT_RDWR);
    }

    void close(const std::uintptr_t socket) noexcept
    {
        ::close(native(socket));
    }

    void close_abortively(const std::uintptr_t socket) noexcept
    {
        linger option {};
        option.l_onoff = 1;
        option.l_linger = 0;
        setsockopt(native(socket), SOL_SOCKET, SO_LINGER, &option, sizeof(option));
        ::close(native(socket));
    }
} // namespace luil::testing::loopback_socket
