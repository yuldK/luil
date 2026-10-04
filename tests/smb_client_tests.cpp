#include "luil/net/smb_client.h"

#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// libsmb2 위의 SMB client를 기기에서 돌린다. 서버 없이 볼 수 있는 것만 본다 — 실제 서버와의 왕복은 사람 손으로
// 확인한다 (docs/smb-client-design.md).
namespace {
    using namespace std::chrono_literals;
    using luil::net::smb_client;
    using luil::net::smb_error_kind;
    using luil::net::smb_request;
    using luil::net::smb_response;

    class collector
    {
    public:
        void add(smb_response response)
        {
            const std::lock_guard lock { mutex_ };
            responses_.push_back(std::move(response));
            arrived_.notify_all();
        }

        [[nodiscard]] bool wait_for(const std::size_t count, const std::chrono::milliseconds limit)
        {
            std::unique_lock lock { mutex_ };
            return arrived_.wait_for(lock, limit, [this, count] { return responses_.size() >= count; });
        }

        [[nodiscard]] std::vector<smb_response> snapshot() const
        {
            const std::lock_guard lock { mutex_ };
            return responses_;
        }

    private:
        mutable std::mutex mutex_ {};
        std::condition_variable arrived_ {};
        std::vector<smb_response> responses_ {};
    };

    [[nodiscard]] std::unique_ptr<smb_client> make_client(collector& sink, const std::size_t max_in_flight = 4)
    {
        luil::net::smb_client_config config {};
        config.deliver = [&sink](smb_response response) { sink.add(std::move(response)); };
        config.max_in_flight = max_in_flight;
        std::u8string error {};
        auto client { smb_client::create(std::move(config), error) };
        REQUIRE(client != nullptr);
        return client;
    }

    // 연결은 받지만(커널의 대기열) 한 바이트도 답하지 않는 서버다. 협상 응답을 영영 기다리게 한다.
    class silent_server
    {
    public:
        silent_server()
        {
            socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            socklen_t length { sizeof(address) };
            if (socket_ >= 0 && ::bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && ::listen(socket_, 8) == 0
                && ::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &length) == 0)
                port_ = ntohs(address.sin_port);
        }

        silent_server(const silent_server&) = delete;
        silent_server& operator=(const silent_server&) = delete;

        ~silent_server()
        {
            if (socket_ >= 0)
                ::close(socket_);
        }

        [[nodiscard]] std::u8string server() const
        {
            const std::string text { "127.0.0.1:" + std::to_string(port_) };
            return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
        }

        [[nodiscard]] std::uint16_t port() const noexcept
        {
            return port_;
        }

    private:
        int socket_ { -1 };
        std::uint16_t port_ { 0 };
    };

    // 방금 닫은 포트다. 연결하면 거절된다.
    [[nodiscard]] std::u8string closed_server()
    {
        std::uint16_t port { 0 };
        {
            const silent_server temporary {};
            port = temporary.port();
        }
        const std::string text { "127.0.0.1:" + std::to_string(port) };
        return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
    }

    [[nodiscard]] smb_request list_on(std::u8string server, const std::chrono::milliseconds timeout)
    {
        smb_request request {};
        request.location.server = std::move(server);
        request.location.share = u8"photos";
        request.location.user = u8"reader";
        request.location.password = u8"secret";
        request.timeout = timeout;
        return request;
    }
} // namespace

TEST_CASE("The Android SMB build is supported and refuses a bad request before the wire", "[net][smb][android]")
{
    REQUIRE(smb_client::supported());
    collector sink {};
    const auto client { make_client(sink) };
    smb_request request { list_on(u8"nas box", 1s) };
    std::u8string error {};
    CHECK_FALSE(static_cast<bool>(client->send(request, error)));
    CHECK(error.empty() == false);
    CHECK(client->in_flight() == 0u);
}

