#include "net/mdns_query.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <optional>

namespace luil::net {
    namespace {
        // mDNS 패킷의 상한이다 (점보 프레임까지, RFC 6762 17).
        constexpr std::size_t max_packet { 9000 };
        // poll 한 번이 기다리는 상한이다. 멈춤을 이만큼 안에 본다.
        constexpr int poll_slice_ms { 100 };

        // 소켓 하나를 쥐고 끝에 닫는다.
        class socket_guard
        {
        public:
            explicit socket_guard(const int family) noexcept
                : socket_ { ::socket(family, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP) }
            {
            }

            socket_guard(const socket_guard&) = delete;
            socket_guard& operator=(const socket_guard&) = delete;

            ~socket_guard()
            {
                if (socket_ >= 0)
                    ::close(socket_);
            }

            [[nodiscard]] int get() const noexcept
            {
                return socket_;
            }

        private:
            int socket_ { -1 };
        };

        [[nodiscard]] bool is_version6(const std::string& address)
        {
            return address.find(':') != std::string::npos;
        }

        // 받은 패킷 하나를 읽어 레코드를 더한다.
        void accept_packet(const std::uint8_t* const data, const std::size_t size, const std::string& scope, std::vector<dns_record>& records)
        {
            const std::optional<dns_message> message { parse_dns_message(std::span<const std::uint8_t> { data, size }) };
            if (message.has_value() == false || message->response() == false)
                return;
            for (dns_record record : message->records)
            {
                record.scope = scope;
                records.push_back(std::move(record));
            }
        }
    }

