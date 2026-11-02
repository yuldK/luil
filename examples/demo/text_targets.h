#pragma once

#include "demo/common.h"
#include "demo/palette.h"
#include "demo/workbench_page.h"
#include "luil/ui/ui_interaction.h"

#include <optional>

namespace demo {
    // 어느 element가 텍스트 칸인지다 (`interaction_policy::text_target_of`).
    //  - 여기에 없는 kind는 편집 대상이 없어 키 입력이 닿지 않고, UIA Value도 읽기 전용으로 답한다.
    //  - 정책 밖의 함수로 둔 것은 page test가 창 없이 데모의 모든 편집 칸이 등록됐는지 묻게 하려는 것이다.
    [[nodiscard]] inline std::optional<luil::text_input_target> text_target_of_kind(const luil::ui_element_kind kind) noexcept
    {
        if (kind == kind_number_input)
            return target_number;
        if (kind == kind_note_input)
            return target_note;
        if (kind == kind_memo_input)
            return target_memo;
        if (kind == kind_link_view_input)
            return target_link_view;
        if (kind == kind_code_input)
            return target_code;
        if (kind == kind_line_break_input)
            return target_line_break;
        if (kind == kind_tool_note_input)
            return target_tool_note;
        // popup 안의 검색 칸도 여느 텍스트 칸과 같이 등록한다.
        // popup이라고 다른 경로가 아니다 (popup-ime-design.md).
        if (kind == kind_dropdown_search_input)
            return target_dropdown_search;
        if (kind == kind_tool_menu_search_input)
            return target_tool_menu_search;
        if (kind == kind_network_url_input)
            return target_network_url;
        if (kind == kind_workbench_summary_input)
            return target_workbench_summary;
        // 콤보는 root와 안쪽 입력 칸이 같은 kind다. 둘 다 같은 편집 대상이다 (combo-box-design.md).
        if (kind == kind_workbench_author_input)
            return target_workbench_author;
        if (kind == kind_workbench_diff)
            return target_workbench_diff;
        if (kind == kind_workbench_find_input)
            return target_workbench_find;
        if (kind == luil::ui_element_kind::command_palette_search)
            return target_palette;
        return std::nullopt;
    }
}
