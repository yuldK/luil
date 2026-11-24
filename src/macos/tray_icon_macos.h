#pragma once

#include "host/tray_sync.h"
#include "luil/app/tray_icon.h"
#include "luil/ui/ui_events.h"
#include "luil/win32/win32_window.h"

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>

// 메뉴 막대의 상태 항목(NSStatusItem)으로 세우는 알림 영역 아이콘이다 (tray-icon-design.md의 macOS 갈래).
//  - 어떤 호출을 할지는 core의 `tray_sync`가 고른다. 여기는 상태 항목을 세우고 그림·툴팁을 옮기고 결과를 되돌린다.
//  - 메뉴 막대에는 탐색기 재시작도 셸 시간 초과도 없다. 보고는 `off`·`shown`·`failed`만 낸다 (`waiting`이 없다).
//  - 사건 규칙은 Win32와 같다. 메뉴가 있으면 오른쪽 클릭은 메뉴를 띄우고 `context`를 내지 않는다. 메뉴는 고른 키를
//    `menu->select`로만 내고 `after_select`는 쓰지 않는다 (시스템 메뉴는 스스로 닫힌다).
namespace luil::macos {
    // 상태 항목 그림의 한 변이다 (점). 메뉴 막대 두께(22~24점) 안에 든다.
    inline constexpr double tray_icon_points { 18.0 };

    // 상태 항목 단추를 누른 마우스 단추다. `none`은 마우스가 아닌 누름이다 (VoiceOver의 누름 동작).
    enum class tray_mouse_button
    {
        none,
        left,
        right,
    };

    // 단추의 동작 하나에서 복사해 둔 값이다. AppKit을 모르게 둔다 — 풀이를 core test로 돌린다.
    struct tray_click
    {
        tray_mouse_button button { tray_mouse_button::none };
        // 마우스 사건의 `clickCount`다.
        int click_count { 1 };
        // Control을 누른 채였다. Control+왼쪽 클릭은 macOS의 오른쪽 클릭이다.
        bool control { false };
        screen_point point {};
    };

    // 누름 하나를 사건으로 푼다.
    //  - 왼쪽 두 번째 누름(`click_count` 2 이상)은 `double_click`이다. 첫 누름은 `select`로 먼저 나갔다.
    //  - 오른쪽 누름과 Control+왼쪽 누름은 `context`다. 메뉴를 띄울지는 부른 쪽이 그 순간의 요청으로 정한다.
    //  - 마우스가 아닌 누름은 키보드 `select`다.
    [[nodiscard]] inline tray_icon_event tray_event_of_click(const tray_click& click) noexcept
    {
        tray_icon_event event {};
        event.point = click.point;
        switch (click.button)
        {
        case tray_mouse_button::left:
            if (click.control)
                event.action = tray_icon_action::context;
            else if (click.click_count >= 2)
                event.action = tray_icon_action::double_click;
            return event;
        case tray_mouse_button::right:
            event.action = tray_icon_action::context;
            return event;
        case tray_mouse_button::none:
            break;
        }
        event.keyboard = true;
        return event;
    }

    // AppKit의 화면 점(왼쪽 아래 원점) → luil의 화면 픽셀이다 (`screen_point`).
    //  - 원점은 주 화면의 왼쪽 위이고 아래로 늘어난다. 단위는 점에 주 화면의 배율을 곱한 픽셀이다 (macos_app.mm과 같다).
    //  - `primary_height`는 주 화면의 높이(점)이고 `scale`은 그 배율이다. 0 이하의 배율은 1이다.
    [[nodiscard]] inline screen_point tray_screen_point(const double x, const double y, const double primary_height, const double scale) noexcept
    {
        const double factor { scale > 0.0 ? scale : 1.0 };
        return { static_cast<int>(std::lround(x * factor)), static_cast<int>(std::lround((primary_height - y) * factor)) };
    }

    // core의 상태를 보고로 옮긴다. 기다릴 셸이 없으므로 `waiting`(더하기 실패)은 `failed`다.
    [[nodiscard]] inline win32::tray_status tray_status_of(const tray_sync_state state) noexcept
    {
        switch (state)
        {
        case tray_sync_state::shown:
            return win32::tray_status::shown;
        case tray_sync_state::waiting:
        case tray_sync_state::failed:
            return win32::tray_status::failed;
        case tray_sync_state::off:
            break;
        }
        return win32::tray_status::off;
    }

    // 앱의 알림 영역 아이콘 하나다. UI thread(메인 thread)에서만 부르고, `NSApp`이 선 뒤에 만든다.
    //  - 그림은 `choose_tray_image`로 고른다. 메뉴 막대가 밝으면(상태 항목 단추의 모양이 Aqua 쪽이면) `light_icon`이다.
    //    비면 앱 아이콘(`NSApp.applicationIconImage`)이다. 템플릿 그림으로 두지 않는다 (앱 그림의 색을 그대로 보인다).
    //  - 툴팁은 단추의 `toolTip`이자 보조 기술의 이름이다. 비면 앱 이름이다.
    //  - 사건은 마지막 `update`가 받은 요청의 `activate`·`menu`로 낸다 (메뉴와 `activate`는 견주지 않는다).
    //  - 메뉴 막대의 밝기와 화면 배율이 바뀌면 스스로 다시 맞춘다. 고대비는 부른 쪽이 `update`로 알린다.
    class tray_icon_host
    {
    public:
        tray_icon_host();
        tray_icon_host(const tray_icon_host&) = delete;
        tray_icon_host(tray_icon_host&&) = delete;
        tray_icon_host& operator=(const tray_icon_host&) = delete;
        tray_icon_host& operator=(tray_icon_host&&) = delete;
        // 걸린 상태 항목을 거둔다. 보고는 내지 않는다.
        ~tray_icon_host();

        // `activate`와 메뉴 선택이 낸 행동을 넘기는 길이다 (host의 `dispatch`). 빈 행동(`std::monostate`)도 넘긴다.
        void set_dispatch(std::function<void(input_action)> dispatch);
        // 보고할 상태가 바뀌었을 때 한 번 불린다 (`window_delegate::make_tray_message`로 넘길 값이다).
        void set_report(std::function<void(const win32::tray_report&)> report);
        // `activate`·`select`가 던진 예외를 알리는 길이다 (영문 진단). 비면 조용히 버린다.
        void set_error_log(std::function<void(const std::u8string&)> log);

        // 바라는 아이콘을 메뉴 막대에 맞춘다. frame이 바뀌거나 고대비가 바뀔 때 부른다. 바뀐 것이 없으면 아무 일도 없다.
        //  - 메뉴가 떠 있는 동안 아이콘이 빠지면 메뉴를 닫고, 맞추기는 메뉴가 닫힌 뒤에 한다.
        void update(const std::optional<tray_icon>& wanted, bool high_contrast);

        // 마지막으로 보고한 상태다.
        [[nodiscard]] const win32::tray_report& report() const noexcept;

        struct impl;

    private:
        std::unique_ptr<impl> impl_;
    };
}
