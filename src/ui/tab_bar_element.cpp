#include "luil/ui/tab_bar_element.h"

#include "ui/badge_paint.h"
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

#include <algorithm>
#include <memory>
#include <utility>

namespace luil {
    namespace {
        // 탭 배지의 글자 크기, 상태 점의 반지름, 이름과의 사이다 (논리 픽셀).
        constexpr float tab_badge_font_size { 10.0f };
        constexpr float tab_badge_dot_radius { 3.5f };
        constexpr float tab_badge_gap { 6.0f };
        // 놓을 틈을 보이는 세로 막대의 두께다 (논리 픽셀).
        constexpr float tab_gap_marker_width { 2.0f };

        // 막대의 탭 키들이다 (그리기 순서). 탭과 막대의 drop 판정이 같은 값을 본다.
        using tab_order = std::shared_ptr<const std::vector<std::u8string>>;

        // 탭 상자의 어느 반쪽인가다.
        [[nodiscard]] drop_side side_at(const rect_f& box, const float x) noexcept
        {
            return x < box.x + box.width / 2.0f ? drop_side::before : drop_side::after;
        }

        // 이 막대의 탭인가 (자기 자신 포함). 막대 id만 보면 창마다 같은 owner로 지은 막대들이 서로를 자기로 본다 —
        // 키가 이 막대에 있어야 이 막대의 탭이다.
        [[nodiscard]] bool own_tab(const drag_payload& payload, const ui_element_id& container, const std::vector<std::u8string>& order)
        {
            return payload.source.kind == ui_element_kind::tab && payload.container == container && payload.dragged_owner.empty() == false
                && std::find(order.begin(), order.end(), payload.dragged_owner) != order.end();
        }

        // 다른 막대(다른 창)에서 끌어 온 탭인가.
        [[nodiscard]] bool foreign_tab(const drag_payload& payload, const ui_element_id& container, const std::vector<std::u8string>& order)
        {
            return payload.source.kind == ui_element_kind::tab && payload.dragged_owner.empty() == false && payload.files.empty() && own_tab(payload, container, order) == false;
        }

        // 같은 막대 안에서 그 자리에 놓으면 순서가 바뀌는가다. `slot`은 끼울 자리(0 = 맨 앞, 탭 수 = 맨 끝)다.
        //  - 자기 바로 앞 틈과 바로 뒤 틈은 지금 자리 그대로다.
        [[nodiscard]] bool changes_order(const std::vector<std::u8string>& order, const std::u8string_view moved, const std::size_t slot)
        {
            const auto found { std::find(order.begin(), order.end(), moved) };
            if (found == order.end())
                return false;
            const auto from { static_cast<std::size_t>(found - order.begin()) };
            return slot != from && slot != from + 1u;
        }

        // 받는 틈의 세로 막대다. `x`는 막대의 왼쪽 끝이다.
        void draw_gap_marker(draw_context& context, const float x, const rect_f& box)
        {
            const float scale { context.scale > 0.0f ? context.scale : 1.0f };
            context.canvas.drawRect(SkRect::MakeXYWH(x, box.y, tab_gap_marker_width * scale, box.height), solid_paint(context.palette.accent));
        }

