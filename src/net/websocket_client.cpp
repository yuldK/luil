#include "luil/net/websocket_client.h"

#include "net/websocket_transport.h"

#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace luil::net {
    bool websocket_utf8(const std::span<const std::uint8_t> bytes) noexcept
    {
        for (std::size_t at { 0 }; at < bytes.size();)
        {
            const auto first { bytes[at++] };
            if (first < 0x80)
                continue;
            const int count { first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2 : first >= 0xf0 && first <= 0xf4 ? 3 : -1 };
            if (count < 0 || bytes.size() - at < static_cast<std::size_t>(count))
                return false;
            const auto next { bytes[at] };
            if ((first == 0xe0 && next < 0xa0) || (first == 0xed && next >= 0xa0) || (first == 0xf0 && next < 0x90) || (first == 0xf4 && next >= 0x90))
                return false;
            for (int index { 0 }; index < count; ++index)
                if ((bytes[at++] & 0xc0) != 0x80)
                    return false;
        }
        return true;
    }

    bool websocket_close_code(const std::uint16_t code) noexcept
    {
        return (code >= 1000 && code <= 1014 && code != 1004 && code != 1005 && code != 1006) || (code >= 3000 && code <= 4999);
    }

    struct websocket_client::engine
    {
        struct outgoing
        {
            std::vector<std::uint8_t> bytes {};
            bool text { false };
            bool close { false };
            std::uint16_t code { 1000 };
        };
        enum class close_state
        {
            none,
            queued,
            sending,
            sent,
            failed,
        };

        websocket_request request {};
        websocket_client_config config {};
        std::unique_ptr<websocket_transport> transport {};
        std::mutex mutex {};
        std::mutex stop_mutex {};
        std::condition_variable changed {};
        std::deque<outgoing> pending {};
        std::size_t pending_bytes { 0 };
        bool opened { false };
        close_state close_progress { close_state::none };
        bool writing_data { false };
        bool peer_closed { false };
        bool finished { false };
        bool stopped { false };
        http_error send_error {};
        std::uint64_t sequence { 0 };
        std::thread reader {};
        std::thread writer {};

        void deliver(websocket_event event) noexcept
        {
            event.sequence = ++sequence;
            if (config.deliver != nullptr)
                try
                {
                    config.deliver(std::move(event));
                }
                catch (...)
                {
                }
        }

        [[nodiscard]] bool send_close(const std::uint16_t code, const std::u8string_view reason)
        {
            http_error error {};
            const bool sent { transport->close(code, reason, error) };
            if (sent == false && error.empty())
                error = { .kind = http_error_kind::system_error, .message = u8"WebSocket close could not be sent." };
            {
                const std::scoped_lock lock { mutex };
                close_progress = sent ? close_state::sent : close_state::failed;
                if (sent == false)
                    send_error = std::move(error);
            }
            changed.notify_all();
            return sent;
        }

        [[nodiscard]] bool respond_to_close(const std::uint16_t code, const std::u8string_view reason, http_error& error)
        {
            std::unique_lock lock { mutex };
            // writer가 다음 항목을 꺼내기 전에 대기 데이터를 버린다. 이미 시작한 송신과 Close는 끝날 때까지 기다린다.
            peer_closed = true;
            pending.clear();
            pending_bytes = 0;
            changed.notify_all();
            const auto idle = [&] {
                return stopped || send_error.empty() == false || (writing_data == false && close_progress != close_state::sending);
            };
            if (changed.wait_for(lock, config.close_timeout, idle) == false)
            {
                error = { .kind = http_error_kind::timed_out, .message = u8"Timed out waiting to send WebSocket close." };
                return false;
            }
            if (stopped || send_error.empty() == false)
                return false;
            if (close_progress == close_state::sent)
                return true;

            // peer_closed를 본 writer는 새 항목을 꺼내지 않는다. 예약만 된 로컬 Close 대신 여기서 응답한다.
            close_progress = close_state::sending;
            lock.unlock();
            return send_close(code == 1005 ? 1000 : code, reason);
        }

        void read() noexcept
        {
            http_error error {};
            std::uint16_t code { 1006 };
            std::u8string reason {};
            try
            {
                if (transport->open(request, config.max_message_bytes, error))
                {
                    {
                        const std::scoped_lock lock { mutex };
                        opened = stopped == false;
                    }
                    changed.notify_all();
                    bool live { false };
                    {
                        const std::scoped_lock lock { mutex };
                        live = stopped == false;
                    }
                    if (live)
                        deliver({ .kind = websocket_event_kind::opened });
                    std::vector<std::uint8_t> message {};
                    bool text { false };
                    bool fragment { false };
                    for (;;)
                    {
                        websocket_packet packet {};
                        if (transport->receive(packet, config.max_message_bytes - message.size(), error) == false)
                            break;
                        if (packet.closed)
                        {
                            if ((packet.code != 1005 && websocket_close_code(packet.code) == false) || websocket_utf8(packet.bytes) == false)
                            {
                                error = { .kind = http_error_kind::system_error, .message = u8"Invalid WebSocket close payload." };
                                break;
                            }
                            std::u8string peer_reason {};
                            if (packet.bytes.empty() == false)
                                peer_reason.assign(reinterpret_cast<const char8_t*>(packet.bytes.data()), packet.bytes.size());
                            if (respond_to_close(packet.code, peer_reason, error))
                            {
                                code = packet.code;
                                reason = std::move(peer_reason);
                            }
                            break;
                        }
                        if (fragment == false)
                            text = packet.text;
                        if (packet.text != text || packet.bytes.size() > config.max_message_bytes - message.size())
                        {
                            error = { .kind = http_error_kind::body_too_large, .message = u8"WebSocket message exceeds its limit or changes type." };
                            code = 1009;
                            break;
                        }
                        message.insert(message.end(), packet.bytes.begin(), packet.bytes.end());
                        fragment = packet.final == false;
                        if (packet.final)
                        {
                            if (text && websocket_utf8(message) == false)
                            {
                                error = { .kind = http_error_kind::system_error, .message = u8"WebSocket text is not valid UTF-8." };
                                code = 1007;
                                break;
                            }
                            deliver({ .kind = text ? websocket_event_kind::text : websocket_event_kind::binary, .data = std::make_shared<const std::vector<std::uint8_t>>(std::move(message)) });
                            message.clear();
                        }
                    }
                }
            }
            catch (...)
            {
                error = { .kind = http_error_kind::system_error, .message = u8"WebSocket worker failed." };
            }
            {
                const std::scoped_lock lock { mutex };
                opened = false;
                finished = true;
                pending.clear();
                pending_bytes = 0;
                if (send_error.empty() == false)
                    error = send_error;
                if (stopped)
                {
                    code = 1006;
                    reason.clear();
                    error = { .kind = http_error_kind::stopped, .message = u8"WebSocket stopped." };
                }
            }
            changed.notify_all();
            transport->shutdown();
            deliver({ .kind = websocket_event_kind::closed, .close_code = code, .close_reason = std::move(reason), .error = std::move(error) });
        }

        void write() noexcept
        {
            try
            {
                for (;;)
                {
                    outgoing item {};
                    {
                        std::unique_lock lock { mutex };
                        changed.wait(lock, [&] { return stopped || finished || peer_closed || (opened && pending.empty() == false); });
                        if (stopped || finished || peer_closed)
                            return;
                        item = std::move(pending.front());
                        pending.pop_front();
                        pending_bytes -= item.bytes.size();
                        if (item.close)
                            close_progress = close_state::sending;
                        else
                            writing_data = true;
                    }
                    if (item.close)
                    {
                        if (send_close(item.code, { reinterpret_cast<const char8_t*>(item.bytes.data()), item.bytes.size() }))
                        {
                            std::unique_lock lock { mutex };
                            if (changed.wait_for(lock, config.close_timeout, [&] { return stopped || finished || peer_closed; }))
                                return;
                            send_error = { .kind = http_error_kind::timed_out, .message = u8"Timed out waiting for WebSocket close." };
                        }
                        transport->shutdown();
                        return;
                    }

                    http_error error {};
                    const bool sent { transport->send(item.bytes, item.text, error) };
                    if (sent == false && error.empty())
                        error = { .kind = http_error_kind::system_error, .message = u8"WebSocket data could not be sent." };
                    {
                        const std::scoped_lock lock { mutex };
                        writing_data = false;
                        if (sent == false)
                            send_error = std::move(error);
                    }
                    changed.notify_all();
                    if (sent == false)
                    {
                        transport->shutdown();
                        return;
                    }
                }
            }
            catch (...)
            {
                {
                    const std::scoped_lock lock { mutex };
                    send_error = { .kind = http_error_kind::system_error, .message = u8"WebSocket writer failed." };
                }
                changed.notify_all();
                transport->shutdown();
            }
        }

        bool enqueue(const std::span<const std::uint8_t> bytes, const bool text, const bool close = false, const std::uint16_t code = 1000)
        {
            const std::scoped_lock lock { mutex };
            if (opened == false || close_progress != close_state::none || peer_closed || stopped || send_error.empty() == false
                || bytes.size() > config.max_message_bytes || pending.size() >= config.max_pending_messages
                || bytes.size() > config.max_pending_bytes - pending_bytes)
                return false;
            pending.push_back({ { bytes.begin(), bytes.end() }, text, close, code });
            pending_bytes += bytes.size();
            if (close)
                close_progress = close_state::queued;
            changed.notify_all();
            return true;
        }
    };

    websocket_client::websocket_client(std::unique_ptr<engine> engine) : engine_ { std::move(engine) }
    {
    }

    websocket_client::~websocket_client()
    {
        stop();
    }

    std::unique_ptr<websocket_client> websocket_client::create(websocket_request request, websocket_client_config config, std::u8string& error)
    {
        if ((request.url.starts_with(u8"ws://") == false && request.url.starts_with(u8"wss://") == false)
            || request.url.find_first_of(u8"\r\n\t #") != std::u8string::npos || request.url.find(char8_t {}) != std::u8string::npos
            || request.connect_timeout.count() <= 0 || request.connect_timeout.count() > 2147483647 || config.close_timeout.count() < 0
            || config.max_message_bytes == 0 || config.max_message_bytes > 64u * 1024u * 1024u || config.max_pending_messages == 0 || config.max_pending_bytes == 0)
        {
            error = u8"Invalid WebSocket URL or configuration.";
            return nullptr;
        }
        for (const auto character : request.url)
            if (character < 32 || character >= 127)
            {
                error = u8"WebSocket URL must be ASCII-escaped.";
                return nullptr;
            }
        const auto authority { request.url.substr(request.url.find(u8"://") + 3) };
        if (authority.substr(0, authority.find_first_of(u8"/?")).find(u8'@') != std::u8string::npos)
        {
            error = u8"WebSocket URL credentials are not supported.";
            return nullptr;
        }
        auto owned { std::make_unique<engine>() };
        owned->request = std::move(request);
        owned->config = std::move(config);
        owned->transport = make_websocket_transport();
        if (owned->transport == nullptr)
        {
            error = u8"WebSocket transport is unavailable.";
            return nullptr;
        }
        auto client { std::unique_ptr<websocket_client> { new websocket_client { std::move(owned) } } };
        client->engine_->reader = std::thread { [state = client->engine_.get()] { state->read(); } };
        client->engine_->writer = std::thread { [state = client->engine_.get()] { state->write(); } };
        return client;
    }

    bool websocket_client::send_text(const std::u8string_view text)
    {
        const std::span bytes { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() };
        return websocket_utf8(bytes) && engine_->enqueue(bytes, true);
    }

    bool websocket_client::send_binary(const std::span<const std::uint8_t> bytes)
    {
        return engine_->enqueue(bytes, false);
    }

    bool websocket_client::close(const std::uint16_t code, const std::u8string_view reason)
    {
        const std::span bytes { reinterpret_cast<const std::uint8_t*>(reason.data()), reason.size() };
        return reason.size() <= 123 && websocket_close_code(code) && websocket_utf8(bytes) && engine_->enqueue(bytes, false, true, code);
    }

    void websocket_client::stop() noexcept
    {
        assert(engine_->reader.get_id() != std::this_thread::get_id());
        const std::scoped_lock stop_lock { engine_->stop_mutex };
        {
            const std::scoped_lock lock { engine_->mutex };
            engine_->stopped = true;
        }
        engine_->changed.notify_all();
        engine_->transport->shutdown();
        if (engine_->reader.joinable())
            engine_->reader.join();
        if (engine_->writer.joinable())
            engine_->writer.join();
    }
}
