#include "net/ios/dns_sd_query.h"

#include <arpa/inet.h>
#include <dns_sd.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>

#include <algorithm>
#include <memory>
#include <optional>

namespace luil::net {
    namespace {
        // poll 한 번이 기다리는 상한이다. 멈춤을 이만큼 안에 본다.
        constexpr int poll_slice_ms { 100 };
        constexpr char smb_service_type[] { "_smb._tcp" };
        constexpr char local_domain[] { "local." };

        // 질의 하나를 쥐고 끝에 거둔다.
        class service_ref
        {
        public:
            service_ref() noexcept = default;
            service_ref(const service_ref&) = delete;
            service_ref& operator=(const service_ref&) = delete;

            ~service_ref()
            {
                if (ref_ != nullptr)
                    DNSServiceRefDeallocate(ref_);
            }

            [[nodiscard]] DNSServiceRef* out() noexcept
            {
                return &ref_;
            }

            [[nodiscard]] DNSServiceRef get() const noexcept
            {
                return ref_;
            }

        private:
            DNSServiceRef ref_ { nullptr };
        };

        [[nodiscard]] std::u8string to_u8(const std::string& text)
        {
            return { reinterpret_cast<const char8_t*>(text.data()), text.size() };
        }

        // 주소를 글로 옮긴다. 링크 로컬 IPv6는 받은 인터페이스를 붙인다 ("fe80::1%en0").
        [[nodiscard]] std::string address_text(const sockaddr* const address)
        {
            char text[INET6_ADDRSTRLEN + IF_NAMESIZE + 2] {};
            if (address->sa_family == AF_INET)
            {
                const auto* const version4 { reinterpret_cast<const sockaddr_in*>(address) };
                if (inet_ntop(AF_INET, &version4->sin_addr, text, sizeof(text)) == nullptr)
                    return {};
                return text;
            }
            if (address->sa_family != AF_INET6)
                return {};
            const auto* const version6 { reinterpret_cast<const sockaddr_in6*>(address) };
            if (inet_ntop(AF_INET6, &version6->sin6_addr, text, sizeof(text)) == nullptr)
                return {};
            std::string result { text };
            if (IN6_IS_ADDR_LINKLOCAL(&version6->sin6_addr) && version6->sin6_scope_id != 0)
            {
                char name[IF_NAMESIZE] {};
                if (if_indextoname(version6->sin6_scope_id, name) != nullptr)
                    result += std::string { "%" } + name;
            }
            return result;
        }

        // 받은 주소들이다. IPv4를 먼저 둔다.
        struct address_list
        {
            std::vector<std::string> version4 {};
            std::vector<std::string> version6 {};

            void add(const sockaddr* const address)
            {
                const std::string text { address_text(address) };
                if (text.empty())
                    return;
                auto& list { address->sa_family == AF_INET ? version4 : version6 };
                if (std::ranges::find(list, text) == list.end())
                    list.push_back(text);
            }
        };

        // 질의들의 소켓을 deadline까지 돌린다. 읽을 것이 온 질의마다 콜백을 부른다. done이나 stop이 참이면 일찍 끝낸다.
        void pump(const std::function<std::vector<DNSServiceRef>()>& refs, const std::chrono::steady_clock::time_point deadline, const std::function<bool()>& stop,
            const std::function<bool()>& done)
        {
            while (stop() == false && done() == false)
            {
                const auto now { std::chrono::steady_clock::now() };
                if (now >= deadline)
                    return;
                const std::vector<DNSServiceRef> current { refs() };
                std::vector<pollfd> fds {};
                fds.reserve(current.size());
                for (const DNSServiceRef ref : current)
                    fds.push_back(pollfd { DNSServiceRefSockFD(ref), POLLIN, 0 });
                const auto left { std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count() };
                const int wait { static_cast<int>(std::clamp<long long>(left, 0, poll_slice_ms)) };
                if (::poll(fds.data(), static_cast<nfds_t>(fds.size()), wait) <= 0)
                    continue;
                for (std::size_t index { 0 }; index < fds.size(); ++index)
                    if ((fds[index].revents & (POLLIN | POLLHUP | POLLERR)) != 0)
                        static_cast<void>(DNSServiceProcessResult(current[index]));
            }
        }

        // 찾은 인스턴스 하나다. 풀기(호스트·포트)와 주소 묻기를 차례로 건다.
        struct instance
        {
            std::string name {};
            std::uint32_t interface_index { 0 };
            std::string type {};
            std::string domain {};
            service_ref resolve {};
            service_ref addresses {};
            std::string host {};
            std::uint16_t port { 445 };
            bool resolved { false };
            address_list found {};
        };

        struct browse_state
        {
            std::vector<std::unique_ptr<instance>> instances {};
        };

