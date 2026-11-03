#pragma once

#include "luil/ui/image_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/ui_events.h"

#include <functional>
#include <optional>
#include <string>

namespace luil {
    // 알림 영역(트레이) 아이콘이 앱에 돌려주는 사건이다 (docs/tray-icon-design.md).
    enum class tray_icon_action
    {
        // 왼쪽 클릭, 키보드로 고름(Enter·Space)이다.
        select,
        // 왼쪽 두 번 클릭이다. 앞에 `select`가 하나 먼저 온다고 알려져 있다 (실측 전).
        double_click,
        // 오른쪽 클릭·Shift+F10·메뉴 키다. `tray_icon::menu`가 있으면 오지 않는다 (라이브러리가 메뉴를 띄운다).
        context,
    };

    struct tray_icon_event
    {
        tray_icon_action action { tray_icon_action::select };
        // 키보드로 냈다.
        bool keyboard { false };
        // 셸이 준 화면 물리 좌표다 (아이콘 자리나 커서).
        screen_point point {};

        [[nodiscard]] bool operator==(const tray_icon_event&) const noexcept = default;
    };

    // 알림 영역의 아이콘 하나다. `ui_frame::tray`에 실으면 서고 빼면 사라진다.
    //  - 앱 상태다. 매 frame 지금 바라는 값을 싣고, UI thread가 바뀐 것만 셸에 옮긴다 (`taskbar_request`와 같다).
    //  - 그림은 몸통(`ui_image::backing`)으로, 툴팁은 값으로 견준다. 같은 `ui_image` 복사본을 frame마다 실으면 되고,
    //    새로 지으면 새 그림이다. 메뉴와 `activate`는 견주지 않는다. 사건이 올 때 그 순간의 frame에서 읽는다.
    //  - 하지 못하는 플랫폼(Android)은 받고 무시한다. 무엇을 하는지는 `ui_platform::capabilities::tray`가 말한다.
    //  - 셸에 닿았는지는 Win32의 `tray_report`가 말한다.
    struct tray_icon
    {
        // 그림이다. 비면 창 아이콘(`window_config::icon_resource`, 없으면 시스템 기본)이다.
        ui_image icon {};
        // 밝은 작업 표시줄에서 쓸 그림이다. 비면 `icon`이다.
        //  - 알림 영역은 앱 모드가 아니라 작업 표시줄의 테마를 따른다. 앱이 밝아도 작업 표시줄은 어두울 수 있다.
        ui_image light_icon {};
        // 고대비에서 쓸 그림이다. 비면 위 규칙을 따른다.
        ui_image high_contrast_icon {};
        // 툴팁이자 보조 기술이 읽는 이름이다. 비면 창 제목이다. 127 UTF-16 코드 단위에서 자른다.
        std::u8string tooltip {};
        // 오른쪽 클릭 메뉴다. 있으면 라이브러리가 시스템 메뉴로 띄우고 고른 키를 `menu->select`로 낸다. 비면 `context`
        // 사건이 온다.
        //  - 라이브러리가 읽는 칸은 `items`(글·흐림·구분선·체크·라디오·하위 메뉴·단축키 글)와 `select`다. 항목 아이콘
        //    글리프와 열림 상태 칸은 쓰지 않는다. 메뉴는 OS가 그려 앱 테마를 따르지 않는다.
        std::optional<menu_config> menu {};
        // 사건이다. UI thread에서 그 순간의 frame 것을 부른다. 비면 사건을 버린다.
        //  - 라이브러리는 사건에서 창을 앞으로 세우지 않는다. 앱이 `main_hidden`을 끄고 `focus_request::activate`를 싣는다.
        std::function<input_action(const tray_icon_event&)> activate {};
    };
}
