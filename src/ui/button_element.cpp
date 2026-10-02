#include "luil/ui/button_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <utility>

namespace luil {
    button_colors button_colors_for(const button_config& config, const ui_color_palette& palette)
    {
        // 역할이 먼저 자리를 채운다. 기본은 도구 막대다.
        button_colors colors {
            .foreground = palette.primary_foreground,
            .hover_background = palette.button_hover_background,
            .hover_foreground = palette.button_hover_foreground,
            .pressed_background = palette.button_pressed_background,
        };
        switch (config.role)
        {
        case button_visual_role::caption:
            colors.foreground = palette.caption.foreground;
            colors.hover_background = palette.caption.button_hover_background;
            colors.hover_foreground = palette.caption.button_hover_foreground;
            colors.pressed_background = palette.caption.button_hover_background;
            break;
        case button_visual_role::caption_close:
            colors.foreground = palette.caption.foreground;
            colors.hover_background = palette.caption.close_button_hover_background;
            colors.hover_foreground = palette.caption.close_button_hover_foreground;
            colors.pressed_background = palette.caption.close_button_hover_background;
            break;
        case button_visual_role::danger:
            // 쉬는 동안에도 오류색 글리프다 — 무엇이 사라지는지는 누르기 전에 말해야 한다.
            // 채운 오류색 위의 글자는 그 바탕을 위해 만든 역할(`error_foreground`)이다.
            colors.foreground = palette.error_accent;
            colors.hover_background = palette.error_accent;
            colors.hover_foreground = palette.error_foreground;
            colors.pressed_background = palette.error_accent;
            break;
        case button_visual_role::toolbar:
            break;
        }

        // 켜진 토글은 hover가 아닌 동안에도 바탕과 강조색으로 상태를 계속 알린다.
        if (config.active)
        {
            colors.rest_background = palette.active_toggle_background;
            colors.foreground = palette.accent_emphasis_foreground;
        }

        // 선택자는 마지막에 얹는다.
        // 비어 있는 자리는 손대지 않는다 — 그것이 "역할 그대로"다.
        if (config.foreground)
            colors.foreground = config.foreground(palette);
        if (config.hover_background)
            colors.hover_background = config.hover_background(palette);
        if (config.hover_foreground)
            colors.hover_foreground = config.hover_foreground(palette);
        if (config.pressed_background)
            colors.pressed_background = config.pressed_background(palette);
        if (config.rest_background)
            colors.rest_background = config.rest_background(palette);
        return colors;
    }

    button_element::button_element(ui_element_id id, button_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
    {}

    void button_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void button_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const button_colors colors { button_colors_for(config_, context.palette) };
        const bool hovered { enabled() && interaction.hovered == id() };
        const bool pressed { enabled() && interaction.pressed == id() };

        const rect_f box { bounds() };
        // 쉬는 동안의 바탕이 비어 있으면 아무것도 깔지 않는다 — 지금까지의 아이콘
        // 버튼이 그 특수 경우이고, 켜진 토글과 선택자만 그 자리를 채운다.
        const ui_color fill_color { pressed ? colors.pressed_background : (hovered ? colors.hover_background : colors.rest_background) };
        if (fill_color != 0)
        {
            const SkRect background { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
            const float radius { config_.corner_radius * scale };
            const SkPaint fill { solid_paint(fill_color) };
            context.canvas.drawRRect(SkRRect::MakeRectXY(background, radius, radius), fill);
        }

        if (context.codicon_typeface == nullptr)
            return;

        const ui_color icon_color { hovered || pressed ? colors.hover_foreground : colors.foreground };
        // 비활성은 element마다 알파를 발명하지 않고 팔레트 역할 하나를 쓴다.
        const SkPaint icon_paint { solid_paint(enabled() ? icon_color : context.palette.disabled_foreground) };
        const char32_t glyph { context.maximized && config_.maximized_glyph != 0 ? config_.maximized_glyph : config_.glyph };
        const SkFont icon_font { sk_ref_sp(context.codicon_typeface), config_.icon_size * scale };
        draw_centered_glyph(context.canvas, glyph, box, icon_font, icon_paint);
    }
} // namespace luil
