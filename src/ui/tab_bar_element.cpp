#include "luil/ui/tab_bar_element.h"

#include "ui/reorder_drop.h"

#include "luil/generated/codicons.h"
#include "luil/ui/button_element.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/stack_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <memory>
#include <utility>

namespace luil {
    namespace {
        // 탭 한 개다.
        // 누르면 선택 메시지를 내고, 끌어 놓으면 순서 변경 메시지를 낸다.
        class tab_element final : public ui_element
        {
        public:
            tab_element(
                tab_item item, const bool selected, const tab_message_factory& select, const tab_message_factory& close, const tab_reorder_message_factory& reorder, const ui_element_id& container)
                : ui_element { ui_element_id { ui_element_kind::tab, item.key } }
                , item_ { std::move(item) }
                , selected_ { selected }
            {
                // 묶음 안에서 글자로 찾을 때 보는 글이다 (탭에 적힌 이름과 같은 것).
                set_search_label(item_.label);
                if (select != nullptr)
                {
                    const std::u8string key { item_.key };
                    set_action(ui_trigger::left_click, [key, &select](const ui_action_context&) -> std::vector<input_action> { return { select(key) }; });
                }

                if (reorder != nullptr)
                {
                    // 잡은 지점과 탭 원점의 차이는 배치가 정하므로 잡는 순간의 bounds에서 읽는다.
                    const ui_element* const self { this };
                    const std::u8string key { item_.key };
                    std::u8string label { item_.label };
                    // 이 탭이 속한 막대다. 놓는 쪽이 같은 막대인지를 이것으로 가른다.
                    drag_source source {};
                    source.make_payload = [self, key, label, container](const ui_action_context& context) {
                        drag_payload payload {};
                        payload.source = context.element;
                        payload.container = container;
                        payload.dragged_owner = key;
                        payload.label = label;
                        payload.grab_offset_x = context.x - self->bounds().x;
                        payload.grab_offset_y = context.y - self->bounds().y;
                        return payload;
                    };
                    set_drag_source(std::move(source));

                    drop_target drop {};
                    // 같은 막대의 탭만 받는다. 자기 자리에 놓는 것은 이동이 아니므로 받지
                    // 않는다 (강조도 뜨지 않는다) — 규칙은 목록과 한 벌이다 (reorder_drop.h).
                    drop.accepts = [key, container](const drag_payload& payload) { return accepts_reorder_drop(payload, ui_element_kind::tab, container, key); };
                    drop.on_drop = [key, &reorder](const drag_payload& payload, const ui_action_context&) -> std::vector<input_action> { return { reorder(payload.dragged_owner, key) }; };
                    set_drop_target(std::move(drop));
                }

                if (item_.closable == false || close == nullptr)
                    return;

                const std::u8string key { item_.key };
                const button_config close_button {
                    .glyph = codicons::icon_close,
                    .icon_size = 11.0f,
                    .corner_radius = 3.0f,
                };
                auto button { std::make_unique<button_element>(ui_element_id { ui_element_kind::tab_close, key }, close_button) };
                // 항목에 딸린 보조 버튼이다. 묶음 안에 두면 ←/→가 탭과 닫기를
                // 번갈아 지난다 — 키보드로는 넘침 메뉴가 그 자리다
                // (focus-group-design.md).
                button->set_tab_stop(false);
                button->set_cursor(ui_cursor::hand);
                button->set_action(ui_trigger::left_click, [key, &close](const ui_action_context&) -> std::vector<input_action> { return { close(key) }; });
                close_ = button.get();
                add_child(std::move(button));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                if (close_ == nullptr)
                    return;
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const float size { 18.0f * scale };
                const float inset { 6.0f * scale };
                close_->arrange(context.for_child({ context.slot.x + context.slot.width - inset - size, context.slot.y + (context.slot.height - size) / 2.0f, size, size }));
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const bool dragged { interaction.drag.has_value() && interaction.drag->payload.dragged_owner == item_.key };
                const bool drop_here { interaction.drag.has_value() && interaction.drag->hovered_drop_target == id() };

                ui_color background { 0 };
                bool fill { false };
                if (drop_here)
                {
                    background = with_alpha(context.palette.accent, 0.30f);
                    fill = true;
                }
                else if (selected_)
                {
                    background = context.palette.surface_background;
                    fill = true;
                }
                else if (interaction.pressed == id())
                {
                    background = context.palette.button_pressed_background;
                    fill = true;
                }
                else if (interaction.hovered == id())
                {
                    background = context.palette.button_hover_background;
                    fill = true;
                }
                if (fill)
                {
                    SkPaint paint { solid_paint(background) };
                    if (dragged)
                        paint.setAlphaf(0.35f);
                    context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), paint);
                }

