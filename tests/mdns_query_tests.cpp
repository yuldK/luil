#include "net/mdns_query.h"

#include "dns_test_packets.h"

#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

// mDNS 한 번 질의의 소켓 쪽을 기기에서 본다. 같은 네트워크 대신 루프백의 가짜 응답자에 묻는다 — 응답자는 받은 질의를
// 보낸 주소와 포트로 답한다 (legacy unicast와 같은 길).
namespace {
    using namespace std::chrono_literals;

    // 루프백의 UDP 응답자다. 질의 하나를 받을 때마다 `reply`를 보낸 쪽에 돌려준다 (비어 있으면 답하지 않는다).
    class responder
    {
    public:
        explicit responder(std::vector<std::uint8_t> reply)
            : reply_ { std::move(reply) }
        {
            socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            socklen_t length { sizeof(address) };
            if (::bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && ::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &length) == 0)
                port_ = ntohs(address.sin_port);
            thread_ = std::thread { [this] { serve(); } };
        }

        responder(const responder&) = delete;
        responder& operator=(const responder&) = delete;

        ~responder()
        {
            running_ = false;
            thread_.join();
            ::close(socket_);
        }

        [[nodiscard]] luil::net::mdns_destination destination() const
        {
            return luil::net::mdns_destination { "127.0.0.1", port_, {}, 0 };
        }

        [[nodiscard]] int queries() const noexcept
        {
            return queries_.load();
        }

    private:
        void serve()
        {
            std::array<std::uint8_t, 2048> buffer {};
            while (running_)
            {
                pollfd probe { socket_, POLLIN, 0 };
                if (::poll(&probe, 1, 50) <= 0)
                    continue;
                sockaddr_in source {};
                socklen_t length { sizeof(source) };
                if (::recvfrom(socket_, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&source), &length) <= 0)
                    continue;
                ++queries_;
                if (reply_.empty() == false)
                    static_cast<void>(::sendto(socket_, reply_.data(), reply_.size(), 0, reinterpret_cast<sockaddr*>(&source), length));
            }
        }

        std::vector<std::uint8_t> reply_ {};
        int socket_ { -1 };
        std::uint16_t port_ { 0 };
        std::atomic<bool> running_ { true };
        std::atomic<int> queries_ { 0 };
        std::thread thread_ {};
    };

    [[nodiscard]] std::vector<std::uint8_t> browse_query()
    {
        const std::array<luil::net::dns_question, 1> questions { luil::net::dns_question { luil::net::smb_service_name(), luil::net::dns_type_ptr } };
        return luil::net::make_dns_query(1, questions);
    }
} // namespace

TEST_CASE("A one-shot query collects the unicast answer of a responder", "[net][smb][android][mdns]")
{
    const responder nas { dns_test::nas_response(1) };
    const std::array<luil::net::mdns_destination, 1> destinations { nas.destination() };
    std::vector<luil::net::dns_record> records {};
    std::string error {};
    const auto started { std::chrono::steady_clock::now() };
    // 서버 하나를 찾으면 끝낸다.
    const auto stop = [](const std::vector<luil::net::dns_record>& got) { return luil::net::collect_smb_servers(got).empty() == false; };
    REQUIRE(luil::net::mdns_exchange(browse_query(), destinations, started + 3s, stop, records, error));
    CHECK(std::chrono::steady_clock::now() - started < 2s);
    CHECK(nas.queries() == 1);
    const std::vector<luil::net::smb_server_found> servers { luil::net::collect_smb_servers(records) };
    REQUIRE(servers.size() == 1u);
    CHECK(servers[0].name == u8"Living Room NAS");
    CHECK(servers[0].host == u8"nas.local");
    REQUIRE(servers[0].addresses.empty() == false);
    // IPv4로 온 답이라 링크 로컬 IPv6의 범위를 모른다. IPv4 주소만 쓸 수 있다.
    CHECK(servers[0].addresses == std::vector<std::u8string> { u8"192.168.0.10" });
}

TEST_CASE("A query without answers ends at its deadline", "[net][smb][android][mdns]")
{
    const responder silent { {} };
    const std::array<luil::net::mdns_destination, 1> destinations { silent.destination() };
    std::vector<luil::net::dns_record> records {};
    std::string error {};
    const auto started { std::chrono::steady_clock::now() };
    REQUIRE(luil::net::mdns_exchange(browse_query(), destinations, started + 300ms, {}, records, error));
    const auto elapsed { std::chrono::steady_clock::now() - started };
    CHECK(elapsed >= 290ms);
    CHECK(elapsed < 1s);
    CHECK(records.empty());
}

TEST_CASE("A query stops early when asked to", "[net][smb][android][mdns]")
{
    const responder silent { {} };
    const std::array<luil::net::mdns_destination, 1> destinations { silent.destination() };
    std::vector<luil::net::dns_record> records {};
    std::string error {};
    std::atomic<bool> stopped { false };
    const auto stop_soon = [&stopped] {
        std::this_thread::sleep_for(150ms);
        stopped = true;
    };
    std::thread stopper { stop_soon };
    const auto started { std::chrono::steady_clock::now() };
    REQUIRE(luil::net::mdns_exchange(browse_query(), destinations, started + 5s, [&stopped](const std::vector<luil::net::dns_record>&) { return stopped.load(); }, records, error));
    stopper.join();
    CHECK(std::chrono::steady_clock::now() - started < 1s);
}

TEST_CASE("Garbage and queries from the network are ignored", "[net][smb][android][mdns]")
{
    // 응답 비트가 없는 패킷(누군가의 질의)과 깨진 패킷은 버린다.
    dns_test::packet question { 1, 0x0000 };
    question.ptr("_smb._tcp.local", "Fake._smb._tcp.local");
    const responder asking { question.finish() };
    const responder broken { { 0, 1, 0x84, 0, 0, 0, 0, 5 } };
    const std::array<luil::net::mdns_destination, 2> destinations { asking.destination(), broken.destination() };
    std::vector<luil::net::dns_record> records {};
    std::string error {};
    REQUIRE(luil::net::mdns_exchange(browse_query(), destinations, std::chrono::steady_clock::now() + 300ms, {}, records, error));
    CHECK(records.empty());
}

TEST_CASE("This device offers multicast destinations for the local network", "[net][smb][android][mdns]")
{
    // 묶음 주소와 포트만 본다 (인터페이스는 기기마다 다르다). 와이파이가 꺼져 있으면 볼 것이 없다.
    const std::vector<luil::net::mdns_destination> destinations { luil::net::mdns_destinations() };
    if (destinations.empty())
        SKIP("No multicast network interface is up on this device.");
    for (const luil::net::mdns_destination& destination : destinations)
    {
        CHECK((destination.address == "224.0.0.251" || destination.address == "ff02::fb"));
        CHECK(destination.port == 5353);
    }
}
