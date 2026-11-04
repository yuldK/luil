#include "luil/ui/menu_bar_element.h"

#include "luil/ui/draw_primitives.h"
#include "luil/ui/text_block.h"
#include "luil/ui/ui_tree.h"
#include "ui/access_key.h"
#include "ui/default_text.h"
#include "ui/text_button_paint.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

namespace luil {
    namespace {
        // 밑줄의 굵기와 글 바닥선에서의 거리다 (논리 픽셀).
        constexpr float underline_thickness { 1.0f };
        constexpr float underline_gap { 2.0f };

        // 막대 항목 하나다.
        // hover·키보드로 선 자리·열림을 따로 그리고, 누름과 hover 영역과 보조 기술의 펼치기·접기가 각각 절대 상태를 낸다.
        class menu_bar_item_element final : public ui_element
        {
        public:
            menu_bar_item_element(menu_bar_item_config item, const menu_bar_config& config)
                : ui_element { ui_element_id { ui_element_kind::menu_bar_item, item.key } }
                , item_ { std::move(item) }
                , current_ { config.state.current == item_.key }
                , open_ { config.state.current == item_.key && config.state.open }
                , keyboard_ { config.state.keyboard }
                , set_state_ { config.set_state }
            {
                set_enabled(config.enabled && item_.enabled);
                // 항목은 Tab 자리가 아니다. 선 자리는 상태가 그린다 (docs/menu-bar-design.md의 결정 1).
                set_tab_stop(false);
                // 눌러도 텍스트 칸의 초점을 가져가지 않는다. 메뉴를 고르고 나면 글을 치던 자리로 돌아가야 한다.
                set_preserve_pointer_focus(true);
                if (set_state_ == nullptr)
                    return;
                // 닫힌 항목은 그 메뉴를 열고, 열린 항목은 닫는다. 누를 때 펼쳐 있던 항목은 누를 때의 액션이 나간다
                // (menu-focus-fixes-design.md의 X-4) — 바깥 누름의 닫힘이 먼저 게시되어도 다시 열리지 않는다.
                const menu_bar_state target { open_ ? menu_bar_state {} : menu_bar_state { item_.key, true, false } };
                set_action(ui_trigger::left_click, [set_state = set_state_, target](const ui_action_context&) -> std::vector<input_action> { return { set_state(target) }; });
                // 열린 동안 다른 항목에 포인터가 오면 그 메뉴로 옮긴다. 지연이 없고 마우스·펜만이다 (결정 6).
                if (config.state.open && current_ == false && enabled())
                {
                    hover_region_target region {};
                    region.on_enter = [set_state = set_state_, key = item_.key]() { return set_state(menu_bar_state { key, true, false }); };
                    set_hover_region(std::move(region));
                }
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const rect_f box { bounds() };
                if (box.width <= 0.0f || box.height <= 0.0f)
                    return;
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                // hover와 키보드로 선 항목은 hover 바탕, 열린 항목과 누르는 항목은 눌림 바탕이다. 그 밖에는 바탕이 없다.
                const bool pressed { open_ || interaction.pressed == id() };
                const bool hovered { interaction.hovered == id() || current_ };
                if (enabled() && (pressed || hovered))
                {
                    const text_button_state state { .enabled = true, .hovered = hovered, .pressed = pressed };
                    const float radius { context.metrics.control_corner_radius * scale };
                    context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radius, radius),
                        solid_paint(text_button_background(text_button_fill::plain, state, context.palette)));
                }

                const std::u8string text { access_key_display(item_.label, item_.access_key) };
                const SkFont font { make_text_font(context, { .size = context.metrics.body_font_size, .weight = context.metrics.font_weights.body_text }) };
                const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
                const float width { measure_text(text, font) };
                const float x { box.x + std::max(0.0f, (box.width - width) / 2.0f) };
                const float baseline { box.y + centered_text_baseline(font, box.height) };
                draw_text(context.canvas, text, x, baseline, font, foreground);

