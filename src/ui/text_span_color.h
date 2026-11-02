#pragma once

#include "luil/theme/ui_theme.h"
#include "luil/ui/text_document.h"

namespace luil {
    // 구간의 색 역할을 팔레트 칸으로 잇는다 (`text_area_element`가 그린다).
    // 비공개 헤더인 이유는 시험이 여섯 구문 역할이 각자 제 칸으로 가는지 직접 부르기 위해서다.
    //  - 모르는 값은 본문색이다. 앞으로 역할이 늘어도 글이 사라지지 않는다.
    [[nodiscard]] inline ui_color span_foreground(const text_color_role role, const ui_color_palette& palette) noexcept
    {
        switch (role)
        {
        case text_color_role::secondary:
            return palette.secondary_foreground;
        case text_color_role::accent:
            return palette.accent;
        case text_color_role::warning:
            return palette.warning_accent;
        case text_color_role::error:
            return palette.error_accent;
        case text_color_role::keyword:
            return palette.syntax_keyword;
        case text_color_role::string:
            return palette.syntax_string;
        case text_color_role::comment:
            return palette.syntax_comment;
        case text_color_role::number:
            return palette.syntax_number;
        case text_color_role::type:
            return palette.syntax_type;
        case text_color_role::function:
            return palette.syntax_function;
        default:
            return palette.primary_foreground;
        }
    }
}
