#include "luil/ui/badge_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <utility>

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
                return palette.accent_emphasis_foreground;
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
    } // namespace

    badge_element::badge_element(ui_element_id id, badge_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
    {}

    void badge_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void badge_element::draw(draw_context& context, const interaction_snapshot&) const
    {
        const rect_f box { bounds() };
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const ui_color tone { tone_color(context.palette, config_.tone) };
        const bool emphasized { config_.tone == badge_tone::accent };
        const float radius { box.height / 2.0f };
        const SkRect shape { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };

        // 강조 배지만 바탕이 tone이고 나머지는 notice 바탕이다.
        // 고대비에서 notice 바탕은 창 배경으로 접히지만 테가 남아 배지는 보인다.
        context.canvas.drawRRect(SkRRect::MakeRectXY(shape, radius, radius), solid_paint(emphasized ? tone : context.palette.notice_background));

        SkPaint border { solid_paint(tone) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(scale);
        // 테가 알약 안쪽에 온전히 들어오도록 반 픽셀 오므린다.
        const SkRect inset { SkRect::MakeXYWH(box.x + scale / 2.0f, box.y + scale / 2.0f, box.width - scale, box.height - scale) };
        context.canvas.drawRRect(SkRRect::MakeRectXY(inset, radius, radius), border);

        if (config_.text.empty() && shows_glyph(config_.tone) == false)
            return;

        const SkFont font { sk_ref_sp(context.ui_typeface), config_.font_size * scale };
        const SkPaint foreground { solid_paint(tone_foreground(context.palette, config_.tone)) };
        const float padding { badge_padding * scale };
        float text_left { box.x + padding };

        if (shows_glyph(config_.tone))
        {
            const float glyph_size { config_.font_size * scale };
            const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), glyph_size };
            draw_centered_glyph(context.canvas, tone_glyph(config_.tone), { text_left, box.y, glyph_size, box.height }, glyph_font, foreground);
            text_left += glyph_size + badge_glyph_gap * scale;
        }

        const float text_width { box.x + box.width - padding - text_left };
        static_cast<void>(draw_text_within(context.canvas, config_.text, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
    }
    access_info badge_element::accessibility() const
    {
        return { .role = access_role::static_text, .name = config_.text };
    }
} // namespace luil