                // 선택된 탭은 아래 가장자리의 강조선으로 표시한다.
                if (selected_)
                {
                    const float line { 2.0f * scale };
                    context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y + box.height - line, box.width, line), solid_paint(context.palette.accent));
                }

                float text_left { box.x + 10.0f * scale };
                if (item_.icon != 0)
                {
                    const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 13.0f * scale };
                    const SkPaint dim { solid_paint(context.palette.secondary_foreground) };
                    draw_centered_glyph(context.canvas, item_.icon, { text_left, box.y, 16.0f * scale, box.height }, glyph_font, dim);
                    text_left += 22.0f * scale;
                }

                // 닫기 버튼 자리와 겹치지 않게 오른쪽을 비운다.
                const float reserved { close_ != nullptr ? 28.0f * scale : 10.0f * scale };
                const float text_width { box.x + box.width - reserved - text_left };
                if (text_width > 0.0f)
                {
                    const SkFont font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
                    SkPaint foreground { solid_paint(context.palette.primary_foreground) };
                    if (selected_ == false)
                        foreground.setAlphaf(0.75f);
                    static_cast<void>(draw_text_within(context.canvas, item_.label, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
                }
                draw_children(context, interaction);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::tab, .name = item_.label, .selected = selected_ };
            }

        private:
            tab_item item_ {};
            bool selected_ { false };
            ui_element* close_ { nullptr };
        };
    } // namespace

    namespace {
        // 넘침 메뉴 버튼이다.
        // 아래로 지나가는 탭을 가리도록 바탕을 불투명하게 칠한다.
        class tab_overflow_element final : public ui_element
        {
        public:
            tab_overflow_element(std::u8string owner, const ui_action& action, std::u8string tooltip)
                : ui_element { ui_element_id { ui_element_kind::tab_overflow, std::move(owner) } }
            {
                set_cursor(ui_cursor::hand);
                set_action(ui_trigger::left_click, action);
                // 글리프뿐이라 이름의 원천이 이것뿐이다 — 비면 접근 tree에서 빠진다.
                set_tooltip(std::move(tooltip));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(context.palette.window_background));
                if (interaction.pressed == id() || interaction.hovered == id())
                {
                    const ui_color overlay { interaction.pressed == id() ? context.palette.button_pressed_background : context.palette.button_hover_background };
                    context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(overlay));
                }
                // 막대 바닥의 경계선이 버튼 아래에서도 이어진다.
                context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y + box.height - scale, box.width, scale), solid_paint(context.palette.divider));

                const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 14.0f * scale };
                const SkPaint glyph { solid_paint(context.palette.secondary_foreground) };
                draw_centered_glyph(context.canvas, codicons::icon_ellipsis, box, glyph_font, glyph);
            }
        };
    } // namespace

    tab_bar_element::tab_bar_element(tab_bar_config config)
        : ui_element { ui_element_id { ui_element_kind::tab_bar, config.owner } }
        , config_ { std::move(config) }
    {
        // 넘친 탭이 막대 밖으로 새지 않고 잘린 탭은 눌리지도 않는다.
        //  - 잘라내기는 띠가 이미 하지만, 막대는 넘침 버튼까지 아우르는 바깥
        //    경계라 자기 몫을 그대로 갖는다.
        set_clip_children(true);
        // 탭 막대는 Tab에서 한 자리이고 그 안은 ←/→가 돈다.
        // 들어오면 선택된 탭에 선다 (앱 상태를 그대로 옮긴다).
        //  - 탭이 띠·레인 아래로 한 겹 더 들어가도 `focus_order`는 tree를 걸어
        //    모으므로 자리 판정이 바뀌지 않는다.
        set_focus_group(focus_axis::horizontal);
        set_focus_entry(ui_element_id { ui_element_kind::tab, config_.selected });

        // 탭을 늘어놓는 레인이다. 간격도 여백도 없어 탭이 맞붙는다.
        stack_config lane_config {};
        lane_config.direction = stack_direction::row;
        auto lane { std::make_unique<stack_element>(ui_element_id { ui_element_kind::tab_lane, config_.owner }, lane_config) };
        for (tab_item& item : config_.items)
        {
            const bool selected { item.key == config_.selected };
            lane->add(std::make_unique<tab_element>(std::move(item), selected, config_.select, config_.close, config_.reorder, id()), config_.tab_width);
        }

        // 흘리기·잘라내기·다듬기는 띠의 몫이다.
        // 내용 폭은 담는 쪽이 알려 준다 — 측정 단계가 없다.
        strip_config strip {};
        strip.content_width = static_cast<float>(config_.items.size()) * config_.tab_width;
        strip.scroll_offset = config_.scroll_offset;
        auto lane_strip { std::make_unique<strip_element>(ui_element_id { ui_element_kind::tab_strip, config_.owner }, strip) };
        lane_strip->set_content(std::move(lane));
        strip_ = lane_strip.get();
        add_child(std::move(lane_strip));

        // 띠보다 나중에 담아 위에 그려지고 hit도 먼저 가져간다.
        // 넘치는지는 arrange가 정하므로 그때까지 보이지 않는다.
        if (config_.overflow != nullptr)
        {
            auto button { std::make_unique<tab_overflow_element>(config_.owner, config_.overflow, config_.overflow_tooltip) };
            button->set_visible(false);
            overflow_ = button.get();
            add_child(std::move(button));
        }
    }

    float tab_bar_element::maximum_scroll() const noexcept
    {
        return maximum_scroll_;
    }

    float tab_bar_element::scroll_offset() const noexcept
    {
        return config_.scroll_offset;
    }

    void tab_bar_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float content_width { static_cast<float>(config_.items.size()) * config_.tab_width * scale };

        // 넘칠 때만 넘침 메뉴 버튼이 오른쪽 끝의 자리를 가져간다.
        //  - 판정은 버튼 폭을 떼기 **전**의 slot 폭으로 한다. 뗀 뒤에 재면
        //    "버튼이 있어서 넘친다"가 되어 자기참조다.
        float lane_width { context.slot.width };
        if (overflow_ != nullptr)
        {
            const bool overflowing { content_width > context.slot.width };
            overflow_->set_visible(overflowing);
            if (overflowing)
            {
                const float button_width { tab_overflow_width * scale };
                lane_width -= button_width;
                overflow_->arrange(context.for_child({ context.slot.x + context.slot.width - button_width, context.slot.y, button_width, context.slot.height }));
            }
        }

        // 남는 자리를 띠가 통째로 받는다. 흘리기·잘라내기·다듬기는 그 안에서 끝난다.
        //  - 레인이 실제로 좁아지므로 **마지막 탭이 버튼 밑을 지나지 않는다.**
        strip_->arrange(context.for_child({ context.slot.x, context.slot.y, lane_width, context.slot.height }));
        // 다듬은 값을 그대로 물려받아 앱이 같은 값을 상태에 되돌릴 수 있게 한다.
        maximum_scroll_ = strip_->maximum_scroll();
        config_.scroll_offset = strip_->scroll_offset();
    }

    void tab_bar_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 막대 바닥의 경계선이다.
        // 내용 영역과의 구분은 앱이 아니라 막대가 갖는다.
        const rect_f box { bounds() };
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y + box.height - scale, box.width, scale), solid_paint(context.palette.divider));
        draw_children(context, interaction);
    }
    access_info tab_bar_element::accessibility() const
    {
        return { .role = access_role::tab_list, .name = tooltip() };
    }
} // namespace luil
