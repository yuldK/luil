#include "luil/ui/menu_element.h"

#include "luil/ui/draw_primitives.h"
#include "luil/generated/codicons.h"
#include "luil/ui/text_block.h"

#include <algorithm>

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <memory>
#include <utility>

namespace luil {
    namespace {
        // 메뉴 항목 한 줄이다.
        // 마우스 hover·눌림과 키보드 강조를 같은 모양으로 그린다.
        class menu_item_element final : public ui_element
        {
        public:
            menu_item_element(menu_item_config item, const menu_config& config)
                : ui_element { ui_element_id { ui_element_kind::menu_item, item.key } }
                , item_ { std::move(item) }
                , opened_ { item_.children.empty() == false && config.opened_key == item_.key }
                , open_submenu_ { &config.open_submenu }
                , menu_ { &config }
            {
                set_enabled(item_.enabled);
                // 메뉴가 열려 있으면 키보드는 메뉴의 것이다 (↑/↓/Enter/Esc).
                // Tab이 항목에 서면 메뉴는 열린 채로 초점만 밖으로 달아난다.
                set_tab_stop(false);
                const auto& select { item_.children.empty() ? config.select : config.open_submenu };
                if (select == nullptr)
                    return;
                const std::u8string key { item_.key };
                set_cursor(ui_cursor::hand);
                // 끝 항목은 고른 뒤의 메시지(`after_select`)를 함께 낸다. 하위 메뉴를 여는 항목은 고른 것이 아니다.
                const ui_action* const after { item_.children.empty() ? &config.after_select : nullptr };
                set_action(ui_trigger::left_click, [key, &select, after](const ui_action_context& context) -> std::vector<input_action> {
                    std::vector<input_action> actions { select(key) };
                    if (after != nullptr && *after != nullptr)
                        for (input_action& action : (*after)(context))
                            actions.push_back(std::move(action));
                    return actions;
                });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                // 키보드 강조는 hover와 같은 자리 표시다.
                // 하위 메뉴를 연 항목도 강조로 남는다 — 포인터와 키 강조가 자식으로 옮겨 가도 어느 항목의 자식인지 보인다.
                const bool highlighted { interaction.hovered == id() || interaction.menu_highlight == id() || opened_ || shown_as_default(interaction) };
                if (enabled() && (highlighted || interaction.pressed == id()))
                {
                    const ui_color background { interaction.pressed == id() ? context.palette.button_pressed_background : context.palette.button_hover_background };
                    const float radius { context.metrics.control_corner_radius * scale };
                    context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radius, radius), solid_paint(background));
                }

