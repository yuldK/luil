#pragma once

#include "luil/ui/commands.h"
#include "luil/ui/rich_text.h"
#include "luil/ui/ui_element.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 서식 명령의 id다. 앱의 명령 처리가 `rich_text_command_of`로 명령을 되찾는다.
    inline constexpr std::u8string_view rich_text_bold_command_id { u8"format.bold" };
    inline constexpr std::u8string_view rich_text_italic_command_id { u8"format.italic" };
    inline constexpr std::u8string_view rich_text_underline_command_id { u8"format.underline" };
    inline constexpr std::u8string_view rich_text_code_command_id { u8"format.code" };
    inline constexpr std::u8string_view rich_text_link_command_id { u8"format.link" };
    inline constexpr std::u8string_view rich_text_clear_format_command_id { u8"format.clear" };

    // 메뉴·팔레트에 보일 명령 이름이다. 비면 라이브러리 기본 문구다 (굵게, 기울임꼴, 밑줄, 코드, 링크, 서식 지우기).
    struct rich_text_command_labels
    {
        std::u8string bold {};
        std::u8string italic {};
        std::u8string underline {};
        std::u8string code {};
        std::u8string link {};
        std::u8string clear_format {};
    };

    // { 굵게(Ctrl+B), 기울임(Ctrl+I), 밑줄(Ctrl+U), 코드, 링크(Ctrl+K), 서식 지우기 }다. 범위는 `focused`이고 모두 켠다.
    //  - 코드와 서식 지우기는 기본 단축키가 없다. Ctrl+E는 2단계 가운데 정렬의 자리이고 Ctrl+Space는 IME 전환과 겹치는
    //    환경이 있다. 사용자가 다시 정한 단축키(`user_shortcuts`)는 앱이 결과에 채운다.
    //  - 서식 넷과 링크는 체크 명령이고 `checked`가 `state`를 따른다. 서식 지우기는 체크가 없다.
    //
    // **판정용 표는 정적이다.** 판정은 input thread가 하고 칸의 기록은 logic thread의 것이라 판정이 편집 상태를 읽을 수
    // 없다. 키 처리기에는 `make_rich_text_commands({})`를 넘기고, 체크와 켜짐은 메뉴·팔레트에 보이는 표만
    // `rich_text_state_of`로 짓는다 (실행 취소 명령표의 같은 규칙).
    [[nodiscard]] std::vector<app_command> make_rich_text_commands(const rich_text_state& state, const rich_text_command_labels& labels = {});

    // 명령 id의 서식 명령이다. 서식 명령표의 id가 아니면 없다.
    [[nodiscard]] std::optional<rich_text_command> rich_text_command_of(std::u8string_view id);

    // 서식 칸에 다는 키 처리기다 (`ui_element::set_key_handler`).
    //  - 칸이 쓰지 않는 Ctrl+B·I·U·K는 칸의 처리기에 닿는다. 명령표를 `match_command`(범위 `focused`)로 맞추고
    //    `make_action`이 지은 앱 메시지 하나를 낸다. 맞지 않는 키는 소비하지 않는다 (위로 흐른다).
    //  - 명령표는 정적 판정용 표다. 사용자 단축키를 반영하려면 앱이 `user_shortcuts`를 채운 표를 넘긴다.
    [[nodiscard]] key_handler make_rich_text_key_handler(std::vector<app_command> commands, std::function<input_action(rich_text_command)> make_action);
}
