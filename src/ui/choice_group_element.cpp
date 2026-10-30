#include "luil/ui/choice_group_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace luil {
    namespace {
        // 라디오 선택지 한 행이다.
        // 동그라미와 라벨을 그리고 행 전체가 눌린다.
        class radio_choice_element final : public ui_element
        {
        public:
            radio_choice_element(choice_item item, const bool selected, const choice_message_factory& select)
                : ui_element { ui_element_id { ui_element_kind::choice, item.value } }
                , item_ { std::move(item) }
                , selected_ { selected }
            {
                // 묶음 안에서 글자로 찾을 때 보는 글이다 (보이는 라벨과 같은 것).
                set_search_label(item_.label);
                if (select == nullptr)
                    return;
                const std::u8string value { item_.value };
                set_cursor(ui_cursor::hand);
                set_action(ui_trigger::left_click, [value, &select](const ui_action_context&) -> std::vector<input_action> { return { select(value) }; });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                draw_hover_fill(context, box, id(), interaction, enabled(), context.metrics.control_corner_radius);

                // 바깥 고리와, 선택되었을 때만 채우는 안쪽 점이다.
                const float inset { 4.0f * scale };
                const float ring_size { 14.0f * scale };
                const float ring_top { box.y + (box.height - ring_size) / 2.0f };
                SkPaint ring { solid_paint(selected_ ? context.palette.accent : context.palette.control_border) };
                ring.setStyle(SkPaint::kStroke_Style);
                ring.setStrokeWidth(1.5f * scale);
                ring.setAntiAlias(true);
                context.canvas.drawOval(SkRect::MakeXYWH(box.x + inset, ring_top, ring_size, ring_size), ring);
                if (selected_)
                {
                    const float dot_size { 6.0f * scale };
                    const float dot_inset { (ring_size - dot_size) / 2.0f };
                    SkPaint dot { solid_paint(context.palette.accent) };
                    dot.setAntiAlias(true);
                    context.canvas.drawOval(SkRect::MakeXYWH(box.x + inset + dot_inset, ring_top + dot_inset, dot_size, dot_size), dot);
                }

                const float text_left { box.x + inset + ring_size + 8.0f * scale };
                const float text_width { box.x + box.width - inset - text_left };
                if (text_width > 0.0f)
                {
                    const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(context.metrics.body_font_size, scale) };
                    const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
                    static_cast<void>(draw_text_within(context.canvas, item_.label, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
                }
            }

            [[nodiscard]] access_info accessibility() const override
            {
                // 고르는 값이라 Toggle이 아니라 SelectionItem이다
                // (accessibility-action-design.md).
                return { .role = access_role::radio_button, .name = item_.label, .selected = selected_ };
            }

        private:
            choice_item item_ {};
            bool selected_ { false };
        };

        // 선택 묶음의 칸 하나다.
        // 고른 칸은 트랙 위에 밝은 판(`thumb_background`)이 뜨고 글이 진해진다. 나머지는 흐린 글만 둔다.
        //  - 밝기와 굵기로 말하고 강조색을 쓰지 않는다 — 강조 채움은 「누르면 실행」하는 기본 단추의 그림이다.
        class toggle_segment_element final : public ui_element
        {
        public:
            toggle_segment_element(choice_item item, const bool selected, const choice_message_factory& select)
                : ui_element { ui_element_id { ui_element_kind::choice, item.value } }
                , item_ { std::move(item) }
                , selected_ { selected }
            {
                set_search_label(item_.label);
                if (select == nullptr)
                    return;
                const std::u8string value { item_.value };
                set_cursor(ui_cursor::hand);
                set_action(ui_trigger::left_click, [value, &select](const ui_action_context&) -> std::vector<input_action> { return { select(value) }; });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                if (box.width <= 0.0f || box.height <= 0.0f)
                    return;

                const float radius { context.metrics.row_corner_radius * scale };
                const SkRect shape { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                if (selected_)
                {
                    // 비활성 묶음은 판을 트랙과 같은 옅은 채움으로 낮춰, 고른 자리만 남기고 뜬 느낌을 지운다.
                    SkPaint thumb { solid_paint(enabled() ? context.palette.thumb_background : context.palette.input_background) };
                    thumb.setAntiAlias(true);
                    context.canvas.drawRRect(SkRRect::MakeRectXY(shape, radius, radius), thumb);
                    // 판의 가장자리다. 라이트의 흰 판이 밝은 트랙에서 번지지 않게 1px 선을 몸 안쪽에 긋는다.
                    SkPaint edge { solid_paint(context.palette.divider) };
                    edge.setStyle(SkPaint::kStroke_Style);
                    edge.setStrokeWidth(scale);
                    edge.setAntiAlias(true);
                    const SkRect inner { shape.makeInset(scale / 2.0f, scale / 2.0f) };
                    context.canvas.drawRRect(SkRRect::MakeRectXY(inner, radius - scale / 2.0f, radius - scale / 2.0f), edge);
                }
                else
                    draw_hover_fill(context, box, id(), interaction, enabled(), context.metrics.row_corner_radius);

                ui_color text_color { context.palette.secondary_foreground };
                if (enabled() == false)
                    text_color = context.palette.disabled_foreground;
                else if (selected_)
                    text_color = context.palette.selection_foreground;
                SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(context.metrics.body_font_size, scale) };
                // 고른 글은 획을 조금 두껍게 한다. 칸 폭은 여백에 이 몫을 담아 두어 고른 칸이 바뀌어도 들썩이지 않는다.
                font.setEmbolden(selected_);
                const SkPaint foreground { solid_paint(text_color) };
                const float inset { std::min(choice_segment_padding * scale, box.width / 2.0f) };
                const float text_room { box.width - inset * 2.0f };
                const float text_width { std::min(measure_text(item_.label, font), text_room) };
                static_cast<void>(draw_text_within(
                    context.canvas, item_.label, box.x + (box.width - text_width) / 2.0f, box.y + centered_text_baseline(font, box.height), text_room, font, foreground));
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::radio_button, .name = item_.label, .selected = selected_ };
            }

        private:
            choice_item item_ {};
            bool selected_ { false };
        };

        // 선택 묶음 칸 하나의 맞춤 폭이다 (논리 픽셀). 가장 긴 글에 좌우 여백을 더한다.
        [[nodiscard]] float fitted_segment_width(const choice_group_config& config, const measure_context& context)
        {
            float widest { 0.0f };
            for (const choice_item& item : config.items)
                widest = std::max(widest, context.text_width(item.label, context.style_metrics().body_font_size));
            return std::ceil(widest) + choice_segment_padding * 2.0f;
        }

        // 칸 수에 맞춘 트랙 전체의 폭이다 (논리 픽셀).
        [[nodiscard]] float track_width_for(const float segment_width, const std::size_t count) noexcept
        {
            if (count == 0u)
                return 0.0f;
            const float segments { static_cast<float>(count) };
            return segment_width * segments + choice_segment_gap * (segments - 1.0f) + choice_track_padding * 2.0f;
        }
    }

    choice_group_element::choice_group_element(choice_group_config config)
        : ui_element { ui_element_id { ui_element_kind::choice_group, config.owner } }
        , config_ { std::move(config) }
    {
        // 선택지 묶음은 Tab에서 한 자리다. 방향은 이미 style이 알고 있다 —
        // 라디오는 세로로 쌓이고 토글은 가로로 늘어선다.
        set_focus_group(config_.style == choice_style::radio ? focus_axis::vertical : focus_axis::horizontal);
        // 들어오면 선택된 값에 선다 (앱 상태를 그대로 옮긴다).
        set_focus_entry(ui_element_id { ui_element_kind::choice, config_.selected });
        for (const choice_item& item : config_.items)
        {
            const bool selected { item.value == config_.selected };
            if (config_.style == choice_style::radio)
                add_child(std::make_unique<radio_choice_element>(item, selected, config_.select));
            else
                add_child(std::make_unique<toggle_segment_element>(item, selected, config_.select));
        }
    }

    float choice_group_element::height_for(const choice_group_config& config) noexcept
    {
        if (config.style == choice_style::toggle)
            return choice_toggle_height;
        return static_cast<float>(config.items.size()) * choice_radio_row_height;
    }

    void choice_group_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        if (config_.style == choice_style::radio)
        {
            set_bounds({ context.slot.x, context.slot.y, context.slot.width, height_for(config_) * scale });
            const float row_height { choice_radio_row_height * scale };
            float y { context.slot.y };
            for (const std::unique_ptr<ui_element>& row : children())
            {
                row->arrange(context.for_child({ context.slot.x, y, context.slot.width, row_height }));
                y += row_height;
            }
            return;
        }

        // 선택 묶음의 자리는 트랙이다. 맞춤이면 칸 폭에서 잰 트랙만큼, 채움이면 받은 폭 전부다.
        float track { context.slot.width };
        if (config_.width == choice_width::fit)
            track = std::min(context.slot.width, track_width_for(fitted_segment_width(config_, context.measure()), children().size()) * scale);
        set_bounds({ context.slot.x, context.slot.y, track, choice_toggle_height * scale });
        if (children().empty())
            return;

        // 칸들은 트랙 안쪽을 같은 폭으로 나눠 갖는다 — 맞춤은 그 폭이 곧 맞춤 폭이고, 모자란 자리에서는 같이 준다.
        const float padding { choice_track_padding * scale };
        const float gap { choice_segment_gap * scale };
        const float count { static_cast<float>(children().size()) };
        const float width { std::max(0.0f, (track - padding * 2.0f - gap * (count - 1.0f)) / count) };
        const float height { std::max(0.0f, choice_toggle_height * scale - padding * 2.0f) };
        float x { context.slot.x + padding };
        for (const std::unique_ptr<ui_element>& segment : children())
        {
            segment->arrange(context.for_child({ x, context.slot.y + padding, width, height }));
            x += width + gap;
        }
    }

    void choice_group_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        if (config_.style == choice_style::toggle)
        {
            // 칸들이 앉는 한 판(트랙)이다. 칸 사이 틈도 이 판이라 묶음이 한 덩어리로 읽힌다.
            const float scale { context.scale > 0.0f ? context.scale : 1.0f };
            const rect_f box { bounds() };
            const float radius { (context.metrics.row_corner_radius + choice_track_padding) * scale };
            SkPaint track { solid_paint(context.palette.input_background) };
            track.setAntiAlias(true);
            context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radius, radius), track);
        }
        draw_children(context, interaction);
    }

    std::optional<size_f> choice_group_element::preferred_size(const measure_context& context, const float available_width) const
    {
        if (config_.style == choice_style::radio)
            return std::nullopt;
        if (config_.width == choice_width::fill)
            return size_f { available_width, choice_toggle_height };
        return size_f { track_width_for(fitted_segment_width(config_, context), config_.items.size()), choice_toggle_height };
    }

    access_info choice_group_element::accessibility() const
    {
        // **스타일이 갈라도 하는 일은 하나다** — 여럿 가운데 하나를 고른다.
        // 그래서 두 스타일 다 항목들의 선택 container로 서고, 이름은 앱이
        // 준 말이다 (「무엇을 고르는 중인가」는 묶음만이 답할 수 있다).
        return { .role = access_role::radio_group, .name = config_.name };
    }
}