    std::vector<mdns_destination> mdns_destinations()
    {
        std::vector<mdns_destination> destinations {};
        ifaddrs* interfaces { nullptr };
        if (::getifaddrs(&interfaces) != 0)
            return destinations;
        std::vector<unsigned> version6_seen {};
        for (const ifaddrs* item { interfaces }; item != nullptr; item = item->ifa_next)
        {
            if (item->ifa_addr == nullptr || item->ifa_name == nullptr)
                continue;
            const unsigned flags { item->ifa_flags };
            if ((flags & IFF_UP) == 0 || (flags & IFF_MULTICAST) == 0 || (flags & IFF_LOOPBACK) != 0)
                continue;
            const unsigned index { ::if_nametoindex(item->ifa_name) };
            if (item->ifa_addr->sa_family == AF_INET)
            {
                char text[INET_ADDRSTRLEN] {};
                const auto* const address { reinterpret_cast<const sockaddr_in*>(item->ifa_addr) };
                if (::inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) == nullptr)
                    continue;
                destinations.push_back(mdns_destination { "224.0.0.251", 5353, text, index });
            }
            else if (item->ifa_addr->sa_family == AF_INET6 && index != 0 && std::ranges::find(version6_seen, index) == version6_seen.end())
            {
                // IPv6는 인터페이스 번호로 내보내므로 인터페이스마다 하나다.
                version6_seen.push_back(index);
                destinations.push_back(mdns_destination { "ff02::fb", 5353, {}, index });
            }
        }
        ::freeifaddrs(interfaces);
        return destinations;
    }

    bool mdns_exchange(const std::span<const std::uint8_t> query, const std::span<const mdns_destination> destinations, const std::chrono::steady_clock::time_point deadline,
        const std::function<bool(const std::vector<dns_record>&)>& stop, std::vector<dns_record>& records, std::string& error)
    {
        socket_guard version4 { AF_INET };
        socket_guard version6 { AF_INET6 };
        if (version4.get() < 0 && version6.get() < 0)
        {
            error = "Could not open a UDP socket: " + std::string { std::strerror(errno) };
            return false;
        }
        // 임시 포트에 묶는다. 응답자가 이 포트로 유니캐스트로 답한다. mDNS는 홉 수 255로 보낸다 (RFC 6762 11).
        const int hops { 255 };
        if (version4.get() >= 0)
        {
            sockaddr_in local {};
            local.sin_family = AF_INET;
            static_cast<void>(::bind(version4.get(), reinterpret_cast<sockaddr*>(&local), sizeof(local)));
            static_cast<void>(::setsockopt(version4.get(), IPPROTO_IP, IP_MULTICAST_TTL, &hops, sizeof(hops)));
        }
        if (version6.get() >= 0)
        {
            const int only { 1 };
            static_cast<void>(::setsockopt(version6.get(), IPPROTO_IPV6, IPV6_V6ONLY, &only, sizeof(only)));
            sockaddr_in6 local {};
            local.sin6_family = AF_INET6;
            static_cast<void>(::bind(version6.get(), reinterpret_cast<sockaddr*>(&local), sizeof(local)));
            static_cast<void>(::setsockopt(version6.get(), IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops)));
        }

        std::size_t sent { 0 };
        for (const mdns_destination& destination : destinations)
        {
            if (is_version6(destination.address))
            {
                if (version6.get() < 0)
                    continue;
                sockaddr_in6 target {};
                target.sin6_family = AF_INET6;
                target.sin6_port = htons(destination.port);
                if (::inet_pton(AF_INET6, destination.address.c_str(), &target.sin6_addr) != 1)
                    continue;
                target.sin6_scope_id = destination.interface_index;
                if (destination.interface_index != 0)
                    static_cast<void>(::setsockopt(version6.get(), IPPROTO_IPV6, IPV6_MULTICAST_IF, &destination.interface_index, sizeof(destination.interface_index)));
                if (::sendto(version6.get(), query.data(), query.size(), 0, reinterpret_cast<sockaddr*>(&target), sizeof(target)) >= 0)
                    ++sent;
                continue;
            }
            if (version4.get() < 0)
                continue;
            sockaddr_in target {};
            target.sin_family = AF_INET;
            target.sin_port = htons(destination.port);
            if (::inet_pton(AF_INET, destination.address.c_str(), &target.sin_addr) != 1)
                continue;
            if (destination.interface_address.empty() == false)
            {
                in_addr through {};
                if (::inet_pton(AF_INET, destination.interface_address.c_str(), &through) == 1)
                    static_cast<void>(::setsockopt(version4.get(), IPPROTO_IP, IP_MULTICAST_IF, &through, sizeof(through)));
            }
            if (::sendto(version4.get(), query.data(), query.size(), 0, reinterpret_cast<sockaddr*>(&target), sizeof(target)) >= 0)
                ++sent;
        }
        if (sent == 0)
        {
            error = destinations.empty() ? std::string { "No network interface can send multicast." } : "Could not send the query: " + std::string { std::strerror(errno) };
            return false;
        }

        std::vector<std::uint8_t> buffer(max_packet);
        while (true)
        {
            if (stop && stop(records))
                return true;
            const auto now { std::chrono::steady_clock::now() };
            if (now >= deadline)
                return true;
            pollfd polls[2] { { version4.get(), POLLIN, 0 }, { version6.get(), POLLIN, 0 } };
            const int wait { static_cast<int>(std::min<std::int64_t>(poll_slice_ms, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count() + 1)) };
            const int ready { ::poll(polls, 2, wait) };
            if (ready < 0)
            {
                if (errno == EINTR)
                    continue;
                error = "poll failed: " + std::string { std::strerror(errno) };
                return true;
            }
            for (const pollfd& item : polls)
            {
                if (item.fd < 0 || (item.revents & POLLIN) == 0)
                    continue;
                sockaddr_storage source {};
                socklen_t length { sizeof(source) };
                const ssize_t size { ::recvfrom(item.fd, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&source), &length) };
                if (size <= 0)
                    continue;
                std::string scope {};
                if (source.ss_family == AF_INET6)
                {
                    const auto* const from { reinterpret_cast<const sockaddr_in6*>(&source) };
                    char name[IF_NAMESIZE] {};
                    if (from->sin6_scope_id != 0 && ::if_indextoname(from->sin6_scope_id, name) != nullptr)
                        scope = name;
                }
                accept_packet(buffer.data(), static_cast<std::size_t>(size), scope, records);
            }
        }
    }
}
