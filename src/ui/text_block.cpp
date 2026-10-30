#include "luil/ui/text_block.h"

#include "host/font_registry.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/font_scale.h"
#include "ui/font_synthesis.h"

#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>

namespace luil {
    namespace {
        // label 한 줄과 같은 위아래 여백이다 (`label_element::height_for`).
        constexpr float line_padding { 7.0f };

        [[nodiscard]] float positive_scale(const float scale) noexcept
        {
            return scale > 0.0f ? scale : 1.0f;
        }

        [[nodiscard]] bool visible(const ui_color color) noexcept
        {
            return (color >> 24U) != 0U;
        }

        // 굵기·앱 글꼴은 기본 측정기가 모르므로 그리기와 같은 글꼴을 지어 잰다.
        //  - face는 한 번만 고른다. 측정기는 줄을 바꾸는 동안 여러 번 불린다.
        [[nodiscard]] text_measurer typeface_measurer(sk_sp<SkTypeface> typeface, const font_style style)
        {
            SkFont styled { std::move(typeface) };
            apply_font_style(styled, style);
            return [styled](const std::u8string_view text, const float size) {
                SkFont font { styled };
                font.setSize(size);
                return measure_text(text, font);
            };
        }

        // 배치가 쓸 측정기다. 그리기가 `make_text_font`로 짓는 글꼴과 같은 글꼴로 잰다.
        //  - 문맥에 측정기를 넣었으면(test·앱 측정기) 앱 글꼴이 아닌 한 그것이다 — 지금까지 label이 재던 길이다.
        [[nodiscard]] text_measurer layout_measurer(const measure_context& context, const text_font& font)
        {
            if (font.typeface != nullptr)
                return typeface_measurer(font.typeface, font.style());
            if (context.measurer != nullptr)
                return *context.measurer;
            const bool code { font.face == text_layout_face::code };
            if (font.style() != font_style {})
                return typeface_measurer(code ? configured_code_typeface() : configured_ui_typeface(), font.style());
            return code ? default_code_text_measurer() : default_text_measurer();
        }

        // 그릴 글꼴 그대로 크기만 바꿔 잰다.
        [[nodiscard]] text_measurer font_measurer(const SkFont& font)
        {
            return [font](const std::u8string_view text, const float size) {
                SkFont sized { font };
                sized.setSize(size);
                return measure_text(text, sized);
            };
        }

        // `width`·`line_height`와 같은 단위(물리 픽셀)로 줄을 바꾼다. 폭은 그 단위 그대로 돌려준다.
        [[nodiscard]] text_block_layout wrap_lines(
            const std::u8string_view text, const text_block_style& style, const float width, const float pixel_size, const float line_height, const text_measurer& measure)
        {
            text_block_layout result {};
            // 여러 줄 칸과 같은 배치다 — 공백 우선, 긴 낱말은 글자소 경계에서 바꾼다.
            text_layout_request request {
                .text = std::u8string { text },
                .font_size = pixel_size,
                .width = width,
                .wrap = true,
                .line_height = line_height,
            };
            const text_layout layout { make_text_layout(std::move(request), measure) };
            const std::span<const text_layout_line> lines { layout.lines() };
            const std::size_t shown { style.max_lines > 0 ? std::min(style.max_lines, lines.size()) : lines.size() };
            for (std::size_t index { 0 }; index < shown; ++index)
            {
                result.lines.push_back({ lines[index].begin, lines[index].end });
                result.width = std::max(result.width, lines[index].width);
            }
            if (shown < lines.size() && shown > 0)
            {
                // 마지막 보이는 줄이 남은 글 전체를 맡고 그리기가 끝을 줄인다.
                result.lines.back().end = text.size();
                result.truncated = true;
                result.width = width;
            }
            return result;
        }

