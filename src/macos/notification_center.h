#pragma once

#include "luil/app/app_host.h"
#include "luil/app/notification.h"

#include <functional>

// macOS OS 알림(UserNotifications)의 host 쪽 자리다 (os-notification-design.md의 "macOS"). 공개 함수는 notification.h에 있다.
// AppKit host(macos_app.mm)가 아래 셋을 잇는다.
//  - 알림 센터는 앱 번들(번들 식별자가 있는 서명된 `.app`)에서만 선다. 맨 실행 파일이면 셋 모두 알림 센터를 건드리지 않는다
//    (번들 없이 `UNUserNotificationCenter`를 부르면 예외가 난다). 그때 `notification_status()`는 `unsupported`다.
namespace luil::macos {
    // 알림 센터를 쓸 수 있는 앱 번들인가. 어느 thread에서나 부른다. 알림 센터를 건드리지 않는다.
    //  - host는 이 값을 `ui_platform_capabilities::notifications`에 싣는다. 권한이 없어도 참이다 (그것은 `permission`이 말한다).
    [[nodiscard]] bool notification_center_available();

    // 사용자가 알림에 한 일(눌림·닫음)을 받을 자리를 건다. **메인 thread에서만 부른다.** 사건도 메인 thread에서 온다.
    //  - 알림 센터의 delegate를 이때 건다. 꺼진 앱을 띄운 눌림을 받으려면 앱이 시작을 마치기 전, 곧 `[NSApp run]` 전에 부른다
    //    (AppKit이 `applicationDidFinishLaunching` 뒤에 그 눌림을 delegate에 넘긴다).
    //  - 자리가 서기 전에 온 사건(띄운 눌림 등)은 쥐었다가 걸 때 차례대로 낸다. 64개를 넘으면 오래된 것부터 버리고
    //    `notification_event::lost_before`에 센다.
    //  - host는 받은 사건을 `app_delegate::make_notification_message`로 앱 메시지로 지어 게시하고(`notification_queue`와
    //    `deliver_notification_events`가 게시 실패를 되돌린다), 눌림이면 주 창을 앞으로 세운다.
    //  - host를 거두기 전에 `nullptr`로 푼다. 그 뒤의 사건은 다시 쥔다.
    void set_notification_receiver(std::function<void(notification_event)> receiver);

    // 알림 권한의 지금 상태다 (`app_host::config::permission`의 `app_permission::notifications`). 어느 thread에서나 부른다.
    //  - 허락(조용한 허락 포함)이면 `granted`, 아직 묻지 않았거나 거절했으면 `denied`, 앱 번들이 아니면 `not_required`다.
    //  - 알림 센터에 묻고 답을 기다린다 (몇 ms). 답이 오지 않으면 `denied`다.
    [[nodiscard]] permission_state notification_permission();

    // 사용자에게 알림 권한을 묻는다 (`app_host::config::request_permission`). 어느 thread에서나 부른다.
    //  - 처음이면 OS가 허락 대화를 띄운다. 한 번 답한 뒤에는 묻지 않고 그 답을 곧 다시 준다 (바꾸는 것은 시스템 설정이다).
    //  - 참이면 물었고 `answered`가 메인 thread에서 한 번 불린다. host는 그 값을 `app_delegate::make_permission_message`로 낸다.
    //  - 거짓이면 묻지 않았다 (앱 번들이 아니거나 이미 받았다). `answered`도 불리지 않는다.
    //  - `show_notification`은 묻지 않는다. 묻지 않은 앱의 올리기는 `failed`이고 `notification_status()`가 `permission_needed`다 (Android와 같다).
    [[nodiscard]] bool request_notification_permission(std::function<void(permission_state)> answered);
}