                const float inset { 8.0f * scale };
                float text_left { box.x + inset };
                if (item_.kind == menu_item_kind::radio && item_.checked)
                {
                    context.canvas.drawCircle(text_left + 8.0f * scale, box.y + box.height * 0.5f, 3.0f * scale,
                        solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground));
                }
                const char32_t icon { item_.kind == menu_item_kind::check
                    ? (item_.checked ? codicons::icon_check : 0) : (item_.kind == menu_item_kind::radio ? 0 : item_.icon)
                };
                if (icon != 0)
                {
                    const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 13.0f * scale };
                    const SkPaint glyph { solid_paint(enabled() ? context.palette.secondary_foreground : context.palette.disabled_foreground) };
                    draw_centered_glyph(context.canvas, icon, { text_left, box.y, 16.0f * scale, box.height }, glyph_font, glyph);
                }
                // 아이콘 유무와 무관하게 글자 시작을 맞춰 목록이 줄지어 보이게 한다.
                text_left += 22.0f * scale;

                const SkFont shortcut_font { make_text_font(context, { .size = context.metrics.body_font_size, .weight = context.metrics.font_weights.body_text }) };
                const float arrow_width { item_.children.empty() ? 0.0f : 18.0f * scale };
                if (arrow_width > 0.0f)
                    draw_centered_glyph(context.canvas, codicons::icon_chevron_right,
                        { box.x + box.width - inset - arrow_width, box.y, arrow_width, box.height },
                        SkFont { sk_ref_sp(context.codicon_typeface), 13.0f * scale }, solid_paint(context.palette.primary_foreground));
                const float shortcut_width { measure_text(item_.shortcut_text, shortcut_font) };
                const float shortcut_gap { item_.shortcut_text.empty() ? 0.0f : 16.0f * scale };
                if (shortcut_width > 0.0f)
                    draw_text(context.canvas, item_.shortcut_text, box.x + box.width - inset - arrow_width - shortcut_width,
                        box.y + centered_text_baseline(shortcut_font, box.height), shortcut_font,
                        solid_paint(enabled() ? context.palette.secondary_foreground : context.palette.disabled_foreground));
                const float text_width { box.x + box.width - inset - text_left - shortcut_width - shortcut_gap - arrow_width };
                if (text_width > 0.0f)
                {
                    const SkFont font { make_text_font(context, { .size = context.metrics.body_font_size, .weight = context.metrics.font_weights.body_text }) };
                    const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
                    static_cast<void>(draw_text_within(context.canvas, item_.label, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
                }
            }

            [[nodiscard]] access_info accessibility() const override
            {
                access_info info { .role = access_role::menu_item, .name = item_.label };
                if (item_.kind == menu_item_kind::check)
                    info.checked = item_.checked;
                if (item_.kind == menu_item_kind::radio)
                    info.selected = item_.checked;
                // 자식이 있는 항목은 펼침 상태를 알린다 (UIA ExpandCollapse). 보조 기술이 하위 메뉴가 열렸는지 읽는다.
                if (item_.children.empty() == false)
                    info.expanded = opened_;
                return info;
            }

            // 펼치기는 이 항목의 키로 여는 절대 메시지다 — 클릭으로 흘려도 같은 메시지지만, 펼침 명령은 기본 실행으로
            // 물러서지 않는다 (`ui_element::access_actions`). 접기는 열린 하위 메뉴의 `close_submenu`가 쥐어 여기서 낼 수 없다.
            [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override
            {
                if (request.command != access_command::expand)
                    return ui_element::access_actions(request);
                if (item_.children.empty() || item_.enabled == false || *open_submenu_ == nullptr)
                    return std::nullopt;
                return std::vector<input_action> { (*open_submenu_)(item_.key) };
            }

        private:
            // 강조가 이 메뉴의 어느 항목에도 없을 때 기본 항목(`default_key`)이면 강조처럼 그린다 — Enter가 고를 것이 보인다.
            //  - 질의가 바뀌어 강조한 항목이 사라진 자리도 "없다"다. 그때 Enter가 기본 항목을 고르는 것과 같은 판정이다.
            [[nodiscard]] bool shown_as_default(const interaction_snapshot& interaction) const noexcept
            {
                if (item_.enabled == false || menu_->default_key.empty() || menu_->default_key != item_.key)
                    return false;
                if (interaction.menu_highlight.kind != ui_element_kind::menu_item)
                    return true;
                for (const menu_item_config& sibling : menu_->items)
                    if (sibling.key == interaction.menu_highlight.owner)
                        return false;
                return true;
            }

            menu_item_config item_ {};
            bool opened_ { false };
            // 담은 메뉴의 설정 안 factory다. 메뉴가 항목을 소유하므로 항목보다 오래 산다 (클릭 액션이 같은 것을 쥔다).
            const menu_message_factory* open_submenu_ { nullptr };
            const menu_config* menu_ { nullptr };
        };
    }

    menu_element::menu_element(menu_config config)
        : ui_element { ui_element_id { ui_element_kind::menu, config.owner } }
        , config_ { std::move(config) }
    {
        // 메뉴는 위에 뜨는 표면이다.
        // 항목 사이 여백·구분선을 눌러도 아래 element로 새지 않는다.
        set_hit_opaque(true);
        for (const menu_item_config& item : config_.items)
            add_child(std::make_unique<menu_item_element>(item, config_));
    }

    const menu_config& menu_element::config() const noexcept
    {
        return config_;
    }

    bool menu_element::has_submenu(const std::u8string_view key) const noexcept
    {
        for (const auto& item : config_.items)
            if (item.key == key)
                return item.enabled && item.children.empty() == false && config_.open_submenu != nullptr;
        return false;
    }

    std::optional<std::vector<input_action>> menu_element::open_submenu(const std::u8string_view key) const
    {
        if (has_submenu(key) == false)
            return std::nullopt;
        // 이미 연 항목이어도 여는 메시지를 다시 낸다. 키가 이 메뉴에 왔다는 것은 자식 tree가 아직(또는 더는) 없다는
        // 뜻이라, 빈 답으로 삼키면 →가 자식으로 들어가지 못한다. 열림 경로를 그 키로 두는 메시지라 거듭 내도 같다.
        //  - hover 열림은 `submenu_deadline`이 이미 연 항목을 따로 거른다.
        return std::vector<input_action> { config_.open_submenu(std::u8string { key }) };
    }

    float menu_element::height_for(const menu_config& config) noexcept
    {
        float height { 2.0f * menu_padding };
        for (const menu_item_config& item : config.items)
        {
            if (item.separator_above)
                height += menu_separator_height;
            height += menu_item_height;
        }
        return height;
    }

    float menu_element::width_for(const menu_config& config, const text_measurer& measurer, const float font_size)
    {
        float width { 0.0f };
        const float size { scaled_font_size(font_size, 1.0f) };
        for (const auto& item : config.items)
        {
            const float shortcut { item.shortcut_text.empty() ? 0.0f : 16.0f + measurer(item.shortcut_text, size) };
            width = std::max(width, measurer(item.label, size) + shortcut + (item.children.empty() ? 0.0f : 18.0f));
        }
        return width + 2.0f * menu_padding + 16.0f + 22.0f;
    }

    void menu_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float padding { menu_padding * scale };
        float y { context.slot.y + padding };
        for (std::size_t index = 0; index < children().size(); ++index)
        {
            if (config_.items[index].separator_above)
                y += menu_separator_height * scale;
            children()[index]->arrange(context.for_child({ context.slot.x + padding, y, context.slot.width - 2.0f * padding, menu_item_height * scale }));
            y += menu_item_height * scale;
        }
    }

    void menu_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        // 메뉴는 popup의 창 바탕을 대신하는 판이다. 효과가 선 표면이면 표면 바탕(`backdrop_background`)을 그대로 둔다.
        if (context.backdrop == false)
            context.canvas.drawRect(body, solid_paint(context.palette.surface_background));
        if (config_.border)
        {
            SkPaint border { solid_paint(context.palette.tooltip_border) };
            border.setStyle(SkPaint::kStroke_Style);
            border.setStrokeWidth(1.0f * scale);
            context.canvas.drawRect(body, border);
        }

        // 구분선은 항목 사이 여백의 한가운데를 지난다.
        const SkPaint divider { solid_paint(context.palette.divider) };
        for (std::size_t index = 0; index < children().size(); ++index)
        {
            if (config_.items[index].separator_above == false)
                continue;
            const rect_f item_box { children()[index]->bounds() };
            const float line_y { item_box.y - (menu_separator_height * scale) / 2.0f };
            context.canvas.drawRect(SkRect::MakeXYWH(item_box.x, line_y, item_box.width, 1.0f * scale), divider);
        }
        draw_children(context, interaction);
    }
    access_info menu_element::accessibility() const
    {
        const bool radio { std::any_of(config_.items.begin(), config_.items.end(), [](const auto& item) { return item.kind == menu_item_kind::radio; }) };
        return { .role = access_role::menu, .name = tooltip(), .selection_container = radio };
    }
}
