#pragma once

#include "luil/ui/commands.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/ui_element.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 질의에 맞는 명령의 번호다 (command-palette-design.md). 차례는 이름이 질의로 시작하는 것, 이름 안의 낱말이 질의로
    // 시작하는 것, 이름에 질의가 든 것, id에 질의가 든 것 순이고, 같은 단계 안에서는 명령표의 차례다.
    //  - 대소문자를 접는다 (`fold_case`). 앞뒤 공백은 무시하고, 빈 질의는 전부를 명령표 차례로 준다.
    [[nodiscard]] std::vector<std::size_t> filter_commands(std::span<const app_command> commands, std::u8string_view query);

    // 명령 팔레트다. 검색 칸 하나와 걸러 낸 명령 메뉴 하나다.
    struct command_palette_config
    {
        // 메뉴의 owner다 (`menu_config::owner`). 같은 화면의 다른 메뉴와 가른다.
        std::u8string owner {};
        // 단축키 판정(`match_command`)과 메뉴(`menu_item_from`)가 쓰는 그 명령표다. 줄은 `menu_item_from`으로 짓는다.
        std::vector<app_command> commands {};
        // 검색 칸의 글이다 (앱의 편집 초안). 칸의 kind는 `ui_element_kind::command_palette_search`다.
        text_input_view query {};
        std::u8string placeholder {};
        // 결과가 없을 때의 흐린 줄이다.
        std::u8string empty_label {};
        // 고른 명령의 실행 메시지다. 메뉴·단축키와 같은 메시지를 쓰면 된다.
        std::function<input_action(const std::u8string& id)> run {};
        // 실행 뒤 함께 나가는 닫기 메시지다 (`menu_config::after_select`). Esc는 정책의 `close_menu`로 간다.
        ui_action close {};
        // 한 번에 보이는 줄의 최대 수다. 넘는 결과는 잘라 낸다 — 팔레트는 고르는 자리이지 훑는 자리가 아니다.
        std::size_t maximum_rows { 12 };
    };

    // 검색 칸의 높이와 둘레 여백이다 (논리 픽셀).
    inline constexpr float command_palette_field_height { 28.0f };
    inline constexpr float command_palette_padding { 6.0f };

    // 팔레트의 줄이다. 결과가 없으면 `empty_label`의 고를 수 없는 줄 하나다.
    //  - IME 조합 중에는 `search_query`의 규칙으로 질의를 고른다 (조합 글이 아무것도 맞히지 못하면 확정 글로 물러선다).
    [[nodiscard]] std::vector<menu_item_config> command_palette_items(const command_palette_config& config);
    // 팔레트 전체의 높이다 (논리 픽셀). 줄 수에 따라 달라지므로 popup 크기를 잡을 때 쓴다.
    [[nodiscard]] float command_palette_height(const command_palette_config& config);
    // 팔레트를 짓는다. 메뉴의 `default_key`는 첫 결과라 강조가 없으면 Enter가 그것을 실행하고, `after_select`는 `close`다.
    //  - 키보드 탐색은 정책의 `menu()`가 `{ menu, menu_item }`을 답할 때 붙는다 (검색 메뉴와 같다).
    [[nodiscard]] std::unique_ptr<ui_element> make_command_palette(command_palette_config config);

    // 제목 표시줄 가운데에 두는 팔레트 진입 단추다.
    struct command_palette_entry_config
    {
        std::u8string owner {};
        std::u8string text {};
        // 툴팁이다. 끝에 단축키를 붙인다 (`label_with_shortcut`).
        std::u8string tooltip {};
        key_chord shortcut {};
        ui_action open {};
    };

    // 돋보기 글리프와 글을 단 단추다. caption client라 끌기 띠 위에서도 눌린다 (caption-button-design.md).
    [[nodiscard]] std::unique_ptr<text_button_element> make_command_palette_entry(command_palette_entry_config config);
}
