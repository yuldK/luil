#pragma once

#include "host/notification_queue.h"
#include "luil/win32/win32_window.h"

#include <windows.h>

#include <cstddef>
#include <optional>
#include <string_view>

// 주 창(win32_window.cpp)이 OS 알림과 잇는 비공개 자리다 (os-notification-design.md).
//  - 공개 함수(`show_notification` 등)는 notification_win32.cpp가 정의하고 프로세스 전역 상태 하나를 쓴다 (결정 10).
//  - OS가 thread 풀에서 낸 사건은 대기열에 넣고 붙은 주 창에 `notification_event_message`를 게시하기만 한다.
//    UI thread가 그 메시지에서 대기열을 비우며 앱에 낸다.
namespace luil::win32 {
    // 앱 식별을 세운다. `run_application_window`가 창보다 먼저 부른다. id는 미리 검사했다고 본다.
    //  - 프로세스 AUMID를 걸고 레지스트리를 쓰고 알림 개체를 얻는다. 실패해도 시작은 이어지고 실패 글을 돌려준다.
    //    그때 `notification_status`는 `unsupported`, `show_notification`은 그 글로 `failed`다.
    [[nodiscard]] platform_result apply_app_identity(const app_identity& identity, std::u8string_view fallback_display_name);

    // 식별이 서고 알림 개체를 얻었는가 (`ui_platform_capabilities::notifications`).
    [[nodiscard]] bool notifications_available() noexcept;

    // 이 실행이 알림 클릭으로 떴는지 적는다 (`notification_launch`). 활성화기의 첫 눌림이 `launched`로 가져간다.
    void set_notification_launch(bool launched) noexcept;

    // 활성화기의 클래스 개체를 건다 (결정 4의 길 b). 주 창 thread(STA)에서 사건 받는 창을 단 뒤 부른다.
    //  - 식별이 활성화기를 켰고 알림 개체를 얻었을 때만 건다. 둘째 인스턴스는 부르지 않는다 (COM이 어느 쪽으로 보낼지 모른다).
    //  - 걸면 그 뒤 올리는 토스트는 `Activated`를 잇지 않는다. 푸는 것은 `detach_notification_window`다.
    void register_notification_activator() noexcept;

    // 사건을 받을 주 창을 단다. 대기열에 이미 사건이 있으면 곧 신호를 게시한다.
    void attach_notification_window(HWND window) noexcept;
    // 그 창을 뗀다. host를 거두기 전에 부른다. 활성화기의 클래스 개체와 쥔 토스트의 처리기를 풀고, 활성화기가 없는 식별이면
    // 이 앱의 알림을 거둔다 (꺼진 뒤의 클릭은 받을 곳이 없다).
    void detach_notification_window(HWND window) noexcept;

    // UI thread가 비울 사건 대기열이다.
    [[nodiscard]] notification_queue& notification_events() noexcept;

    // 알림 센터에 남은 이 앱의 알림 수다. 식별이 없거나 읽지 못하면 빈 값이다. 실측 test가 쓴다.
    [[nodiscard]] std::optional<std::size_t> notification_history_count();

    // 어느 thread에서나 사건 하나를 넣고 붙은 창에 신호를 게시한다. OS 처리기가 쓰는 길이고 test가 같은 길로 넣는다.
    void post_notification_event(notification_event event);
}