        [[nodiscard]] text_block_layout single_line(const std::u8string_view text, const float width, const float pixel_size, const text_measurer& measure)
        {
            text_block_layout result {};
            const float natural { measure(text, pixel_size) };
            result.lines.push_back({ 0, text.size() });
            result.truncated = natural > width;
            result.width = result.truncated ? width : natural;
            return result;
        }

        [[nodiscard]] std::u8string elided(const std::u8string_view text, const text_elide elide, const float width, const SkFont& font)
        {
            switch (elide)
            {
            case text_elide::middle:
                return elide_text_middle(text, width, font);
            case text_elide::start:
                return elide_text_start(text, width, font);
            case text_elide::end:
                break;
            }
            return elide_text(text, width, font);
        }

        // 줄마다 그릴 글과 자리다. 테두리와 글자가 같은 자리를 지난다.
        struct placed_line
        {
            std::u8string text {};
            float x { 0.0f };
            float baseline { 0.0f };
            float width { 0.0f };
        };

        rect_f draw_lines(
            draw_context& context, const std::u8string_view text, const rect_f& box, const text_block_style& style, const SkPaint& paint, const SkFont& font, const text_block_layout& layout)
        {
            if (text.empty() || layout.lines.empty())
                return { box.x, box.y, 0.0f, 0.0f };
            const float scale { positive_scale(context.scale) };
            const float line_height { text_block_line_height(style) * scale };
            const float height { static_cast<float>(layout.lines.size()) * line_height };
            // 한 줄을 가운데 세울 때는 상자 높이로 baseline을 잡는다 — 한 줄 label이 그려 온 자리 그대로다.
            const bool centered_single { layout.lines.size() == 1 && style.vertical == text_vertical_align::center };
            float top { box.y };
            if (style.vertical == text_vertical_align::center)
                top = box.y + (box.height - height) * 0.5f;
            else if (style.vertical == text_vertical_align::bottom)
                top = box.y + box.height - height;

            std::vector<placed_line> placed {};
            placed.reserve(layout.lines.size());
            for (std::size_t index { 0 }; index < layout.lines.size(); ++index)
            {
                const text_block_layout::line& line { layout.lines[index] };
                // 앱이 건넨 줄 구간이 글과 어긋나도 글 밖을 읽지 않는다.
                const std::size_t begin { std::min(line.begin, text.size()) };
                const std::size_t end { std::clamp(line.end, begin, text.size()) };
                std::u8string piece { text.substr(begin, end - begin) };
                if (style.wrap)
                {
                    // 말줄임한 마지막 줄은 남은 글을 한 줄로 펴서(줄 바꿈은 공백) 끝을 줄인다.
                    const bool last_truncated { layout.truncated && index + 1 == layout.lines.size() };
                    if (last_truncated)
                        std::replace(piece.begin(), piece.end(), u8'\n', u8' ');
                    while (piece.empty() == false && (piece.back() == u8'\n' || piece.back() == u8'\r'))
                        piece.pop_back();
                    if (last_truncated)
                        piece = elide_text(piece, box.width, font);
                }
                else
                {
                    // 상자가 좁으면 글자를 잘라 옆 UI를 침범하지 않게 한다.
                    piece = elided(piece, style.elide, box.width, font);
                }
                const float width { measure_text(piece, font) };
                float offset { 0.0f };
                if (style.align == text_align::end)
                    offset = box.width - width;
                else if (style.align == text_align::center)
                    offset = (box.width - width) * 0.5f;
                float baseline { top + static_cast<float>(index) * line_height + centered_text_baseline(font, line_height) };
                if (centered_single)
                    baseline = box.y + centered_text_baseline(font, box.height);
                placed.push_back({ std::move(piece), box.x + offset, baseline, width });
            }

            if (style.halo_width > 0.0f && visible(style.halo_color))
            {
                // 획은 윤곽 양쪽으로 반씩 나가므로 바깥 두께의 두 배다.
                SkPaint halo { solid_paint(style.halo_color) };
                halo.setAntiAlias(true);
                halo.setStyle(SkPaint::kStroke_Style);
                halo.setStrokeJoin(SkPaint::kRound_Join);
                halo.setStrokeWidth(style.halo_width * 2.0f * scale);
                for (const placed_line& line : placed)
                    draw_text(context.canvas, line.text, line.x, line.baseline, font, halo);
            }
            float left { box.x + box.width };
            float right { box.x };
            for (const placed_line& line : placed)
            {
                draw_text(context.canvas, line.text, line.x, line.baseline, font, paint);
                if (line.width <= 0.0f)
                    continue;
                left = std::min(left, line.x);
                right = std::max(right, line.x + line.width);
            }
            if (right < left)
                return { box.x, top, 0.0f, height };
            return { left, top, right - left, height };
        }
    }

