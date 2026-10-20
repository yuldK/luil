#include "ui/badge_paint.h"

#include "luil/generated/codicons.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>

namespace luil {
    namespace {
        // 알약 안쪽의 좌우 여백과, 글리프와 글자 사이다 (논리 픽셀).
        constexpr float badge_padding { 6.0f };
        constexpr float badge_glyph_gap { 4.0f };

        // tone이 나르는 색이다. 테가 이 색을 쓴다.
        [[nodiscard]] ui_color tone_color(const ui_color_palette& palette, const badge_tone tone) noexcept
        {
            switch (tone)
            {
            case badge_tone::accent:
                return palette.accent;
            case badge_tone::warning:
                return palette.warning_accent;
            case badge_tone::error:
                return palette.error_accent;
            case badge_tone::neutral:
                break;
            }
            return palette.divider;
        }

        // 글자와 글리프의 색이다.
        //  - 심각도는 테와 **같은 색**이라 색이 접혀도 테·글자·글리프가 함께 남는다.
        //  - 중립은 테 색(구분선)으로 글을 쓰면 읽히지 않는다. 구분선은 보이라고
        //    있는 색이 아니라 가르라고 있는 색이다 — 글은 보조 글자 역할을 쓴다.
        [[nodiscard]] ui_color tone_foreground(const ui_color_palette& palette, const badge_tone tone) noexcept
        {
            switch (tone)
            {
            case badge_tone::accent:
                // 강조 배지만 채운 바탕이다.
                return palette.accent_foreground;
            case badge_tone::neutral:
                return palette.secondary_foreground;
            case badge_tone::warning:
            case badge_tone::error:
                break;
            }
            return tone_color(palette, tone);
        }

        // 심각도가 함께 나르는 글리프다.
        // 토스트와 같은 글리프를 쓴다 — 같은 경고가 두 자리에서 다르게 보이면 안 된다.
        [[nodiscard]] char32_t tone_glyph(const badge_tone tone) noexcept
        {
            return tone == badge_tone::error ? codicons::icon_error : codicons::icon_warning;
        }

        [[nodiscard]] float positive_scale(const draw_context& context) noexcept
        {
            return context.scale > 0.0f ? context.scale : 1.0f;
        }
    } // namespace

    float badge_pill_width(const draw_context& context, const std::u8string_view text, const badge_tone tone, const float font_size)
    {
        const float scale { positive_scale(context) };
        float width { 2.0f * badge_padding * scale };
        const bool glyph { badge_element::shows_glyph(tone) };
        if (glyph)
            width += font_size * scale;
        if (text.empty() == false)
        {
            const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(font_size, scale) };
            width += measure_text(text, font);
            if (glyph)
                width += badge_glyph_gap * scale;
        }
        // 알약이 원보다 좁아지지 않는다.
        return std::max(width, badge_element::height_for({ .font_size = font_size }) * scale);
    }

    void draw_badge_pill(draw_context& context, const rect_f& box, const std::u8string_view text, const badge_tone tone, const float font_size)
    {
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;

        const float scale { positive_scale(context) };
        const ui_color color { tone_color(context.palette, tone) };
        const bool emphasized { tone == badge_tone::accent };
        const float radius { box.height / 2.0f };
        const SkRect shape { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };

        // 강조 배지만 바탕이 tone이고 나머지는 notice 바탕이다.
        // 고대비에서 notice 바탕은 창 배경으로 접히지만 테가 남아 배지는 보인다.
        context.canvas.drawRRect(SkRRect::MakeRectXY(shape, radius, radius), solid_paint(emphasized ? color : context.palette.notice_background));

        SkPaint border { solid_paint(color) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(scale);
        // 테가 알약 안쪽에 온전히 들어오도록 반 픽셀 오므린다.
        const SkRect inset { SkRect::MakeXYWH(box.x + scale / 2.0f, box.y + scale / 2.0f, box.width - scale, box.height - scale) };
        context.canvas.drawRRect(SkRRect::MakeRectXY(inset, radius, radius), border);

        if (text.empty() && badge_element::shows_glyph(tone) == false)
            return;

        const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(font_size, scale) };
        const SkPaint foreground { solid_paint(tone_foreground(context.palette, tone)) };
        const float padding { badge_padding * scale };
        float text_left { box.x + padding };

        if (badge_element::shows_glyph(tone))
        {
            const float glyph_size { font_size * scale };
            const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), glyph_size };
            draw_centered_glyph(context.canvas, tone_glyph(tone), { text_left, box.y, glyph_size, box.height }, glyph_font, foreground);
            text_left += glyph_size + badge_glyph_gap * scale;
        }

        const float text_width { box.x + box.width - padding - text_left };
        static_cast<void>(draw_text_within(context.canvas, text, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
    }

    void draw_badge_dot(draw_context& context, const float center_x, const float center_y, const float radius, const badge_tone tone)
    {
        if (radius <= 0.0f)
            return;
        const float scale { positive_scale(context) };
        const ui_color color { tone_color(context.palette, tone) };
        // 중립 점은 구분선 색이라 채우면 흐리다. 보조 글자 색으로 채우고 테는 tone이 나른다.
        context.canvas.drawCircle(center_x, center_y, radius, solid_paint(tone == badge_tone::neutral ? context.palette.secondary_foreground : color));
        SkPaint border { solid_paint(color) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(scale);
        context.canvas.drawCircle(center_x, center_y, std::max(radius - scale / 2.0f, 0.0f), border);
    }
} // namespace luil
