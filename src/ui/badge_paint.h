#pragma once

#include "luil/ui/badge_element.h"
#include "luil/ui/ui_element.h"

#include <string_view>

namespace luil {
    // 배지 알약을 그리는 규칙 한 벌이다 (`badge_element`와 탭의 배지가 같은 그림이다).
    //
    // 두 자리가 따로 그리면 한쪽만 고대비 규칙(테·글리프)을 지키게 된다. 그래서 그림은 여기 하나다.
    //  - 좌표와 폭은 물리 픽셀이고 글자 크기는 논리 픽셀이다 (`draw_context::scale`로 곱한다).

    // 글 폭에서 알약 폭이다 (둘 다 논리 픽셀). 그리기의 폭과 배치의 원하는 크기(`badge_element::preferred_size`)가 같은
    // 식을 쓴다.
    //  - `glyph`는 앱이 고른 글 앞 글리프가 있는가다. 심각도 tone은 늘 자기 글리프를 쓰므로 그쪽에서는 보지 않는다.
    [[nodiscard]] float badge_pill_width_for(float text_width, bool has_text, badge_tone tone, float font_size, bool glyph = false) noexcept;

    // 그 글을 담는 알약의 폭이다 (물리 픽셀). 그리기 시점의 글꼴로 잰다.
    [[nodiscard]] float badge_pill_width(const draw_context& context, std::u8string_view text, badge_tone tone, float font_size, char32_t glyph = 0);

    // 그 자리에 알약을 그린다. 폭이 좁으면 글이 잘린다.
    //  - `glyph`는 neutral·accent 배지의 글 앞 글리프다 (0이면 없다). 심각도 tone은 심각도 글리프가 정한다.
    //  - `spinning`이면 그 글리프가 진행 중이라 돈다. 움직임 줄이기(`draw_context::reduce_motion`)면 멈춘 그림이다.
    void draw_badge_pill(draw_context& context, const rect_f& box, std::u8string_view text, badge_tone tone, float font_size, char32_t glyph = 0, bool spinning = false);

    // tone이 나르는 색이다 (알약과 점의 테). 탭 상태 표시도 같은 색이다.
    [[nodiscard]] ui_color badge_tone_color(const ui_color_palette& palette, badge_tone tone) noexcept;

    // 글 없는 상태 점이다. 중심과 반지름은 물리 픽셀이다.
    //  - 심각도 tone에는 쓰지 않는다. 그쪽은 글 없는 알약(글리프만)이다 — 색만으로 뜻을 말하지 않는다
    //    (`badge_element::shows_glyph`).
    //  - 점도 테를 두른다. 고대비가 채우기 색을 접어도 테가 남는다.
    void draw_badge_dot(draw_context& context, float center_x, float center_y, float radius, badge_tone tone);
}
