#pragma once

#include "host/notification_queue.h"
#include "luil/app/app_host.h"
#include "luil/app/notification.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// macOS OS 알림(UserNotifications)의 AppKit을 모르는 몫이다 (os-notification-design.md의 "macOS").
// 알림 센터를 부르는 쪽(notification_macos.mm)이 값을 옮겨 오고 여기서 정한다. core test에서 돈다.
namespace luil::macos {
    // 알림 범주 id다. 사용자가 닫은 것(dismiss)도 응답으로 오게 하는 범주이고, 라이브러리가 올리는 알림은 모두 이것이다.
    inline constexpr std::u8string_view notification_category { u8"luil.default" };

    // `UNAuthorizationStatus`를 옮긴 값이다.
    enum class notification_authorization
    {
        not_determined, // 아직 묻지 않았다
        denied,
        authorized,
        provisional, // 묻지 않고 알림 센터에만 조용히 들인다
        other,       // 이 판이 모르는 값이다
    };

    // 알림 설정에서 읽은 것이다.
    struct notification_settings
    {
        notification_authorization authorization { notification_authorization::not_determined };
        // 배너·알림이 뜨는가 (`alertSetting`).
        bool alerts { true };
        // 알림 센터에 남는가 (`notificationCenterSetting`).
        bool center { true };
    };

    // 설정을 공개 답으로 옮긴다.
    //  - 묻지 않았으면 `permission_needed`(앱이 `app_permission::notifications`를 묻는다), 거절했으면 `blocked`다.
    //  - 허락했어도 배너와 알림 센터를 모두 껐으면 보일 자리가 없어 `blocked`다. 모르는 판의 값은 막지 않는다.
    [[nodiscard]] notification_availability notification_availability_of(const notification_settings& settings) noexcept;

    // 설정을 권한 상태로 옮긴다. 허락(조용한 허락 포함)만 `granted`이고 묻지 않은 것과 거절은 `denied`다.
    [[nodiscard]] permission_state notification_permission_of(notification_authorization authorization) noexcept;

    // `UNNotificationRequest`로 옮길 값이다.
    struct notification_request_content
    {
        // 요청 id다. 같은 id로 다시 올리면 알림 센터가 그 알림을 바꾼다.
        std::u8string identifier {};
        std::u8string title {};
        std::u8string body {};
        // 기본 소리를 단다. `silent`면 소리가 없다.
        bool sound { true };
        std::u8string category { notification_category };
    };

    // 알림을 요청 값으로 옮긴다. id·UTF-8 규칙 밖이면 빈 값과 `error`다 (`prepare_notification_text`와 같은 규칙과 자르기).
    //  - 그림은 여기서 다루지 않는다. 파일로 써서 붙이는 것은 플랫폼 쪽이다.
    [[nodiscard]] std::optional<notification_request_content> make_notification_request(const notification& value, std::u8string& error);

    // 사용자가 알림에 한 일이다 (`UNNotificationResponse::actionIdentifier`).
    enum class notification_response_action
    {
        open,    // 알림을 눌렀다 (`UNNotificationDefaultActionIdentifier`)
        dismiss, // 닫았다 (`UNNotificationDismissActionIdentifier`, 범주의 `CustomDismissAction`이 있어야 온다)
        other,   // 그 밖의 동작이다. 라이브러리는 단추를 달지 않으므로 누름으로 본다
    };

    // 응답을 사건으로 옮긴다. id가 규칙 밖이면(이 라이브러리가 올린 것이 아니다) 사건이 없다.
    //  - `launched`는 눌림에만 싣는다.
    [[nodiscard]] std::optional<notification_event> notification_event_from_response(std::u8string_view identifier, notification_response_action action, bool launched);

    // 눌림이 꺼져 있던 앱을 띄웠는지 가린다. 메인 thread에서만 쓴다.
    //  - 앱이 시작을 마치기 전(`applicationDidFinishLaunching` 전)에 온 눌림은 띄운 눌림이다.
    //  - 시작을 마칠 때 AppKit이 알림으로 떴다고 알렸으면(`NSApplicationLaunchUserNotificationKey`) 그 뒤 첫 눌림도 그렇다.
    class notification_launch_tracker
    {
    public:
        void finish_launching(bool by_notification) noexcept;
        // 이 눌림이 앱을 띄웠는가. 띄운 눌림은 한 번만 참이다.
        [[nodiscard]] bool take() noexcept;
        [[nodiscard]] bool finished() const noexcept;

    private:
        bool finished_ { false };
        bool pending_ { false };
    };

    // 사건을 host의 받는 자리로 넘긴다. 받는 자리가 서기 전의 사건은 쥐었다가 설 때 차례대로 낸다. 메인 thread에서만 쓴다.
    //  - 쥔 사건은 `notification_queue`의 규칙을 따른다 (64개 상한, 넘치면 오래된 것을 버리고 `lost_before`에 센다).
    //  - 받는 자리가 던지면 그 사건은 버린다.
    class notification_relay
    {
    public:
        // 비운 자리(`nullptr`)를 주면 다음 사건부터 다시 쥔다.
        void set_receiver(std::function<void(notification_event)> receiver);
        void deliver(notification_event event);
        [[nodiscard]] std::size_t pending() const;

    private:
        void flush();

        std::function<void(notification_event)> receiver_ {};
        notification_queue pending_ {};
    };
}