                // 밑줄은 키보드로 들어왔을 때만 긋는다. "(F)" 덧붙임은 늘 그려 폭이 상태마다 흔들리지 않는다.
                if (keyboard_ == false)
                    return;
                const std::optional<std::size_t> offset { access_key_underline(item_.label, item_.access_key) };
                if (offset.has_value() == false || *offset >= text.size())
                    return;
                const std::u8string_view view { text };
                const float left { x + measure_text(view.substr(0, *offset), font) };
                const float glyph { measure_text(view.substr(*offset, 1), font) };
                context.canvas.drawRect(SkRect::MakeXYWH(left, baseline + underline_gap * scale, glyph, underline_thickness * scale), foreground);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                // 덧붙인 "(F)"는 이름에 넣지 않는다. 키는 AccessKey가 따로 읽힌다.
                //  - `expanded`가 있어 Invoke 대신 ExpandCollapse로 선다 (`invokable`).
                access_info info { .role = access_role::menu_item, .name = item_.label };
                info.expanded = open_;
                if (const char32_t glyph { access_key_glyph(item_.access_key) }; glyph != 0)
                {
                    info.access_key = u8"Alt+";
                    info.access_key += static_cast<char8_t>(glyph);
                }
                return info;
            }

            // 펼치기는 이 항목을 포인터로 연 것과 같은 상태, 접기는 쉬는 막대다. 목표를 나르는 절대 메시지라 오래된
            // 발행본을 본 같은 명령 둘이 열었다 도로 닫지 않는다 (accessibility-action-design.md).
            [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override
            {
                if (request.command != access_command::expand && request.command != access_command::collapse)
                    return ui_element::access_actions(request);
                if (set_state_ == nullptr)
                    return std::nullopt;
                const menu_bar_state target { request.command == access_command::expand ? menu_bar_state { item_.key, true, false } : menu_bar_state {} };
                return std::vector<input_action> { set_state_(target) };
            }

        private:
            menu_bar_item_config item_ {};
            bool current_ { false };
            bool open_ { false };
            bool keyboard_ { false };
            std::function<input_action(const menu_bar_state&)> set_state_ {};
        };

        // 항목 하나의 폭이다 (논리 픽셀). 글은 그리기와 같은 물리 크기로 재고 논리로 되돌린 뒤 올림한다.
        [[nodiscard]] float item_width(const menu_bar_item_config& item, const text_measurer& measurer, const float font_size, const float scale)
        {
            const float ratio { std::isfinite(scale) && scale > 0.0f ? scale : 1.0f };
            const float text { std::ceil(measurer(access_key_display(item.label, item.access_key), scaled_font_size(font_size, ratio)) / ratio) };
            return text + 2.0f * menu_bar_item_padding;
        }

        // 그 항목이 키를 받을 수 있는가 (꺼진 항목은 건너뛴다).
        [[nodiscard]] bool item_usable(const menu_bar_config& config, const std::size_t index) noexcept
        {
            return index < config.items.size() && config.items[index].enabled;
        }

        [[nodiscard]] std::optional<std::size_t> index_of(const menu_bar_config& config, const std::u8string_view key) noexcept
        {
            for (std::size_t index = 0; index < config.items.size(); ++index)
                if (config.items[index].key == key)
                    return index;
            return std::nullopt;
        }

        // 처음(`forward`)이나 끝에서 센 첫 활성 항목이다.
        [[nodiscard]] std::optional<std::size_t> edge_item(const menu_bar_config& config, const bool forward) noexcept
        {
            for (std::size_t step = 0; step < config.items.size(); ++step)
            {
                const std::size_t index { forward ? step : config.items.size() - 1 - step };
                if (item_usable(config, index))
                    return index;
            }
            return std::nullopt;
        }

        // 이웃 활성 항목이다. 끝에서 돌고 꺼진 항목은 건너뛴다. 지금 항목이 없으면 그 방향의 끝 항목이다.
        [[nodiscard]] std::optional<std::size_t> neighbor_item(const menu_bar_config& config, const std::u8string_view current, const bool forward) noexcept
        {
            const std::optional<std::size_t> start { index_of(config, current) };
            if (start.has_value() == false)
                return edge_item(config, forward);
            const std::size_t count { config.items.size() };
            for (std::size_t step = 1; step <= count; ++step)
            {
                const std::size_t index { forward ? (*start + step) % count : (*start + count - step % count) % count };
                if (item_usable(config, index))
                    return index;
            }
            return std::nullopt;
        }

