#include "luil/ui/check_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace luil {
    namespace {
        // 표시 부분과 라벨 사이의 간격, 줄 양쪽의 여백이다 (논리 픽셀).
        constexpr float check_gap { 8.0f };
        constexpr float check_inset { 4.0f };
        // 칩의 왼쪽 여백·체크 표시·표시와 글 사이·오른쪽 여백이다 (논리 픽셀).
        constexpr float chip_leading { 10.0f };
        constexpr float chip_mark { 12.0f };
        constexpr float chip_mark_gap { 6.0f };
        constexpr float chip_trailing { 12.0f };

        [[nodiscard]] bool button_like(const check_style style) noexcept
        {
            return style == check_style::toggle_button || style == check_style::toggle_chip;
        }

        // 칩 한 개를 그린다. 꺼지면 테만, 켜지면 옅은 강조 바탕·강조 테·체크 표시다.
        void draw_chip(draw_context& context, const rect_f& box, const check_config& config, const ui_element_id& id, const interaction_snapshot& interaction, const bool enabled)
        {
            const float scale { context.scale > 0.0f ? context.scale : 1.0f };
            const float radius { box.height / 2.0f };
            const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
            if (config.checked && enabled)
            {
                SkPaint fill { solid_paint(context.palette.active_toggle_background) };
                fill.setAntiAlias(true);
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), fill);
            }
            draw_hover_fill(context, box, id, interaction, enabled, radius / scale);

            // 테가 몸 안쪽에 온전히 들어오도록 반 픽셀 오므린다.
            ui_color line { context.palette.group_border };
            if (enabled == false)
                line = context.palette.divider;
            else if (config.checked)
                line = context.palette.accent;
            SkPaint border { solid_paint(line) };
            border.setStyle(SkPaint::kStroke_Style);
            border.setStrokeWidth(scale);
            border.setAntiAlias(true);
            const SkRect edge { body.makeInset(scale / 2.0f, scale / 2.0f) };
            context.canvas.drawRRect(SkRRect::MakeRectXY(edge, radius - scale / 2.0f, radius - scale / 2.0f), border);

            ui_color text_color { context.palette.primary_foreground };
            if (enabled == false)
                text_color = context.palette.disabled_foreground;
            else if (config.checked)
                text_color = context.palette.accent_emphasis_foreground;
            const SkPaint foreground { solid_paint(text_color) };
            const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(context.metrics.body_font_size, scale) };
            const float baseline { box.y + centered_text_baseline(font, box.height) };
            const float content_left { box.x + (chip_leading + chip_mark + chip_mark_gap) * scale };
            const float content_width { std::max(0.0f, box.x + box.width - chip_trailing * scale - content_left) };
            if (config.checked)
            {
                const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), chip_mark * scale };
                draw_centered_glyph(context.canvas, codicons::icon_check, { box.x + chip_leading * scale, box.y, chip_mark * scale, box.height }, glyph_font, foreground);
                static_cast<void>(draw_text_within(context.canvas, config.label, content_left, baseline, content_width, font, foreground));
                return;
            }
            // 꺼진 칩은 체크 자리까지 합친 안쪽 폭의 가운데에 글을 둔다.
            const float inner_left { box.x + chip_leading * scale };
            const float inner_width { std::max(0.0f, box.width - (chip_leading + chip_trailing) * scale) };
            const float text_width { std::min(measure_text(config.label, font), inner_width) };
            static_cast<void>(draw_text_within(context.canvas, config.label, inner_left + (inner_width - text_width) / 2.0f, baseline, inner_width, font, foreground));
        }
    } // namespace

    check_state aggregate_check(const std::span<const check_state> children) noexcept
    {
        if (children.empty())
            return check_state::unchecked;
        const check_state first { children.front() };
        if (first == check_state::mixed)
            return check_state::mixed;
        for (const check_state state : children)
            if (state != first)
                return check_state::mixed;
        return first;
    }

    check_element::check_element(check_config config)
        : ui_element { ui_element_id { ui_element_kind::check, config.owner } }
        , config_ { std::move(config) }
    {
        // 액션이 없으면 눌리지 않는다 — 커서도 툴팁도 그때만 뜻이 생긴다.
        if (config_.toggle == nullptr)
            return;
        set_cursor(ui_cursor::hand);
        set_tooltip(config_.tooltip);
        set_action(ui_trigger::left_click, config_.toggle);
    }

    float check_element::height_for(const check_config& config) noexcept
    {
        return button_like(config.style) ? text_button_height : check_row_height;
    }

    float check_element::indicator_width_for(const check_config& config) noexcept
    {
        if (button_like(config.style))
            return 0.0f;
        return config.style == check_style::toggle_switch ? check_switch_width : check_box_size;
    }

    void check_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds({ context.slot.x, context.slot.y, context.slot.width, height_for(config_) * scale });
    }

    rect_f check_element::indicator_bounds(const float scale) const noexcept
    {
        const rect_f box { bounds() };
        const float width { indicator_width_for(config_) * scale };
        const float height { (config_.style == check_style::toggle_switch ? check_switch_height : check_box_size) * scale };
        return { box.x + check_inset * scale, box.y + (box.height - height) / 2.0f, width, height };
    }

    void check_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        if (config_.style == check_style::toggle_chip)
        {
            draw_chip(context, box, config_, id(), interaction, enabled());
            return;
        }
        if (config_.style == check_style::toggle_button)
        {
            text_button_config button {};
            button.text = config_.label;
            button.visual = config_.checked ? text_button_visual::accent : text_button_visual::normal;
            text_button_element visual { id(), std::move(button) };
            visual.set_enabled(enabled());
            visual.arrange({ box, scale });
            visual.draw(context, interaction);
            if (config_.checked)
            {
                SkPaint border { solid_paint(enabled() ? context.palette.accent : context.palette.disabled_foreground) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(scale);
                const float radius { context.metrics.control_corner_radius * scale };
                context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radius, radius), border);
            }
            return;
        }
        draw_hover_fill(context, box, id(), interaction, enabled(), context.metrics.control_corner_radius);

        // 꺼진 표시의 선과 켜진 표시의 채움은 팔레트의 역할로만 고른다.
        const ui_color line { enabled() ? context.palette.control_border : context.palette.disabled_foreground };
        const ui_color fill { enabled() ? context.palette.accent : context.palette.disabled_foreground };
        const rect_f mark { indicator_bounds(scale) };
        // 일부 상태는 체크박스에만 있다 (`check_config::mixed`).
        const bool mixed { config_.mixed && config_.style == check_style::checkbox };
        SkPaint stroke { solid_paint(config_.checked || mixed ? fill : line) };
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(1.5f * scale);
        stroke.setAntiAlias(true);
        SkPaint solid { solid_paint(fill) };
        solid.setAntiAlias(true);

        switch (config_.style)
        {
        case check_style::toggle_button:
        case check_style::toggle_chip:
            break;
        case check_style::checkbox: {
            const SkRect shape { SkRect::MakeXYWH(mark.x, mark.y, mark.width, mark.height) };
            const float radius { context.metrics.control_corner_radius * scale };
            if (mixed)
            {
                // 채운 네모 위의 가운데 줄이다. 켜짐과 같은 채움이라 "무언가 켜져 있다"가 보이고, 표시가 달라
                // 전부 켜진 것과 갈린다. 고대비에서도 채움과 줄이 팔레트의 짝(accent·accent_foreground)이라 대비가 산다.
                context.canvas.drawRRect(SkRRect::MakeRectXY(shape, radius, radius), solid);
                SkPaint dash { solid_paint(enabled() ? context.palette.accent_foreground : context.palette.window_background) };
                dash.setStyle(SkPaint::kStroke_Style);
                dash.setStrokeWidth(1.8f * scale);
                dash.setAntiAlias(true);
                dash.setStrokeCap(SkPaint::kRound_Cap);
                const float middle_y { mark.y + mark.height / 2.0f };
                context.canvas.drawLine(mark.x + mark.width * 0.26f, middle_y, mark.x + mark.width * 0.74f, middle_y, dash);
            }
            else if (config_.checked)
            {
                context.canvas.drawRRect(SkRRect::MakeRectXY(shape, radius, radius), solid);
                // 채운 네모 위의 체크 표시다.
                // 꺼진 칸은 회색 채움 위에 바탕색으로 판 표시다.
                SkPaint tick { solid_paint(enabled() ? context.palette.accent_foreground : context.palette.window_background) };
                tick.setStyle(SkPaint::kStroke_Style);
                tick.setStrokeWidth(1.8f * scale);
                tick.setAntiAlias(true);
                tick.setStrokeCap(SkPaint::kRound_Cap);
                // 체크 표시는 꺾인 두 선이다.
                const float left_x { mark.x + mark.width * 0.24f };
                const float left_y { mark.y + mark.height * 0.52f };
                const float knee_x { mark.x + mark.width * 0.43f };
                const float knee_y { mark.y + mark.height * 0.72f };
                const float right_x { mark.x + mark.width * 0.78f };
                const float right_y { mark.y + mark.height * 0.30f };
                context.canvas.drawLine(left_x, left_y, knee_x, knee_y, tick);
                context.canvas.drawLine(knee_x, knee_y, right_x, right_y, tick);
            }
            else
                context.canvas.drawRRect(SkRRect::MakeRectXY(shape, radius, radius), stroke);
            break;
        }
        case check_style::radio: {
            context.canvas.drawOval(SkRect::MakeXYWH(mark.x, mark.y, mark.width, mark.height), stroke);
            if (config_.checked)
            {
                const float dot { mark.width * 0.42f };
                const float inset { (mark.width - dot) / 2.0f };
                context.canvas.drawOval(SkRect::MakeXYWH(mark.x + inset, mark.y + inset, dot, dot), solid);
            }
            break;
        }
        case check_style::toggle_switch: {
            const float radius { mark.height / 2.0f };
            const SkRect track { SkRect::MakeXYWH(mark.x, mark.y, mark.width, mark.height) };
            if (config_.checked)
                context.canvas.drawRRect(SkRRect::MakeRectXY(track, radius, radius), solid);
            else
                context.canvas.drawRRect(SkRRect::MakeRectXY(track, radius, radius), stroke);

            // 손잡이는 꺼짐이면 왼쪽, 켜짐이면 오른쪽 끝에 붙는다.
            const float knob { mark.height - 5.0f * scale };
            const float slack { (mark.height - knob) / 2.0f };
            const float knob_x { config_.checked ? mark.x + mark.width - knob - slack : mark.x + slack };
            SkPaint handle { solid_paint(config_.checked ? (enabled() ? context.palette.accent_foreground : context.palette.window_background) : line) };
            handle.setAntiAlias(true);
            context.canvas.drawOval(SkRect::MakeXYWH(knob_x, mark.y + slack, knob, knob), handle);
            break;
        }
        }

        if (config_.label.empty())
            return;
        const float text_left { mark.x + mark.width + check_gap * scale };
        const float text_width { box.x + box.width - check_inset * scale - text_left };
        if (text_width <= 0.0f)
            return;
        const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(context.metrics.body_font_size, scale) };
        const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
        static_cast<void>(draw_text_within(context.canvas, config_.label, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
    }
    access_info check_element::accessibility() const
    {
        access_info info {};
        switch (config_.style)
        {
        case check_style::toggle_button:
        case check_style::toggle_chip:
            info.role = access_role::button;
            break;
        case check_style::checkbox:
            info.role = access_role::check_box;
            break;
        case check_style::radio:
            info.role = access_role::radio_button;
            break;
        case check_style::toggle_switch:
            info.role = access_role::toggle_switch;
            break;
        }
        // 라벨이 밖에 있는 칸(표 안)은 tooltip이 이름을 대신한다.
        info.name = config_.label.empty() ? tooltip() : config_.label;
        // 라디오는 켬/끔이 아니라 **묶음 안의 하나를 고르는** 값이다.
        // UIA도 그래서 Toggle이 아니라 SelectionItem으로 읽는다
        // (accessibility-action-design.md).
        if (config_.style == check_style::radio)
            info.selected = config_.checked;
        else
            info.checked = config_.checked;
        info.mixed = config_.mixed && config_.style == check_style::checkbox;
        return info;
    }

    std::optional<size_f> check_element::preferred_size(const measure_context& context, const float available_width) const
    {
        static_cast<void>(available_width);
        if (config_.style == check_style::toggle_button)
        {
            text_button_config button {};
            button.text = config_.label;
            const text_button_element visual { id(), std::move(button) };
            return visual.preferred_size(context, available_width);
        }
        if (config_.style == check_style::toggle_chip)
        {
            // 켬/끔과 무관하게 체크 자리를 넣어 잰다 — 그리기의 자리와 같다.
            const float text { std::ceil(context.text_width(config_.label, context.style_metrics().body_font_size)) };
            return size_f { chip_leading + chip_mark + chip_mark_gap + text + chip_trailing, height_for(config_) };
        }
        // 그리기의 자리와 같다 — 안쪽 여백, 표시 부분, 간격과 글, 오른쪽 안쪽 여백이다.
        float width { check_inset + indicator_width_for(config_) + check_inset };
        if (config_.label.empty() == false)
            width += check_gap + std::ceil(context.text_width(config_.label, context.style_metrics().body_font_size));
        return size_f { width, height_for(config_) };
    }
} // namespace luil
