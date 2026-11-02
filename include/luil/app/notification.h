#pragma once

#include "luil/app/platform_services.h"
#include "luil/ui/image_element.h"

#include <cstdint>
#include <string>
#include <string_view>

// 앱 창 밖, 운영 체제의 알림 자리(Windows 토스트와 알림 센터, Android 알림 창)에 알림을 올리고 바꾸고 거둔다
// (docs/os-notification-design.md). 창 안에 그리는 `toast_stack_element`와는 다른 기능이다.
//  - 선언은 여기, 정의는 플랫폼 층에 있다 (`platform_services.h`와 같다).
//  - 사용자가 누른 알림은 `app_delegate::make_notification_message`로 UI thread에 온다.
namespace luil {
    // 올릴 알림 하나다.
    struct notification
    {
        // 앱이 정한 키다. ASCII 글자·숫자와 `.` `_` `-`로 1~64자다. 같은 id로 다시 올리면 그 알림을 바꾼다.
        //  - OS의 활성화 인자로 나가고 눌림 사건이 이것만 돌려준다. 사용자 글을 담지 않는다.
        std::u8string id {};
        // 제목과 본문이다. 제목은 256, 본문은 1024 코드 포인트를 넘으면 그 길이에 맞춰 자르고 끝을 `…`로 바꾼다.
        std::u8string title {};
        std::u8string body {}; // 비어도 된다
        // 알림 옆 그림이다. 비면 없다. 긴 변이 256 px를 넘으면 줄여 보낸다.
        ui_image image {};
        // 소리 없이 올린다.
        bool silent { false };
    };

    enum class notification_event_kind
    {
        activated, // 사용자가 눌렀다
        dismissed, // 사용자가 닫았다 (Windows만)
        failed,    // 올린 뒤 OS가 거절했다 (Windows만)
    };

    // 알림에 일어난 일이다. 앱이 거둔 알림(`close_notification`)과 시간이 지나 알림 센터로 옮겨 간 알림은 사건이 없다.
    struct notification_event
    {
        notification_event_kind kind { notification_event_kind::activated };
        std::u8string id {};
        // activated: 이 클릭이 꺼져 있던 앱을 띄웠다.
        bool launched { false };
        // failed: 개발자가 읽을 진단 영문이다.
        std::u8string error {};
        // 이 사건 앞에서 버린 사건 수다. 앱이 받지 못한 사건이 64개를 넘으면 가장 오래된 것부터 버린다.
        std::uint32_t lost_before { 0 };

        [[nodiscard]] bool operator==(const notification_event&) const = default;
    };

    enum class notification_availability
    {
        available,
        // 사용자·정책이 이 앱의 알림을 껐다.
        blocked,
        // `app_permission::notifications`를 받아야 한다.
        permission_needed,
        // 이 플랫폼·구성에는 길이 없다 (Windows 앱 식별이 없거나 서지 않았다).
        unsupported,
    };

    // 모두 어느 thread에서나 부른다.
    //  - Windows는 앱 식별(`win32::window_config::identity`)이 선 뒤에야 쓸 수 있다. `run_application_window` 전에는 `unsupported`다.
    [[nodiscard]] notification_availability notification_status();
    // 알림을 올린다. 같은 id가 있으면 바꾼다.
    //  - id가 규칙 밖이거나 글이 잘못된 UTF-8이면 OS를 부르지 않고 `failed`다.
    //  - 알림이 꺼졌거나(`blocked`) 권한이 없으면(`permission_needed`) `failed`, 길이 없으면 `unsupported`다. 까닭은 `notification_status`가 말한다.
    //  - `ok`는 OS가 받았다는 뜻이다. 배너가 실제로 떴는지는 알 수 없다 (Windows 집중 지원은 알림 센터로만 보낸다).
    [[nodiscard]] platform_result show_notification(const notification& value);
    // 그 id의 알림을 거둔다. 없는 알림을 거둬도 `ok`다.
    [[nodiscard]] platform_result close_notification(std::u8string_view id);
    // 이 앱이 올린 알림을 모두 거둔다.
    [[nodiscard]] platform_result close_all_notifications();
}