        // 그 키를 접근 키로 둔 첫 활성 항목이다.
        [[nodiscard]] std::optional<std::size_t> access_item(const menu_bar_config& config, const key_code key) noexcept
        {
            for (std::size_t index = 0; index < config.items.size(); ++index)
                if (item_usable(config, index) && access_key_code(config.items[index].access_key) == key)
                    return index;
            return std::nullopt;
        }
    }

    menu_bar_element::menu_bar_element(menu_bar_config config)
        : ui_element { ui_element_id { ui_element_kind::menu_bar, config.owner } }
        , config_ { std::move(config) }
    {
        set_enabled(config_.enabled);
        // 넘친 항목은 막대 밖으로 그리지 않는다. 키보드로는 닿는다.
        set_clip_children(true);
        set_preserve_pointer_focus(true);
        if (config_.access_name.empty() == false)
            set_access_name(config_.access_name);
        for (const menu_bar_item_config& item : config_.items)
            add_child(std::make_unique<menu_bar_item_element>(item, config_));
    }

    const menu_bar_config& menu_bar_element::config() const noexcept
    {
        return config_;
    }

    std::optional<rect_f> menu_bar_element::item_bounds(const std::u8string_view key) const
    {
        for (const auto& child : children())
            if (child->id().owner == key)
                return child->bounds();
        return std::nullopt;
    }

    std::optional<std::vector<input_action>> menu_bar_element::handle_key(const key_pressed_event& event, const bool menu_shown) const
    {
        if (config_.set_state == nullptr || enabled() == false)
            return std::nullopt;
        const menu_bar_state& state { config_.state };
        const bool resting { state.current.empty() };
        const auto send = [this](menu_bar_state next) { return std::optional<std::vector<input_action>> { std::vector<input_action> { config_.set_state(next) } }; };
        const auto key_of = [this](const std::optional<std::size_t> index) { return index.has_value() ? config_.items[*index].key : std::u8string {}; };

        // Alt 단독과 수식 키 없는 F10은 쉬는 막대를 세우고, 서 있거나 열린 막대를 쉬게 한다. Shift+F10은 컨텍스트 메뉴다.
        const bool function_key { config_.f10_activates && event.key == key_code::f10 && event.shift == false && event.shortcut_modifier_down() == false };
        if (event.key == key_code::alt_tap || function_key)
        {
            if (resting == false)
                return send({});
            const std::optional<std::size_t> first { edge_item(config_, true) };
            if (first.has_value() == false)
                return std::vector<input_action> {};
            return send({ key_of(first), false, true });
        }

        // Alt+접근 키는 언제든 그 메뉴를 연다. 열린 동안이면 그 메뉴로 옮긴다. 맞지 않으면 앱 단축키로 흐른다.
        if (event.alt && event.control == false && event.meta == false && access_key_candidate(event.key))
        {
            if (const std::optional<std::size_t> index { access_item(config_, event.key) }; index.has_value())
                return send({ key_of(index), true, true });
            return std::nullopt;
        }
        if (resting || event.shortcut_modifier_down())
            return std::nullopt;

        // 메뉴가 떠 있으면 막대는 메뉴가 쓰지 않은 ←/→와 깊이 0의 Esc만 받는다.
        if (menu_shown)
        {
            if (event.key == key_code::arrow_left || event.key == key_code::arrow_right)
            {
                const std::optional<std::size_t> next { neighbor_item(config_, state.current, event.key == key_code::arrow_right) };
                return next.has_value() ? send({ key_of(next), true, true }) : std::vector<input_action> {};
            }
            if (event.key == key_code::escape)
                return send({ state.current, false, state.keyboard });
            return std::nullopt;
        }

        switch (event.key)
        {
        case key_code::arrow_left:
        case key_code::arrow_right:
        case key_code::home:
        case key_code::end: {
            const bool forward { event.key == key_code::arrow_right || event.key == key_code::home };
            const bool edge { event.key == key_code::home || event.key == key_code::end };
            const std::optional<std::size_t> next { edge ? edge_item(config_, forward) : neighbor_item(config_, state.current, forward) };
            return next.has_value() ? send({ key_of(next), state.open, true }) : std::vector<input_action> {};
        }
        case key_code::arrow_down:
        case key_code::arrow_up:
        case key_code::enter:
        case key_code::space:
            return send({ state.current, true, true });
        case key_code::escape:
            // 열렸다고 했는데 메뉴가 아직 서지 않았으면 메뉴의 Esc와 같이 막대만 남긴다.
            return send(state.open ? menu_bar_state { state.current, false, state.keyboard } : menu_bar_state {});
        default:
            break;
        }
        if (access_key_candidate(event.key))
            if (const std::optional<std::size_t> index { access_item(config_, event.key) }; index.has_value())
                return send({ key_of(index), true, true });
        return std::nullopt;
    }

    float menu_bar_element::width_for(const menu_bar_config& config, const text_measurer& measurer, const float font_size, const float scale)
    {
        float width { 0.0f };
        for (const menu_bar_item_config& item : config.items)
            width += item_width(item, measurer, font_size, scale);
        return width;
    }

    void menu_bar_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const measure_context measure { context.measure() };
        const ui_metrics& metrics { measure.style_metrics() };
        float x { context.slot.x };
        for (std::size_t index = 0; index < children().size(); ++index)
        {
            const menu_bar_item_config& item { config_.items[index] };
            const float text { std::ceil(measure.text_width(access_key_display(item.label, item.access_key), metrics.body_font_size, { metrics.font_weights.body_text })) };
            const float width { (text + 2.0f * menu_bar_item_padding) * scale };
            children()[index]->arrange(context.for_child({ x, context.slot.y, width, context.slot.height }));
            x += width;
        }
    }

    void menu_bar_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 줄 바탕은 칠하지 않는다. 항목이 자기 바탕을 그린다.
        draw_children(context, interaction);
    }

    std::optional<size_f> menu_bar_element::preferred_size(const measure_context& context, const float available_width) const
    {
        static_cast<void>(available_width);
        const ui_metrics& metrics { context.style_metrics() };
        float width { 0.0f };
        for (const menu_bar_item_config& item : config_.items)
            width += std::ceil(context.text_width(access_key_display(item.label, item.access_key), metrics.body_font_size, { metrics.font_weights.body_text })) + 2.0f * menu_bar_item_padding;
        return size_f { width, menu_bar_height };
    }

    access_info menu_bar_element::accessibility() const
    {
        return { .role = access_role::menu_bar, .name = std::u8string { default_text::text_or_default(config_.access_name, default_text::menu_bar_name()) } };
    }

    const menu_bar_element* active_menu_bar(const ui_tree& tree)
    {
        for (const ui_element_id& id : tree.ids_of_kind(ui_element_kind::menu_bar))
        {
            const auto* const bar { dynamic_cast<const menu_bar_element*>(tree.find(id)) };
            if (bar == nullptr || bar->enabled() == false || bar->config().set_state == nullptr)
                continue;
            if (tree.visibly_contains(id) == false || tree.within_focus_trap(id) == false)
                continue;
            return bar;
        }
        return nullptr;
    }

    std::vector<std::u8string> menu_bar_access_conflicts(const menu_bar_config& config, const std::span<const app_command> commands)
    {
        std::vector<std::u8string> conflicts {};
        for (const app_command& command : commands)
        {
            bool conflict { false };
            for (const key_chord& chord : effective_shortcuts(command))
            {
                if (chord.alt == false || chord.control || chord.shift || access_key_candidate(chord.key) == false)
                    continue;
                if (access_item(config, chord.key).has_value())
                    conflict = true;
            }
            if (conflict)
                conflicts.push_back(command.id);
        }
        return conflicts;
    }
}
