#include "luil/ui/choice_group_element.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <cstddef>
#include <memory>
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
                draw_hover_fill(context, box, id(), interaction, enabled(), 3.0f);

                // 바깥 고리와, 선택되었을 때만 채우는 안쪽 점이다.
                const float inset { 4.0f * scale };
                const float ring_size { 14.0f * scale };
                const float ring_top { box.y + (box.height - ring_size) / 2.0f };
                SkPaint ring { solid_paint(selected_ ? context.palette.accent : with_alpha(context.palette.primary_foreground, 0.55f)) };
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
                    const SkFont font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
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
    } // namespace

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
            {
                add_child(std::make_unique<radio_choice_element>(item, selected, config_.select));
                continue;
            }

            // 토글 묶음은 글자 버튼의 강조 스타일을 그대로 쓴다.
            // 선택된 값이 곧 강조다.
            text_button_config button_config {};
            button_config.text = item.label;
            button_config.visual = selected ? text_button_visual::accent : text_button_visual::normal;
            // **그림과 함께 상태도 싣는다.** 강조 채움은 「기본 동작」에도 쓰이는
            // 그림이라 버튼 혼자서는 그 둘을 가를 수 없다. 여기는 가를 수 있다 —
            // 지금 고른 값을 알고 있는 자리다 (accessibility-action-design.md).
            button_config.selected = selected;
            auto button { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::choice, item.value }, std::move(button_config)) };
            button->set_search_label(item.label);
            if (config_.select != nullptr)
            {
                const std::u8string value { item.value };
                const choice_message_factory& select { config_.select };
                button->set_cursor(ui_cursor::hand);
                button->set_action(ui_trigger::left_click, [value, &select](const ui_action_context&) -> std::vector<input_action> { return { select(value) }; });
            }
            add_child(std::move(button));
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
        set_bounds({ context.slot.x, context.slot.y, context.slot.width, height_for(config_) * scale });
        if (children().empty())
            return;

        if (config_.style == choice_style::radio)
        {
            const float row_height { choice_radio_row_height * scale };
            float y { context.slot.y };
            for (const std::unique_ptr<ui_element>& row : children())
            {
                row->arrange(context.for_child({ context.slot.x, y, context.slot.width, row_height }));
                y += row_height;
            }
            return;
        }

        // 토글 묶음은 slot 폭을 같은 크기로 나눠 갖는다.
        const float gap { choice_toggle_gap * scale };
        const float count { static_cast<float>(children().size()) };
        float width { (context.slot.width - gap * (count - 1.0f)) / count };
        if (width < 0.0f)
            width = 0.0f;
        const float height { choice_toggle_height * scale };
        float x { context.slot.x };
        for (const std::unique_ptr<ui_element>& button : children())
        {
            button->arrange(context.for_child({ x, context.slot.y, width, height }));
            x += width + gap;
        }
    }

    void choice_group_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }

    access_info choice_group_element::accessibility() const
    {
        // **스타일이 갈라도 하는 일은 하나다** — 여럿 가운데 하나를 고른다.
        // 그래서 두 스타일 다 항목들의 선택 container로 서고, 이름은 앱이
        // 준 말이다 (「무엇을 고르는 중인가」는 묶음만이 답할 수 있다).
        return { .role = access_role::radio_group, .name = config_.name };
    }
} // namespace luil