    SkFont make_text_font(const draw_context& context, const text_font& font)
    {
        SkTypeface* typeface { font.typeface.get() };
        if (typeface == nullptr)
            typeface = font.face == text_layout_face::code && context.code_typeface != nullptr ? context.code_typeface : context.ui_typeface;
        SkFont result { sk_ref_sp(typeface), scaled_font_size(font.size, positive_scale(context.scale)) };
        apply_font_style(result, font.style());
        return result;
    }

    void apply_font_style(SkFont& font, const font_style style)
    {
        styled_typeface styled { style_typeface(font.getTypeface(), style) };
        if (styled.typeface != nullptr)
            font.setTypeface(std::move(styled.typeface));
        else
        {
            // face가 없으면 찾을 곳도 없다. 요청한 모양을 통째로 흉내 낸다.
            styled.embolden = font_weight_value(style.weight) >= font_weight_value(synthetic_bold_threshold);
            styled.skew = style.slant == font_slant::italic;
        }
        font.setEmbolden(styled.embolden);
        font.setSkewX(styled.skew ? synthetic_skew : 0.0f);
    }

    float text_block_line_height(const text_block_style& style) noexcept
    {
        return style.line_height > 0.0f ? style.line_height : text_line_height(style.font.size, line_padding);
    }

    text_block_layout measure_text_block(const measure_context& context, const std::u8string_view text, const text_block_style& style, const float available_width)
    {
        if (text.empty())
            return {};
        const float scale { positive_scale(context.scale) };
        const float width { std::max(1.0f, available_width) };
        const float pixel_size { scaled_font_size(style.font.size, scale) };
        const float line_height { text_block_line_height(style) };
        const text_measurer measure { layout_measurer(context, style.font) };
        text_block_layout result { style.wrap ? wrap_lines(text, style, width * scale, pixel_size, line_height * scale, measure) : single_line(text, width * scale, pixel_size, measure) };
        // 줄였으면 받은 폭 그대로다 — 나눗셈 잔차로 받은 폭과 어긋나지 않게 한다.
        result.width = result.truncated ? width : result.width / scale;
        result.height = static_cast<float>(result.lines.size()) * line_height;
        return result;
    }

    rect_f draw_text_block(draw_context& context, const std::u8string_view text, const rect_f& box, const text_block_style& style, const SkPaint& paint)
    {
        if (text.empty())
            return { box.x, box.y, 0.0f, 0.0f };
        const SkFont font { make_text_font(context, style.font) };
        text_block_layout layout {};
        if (style.wrap)
        {
            const float scale { positive_scale(context.scale) };
            layout = wrap_lines(text, style, std::max(1.0f, box.width), font.getSize(), text_block_line_height(style) * scale, font_measurer(font));
        }
        else
        {
            // 한 줄은 그리기가 줄이므로 미리 잴 것이 없다.
            layout.lines.push_back({ 0, text.size() });
        }
        return draw_lines(context, text, box, style, paint, font, layout);
    }

    rect_f draw_text_block(draw_context& context, const std::u8string_view text, const rect_f& box, const text_block_style& style, const SkPaint& paint, const text_block_layout& layout)
    {
        return draw_lines(context, text, box, style, paint, make_text_font(context, style.font), layout);
    }
}