TEST_CASE("A closed port answers cannot_connect", "[net][smb][android]")
{
    collector sink {};
    const auto client { make_client(sink) };
    std::u8string error {};
    const luil::net::smb_ticket ticket { client->send(list_on(closed_server(), 5s), error) };
    REQUIRE(static_cast<bool>(ticket));
    REQUIRE(sink.wait_for(1, 10s));
    const smb_response response { sink.snapshot().at(0) };
    CHECK(response.ticket == ticket);
    CHECK(response.error.kind == smb_error_kind::cannot_connect);
    // 오류 글에 암호가 들어가지 않는다.
    CHECK(response.error.message.find(u8"secret") == std::u8string::npos);
}

TEST_CASE("A server that never answers runs into the timeout", "[net][smb][android]")
{
    const silent_server server {};
    REQUIRE(server.port() != 0);
    collector sink {};
    const auto client { make_client(sink) };
    std::u8string error {};
    const auto started { std::chrono::steady_clock::now() };
    REQUIRE(static_cast<bool>(client->send(list_on(server.server(), 300ms), error)));
    REQUIRE(sink.wait_for(1, 10s));
    CHECK(sink.snapshot().at(0).error.kind == smb_error_kind::timed_out);
    CHECK(std::chrono::steady_clock::now() - started < 3s);
}

TEST_CASE("Cancelling a waiting request answers it once as cancelled", "[net][smb][android]")
{
    const silent_server server {};
    collector sink {};
    const auto client { make_client(sink, 1) };
    std::u8string error {};
    const luil::net::smb_ticket ticket { client->send(list_on(server.server(), 10s), error) };
    REQUIRE(static_cast<bool>(ticket));
    // 상한이 1이라 두 번째는 숨은 줄 없이 거절된다.
    CHECK_FALSE(static_cast<bool>(client->send(list_on(server.server(), 10s), error)));
    CHECK(client->in_flight() == 1u);
    client->cancel(ticket);
    REQUIRE(sink.wait_for(1, 5s));
    CHECK(sink.snapshot().at(0).error.kind == smb_error_kind::cancelled);
    // 모르는 표의 취소는 아무 일도 없다.
    client->cancel(luil::net::smb_ticket { 999 });
    CHECK_FALSE(sink.wait_for(2, 300ms));
}

TEST_CASE("Overlapping requests create and tear down their contexts safely", "[net][smb][android]")
{
    // 닫힌 포트는 연결이 바로 실패해, worker들이 libsmb2 문맥을 겹쳐 만들고 거둔다 (전역 문맥 목록의 경합 자리).
    constexpr std::size_t requests { 200 };
    constexpr std::size_t width { 8 };
    collector sink {};
    const auto client { make_client(sink, width) };
    const std::u8string server { closed_server() };
    std::size_t sent { 0 };
    const auto deadline { std::chrono::steady_clock::now() + 60s };
    while (sent < requests && std::chrono::steady_clock::now() < deadline)
    {
        std::u8string error {};
        if (static_cast<bool>(client->send(list_on(server, 5s), error)))
        {
            ++sent;
            continue;
        }
        // 포화다. 숨은 줄이 없으므로 자리가 날 때까지 기다린다.
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(sent == requests);
    REQUIRE(sink.wait_for(requests, 60s));
    for (const smb_response& response : sink.snapshot())
        CHECK(response.error.kind == smb_error_kind::cannot_connect);
}

TEST_CASE("Stopping answers nothing more and returns within its budget", "[net][smb][android]")
{
    const silent_server server {};
    collector sink {};
    const auto client { make_client(sink) };
    std::u8string error {};
    REQUIRE(static_cast<bool>(client->send(list_on(server.server(), 10s), error)));
    REQUIRE(static_cast<bool>(client->send(list_on(server.server(), 10s), error)));
    const auto started { std::chrono::steady_clock::now() };
    client->stop();
    CHECK(std::chrono::steady_clock::now() - started < 2s);
    CHECK_FALSE(static_cast<bool>(client->send(list_on(server.server(), 10s), error)));
    CHECK_FALSE(sink.wait_for(1, 300ms));
    client->stop();
}
