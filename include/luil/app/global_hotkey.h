#pragma once

#include "luil/ui/commands.h"
#include "luil/ui/ui_events.h"

#include <functional>
#include <string>

namespace luil {
    // 전역 단축키 하나다 (docs/global-hotkey-design.md). 앱이 뒤에 있거나 창이 최소화·숨김이어도 OS에서 받는 키 조합이다.
    // `ui_frame::hotkeys`에 실으면 걸리고 빼면 풀린다.
    //  - 앱 상태다. 매 frame 지금 바라는 목록을 싣고, UI thread가 바뀐 것만 OS에 옮긴다 (`taskbar_request`·`tray_icon`과 같다).
    //    견주는 값은 `id`와 `chord`이고 `pressed`는 견주지 않는다. 눌림이 올 때 그 순간의 frame에서 읽는다.
    //  - 못 거는 것이 정상 경로다. 다른 프로그램이 쓰는 조합은 걸리지 않는다. 걸렸는지는 Win32의 `hotkey_report`가 항목마다 말한다.
    //  - 하지 못하는 플랫폼(Android)은 받고 무시한다. 무엇을 하는지는 `ui_platform::capabilities::global_hotkeys`가 말한다.
    //  - 라이브러리는 거는 키가 시스템 전체 입력을 해치지 않게 거른다. 수정자 없는 글자·이동·편집 키와 OS가 예약한 조합은
    //    OS를 부르지 않고 `hotkey_status::needs_modifier`·`reserved`로 알린다. 키를 시스템 전체에서 가져가는 기능이라
    //    앱은 사용자가 켜는 설정 뒤에 두기를 권한다.
    struct global_hotkey
    {
        // 앱이 정한 키다. 목록 안에서 겹치지 않는다. 비었거나 앞 항목과 같으면 걸지 않는다 (`invalid_id`).
        std::u8string id {};
        // 조합이다. Ctrl·Shift·Alt와 키 하나다. Windows 키 조합은 못 건다 (결정 1). 앱 안 단축키(`app_command::shortcut`)와
        // 같은 값을 쓰므로 `parse_shortcut`·`shortcut_label`·키 잡기 칸이 그대로 통한다.
        key_chord chord {};
        // 눌렀을 때 UI thread에서 부른다. 눌린 순간의 frame에 실린 같은 id의 것이다. 비면 눌림을 버린다.
        //  - 한 번 눌러 한 번 온다. 누르고 있어도 반복하지 않고 뗌 사건은 없다.
        //  - 라이브러리는 눌림에서 창을 앞으로 세우지 않는다. 앱이 `main_hidden`을 끄고 `focus_request::activate`를 싣는다.
        //  - 앱이 앞에 있어도 같은 길이다. 걸린 조합은 OS가 키 입력 단계에서 가져가 앱 안 키 이벤트로 오지 않는다.
        std::function<input_action()> pressed {};
    };
}