        // 탭 한 개다.
        // 누르면 선택 메시지를 내고, 끌어 놓으면 순서 변경 메시지를 낸다.
        class tab_element final : public ui_element
        {
        public:
            tab_element(tab_item item, const bool selected, const tab_bar_config& bar, const ui_element_id& container, const std::size_t index, tab_order order)
                : ui_element { ui_element_id { ui_element_kind::tab, item.key } }
                , item_ { std::move(item) }
                , selected_ { selected }
                , gaps_ { bar.move != nullptr || bar.receive != nullptr }
            {
                const tab_message_factory& select { bar.select };
                const tab_message_factory& close { bar.close };
                const tab_reorder_message_factory& reorder { bar.reorder };
                const tab_context_message_factory& context_menu { bar.context_menu };
                // 묶음 안에서 글자로 찾을 때 보는 글이다 (탭에 적힌 이름과 같은 것).
                set_search_label(item_.label);
                if (select != nullptr)
                {
                    const std::u8string key { item_.key };
                    set_action(ui_trigger::left_click, [key, &select](const ui_action_context&) -> std::vector<input_action> { return { select(key) }; });
                }

                // 닫기 버튼도 같은 동작을 받으므로 그 버튼을 만들기 전에 한 번 짓는다.
                ui_action open_menu {};
                if (context_menu != nullptr)
                {
                    const std::u8string key { item_.key };
                    open_menu = [key, &context_menu](const ui_action_context& context) -> std::vector<input_action> { return { context_menu(key, context) }; };
                    set_action(ui_trigger::right_click, open_menu);
                }

                if (reorder != nullptr || bar.move != nullptr || bar.unclaimed_drop != nullptr)
                {
                    // 잡은 지점과 탭 원점의 차이는 배치가 정하므로 잡는 순간의 bounds에서 읽는다.
                    const ui_element* const self { this };
                    const std::u8string key { item_.key };
                    std::u8string label { item_.label };
                    // 이 탭이 속한 막대다. 놓는 쪽이 같은 막대인지를 이것으로 가른다.
                    drag_source source {};
                    source.make_payload = [self, key, label, container, outside_cursor = bar.outside_cursor](const ui_action_context& context) {
                        drag_payload payload {};
                        payload.source = context.element;
                        payload.container = container;
                        payload.dragged_owner = key;
                        payload.label = label;
                        payload.grab_offset_x = context.x - self->bounds().x;
                        payload.grab_offset_y = context.y - self->bounds().y;
                        payload.outside_cursor = outside_cursor;
                        return payload;
                    };
                    // 받는 대상 없이 놓였다 — 창 밖에 놓아 새 창으로 떼어 낼 자리다. 무엇을 할지는 앱이 정한다.
                    if (bar.unclaimed_drop != nullptr)
                        source.on_unclaimed_drop = [&unclaimed = bar.unclaimed_drop](const drag_payload& payload, const drag_release& release) -> std::vector<input_action> {
                            return { unclaimed(payload, release) };
                        };
                    set_drag_source(std::move(source));
                }

                if (gaps_)
                {
                    // 앞·뒤 반쪽으로 끼울 틈을 가른다. 같은 막대의 탭은 순서가 바뀌는 틈만, 다른 막대의 탭은 어느 틈이든
                    // 받는다 (bar.move·bar.receive 중 선 것만).
                    //  - `move` 없이 `reorder`와 `receive`만 단 막대는 같은 막대 안을 지금처럼 `reorder`로 옮긴다 (앞·뒤를
                    //    싣지 않는 예전 메시지라 자기 자신만 거절한다).
                    const ui_element* const self { this };
                    const std::u8string key { item_.key };
                    const bool moving { bar.move != nullptr };
                    const bool reordering { reorder != nullptr };
                    const bool receiving { bar.receive != nullptr };
                    drop_target drop {};
                    drop.accepts = [container, order, moving, reordering, receiving](const drag_payload& payload) {
                        return ((moving || reordering) && own_tab(payload, container, *order)) || (receiving && foreign_tab(payload, container, *order));
                    };
                    drop.accepts_at = [self, key, container, order, index, moving](const drag_payload& payload, const float x, const float) {
                        if (own_tab(payload, container, *order) == false)
                            return true;
                        if (moving == false)
                            return payload.dragged_owner != key;
                        const std::size_t slot { index + (side_at(self->bounds(), x) == drop_side::after ? 1u : 0u) };
                        return changes_order(*order, payload.dragged_owner, slot);
                    };
                    drop.on_drop = [self, key, container, order, &move = bar.move, &reorder, &receive = bar.receive](const drag_payload& payload,
                                       const ui_action_context& context) -> std::vector<input_action> {
                        const drop_side side { side_at(self->bounds(), context.x) };
                        if (own_tab(payload, container, *order) == false)
                            return { receive(payload, key, side) };
                        if (move != nullptr)
                            return { move(payload.dragged_owner, key, side) };
                        return { reorder(payload.dragged_owner, key) };
                    };
                    // 테두리 대신 틈의 세로 막대를 스스로 그린다.
                    drop.custom_highlight = true;
                    set_drop_target(std::move(drop));
                }
                else if (reorder != nullptr)
                {
                    const std::u8string key { item_.key };
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
                // 우클릭은 hit된 element의 것만 나가고 부모로 올라가지 않는다.
                // 버튼 위의 우클릭도 그 탭의 메뉴가 되도록 같은 동작을 단다.
                if (open_menu != nullptr)
                    button->set_action(ui_trigger::right_click, std::move(open_menu));
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
                const bool drop_here { interaction.drag.has_value() && interaction.drag->payload.suppress_drop_highlight == false && interaction.drag->hovered_drop_target == id() };

                ui_color background { 0 };
                bool fill { false };
                // 앞·뒤로 끼우는 막대는 바탕 대신 틈의 막대로 받는 자리를 보인다 (그림은 아래에서 맨 위에 얹는다).
                if (drop_here && gaps_ == false)
                {
                    background = context.palette.drop_target_background;
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
                float text_right { box.x + box.width - reserved };
                // 배지가 이름보다 먼저 자리를 갖는다 — 잘린 개수는 틀린 개수로 읽힌다.
                if (item_.badge.has_value() && text_right > text_left)
                    text_right = draw_badge(context, text_left, text_right, box, scale) - tab_badge_gap * scale;
                const float text_width { text_right - text_left };
                if (text_width > 0.0f)
                {
                    const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(context.metrics.body_font_size, scale) };
                    SkPaint foreground { solid_paint(context.palette.primary_foreground) };
                    if (selected_ == false)
                        foreground.setAlphaf(0.75f);
                    static_cast<void>(draw_text_within(context.canvas, item_.label, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
                }
                draw_children(context, interaction);
                if (drop_here && gaps_)
                {
                    const float marker { tab_gap_marker_width * scale };
                    draw_gap_marker(context, side_at(box, interaction.drag->x) == drop_side::before ? box.x : box.x + box.width - marker, box);
                }
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::tab, .name = access_label(), .selected = selected_ };
            }

        private:
            // 보조 기술이 읽는 이름이다. 배지의 말이 이름 뒤에 붙는다 — 개수가 바뀌면 이름이 바뀌어
            // 보조 기술이 그 변화를 듣는다.
            [[nodiscard]] std::u8string access_label() const
            {
                if (item_.badge.has_value() == false)
                    return item_.label;
                const std::u8string& spoken { item_.badge->description.empty() ? item_.badge->text : item_.badge->description };
                if (spoken.empty())
                    return item_.label;
                return item_.label + u8", " + spoken;
            }

            // 배지를 `right`에 붙여 그리고, 그 왼쪽 끝을 답한다 (물리 픽셀).
            //  - `left`보다 왼쪽으로는 나가지 않는다. 그만큼도 없으면 배지가 잘린다.
            [[nodiscard]] float draw_badge(draw_context& context, const float left, const float right, const rect_f& box, const float scale) const
            {
                const tab_badge& badge { *item_.badge };
                if (badge.text.empty() && badge_element::shows_glyph(badge.tone) == false)
                {
                    const float radius { tab_badge_dot_radius * scale };
                    draw_badge_dot(context, right - radius, box.y + box.height / 2.0f, radius, badge.tone);
                    return right - 2.0f * radius;
                }
                const float height { badge_element::height_for({ .font_size = tab_badge_font_size }) * scale };
                const float width { std::min(badge_pill_width(context, badge.text, badge.tone, tab_badge_font_size), right - left) };
                draw_badge_pill(context, { right - width, box.y + (box.height - height) / 2.0f, width, height }, badge.text, badge.tone, tab_badge_font_size);
                return right - width;
            }

            tab_item item_ {};
            bool selected_ { false };
            // 앞·뒤 틈으로 받는다 (`tab_bar_config::move`·`receive`).
            bool gaps_ { false };
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
        // 탭 키의 순서다. 아래에서 항목을 옮겨 담기 전에 적어 둔다 — 탭과 막대 끝의 drop 판정이 함께 본다.
        std::vector<std::u8string> keys {};
        keys.reserve(config_.items.size());
        for (const tab_item& item : config_.items)
            keys.push_back(item.key);
        const tab_order order { std::make_shared<const std::vector<std::u8string>>(std::move(keys)) };
        std::size_t index { 0 };
        for (tab_item& item : config_.items)
        {
            const bool selected { item.key == config_.selected };
            auto tab { std::make_unique<tab_element>(std::move(item), selected, config_, id(), index, order) };
            last_tab_ = tab.get();
            lane->add(std::move(tab), config_.tab_width);
            ++index;
        }

        // 흘리기·잘라내기·다듬기는 띠의 몫이다.
        // 내용 폭은 담는 쪽이 알려 준다 — 측정 단계가 없다.
        strip_config strip {};
        strip.content_width = static_cast<float>(config_.items.size()) * config_.tab_width;
        strip.scroll_offset = config_.scroll_offset;
        strip.scroll = config_.scroll;
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

        // 마지막 탭 뒤의 빈 곳도 끼울 틈이다 (마지막 탭의 뒤). 탭이 없는 막대는 막대 전체가 그 틈이다.
        //  - 탭 위의 자리는 탭이 먼저 답한다. 탭이 거절한 자리(순서가 그대로)는 여기로 내려오지만, 이 대상은 마지막
        //    탭의 오른쪽만 받으므로 그 거절을 뒤집지 않는다.
        if (config_.move != nullptr || config_.receive != nullptr)
        {
            const tab_bar_element* const self { this };
            const ui_element_id container { id() };
            const bool moving { config_.move != nullptr };
            const bool receiving { config_.receive != nullptr };
            drop_target end {};
            end.accepts = [container, order, moving, receiving](const drag_payload& payload) {
                return (moving && own_tab(payload, container, *order)) || (receiving && foreign_tab(payload, container, *order));
            };
            end.accepts_at = [self, container, order](const drag_payload& payload, const float x, const float y) {
                if (self->end_gap_contains(x, y) == false)
                    return false;
                return own_tab(payload, container, *order) == false || changes_order(*order, payload.dragged_owner, order->size());
            };
            end.on_drop = [self, container, order](const drag_payload& payload, const ui_action_context&) -> std::vector<input_action> {
                const std::u8string last { order->empty() ? std::u8string {} : order->back() };
                if (own_tab(payload, container, *order))
                    return { self->config_.move(payload.dragged_owner, last, drop_side::after) };
                return { self->config_.receive(payload, last, drop_side::after) };
            };
            end.custom_highlight = true;
            set_drop_target(std::move(end));
        }
    }

    float tab_bar_element::end_gap_x() const noexcept
    {
        return last_tab_ != nullptr ? last_tab_->bounds().x + last_tab_->bounds().width : strip_->bounds().x;
    }

    bool tab_bar_element::end_gap_contains(const float x, const float y) const noexcept
    {
        // 띠 안이어야 한다 — 넘침 버튼 위는 틈이 아니다.
        return strip_->bounds().contains(x, y) && x >= end_gap_x();
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
        // 마지막 탭 뒤의 틈이 받는 자리면 그 틈에 세로 막대를 세운다. 띠가 자른 자리 밖(흘러 숨은 끝)에는 그리지 않는다.
        if (interaction.drag.has_value() && interaction.drag->payload.suppress_drop_highlight == false && interaction.drag->hovered_drop_target == id())
        {
            const rect_f lane { strip_->bounds() };
            const float marker { tab_gap_marker_width * scale };
            const float x { std::clamp(end_gap_x(), lane.x, std::max(lane.x, lane.x + lane.width - marker)) };
            draw_gap_marker(context, x, lane);
        }
    }
    access_info tab_bar_element::accessibility() const
    {
        // 이름은 `search_label` → 툴팁 순서다 (`ui_element::access_name`).
        // 막대에 툴팁을 달면 탭 오른쪽 빈 자리에서 글 상자가 뜬다.
        return { .role = access_role::tab_list, .name = access_name() };
    }
} // namespace luil
