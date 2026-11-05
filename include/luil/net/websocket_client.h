#pragma once

#include "luil/net/http_message.h"

#include <functional>
#include <memory>
#include <span>

namespace luil::net {
    enum class websocket_event_kind { opened, text, binary, closed };

    struct websocket_event
    {
        websocket_event_kind kind { websocket_event_kind::closed };
        std::uint64_t sequence { 0 };
        // 완성된 메시지다. 빈 메시지도 배달한다. 공유 불변 값이라 app_message로 복사가 싸다.
        std::shared_ptr<const std::vector<std::uint8_t>> data {};
        std::uint16_t close_code { 1006 };
        std::u8string close_reason {};
        http_error error {};
    };

    struct websocket_request
    {
        std::u8string url {};
        std::chrono::milliseconds connect_timeout { 10000 };
    };

    struct websocket_client_config
    {
        // reader thread에서만 부른다. HTTP처럼 post_app_message로 옮긴다. 여기서 stop/소멸은 금지다.
        std::function<void(websocket_event)> deliver {};
        std::size_t max_message_bytes { 8u * 1024u * 1024u };
        std::size_t max_pending_messages { 64 };
        std::size_t max_pending_bytes { 8u * 1024u * 1024u };
        std::chrono::milliseconds close_timeout { 3000 };
    };

    // 연결 하나다. reader와 writer를 소유해 수신 대기가 송신을 막지 않는다. 재접속은 새 client로 명시한다.
    class websocket_client final
    {
    public:
        websocket_client(const websocket_client&) = delete;
        websocket_client& operator=(const websocket_client&) = delete;
        ~websocket_client();
        // URL·설정 오류는 여기서 실패한다. 연결·TLS·upgrade 오류는 closed 이벤트로 온다.
        [[nodiscard]] static std::unique_ptr<websocket_client> create(websocket_request request, websocket_client_config config, std::u8string& error);
        // 큐가 찼거나 연결이 아직 열리지 않았거나 닫는 중이면 false다. 숨은 무한 큐가 없다.
        [[nodiscard]] bool send_text(std::u8string_view text);
        [[nodiscard]] bool send_binary(std::span<const std::uint8_t> bytes);
        [[nodiscard]] bool close(std::uint16_t code = 1000, std::u8string_view reason = {});
        // 연결을 끊고 join한다. 돌아온 뒤에는 deliver가 없다. 어느 앱 thread에서든 멱등으로 부른다.
        void stop() noexcept;

    private:
        struct engine;
        explicit websocket_client(std::unique_ptr<engine> engine);
        std::unique_ptr<engine> engine_ {};
    };
}
