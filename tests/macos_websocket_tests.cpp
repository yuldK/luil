#include "luil/net/websocket_client.h"

#include "loopback_http_server.h"
#include "loopback_socket.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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
#include <vector>

// macOS의 WebSocket 전송(NSURLSessionWebSocketTask)을 실제 되돌이 서버와 주고받으며 본다.
// Windows의 같은 축은 외부 시험 서버가 있어야 도는 `[.live]` 시험이다 (selected_nodes_tests.cpp). 여기서는 같은 경로를
// test 안의 작은 서버가 맡아 CTest가 늘 돌린다.
namespace {
    using namespace std::chrono_literals;
    using luil::net::http_error_kind;
    using luil::net::websocket_client;
    using luil::net::websocket_client_config;
    using luil::net::websocket_event;
    using luil::net::websocket_event_kind;
    namespace loopback = luil::testing::loopback_socket;

    // ---- 핸드셰이크 계산 (RFC 6455 4.2.2의 Sec-WebSocket-Accept) ----

    [[nodiscard]] std::uint32_t rotate_left(const std::uint32_t value, const int bits) noexcept
    {
        return (value << bits) | (value >> (32 - bits));
    }

    [[nodiscard]] std::array<std::uint8_t, 20> sha1(const std::string_view text)
    {
        std::vector<std::uint8_t> message { text.begin(), text.end() };
        const std::uint64_t bit_length { static_cast<std::uint64_t>(message.size()) * 8u };
        message.push_back(0x80u);
        while (message.size() % 64u != 56u)
            message.push_back(0u);
        for (int shift { 56 }; shift >= 0; shift -= 8)
            message.push_back(static_cast<std::uint8_t>(bit_length >> shift));

        std::array<std::uint32_t, 5> state { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
        for (std::size_t block { 0 }; block < message.size(); block += 64u)
        {
            std::array<std::uint32_t, 80> words {};
            for (std::size_t index { 0 }; index < 16u; ++index)
                words[index] = (static_cast<std::uint32_t>(message[block + index * 4u]) << 24u) | (static_cast<std::uint32_t>(message[block + index * 4u + 1u]) << 16u)
                    | (static_cast<std::uint32_t>(message[block + index * 4u + 2u]) << 8u) | static_cast<std::uint32_t>(message[block + index * 4u + 3u]);
            for (std::size_t index { 16 }; index < 80u; ++index)
                words[index] = rotate_left(words[index - 3u] ^ words[index - 8u] ^ words[index - 14u] ^ words[index - 16u], 1);

            std::uint32_t a { state[0] };
            std::uint32_t b { state[1] };
            std::uint32_t c { state[2] };
            std::uint32_t d { state[3] };
            std::uint32_t e { state[4] };
            for (std::size_t index { 0 }; index < 80u; ++index)
            {
                std::uint32_t mixed { 0 };
                std::uint32_t constant { 0 };
                if (index < 20u)
                {
                    mixed = (b & c) | (~b & d);
                    constant = 0x5A827999u;
                }
                else if (index < 40u)
                {
                    mixed = b ^ c ^ d;
                    constant = 0x6ED9EBA1u;
                }
                else if (index < 60u)
                {
                    mixed = (b & c) | (b & d) | (c & d);
                    constant = 0x8F1BBCDCu;
                }
                else
                {
                    mixed = b ^ c ^ d;
                    constant = 0xCA62C1D6u;
                }
                const std::uint32_t next { rotate_left(a, 5) + mixed + e + constant + words[index] };
                e = d;
                d = c;
                c = rotate_left(b, 30);
                b = a;
                a = next;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
        }

        std::array<std::uint8_t, 20> digest {};
        for (std::size_t index { 0 }; index < 20u; ++index)
            digest[index] = static_cast<std::uint8_t>(state[index / 4u] >> (24u - (index % 4u) * 8u));
        return digest;
    }

    [[nodiscard]] std::string base64(const std::span<const std::uint8_t> bytes)
    {
        constexpr std::string_view alphabet { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" };
        std::string text {};
        for (std::size_t index { 0 }; index < bytes.size(); index += 3u)
        {
            const std::size_t count { std::min<std::size_t>(3u, bytes.size() - index) };
            std::uint32_t group { static_cast<std::uint32_t>(bytes[index]) << 16u };
            if (count > 1u)
                group |= static_cast<std::uint32_t>(bytes[index + 1u]) << 8u;
            if (count > 2u)
                group |= bytes[index + 2u];
            text.push_back(alphabet[(group >> 18u) & 0x3Fu]);
            text.push_back(alphabet[(group >> 12u) & 0x3Fu]);
            text.push_back(count > 1u ? alphabet[(group >> 6u) & 0x3Fu] : '=');
            text.push_back(count > 2u ? alphabet[group & 0x3Fu] : '=');
        }
        return text;
    }

    [[nodiscard]] std::string lowercase(std::string text)
    {
        for (char& character : text)
            if (character >= 'A' && character <= 'Z')
                character = static_cast<char>(character - 'A' + 'a');
        return text;
    }

    // ---- 서버 ----

    struct frame
    {
        bool final { true };
        std::uint8_t opcode { 0 };
        std::vector<std::uint8_t> payload {};
    };

    [[nodiscard]] std::vector<std::uint8_t> bytes_of(const std::string_view text)
    {
        return { text.begin(), text.end() };
    }

    // 길이별 경로를 맡는 작은 WebSocket 서버다. 연결마다 thread 하나가 돈다.
    //  - `/echo`: 받은 메시지를 되돌린다. `close:N`이면 N과 "asked"로 닫고, Close에는 같은 Close로 답한다.
    //  - `/remoteclose`: 열자마자 4001 "bye"로 닫는다.
    //  - `/reject`: upgrade를 403으로 거절한다.
    //  - `/big?n=N`: N바이트 바이너리 하나를 보낸 뒤 `/echo`처럼 돈다.
    //  - `/fragment`: 조각 사이에 ping을 끼운 메시지 넷을 보낸다.
    //  - `/silent`: Close에 답하지 않는다.
    class websocket_test_server
    {
    public:
        websocket_test_server()
        {
            if (loopback::startup() == false)
                return;
            listener_ = loopback::bind_loopback(true, port_);
            if (listener_ != luil::testing::loopback_no_socket)
                acceptor_ = std::thread { [this] { accept_loop(); } };
        }

        websocket_test_server(const websocket_test_server&) = delete;
        websocket_test_server& operator=(const websocket_test_server&) = delete;

        ~websocket_test_server()
        {
            stopping_.store(true);
            if (acceptor_.joinable())
                acceptor_.join();
            {
                const std::scoped_lock lock { mutex_ };
                for (const std::uintptr_t socket : sockets_)
                    loopback::shutdown_both(socket);
            }
            for (std::thread& worker : workers_)
                worker.join();
            for (const std::uintptr_t socket : sockets_)
                loopback::close(socket);
            if (listener_ != luil::testing::loopback_no_socket)
                loopback::close(listener_);
            loopback::cleanup();
        }

        [[nodiscard]] std::uint16_t port() const noexcept
        {
            return port_;
        }

        [[nodiscard]] std::u8string url(const std::string_view path) const
        {
            const std::string text { "ws://127.0.0.1:" + std::to_string(port_) + std::string { path } };
            return { text.begin(), text.end() };
        }

    private:
        void accept_loop()
        {
            while (stopping_.load() == false)
            {
                bool failed { false };
                if (loopback::wait_readable(listener_, 50ms, failed) == false)
                {
                    if (failed)
                        return;
                    continue;
                }
                bool transient { false };
                const std::uintptr_t client { loopback::accept(listener_, transient) };
                if (client == luil::testing::loopback_no_socket)
                {
                    if (transient)
                        continue;
                    return;
                }
                const std::scoped_lock lock { mutex_ };
                sockets_.push_back(client);
                workers_.emplace_back([this, client] { serve(client); });
            }
        }

        [[nodiscard]] static bool read_exact(const std::uintptr_t socket, std::uint8_t* data, std::size_t size)
        {
            while (size != 0)
            {
                const loopback::receive_result result { loopback::receive(socket, data, size) };
                if (result.status != loopback::receive_status::data)
                    return false;
                data += result.size;
                size -= result.size;
            }
            return true;
        }

        [[nodiscard]] static bool write_all(const std::uintptr_t socket, const std::span<const std::uint8_t> bytes)
        {
            std::size_t sent { 0 };
            while (sent < bytes.size())
            {
                const int written { loopback::send(socket, bytes.data() + sent, bytes.size() - sent) };
                if (written <= 0)
                    return false;
                sent += static_cast<std::size_t>(written);
            }
            return true;
        }

        [[nodiscard]] static bool send_frame(const std::uintptr_t socket, const std::uint8_t opcode, const std::span<const std::uint8_t> payload, const bool final = true)
        {
            std::vector<std::uint8_t> bytes { static_cast<std::uint8_t>((final ? 0x80u : 0u) | opcode) };
            if (payload.size() < 126u)
                bytes.push_back(static_cast<std::uint8_t>(payload.size()));
            else if (payload.size() < 65536u)
            {
                bytes.push_back(126u);
                bytes.push_back(static_cast<std::uint8_t>(payload.size() >> 8u));
                bytes.push_back(static_cast<std::uint8_t>(payload.size()));
            }
            else
            {
                bytes.push_back(127u);
                for (int shift { 56 }; shift >= 0; shift -= 8)
                    bytes.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(payload.size()) >> shift));
            }
            bytes.insert(bytes.end(), payload.begin(), payload.end());
            return write_all(socket, bytes);
        }

        [[nodiscard]] static bool send_close(const std::uintptr_t socket, const std::uint16_t code, const std::string_view reason)
        {
            std::vector<std::uint8_t> payload { static_cast<std::uint8_t>(code >> 8u), static_cast<std::uint8_t>(code) };
            payload.insert(payload.end(), reason.begin(), reason.end());
            return send_frame(socket, 8u, payload);
        }

        [[nodiscard]] static std::optional<frame> read_frame(const std::uintptr_t socket)
        {
            std::array<std::uint8_t, 2> head {};
            if (read_exact(socket, head.data(), head.size()) == false)
                return std::nullopt;
            frame result {};
            result.final = (head[0] & 0x80u) != 0;
            result.opcode = static_cast<std::uint8_t>(head[0] & 0x0Fu);
            std::uint64_t length { static_cast<std::uint64_t>(head[1] & 0x7Fu) };
            if (length == 126u || length == 127u)
            {
                std::array<std::uint8_t, 8> extended {};
                const std::size_t size { length == 126u ? 2u : 8u };
                if (read_exact(socket, extended.data(), size) == false)
                    return std::nullopt;
                length = 0;
                for (std::size_t index { 0 }; index < size; ++index)
                    length = (length << 8u) | extended[index];
            }
            std::array<std::uint8_t, 4> mask {};
            const bool masked { (head[1] & 0x80u) != 0 };
            if (masked && read_exact(socket, mask.data(), mask.size()) == false)
                return std::nullopt;
            result.payload.resize(static_cast<std::size_t>(length));
            if (read_exact(socket, result.payload.data(), result.payload.size()) == false)
                return std::nullopt;
            if (masked)
                for (std::size_t index { 0 }; index < result.payload.size(); ++index)
                    result.payload[index] ^= mask[index % 4u];
            return result;
        }

        // 핸드셰이크를 읽고 경로를 돌려준다. upgrade 요청이 아니면 빈 글이다.
        [[nodiscard]] static std::string read_handshake(const std::uintptr_t socket, std::string& key)
        {
            std::string request {};
            std::array<char, 1024> buffer {};
            while (request.find("\r\n\r\n") == std::string::npos)
            {
                const loopback::receive_result result { loopback::receive(socket, buffer.data(), buffer.size()) };
                if (result.status != loopback::receive_status::data)
                    return {};
                request.append(buffer.data(), result.size);
            }
            const std::size_t target_start { request.find(' ') + 1u };
            const std::string target { request.substr(target_start, request.find(' ', target_start) - target_start) };
            const std::string lower { lowercase(request) };
            const std::size_t at { lower.find("\r\nsec-websocket-key:") };
            if (at == std::string::npos)
                return {};
            std::size_t value { at + std::string_view { "\r\nsec-websocket-key:" }.size() };
            while (request[value] == ' ')
                ++value;
            key = request.substr(value, request.find("\r\n", value) - value);
            return target;
        }

        void serve(const std::uintptr_t socket)
        {
            loopback::set_receive_timeout(socket, 20s);
            std::string key {};
            const std::string target { read_handshake(socket, key) };
            if (target.empty())
                return;
            if (target == "/reject")
            {
                constexpr std::string_view refusal { "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" };
                static_cast<void>(write_all(socket, bytes_of(refusal)));
                loopback::shutdown_send(socket);
                return;
            }
            const std::array<std::uint8_t, 20> digest { sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11") };
            const std::string accept { "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + base64(digest) + "\r\n\r\n" };
            if (write_all(socket, bytes_of(accept)) == false)
                return;

            if (target == "/remoteclose" && send_close(socket, 4001, "bye") == false)
                return;
            if (target.starts_with("/big?n="))
            {
                const std::vector<std::uint8_t> big(static_cast<std::size_t>(std::stoul(target.substr(7))), std::uint8_t { 0x5A });
                if (send_frame(socket, 2u, big) == false)
                    return;
            }
            if (target == "/fragment")
            {
                const std::string korean { reinterpret_cast<const char*>(u8"한") };
                const std::string rest { korean.substr(2) + reinterpret_cast<const char*>(u8" ✓") };
                // 첫 메시지는 조각 둘 사이에 ping을 끼운다. 넷째는 한 글자를 바이트 중간에서 가른다.
                const bool first { send_frame(socket, 1u, bytes_of("hello "), false) && send_frame(socket, 9u, bytes_of("p")) && send_frame(socket, 0u, bytes_of("world")) };
                const bool empty { first && send_frame(socket, 1u, {}) && send_frame(socket, 2u, {}) };
                if (empty == false || send_frame(socket, 1u, bytes_of(korean.substr(0, 2)), false) == false || send_frame(socket, 0u, bytes_of(rest)) == false)
                    return;
            }

            while (stopping_.load() == false)
            {
                const std::optional<frame> received { read_frame(socket) };
                if (received.has_value() == false)
                    return;
                if (received->opcode == 8u)
                {
                    if (target != "/silent")
                    {
                        static_cast<void>(send_frame(socket, 8u, received->payload));
                        loopback::shutdown_send(socket);
                    }
                    continue;
                }
                if (received->opcode == 9u)
                {
                    static_cast<void>(send_frame(socket, 10u, received->payload));
                    continue;
                }
                if (received->opcode == 10u)
                    continue;
                const std::string text { received->payload.begin(), received->payload.end() };
                if (text.starts_with("close:"))
                {
                    static_cast<void>(send_close(socket, static_cast<std::uint16_t>(std::stoul(text.substr(6))), "asked"));
                    continue;
                }
                if (send_frame(socket, received->opcode, received->payload) == false)
                    return;
            }
        }

        std::uintptr_t listener_ { luil::testing::loopback_no_socket };
        std::uint16_t port_ { 0 };
        std::atomic<bool> stopping_ { false };
        std::thread acceptor_ {};
        std::mutex mutex_ {};
        std::vector<std::uintptr_t> sockets_ {};
        std::vector<std::thread> workers_ {};
    };

    // ---- client 쪽 ----

    struct event_log
    {
        std::mutex mutex {};
        std::condition_variable changed {};
        std::vector<websocket_event> events {};

        void push(websocket_event event)
        {
            {
                const std::scoped_lock lock { mutex };
                events.push_back(std::move(event));
            }
            changed.notify_all();
        }

        [[nodiscard]] bool wait(const std::function<bool(const std::vector<websocket_event>&)>& done, const std::chrono::milliseconds limit = 5000ms)
        {
            std::unique_lock lock { mutex };
            return changed.wait_for(lock, limit, [&] { return done(events); });
        }

        [[nodiscard]] std::vector<websocket_event> snapshot()
        {
            const std::scoped_lock lock { mutex };
            return events;
        }
    };

    [[nodiscard]] std::size_t count_kind(const std::vector<websocket_event>& events, const websocket_event_kind kind)
    {
        return static_cast<std::size_t>(std::ranges::count_if(events, [kind](const websocket_event& event) { return event.kind == kind; }));
    }

    [[nodiscard]] bool has_kind(const std::vector<websocket_event>& events, const websocket_event_kind kind)
    {
        return count_kind(events, kind) != 0;
    }

    [[nodiscard]] std::vector<websocket_event> messages(const std::vector<websocket_event>& events)
    {
        std::vector<websocket_event> data {};
        for (const websocket_event& event : events)
            if (event.kind == websocket_event_kind::text || event.kind == websocket_event_kind::binary)
                data.push_back(event);
        return data;
    }

    [[nodiscard]] std::string text_of(const websocket_event& event)
    {
        return event.data == nullptr ? std::string {} : std::string { event.data->begin(), event.data->end() };
    }

    [[nodiscard]] std::unique_ptr<websocket_client> connect(event_log& log, const std::u8string& url, websocket_client_config config = {}, const std::chrono::milliseconds connect_timeout = 3000ms)
    {
        config.deliver = [&log](websocket_event event) { log.push(std::move(event)); };
        std::u8string error {};
        auto client { websocket_client::create({ .url = url, .connect_timeout = connect_timeout }, std::move(config), error) };
        REQUIRE(client != nullptr);
        return client;
    }

    [[nodiscard]] bool opened_or_closed(const std::vector<websocket_event>& events)
    {
        return has_kind(events, websocket_event_kind::opened) || has_kind(events, websocket_event_kind::closed);
    }

    [[nodiscard]] bool closed(const std::vector<websocket_event>& events)
    {
        return has_kind(events, websocket_event_kind::closed);
    }
}

TEST_CASE("macOS WebSocket echoes text, binary, empty, Korean, and large messages and closes cleanly", "[macos][net][websocket]")
{
    websocket_test_server server {};
    REQUIRE(server.port() != 0);
    event_log log {};
    auto client { connect(log, server.url("/echo")) };
    REQUIRE(log.wait(opened_or_closed));
    REQUIRE(has_kind(log.snapshot(), websocket_event_kind::opened));

    const std::vector<std::uint8_t> binary { 0, 1, 2, 0xFF, 0x80 };
    std::vector<std::uint8_t> big(200000);
    for (std::size_t index { 0 }; index < big.size(); ++index)
        big[index] = static_cast<std::uint8_t>(index * 7u);
    REQUIRE(client->send_text(u8"hello"));
    REQUIRE(client->send_text(u8""));
    REQUIRE(client->send_text(u8"한글 메시지 ✓"));
    REQUIRE(client->send_binary(binary));
    REQUIRE(client->send_binary({}));
    REQUIRE(client->send_binary(big));
    REQUIRE(log.wait([](const auto& events) { return messages(events).size() >= 6u; }, 8000ms));

    const std::vector<websocket_event> data { messages(log.snapshot()) };
    REQUIRE(data.size() == 6u);
    CHECK(text_of(data[0]) == "hello");
    CHECK(data[1].kind == websocket_event_kind::text);
    CHECK(text_of(data[1]).empty());
    CHECK(text_of(data[2]) == std::string { reinterpret_cast<const char*>(u8"한글 메시지 ✓") });
    CHECK(data[3].kind == websocket_event_kind::binary);
    CHECK(*data[3].data == binary);
    CHECK(data[4].kind == websocket_event_kind::binary);
    CHECK(data[4].data->empty());
    CHECK(*data[5].data == big);

    REQUIRE(client->close(1000, u8"done"));
    CHECK_FALSE(client->send_text(u8"after close"));
    REQUIRE(log.wait(closed));
    const websocket_event last { log.snapshot().back() };
    CHECK(last.kind == websocket_event_kind::closed);
    CHECK(last.close_code == 1000);
    CHECK(last.close_reason == u8"done");
    CHECK(last.error.empty());
    client->stop();
    client->stop();
}

TEST_CASE("macOS WebSocket reports the peer's close code and reason", "[macos][net][websocket]")
{
    websocket_test_server server {};
    REQUIRE(server.port() != 0);
    event_log log {};

    SECTION("the server closes first")
    {
        auto client { connect(log, server.url("/remoteclose")) };
        REQUIRE(log.wait(closed));
        client->stop();
        const std::vector<websocket_event> events { log.snapshot() };
        CHECK(count_kind(events, websocket_event_kind::closed) == 1u);
        CHECK(events.back().close_code == 4001);
        CHECK(events.back().close_reason == u8"bye");
        CHECK(events.back().error.empty());
    }
    SECTION("the server closes when asked")
    {
        auto client { connect(log, server.url("/echo")) };
        REQUIRE(log.wait(opened_or_closed));
        REQUIRE(client->send_text(u8"close:3005"));
        REQUIRE(log.wait(closed));
        client->stop();
        CHECK(log.snapshot().back().close_code == 3005);
        CHECK(log.snapshot().back().close_reason == u8"asked");
        CHECK(log.snapshot().back().error.empty());
    }
}

TEST_CASE("macOS WebSocket reassembles fragments with an interleaved ping", "[macos][net][websocket]")
{
    websocket_test_server server {};
    REQUIRE(server.port() != 0);
    event_log log {};
    auto client { connect(log, server.url("/fragment")) };
    REQUIRE(log.wait([](const auto& events) { return messages(events).size() >= 4u || closed(events); }));
    client->stop();
    const std::vector<websocket_event> data { messages(log.snapshot()) };
    REQUIRE(data.size() == 4u);
    CHECK(text_of(data[0]) == "hello world");
    CHECK(data[1].kind == websocket_event_kind::text);
    CHECK(data[1].data->empty());
    CHECK(data[2].kind == websocket_event_kind::binary);
    CHECK(data[2].data->empty());
    CHECK(text_of(data[3]) == std::string { reinterpret_cast<const char*>(u8"한 ✓") });
}

TEST_CASE("macOS WebSocket answers a refused upgrade and a closed port without opening", "[macos][net][websocket]")
{
    websocket_test_server server {};
    REQUIRE(server.port() != 0);
    event_log log {};

    SECTION("the server refuses the upgrade")
    {
        auto client { connect(log, server.url("/reject")) };
        REQUIRE(log.wait(closed));
        client->stop();
        const std::vector<websocket_event> events { log.snapshot() };
        CHECK_FALSE(has_kind(events, websocket_event_kind::opened));
        CHECK(events.back().close_code == 1006);
        CHECK(events.back().error.kind == http_error_kind::cannot_connect);
    }
    SECTION("nothing listens on the port")
    {
        const luil::testing::loopback_dead_port dead {};
        REQUIRE(dead.port() != 0);
        const std::string url { "ws://127.0.0.1:" + std::to_string(dead.port()) + "/" };
        auto client { connect(log, std::u8string { url.begin(), url.end() }) };
        REQUIRE(log.wait(closed));
        client->stop();
        const std::vector<websocket_event> events { log.snapshot() };
        CHECK_FALSE(has_kind(events, websocket_event_kind::opened));
        CHECK(events.back().error.empty() == false);
    }
}

TEST_CASE("macOS WebSocket refuses a message over the limit and takes one at the limit", "[macos][net][websocket]")
{
    websocket_test_server server {};
    REQUIRE(server.port() != 0);
    event_log log {};
    websocket_client_config config {};
    config.max_message_bytes = 50000;

    SECTION("over the limit")
    {
        auto client { connect(log, server.url("/big?n=60000"), config) };
        REQUIRE(log.wait(closed, 8000ms));
        client->stop();
        const std::vector<websocket_event> events { log.snapshot() };
        CHECK(count_kind(events, websocket_event_kind::binary) == 0u);
        CHECK(events.back().error.kind == http_error_kind::body_too_large);
    }
    SECTION("at the limit")
    {
        auto client { connect(log, server.url("/big?n=50000"), config) };
        REQUIRE(log.wait([](const auto& events) { return has_kind(events, websocket_event_kind::binary) || closed(events); }));
        client->stop();
        const std::vector<websocket_event> data { messages(log.snapshot()) };
        REQUIRE(data.size() == 1u);
        CHECK(data[0].data->size() == 50000u);
    }
}

TEST_CASE("macOS WebSocket keeps an idle connection open past connect_timeout", "[macos][net][websocket]")
{
    websocket_test_server server {};
    REQUIRE(server.port() != 0);
    event_log log {};
    auto client { connect(log, server.url("/echo"), {}, 500ms) };
    REQUIRE(log.wait(opened_or_closed));
    REQUIRE(has_kind(log.snapshot(), websocket_event_kind::opened));
    std::this_thread::sleep_for(1500ms);
    CHECK_FALSE(closed(log.snapshot()));
    REQUIRE(client->send_text(u8"after idle"));
    REQUIRE(log.wait([](const auto& events) { return messages(events).empty() == false || closed(events); }));
    CHECK(text_of(messages(log.snapshot()).at(0)) == "after idle");
    client->stop();
}

// NSURLSession은 로컬 Close를 보내고 곧 task를 접는다. 상대가 답하지 않아도 보낸 코드로 깨끗하게 닫힌다
// (Windows는 `close_timeout`까지 답을 기다린다 — net/macos/websocket_transport.mm의 머리 주석).
TEST_CASE("macOS WebSocket finishes a local close without waiting for a silent peer", "[macos][net][websocket]")
{
    websocket_test_server server {};
    REQUIRE(server.port() != 0);
    event_log log {};
    websocket_client_config config {};
    config.close_timeout = 3000ms;
    auto client { connect(log, server.url("/silent"), config) };
    REQUIRE(log.wait(opened_or_closed));
    const auto started { std::chrono::steady_clock::now() };
    REQUIRE(client->close(1000, u8"bye"));
    REQUIRE(log.wait(closed));
    CHECK(std::chrono::steady_clock::now() - started < 2000ms);
    client->stop();
    CHECK(log.snapshot().back().close_code == 1000);
    CHECK(log.snapshot().back().error.empty());
}

TEST_CASE("macOS WebSocket stop interrupts a receive and senders promptly", "[macos][net][websocket]")
{
    websocket_test_server server {};
    REQUIRE(server.port() != 0);
    for (int round { 0 }; round < 10; ++round)
    {
        event_log log {};
        auto client { connect(log, server.url("/echo")) };
        REQUIRE(log.wait(opened_or_closed));
        std::atomic<bool> going { true };
        std::vector<std::thread> senders {};
        for (int sender { 0 }; sender < 3; ++sender)
            senders.emplace_back([&] {
                while (going.load())
                    static_cast<void>(client->send_text(u8"spam spam spam"));
            });
        std::this_thread::sleep_for(std::chrono::milliseconds { 5 + round * 5 });
        const auto started { std::chrono::steady_clock::now() };
        client->stop();
        const auto elapsed { std::chrono::steady_clock::now() - started };
        going.store(false);
        for (std::thread& sender : senders)
            sender.join();
        CHECK(elapsed < 2000ms);
        const std::size_t count { log.snapshot().size() };
        std::this_thread::sleep_for(50ms);
        CHECK(log.snapshot().size() == count);
        CHECK(count_kind(log.snapshot(), websocket_event_kind::closed) <= 1u);
    }
}
