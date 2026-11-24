#include "luil/app/file_watcher.h"
#include "luil/app/notification.h"
#include "luil/app/secret_store.h"
#include "luil/net/http_client.h"
#include "net/websocket_transport.h"

#include <memory>
#include <string>

// macOS에 아직 길이 없는 플랫폼 서비스다. 공개 API는 다른 플랫폼과 같고, 모두 "지원 안 함"으로 답한다.
//  - 비밀 저장소(Keychain), OS 알림(UserNotifications), 파일 감시(FSEvents), HTTP·WebSocket(NSURLSession)이 설 자리다.
//  - 앱은 각 API의 실패 답을 이미 다루므로(데모의 각 페이지가 이유를 글로 보인다) 링크와 실행이 그대로 선다.
namespace luil {
    secret_store_result store_secret(std::u8string_view, std::u8string_view, std::span<const std::uint8_t>)
    {
        return { secret_store_status::unsupported, u8"The secret store is not implemented on macOS yet." };
    }

    secret_store_result load_secret(std::u8string_view, std::u8string_view, std::vector<std::uint8_t>& secret)
    {
        secret.clear();
        return { secret_store_status::unsupported, u8"The secret store is not implemented on macOS yet." };
    }

    secret_store_result erase_secret(std::u8string_view, std::u8string_view)
    {
        return { secret_store_status::unsupported, u8"The secret store is not implemented on macOS yet." };
    }

    secret_store_result migrate_legacy_win32_secret(std::u8string_view, std::u8string_view)
    {
        return { secret_store_status::unsupported, u8"There are no legacy Windows secrets on macOS." };
    }

    notification_availability notification_status()
    {
        return notification_availability::unsupported;
    }

    platform_result show_notification(const notification&)
    {
        return { platform_status::unsupported, u8"OS notifications are not implemented on macOS yet.", std::nullopt };
    }

    platform_result close_notification(std::u8string_view)
    {
        return { platform_status::unsupported, u8"OS notifications are not implemented on macOS yet.", std::nullopt };
    }

    platform_result close_all_notifications()
    {
        return { platform_status::unsupported, u8"OS notifications are not implemented on macOS yet.", std::nullopt };
    }

    bool notification_launch(std::span<const std::u8string>) noexcept
    {
        return false;
    }

    struct file_watcher::engine
    {
    };

    file_watcher::file_watcher(std::unique_ptr<engine> owned) noexcept
        : engine_ { std::move(owned) }
    {
    }

    file_watcher::~file_watcher() = default;

    std::unique_ptr<file_watcher> file_watcher::create(file_watcher_config, std::u8string& error)
    {
        error = u8"File watching is not implemented on macOS yet.";
        return nullptr;
    }

    file_watch_ticket file_watcher::add(std::u8string_view, file_watch_scope, std::u8string& error)
    {
        error = u8"File watching is not implemented on macOS yet.";
        return {};
    }

    void file_watcher::remove(file_watch_ticket) noexcept
    {
    }

    void file_watcher::stop() noexcept
    {
    }

}

namespace luil::net {
    std::unique_ptr<websocket_transport> make_websocket_transport()
    {
        return nullptr;
    }

    struct http_client::engine
    {
    };

    http_client::http_client(std::unique_ptr<engine> owned) noexcept
        : engine_ { std::move(owned) }
    {
    }

    http_client::~http_client() = default;

    std::unique_ptr<http_client> http_client::create(http_client_config, std::u8string& error)
    {
        error = u8"The HTTP client is not implemented on macOS yet.";
        return nullptr;
    }

    http_ticket http_client::send(http_request, std::u8string& error)
    {
        error = u8"The HTTP client is not implemented on macOS yet.";
        return {};
    }

    http_ticket http_client::start_heartbeat(http_request, http_heartbeat, std::u8string& error)
    {
        error = u8"The HTTP client is not implemented on macOS yet.";
        return {};
    }

    void http_client::cancel(http_ticket) noexcept
    {
    }

    void http_client::cancel_all() noexcept
    {
    }

    std::size_t http_client::in_flight() const noexcept
    {
        return 0;
    }

    void http_client::stop() noexcept
    {
    }
}