        void on_address(DNSServiceRef, DNSServiceFlags, std::uint32_t, const DNSServiceErrorType error, const char*, const sockaddr* const address, std::uint32_t,
            void* const context)
        {
            if (error == kDNSServiceErr_NoError && address != nullptr)
                static_cast<instance*>(context)->found.add(address);
        }

        void on_resolve(DNSServiceRef, DNSServiceFlags, const std::uint32_t interface_index, const DNSServiceErrorType error, const char*, const char* const host,
            const std::uint16_t port, std::uint16_t, const unsigned char*, void* const context)
        {
            auto* const item { static_cast<instance*>(context) };
            if (error != kDNSServiceErr_NoError || host == nullptr || item->resolved)
                return;
            item->resolved = true;
            item->host = host;
            // 끝의 점은 떼어 "nas.local"로 둔다.
            if (item->host.ends_with('.'))
                item->host.pop_back();
            item->port = ntohs(port);
            static_cast<void>(DNSServiceGetAddrInfo(item->addresses.out(), 0, interface_index, kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6, host, on_address, item));
        }

        void on_browse(DNSServiceRef, const DNSServiceFlags flags, const std::uint32_t interface_index, const DNSServiceErrorType error, const char* const name,
            const char* const type, const char* const domain, void* const context)
        {
            if (error != kDNSServiceErr_NoError || (flags & kDNSServiceFlagsAdd) == 0 || name == nullptr)
                return;
            auto& state { *static_cast<browse_state*>(context) };
            // 같은 서버가 인터페이스마다(유선·무선) 한 번씩 온다. 첫 것만 푼다.
            if (std::ranges::any_of(state.instances, [name](const std::unique_ptr<instance>& item) { return item->name == name; }))
                return;
            auto item { std::make_unique<instance>() };
            item->name = name;
            item->interface_index = interface_index;
            item->type = type != nullptr ? type : smb_service_type;
            item->domain = domain != nullptr ? domain : local_domain;
            if (DNSServiceResolve(item->resolve.out(), 0, interface_index, item->name.c_str(), item->type.c_str(), item->domain.c_str(), on_resolve, item.get())
                != kDNSServiceErr_NoError)
                return;
            state.instances.push_back(std::move(item));
        }

        struct lookup_state
        {
            address_list found {};
        };

        void on_lookup(DNSServiceRef, DNSServiceFlags, std::uint32_t, const DNSServiceErrorType error, const char*, const sockaddr* const address, std::uint32_t,
            void* const context)
        {
            if (error == kDNSServiceErr_NoError && address != nullptr)
                static_cast<lookup_state*>(context)->found.add(address);
        }
    } // namespace

    bool browse_smb_servers(const std::chrono::steady_clock::time_point deadline, const std::function<bool()>& stop, std::vector<smb_server_found>& servers,
        std::string& error)
    {
        browse_state state {};
        service_ref browse {};
        if (const DNSServiceErrorType result { DNSServiceBrowse(browse.out(), 0, kDNSServiceInterfaceIndexAny, smb_service_type, local_domain, on_browse, &state) };
            result != kDNSServiceErr_NoError)
        {
            error = result == kDNSServiceErr_PolicyDenied ? "Local network access was denied (DNS-SD browse)." : "Could not start a DNS-SD browse: " + std::to_string(result);
            return false;
        }
        const auto refs = [&state, &browse] {
            std::vector<DNSServiceRef> current { browse.get() };
            for (const auto& item : state.instances)
                for (const DNSServiceRef ref : { item->resolve.get(), item->addresses.get() })
                    if (ref != nullptr)
                        current.push_back(ref);
            return current;
        };
        pump(refs, deadline, stop, [] { return false; });

        servers.clear();
        for (const auto& item : state.instances)
        {
            if (item->resolved == false)
                continue;
            smb_server_found server {};
            server.name = to_u8(item->name);
            server.host = to_u8(item->host);
            server.port = item->port;
            for (const auto* const list : { &item->found.version4, &item->found.version6 })
                for (const std::string& address : *list)
                    server.addresses.push_back(to_u8(address));
            servers.push_back(std::move(server));
        }
        std::ranges::sort(servers, [](const smb_server_found& left, const smb_server_found& right) { return left.name < right.name; });
        return true;
    }

    std::string resolve_local_address(const std::string& host, const std::chrono::steady_clock::time_point deadline, const std::function<bool()>& stop)
    {
        lookup_state state {};
        service_ref lookup {};
        if (DNSServiceGetAddrInfo(lookup.out(), 0, kDNSServiceInterfaceIndexAny, kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6, host.c_str(), on_lookup, &state)
            != kDNSServiceErr_NoError)
            return {};
        pump([&lookup] { return std::vector<DNSServiceRef> { lookup.get() }; }, deadline, stop, [&state] { return state.found.version4.empty() == false; });
        if (state.found.version4.empty() == false)
            return state.found.version4.front();
        if (state.found.version6.empty() == false)
            return state.found.version6.front();
        return {};
    }
}
