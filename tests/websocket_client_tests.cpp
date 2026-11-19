#include "net/websocket_transport.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace {
    using namespace std::chrono_literals;
    using namespace luil::net;

    struct transport_state
    {
        std::mutex mutex {};
        std::condition_variable changed {};
        bool opened { false };
        bool peer_ready { false };
        bool peer_delivered { false };
        bool send_released { false };
        bool close_released { false };
        bool close_fails { false };
        bool shutdown { false };
        int sends { 0 };
        int closes { 0 };
        std::uint16_t sent_code { 0 };
        std::u8string sent_reason {};
        std::optional<websocket_event> closed {};

        template<typename predicate_type>
        bool wait(const predicate_type& predicate)
        {
            std::unique_lock lock { mutex };
            return changed.wait_for(lock, 2s, predicate);
        }

        void signal(bool& flag)
        {
            const std::scoped_lock lock { mutex };
            flag = true;
            changed.notify_all();
        }
    };

    thread_local transport_state* next_transport {};

    class fake_transport final : public websocket_transport
    {
    public:
        explicit fake_transport(transport_state& state) : state_ { state }
        {
        }

        bool open(const websocket_request&, std::size_t, http_error&) override
        {
            return true;
        }

        bool receive(websocket_packet& packet, std::size_t, http_error&) override
        {
            std::unique_lock lock { state_.mutex };
            if (state_.changed.wait_for(lock, 5s, [&] { return state_.shutdown || state_.peer_ready; }) == false
                || state_.shutdown)
                return false;
            packet.closed = true;
            packet.code = 1001;
            packet.bytes = { 'p', 'e', 'e', 'r' };
            state_.peer_delivered = true;
            state_.changed.notify_all();
            return true;
        }

        bool send(std::span<const std::uint8_t>, bool, http_error&) override
        {
            std::unique_lock lock { state_.mutex };
            ++state_.sends;
            state_.changed.notify_all();
            return state_.changed.wait_for(lock, 5s, [&] { return state_.shutdown || state_.send_released; })
                && state_.shutdown == false;
        }

        bool close(const std::uint16_t code, const std::u8string_view reason, http_error&) override
        {
            std::unique_lock lock { state_.mutex };
            ++state_.closes;
            state_.sent_code = code;
            state_.sent_reason = reason;
            state_.changed.notify_all();
            return state_.changed.wait_for(lock, 5s, [&] { return state_.shutdown || state_.close_released; })
                && state_.shutdown == false && state_.close_fails == false;
        }

        void shutdown() noexcept override
        {
            state_.signal(state_.shutdown);
        }

    private:
        transport_state& state_;
    };

    struct client_fixture
    {
        transport_state state {};
        std::unique_ptr<websocket_client> client {};

        void start(const std::chrono::milliseconds timeout = 500ms)
        {
            next_transport = &state;
            std::u8string error {};
            client = websocket_client::create({ .url = u8"ws://test.invalid/" }, {
                .deliver = [&](websocket_event event) {
                    const std::scoped_lock lock { state.mutex };
                    if (event.kind == websocket_event_kind::opened)
                        state.opened = true;
                    if (event.kind == websocket_event_kind::closed)
                        state.closed = std::move(event);
                    state.changed.notify_all();
                },
                .max_pending_messages = 16384,
                .close_timeout = timeout,
            }, error);
            next_transport = nullptr;
            REQUIRE(client != nullptr);
            REQUIRE(state.wait([&] { return state.opened; }));
        }

        void finish()
        {
            REQUIRE(state.wait([&] { return state.closed.has_value(); }));
            client->stop();
        }
    };
}

namespace luil::net {
    std::unique_ptr<websocket_transport> make_websocket_transport()
    {
        return std::make_unique<fake_transport>(*next_transport);
    }
}

TEST_CASE("WebSocket sends one peer Close response before reporting a clean close", "[websocket]")
{
    client_fixture fixture {};
    fixture.state.close_released = true;
    fixture.start(0ms);
    fixture.state.signal(fixture.state.peer_ready);
    fixture.finish();
    CHECK(fixture.state.closes == 1);
    CHECK(fixture.state.sent_code == 1001);
    CHECK(fixture.state.sent_reason == u8"peer");
    CHECK(fixture.state.closed->close_code == 1001);
    CHECK(fixture.state.closed->close_reason == u8"peer");
    CHECK(fixture.state.closed->error.empty());
}

