#pragma once

#include "luil/ui/commands.h"

#include <array>
#include <string>
#include <string_view>

namespace luil {
    // 실행 취소·다시 실행 명령의 id다. 앱의 명령 처리가 이 id로 `undo_stack::undo`·`redo`를 고른다.
    inline constexpr std::u8string_view undo_command_id { u8"edit.undo" };
    inline constexpr std::u8string_view redo_command_id { u8"edit.redo" };

    struct undo_command_state
    {
        // 메뉴·팔레트에 보일 최종 글이다. 앱이 걸음의 이름과 낱말을 이어 짓는다 ("실행 취소: 체크").
        //  - 비면 라이브러리 기본 문구("실행 취소"·"다시 실행", docs/default-text-design.md)다. 걸음 이름을 잇는 문장은 앱 몫이다.
        // 키 판정용 표에서는 비워도 된다. 판정은 id와 단축키만 본다.
        std::u8string undo_label {};
        std::u8string redo_label {};
        bool can_undo { true };
        bool can_redo { true };
    };

    // { 실행 취소(Ctrl+Z), 다시 실행(Ctrl+Y, 추가로 Ctrl+Shift+Z) }다. 범위는 window이고 `enabled`는 `can_*`를 따른다.
    //
    // **판정용 표는 정적이다.** 단축키 판정(`on_key`)은 input thread의 정책이 하고 stack은 logic thread의 것이라 정책이 stack 상태를
    // 읽을 수 없다. 그래서 `make_undo_commands({})`는 두 명령을 모두 켠다 (`match_command`가 꺼진 명령을 건너뛰기 때문이다).
    // 빈 stack에서 Ctrl+Z가 맞으면 앱의 handler가 거짓을 받고 끝난다.
    // 메뉴·팔레트에 보이는 표만 logic thread가 stack 상태로 짓는다. 꺼진 명령은 꺼진 항목이고 `menu_item_from`이 단축키를 보인다.
    //
    // Ctrl+Z와 Ctrl+Shift+Z는 `match_command`가 수정자를 정확히 견주므로 서로 맞지 않는다.
    // 열린 메뉴나 대화 상자 뒤의 문서가 되돌아가지 않으려면 앱이 활성 범위를 `modal`로 `match_command`에 넘긴다.
    [[nodiscard]] std::array<app_command, 2> make_undo_commands(undo_command_state state);
}
