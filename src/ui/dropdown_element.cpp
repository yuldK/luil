#include "luil/ui/dropdown_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <optional>
#include <utility>
#include <vector>

namespace luil {
    dropdown_element::dropdown_element(dropdown_config config)
        : ui_element { ui_element_id { ui_element_kind::dropdown, config.owner } }
        , config_ { std::move(config) }
    {
        // 클릭은 토글이 먼저다. 없으면 절대 메시지에 지금 상태의 반대를 담는다 —
        // factory 하나로 클릭과 접근 실행이 함께 선다 (그룹 머리행과 같은 갈래).
        if (config_.toggle != nullptr)
            set_action(ui_trigger::left_click, config_.toggle);
        else if (config_.set_open != nullptr)
        {
            const bool target { config_.open == false };
            set_action(ui_trigger::left_click, [this, target](const ui_action_context&) -> std::vector<input_action> { return { config_.set_open(target) }; });
        }
        else
            return;
        set_cursor(ui_cursor::hand);
    }

    std::optional<std::vector<input_action>> dropdown_element::access_actions(const access_request& request) const
    {
        if (request.command != access_command::expand && request.command != access_command::collapse)
            return ui_element::access_actions(request);
        // 펼치기·접기는 절대 메시지만 탄다 — 클릭(토글)으로 흘리면 오래된
        // 발행본을 본 같은 명령 둘이 목록을 열었다 도로 닫는다
        // (accessibility-action-design.md).
        if (config_.set_open == nullptr)
            return std::nullopt;
        return std::vector<input_action> { config_.set_open(request.command == access_command::expand) };
    }

    void dropdown_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void dropdown_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        const float radius { context.metrics.control_corner_radius * scale };

        // 텍스트 입력 칸과 같은 표면·테두리 규칙이다.
        // 목록이 떠 있으면 초점을 받은 것처럼 테두리를 강조한다.
        ui_color background { context.palette.input_background };
        if (enabled() && interaction.pressed == id())
            background = context.palette.button_pressed_background;
        else if (enabled() && interaction.hovered == id())
            background = context.palette.button_hover_background;
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(background));
        SkPaint border { solid_paint(config_.open ? context.palette.accent : context.palette.input_border) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.0f * scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

        // 오른쪽의 펼침 화살표다.
        // 떠 있는 동안은 뒤집어 닫는 쪽을 가리킨다.
        const float arrow_width { 18.0f * scale };
        const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 12.0f * scale };
        const SkPaint glyph { solid_paint(enabled() ? context.palette.secondary_foreground : context.palette.disabled_foreground) };
        draw_centered_glyph(
            context.canvas, config_.open ? codicons::icon_chevron_up : codicons::icon_chevron_down, { box.x + box.width - arrow_width - 4.0f * scale, box.y, arrow_width, box.height },
            glyph_font, glyph);

        const float inset { 8.0f * scale };
        const float text_width { box.width - inset - arrow_width - 8.0f * scale };
        if (text_width <= 0.0f)
            return;
        const SkFont font { sk_ref_sp(context.ui_typeface), context.metrics.body_font_size * scale };
        const bool empty { config_.text.empty() };
        if (empty && config_.placeholder.empty())
            return;
        ui_color text_color { context.palette.primary_foreground };
        if (empty)
            text_color = context.palette.secondary_foreground;
        else if (enabled() == false)
            text_color = context.palette.disabled_foreground;
        const SkPaint foreground { solid_paint(text_color) };
        static_cast<void>(
            draw_text_within(context.canvas, empty ? config_.placeholder : config_.text, box.x + inset, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
    }
    access_info dropdown_element::accessibility() const
    {
        // 이름은 "무엇을 고르는 칸인가"(placeholder), 값은 지금 고른 라벨이다.
        return { .role = access_role::combo_box, .name = config_.placeholder, .expanded = config_.open, .value = config_.text };
    }
} // namespace luil
