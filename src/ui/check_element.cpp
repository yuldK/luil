#include "luil/ui/check_element.h"

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
        // 표시 부분과 라벨 사이의 간격, 줄 양쪽의 여백이다 (논리 픽셀).
        constexpr float check_gap { 8.0f };
        constexpr float check_inset { 4.0f };
    } // namespace

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
        static_cast<void>(config);
        return check_row_height;
    }

    float check_element::indicator_width_for(const check_config& config) noexcept
    {
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
        draw_hover_fill(context, box, id(), interaction, enabled(), context.metrics.control_corner_radius);

        // 꺼진 표시의 선과 켜진 표시의 채움은 팔레트의 역할로만 고른다.
        const ui_color line { enabled() ? context.palette.control_border : context.palette.disabled_foreground };
        const ui_color fill { enabled() ? context.palette.accent : context.palette.disabled_foreground };
        const rect_f mark { indicator_bounds(scale) };
        SkPaint stroke { solid_paint(config_.checked ? fill : line) };
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(1.5f * scale);
        stroke.setAntiAlias(true);
        SkPaint solid { solid_paint(fill) };
        solid.setAntiAlias(true);

        switch (config_.style)
        {
        case check_style::checkbox: {
            const SkRect shape { SkRect::MakeXYWH(mark.x, mark.y, mark.width, mark.height) };
            const float radius { context.metrics.control_corner_radius * scale };
            if (config_.checked)
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
        const SkFont font { sk_ref_sp(context.ui_typeface), context.metrics.body_font_size * scale };
        const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
        static_cast<void>(draw_text_within(context.canvas, config_.label, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
    }
    access_info check_element::accessibility() const
    {
        access_info info {};
        switch (config_.style)
        {
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
        return info;
    }
} // namespace luil
