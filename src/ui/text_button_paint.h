#pragma once

#include "luil/ui/dialog_elements.h"
#include "luil/ui/ui_element.h"

namespace luil {
    // 글자 버튼의 상자와 글을 그리는 규칙 한 벌이다 (`text_button_element`와 분할 버튼이 같은 그림이다).
    //
    // 두 자리가 따로 그리면 한 dialog에 나란히 선 두 버튼의 색이 어긋난다. 그래서 채움 종류(`text_button_fill_for`)를
    // 색으로 옮기는 판정과 글·아이콘의 배치는 여기 하나다.

    // 그리는 순간의 상호작용 상태다. 포인터가 올라 있거나 눌려 있어도 꺼진 버튼은 그 표시를 받지 않는다.
    struct text_button_state
    {
        bool enabled { true };
        bool hovered { false };
        bool pressed { false };
    };

    // 채움 종류와 상태에서 상자의 바탕색이다. `text_button_config::fill` 선택자는 여기에 얹지 않는다.
    //  - 선택자는 hover와 눌림에도 그 색 그대로라 마지막에 덮는 것이 맞고, 담는 쪽이 설정을 쥔다.
    //  - `text_button_fill::none`은 상자가 없어 부르지 않는다. 불러도 보통 버튼의 바탕을 답한다.
    [[nodiscard]] ui_color text_button_background(text_button_fill fill, const text_button_state& state, const ui_color_palette& palette) noexcept;

    // 글·아이콘이 칠해지는 색이다. 꺼진 버튼은 꺼진 글자 색이다.
    //  - 분할 버튼의 chevron이 본체 글과 같은 색을 쓰려고 따로 꺼냈다.
    [[nodiscard]] ui_color text_button_label_color(const text_button_config& config, text_button_fill fill, const text_button_state& state, const ui_color_palette& palette);

    // `box` 안에 아이콘과 글을 한 덩어리로 가운데 놓아 그린다. 글 색은 채움 종류와 상태가 정한다.
    //  - 좌표는 물리 픽셀이다 (`draw_context::scale`로 글꼴과 아이콘을 곱한다).
    void draw_text_button_label(draw_context& context, const text_button_config& config, text_button_fill fill, const text_button_state& state, const rect_f& box);
}
