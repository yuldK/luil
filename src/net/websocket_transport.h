#pragma once

#include "luil/net/websocket_client.h"

namespace luil::net {
    struct websocket_packet
    {
        std::vector<std::uint8_t> bytes {};
        bool text { false };
        bool final { true };
        bool closed { false };
        std::uint16_t code { 1006 };
    };

    [[nodiscard]] bool websocket_utf8(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] bool websocket_close_code(std::uint16_t code) noexcept;

    class websocket_transport
    {
    public:
        virtual ~websocket_transport() = default;
        virtual bool open(const websocket_request& request, std::size_t limit, http_error& error) = 0;
        virtual bool receive(websocket_packet& packet, std::size_t limit, http_error& error) = 0;
        virtual bool send(std::span<const std::uint8_t> bytes, bool text, http_error& error) = 0;
        virtual bool close(std::uint16_t code, std::u8string_view reason, http_error& error) = 0;
        // open/receive/send 어느 시점이든 깨운다. thread를 join한 뒤에만 객체를 부순다.
        virtual void shutdown() noexcept = 0;
    };
    [[nodiscard]] std::unique_ptr<websocket_transport> make_websocket_transport();
}