TEST_CASE("A peer WebSocket Close takes priority over pending data", "[websocket]")
{
    client_fixture fixture {};
    fixture.state.close_released = true;
    fixture.start(1500ms);
    REQUIRE(fixture.client->send_text(u8"in progress"));
    REQUIRE(fixture.state.wait([&] { return fixture.state.sends == 1; }));
    REQUIRE(fixture.client->send_text(u8"queued"));
    fixture.state.signal(fixture.state.peer_ready);

    // 쓰기 스레드가 붙잡혀 있고 큐 상한에 한참 못 미치므로, 거절을 보면 reader가 Close를 받았음을 알 수 있다.
    const auto deadline { std::chrono::steady_clock::now() + 1s };
    bool accepted { true };
    while (accepted && std::chrono::steady_clock::now() < deadline)
    {
        accepted = fixture.client->send_text({});
        if (accepted)
            std::this_thread::sleep_for(1ms);
    }
    REQUIRE_FALSE(accepted);
    {
        const std::scoped_lock lock { fixture.state.mutex };
        CHECK(fixture.state.closes == 0);
    }
    fixture.state.signal(fixture.state.send_released);
    fixture.finish();
    CHECK(fixture.state.sends == 1);
    CHECK(fixture.state.closes == 1);
    CHECK(fixture.state.sent_code == 1001);
    CHECK(fixture.state.closed->error.empty());
}

TEST_CASE("A queued local WebSocket Close cannot complete the exchange while data is blocked", "[websocket]")
{
    client_fixture fixture {};
    fixture.start(100ms);
    REQUIRE(fixture.client->send_text(u8"in progress"));
    REQUIRE(fixture.state.wait([&] { return fixture.state.sends == 1; }));
    REQUIRE(fixture.client->send_text(u8"queued"));
    REQUIRE(fixture.client->close(1000, u8"local"));
    fixture.state.signal(fixture.state.peer_ready);
    fixture.finish();

    CHECK(fixture.state.sends == 1);
    CHECK(fixture.state.closes == 0);
    CHECK(fixture.state.closed->close_code == 1006);
    CHECK(fixture.state.closed->close_reason.empty());
    CHECK(fixture.state.closed->error.kind == http_error_kind::timed_out);
}

TEST_CASE("A peer WebSocket Close waits for an in-flight local Close and uses its result", "[websocket]")
{
    client_fixture fixture {};
    SECTION("transmission succeeds")
    {
        fixture.state.close_fails = false;
    }
    SECTION("transmission fails")
    {
        fixture.state.close_fails = true;
    }
    fixture.start();
    REQUIRE(fixture.client->close(1000, u8"local"));
    REQUIRE(fixture.state.wait([&] { return fixture.state.closes == 1; }));
    fixture.state.signal(fixture.state.peer_ready);
    REQUIRE(fixture.state.wait([&] { return fixture.state.peer_delivered; }));
    fixture.state.signal(fixture.state.close_released);
    fixture.finish();

    CHECK(fixture.state.closes == 1);
    CHECK(fixture.state.sent_code == 1000);
    CHECK(fixture.state.sent_reason == u8"local");
    if (fixture.state.close_fails)
    {
        CHECK(fixture.state.closed->close_code == 1006);
        CHECK(fixture.state.closed->error.kind == http_error_kind::system_error);
    }
    else
    {
        CHECK(fixture.state.closed->close_code == 1001);
        CHECK(fixture.state.closed->close_reason == u8"peer");
        CHECK(fixture.state.closed->error.empty());
    }
}

TEST_CASE("A local WebSocket Close times out without a peer reply", "[websocket]")
{
    client_fixture fixture {};
    fixture.state.close_released = true;
    fixture.start(100ms);
    REQUIRE(fixture.client->close());
    fixture.finish();
    CHECK(fixture.state.closes == 1);
    CHECK(fixture.state.closed->close_code == 1006);
    CHECK(fixture.state.closed->error.kind == http_error_kind::timed_out);
}

TEST_CASE("WebSocket stop interrupts an in-flight send while responding to peer Close", "[websocket]")
{
    client_fixture fixture {};
    fixture.start(30s);
    REQUIRE(fixture.client->send_text(u8"in progress"));
    REQUIRE(fixture.state.wait([&] { return fixture.state.sends == 1; }));
    fixture.state.signal(fixture.state.peer_ready);
    REQUIRE(fixture.state.wait([&] { return fixture.state.peer_delivered; }));

    const auto started { std::chrono::steady_clock::now() };
    fixture.client->stop();
    CHECK(std::chrono::steady_clock::now() - started < 2s);
    CHECK(fixture.state.closes == 0);
    REQUIRE(fixture.state.closed.has_value());
    CHECK(fixture.state.closed->close_code == 1006);
    CHECK(fixture.state.closed->error.kind == http_error_kind::stopped);
}
