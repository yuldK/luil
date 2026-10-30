#pragma once

#include "luil/ui/text_layout.h"
#include "luil/ui/ui_element.h"

// 앱 typeface를 소유권과 함께 값으로 담는다. 설정을 지우는 모든 자리가 `SkTypeface`의 정의를 봐야 한다.
#include "include/core/SkTypeface.h"

#include <cstddef>
#include <string_view>
#include <vector>

class SkFont;
class SkPaint;

namespace luil {
    // 글이 상자 안에서 서는 가로 자리다. 줄마다 따로 맞춘다.
    enum class text_align
    {
        start,
        center,
        end
    };
    // 들어가지 않는 한 줄 글을 줄이는 자리다. `start`는 앞을 줄여 끝(파일 이름)을 남긴다.
    enum class text_elide
    {
        end,
        middle,
        start
    };
    // 글 덩어리가 상자 안에서 서는 세로 자리다.
    enum class text_vertical_align
    {
        top,
        center,
        bottom
    };

    // 직접 그리는 글의 글꼴이다.
    struct text_font
    {
        // 논리 픽셀이다. 시스템 글자 크기와 화면 배율은 글꼴을 만들 때 곱한다 (`scaled_font_size`).
        float size { 12.0f };
        // `code`인데 문맥에 고정폭 글꼴이 없으면 UI 글꼴이다.
        text_layout_face face { text_layout_face::ui };
        bool bold { false };
        // 있으면 `face` 대신 이 글꼴이다 (`load_typeface_file`로 읽은 앱 글꼴).
        sk_sp<SkTypeface> typeface {};
    };

    // 문맥의 글꼴·배율·시스템 글자 크기로 만든 그리기 글꼴이다.
    //  - `size * scale`로 손수 지은 글꼴은 시스템 글자 크기(Android의 비선형 확대)를 놓친다. 직접 그리는 element가
    //    여기서 글꼴을 얻으면 label·단추와 같은 크기다.
    [[nodiscard]] SkFont make_text_font(const draw_context& context, const text_font& font);

    // 상자 하나에 글을 놓는 규칙이다 (`draw_text_block`·`measure_text_block`).
    struct text_block_style
    {
        text_font font {};
        text_align align { text_align::start };
        text_vertical_align vertical { text_vertical_align::center };
        // 한 줄 글이 넘칠 때 줄이는 자리다. 여러 줄 글은 마지막 줄의 끝을 줄인다.
        text_elide elide { text_elide::end };
        // 상자 폭에서 줄을 바꾼다. 규칙은 여러 줄 label과 같다 — 공백에서 먼저 바꾸고 긴 낱말은 글자소 경계에서
        // 바꾼다. `\n`은 줄 바꿈이다. 거짓이면 한 줄이고 글 안의 줄바꿈은 `draw_text`의 규칙을 따른다.
        bool wrap { false };
        // 바꾼 줄의 최대 수다. 0이면 무제한이다. 넘치면 마지막 보이는 줄이 남은 글을 한 줄로 펴서 끝을 `…`로 줄인다.
        std::size_t max_lines { 0 };
        // 한 줄의 높이다 (논리 픽셀). 0이면 label의 한 줄 높이(`text_line_height(size, 7)`)다.
        float line_height { 0.0f };
        // 그림·사진 위의 글이 읽히도록 글자 둘레에 두르는 테두리다. 폭은 글자 바깥으로 나가는 두께(논리 픽셀)이고
        // 0이거나 색이 완전히 투명하면 두르지 않는다. 모든 줄의 테두리를 먼저 칠하고 글자를 얹는다.
        ui_color halo_color { 0 };
        float halo_width { 0.0f };
    };

    // 상자에 놓은 글의 줄이다 (`measure_text_block`).
    struct text_block_layout
    {
        // 보이는 줄마다의 원문 byte 구간이다. 말줄임한 마지막 줄은 남은 글 전체의 구간이다.
        struct line
        {
            std::size_t begin { 0 };
            std::size_t end { 0 };
        };
        std::vector<line> lines {};
        // 넘쳐서 줄였다 (한 줄이면 폭, 여러 줄이면 `max_lines`).
        bool truncated { false };
        // 가장 넓은 줄의 폭이다. 줄였으면 받은 폭이다.
        float width { 0.0f };
        // 보이는 줄 높이의 합이다.
        float height { 0.0f };
    };

    // 한 줄의 높이다 (논리 픽셀).
    [[nodiscard]] float text_block_line_height(const text_block_style& style) noexcept;

    // 폭 `available_width`(논리 픽셀)에 놓은 글의 줄과 크기다 (논리 픽셀). 그리기와 같은 규칙·글꼴로 잰다.
    //  - 배치에서 칸 높이를 정하는 element가 `preferred_size`에서 부른다. 문맥에 측정기를 넣었으면(test) 그것으로 잰다.
    //  - 넣은 측정기는 `bold`·`face`보다 앞선다 — 굵기는 측정기가 직접 반영해야 한다. 앱 글꼴(`typeface`)만은 그 글꼴로 잰다.
    [[nodiscard]] text_block_layout measure_text_block(const measure_context& context, std::u8string_view text, const text_block_style& style, float available_width);

    // `box`(물리 픽셀) 안에 글을 놓아 그린다. 그린 글이 차지한 자리(물리 픽셀)를 돌려준다 — 글 뒤에 바탕을 깔거나
    // 옆에 다른 그림을 이을 때 쓴다. 글이 비면 `box`의 왼쪽 위에 선 빈 자리다.
    //  - element의 `draw`뿐 아니라 사진·도형을 칠한 뒤 그 위에 글을 얹을 때도 같은 함수다. 상자는 자르지 않는다 —
    //    높이가 모자라면 `max_lines`로 줄을 막는다.
    rect_f draw_text_block(draw_context& context, std::u8string_view text, const rect_f& box, const text_block_style& style, const SkPaint& paint);
    // 배치에서 잰 줄(`measure_text_block`)로 그린다. 그리기마다 줄을 다시 바꾸지 않는다.
    rect_f draw_text_block(draw_context& context, std::u8string_view text, const rect_f& box, const text_block_style& style, const SkPaint& paint, const text_block_layout& layout);
}
