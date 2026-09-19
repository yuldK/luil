#include "luil/ui/menu_element.h"

#include "luil/ui/draw_primitives.h"

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
            menu_item_element(menu_item_config item, const menu_message_factory& select)
                : ui_element { ui_element_id { ui_element_kind::menu_item, item.key } }
                , item_ { std::move(item) }
            {
                set_enabled(item_.enabled);
                // 메뉴가 열려 있으면 키보드는 메뉴의 것이다 (↑/↓/Enter/Esc).
                // Tab이 항목에 서면 메뉴는 열린 채로 초점만 밖으로 달아난다.
                set_tab_stop(false);
                if (select == nullptr)
                    return;
                const std::u8string key { item_.key };
                set_cursor(ui_cursor::hand);
                set_action(ui_trigger::left_click, [key, &select](const ui_action_context&) -> std::vector<input_action> { return { select(key) }; });
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
                const bool highlighted { interaction.hovered == id() || interaction.menu_highlight == id() };
                if (enabled() && (highlighted || interaction.pressed == id()))
                {
                    const ui_color background { interaction.pressed == id() ? context.palette.button_pressed_background : context.palette.button_hover_background };
                    const float radius { context.metrics.control_corner_radius * scale };
                    context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radius, radius), solid_paint(background));
                }

                const float inset { 8.0f * scale };
                float text_left { box.x + inset };
                if (item_.icon != 0)
                {
                    const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 13.0f * scale };
                    const SkPaint glyph { solid_paint(enabled() ? context.palette.secondary_foreground : context.palette.disabled_foreground) };
                    draw_centered_glyph(context.canvas, item_.icon, { text_left, box.y, 16.0f * scale, box.height }, glyph_font, glyph);
                }
                // 아이콘 유무와 무관하게 글자 시작을 맞춰 목록이 줄지어 보이게 한다.
                text_left += 22.0f * scale;

                const float text_width { box.x + box.width - inset - text_left };
                if (text_width > 0.0f)
                {
                    const SkFont font { sk_ref_sp(context.ui_typeface), context.metrics.body_font_size * scale };
                    const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
                    static_cast<void>(draw_text_within(context.canvas, item_.label, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
                }
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::menu_item, .name = item_.label };
            }

        private:
            menu_item_config item_ {};
        };
    } // namespace

    menu_element::menu_element(menu_config config)
        : ui_element { ui_element_id { ui_element_kind::menu, config.owner } }
        , config_ { std::move(config) }
    {
        // 메뉴는 위에 뜨는 표면이다.
        // 항목 사이 여백·구분선을 눌러도 아래 element로 새지 않는다.
        set_hit_opaque(true);
        for (const menu_item_config& item : config_.items)
            add_child(std::make_unique<menu_item_element>(item, config_.select));
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
        context.canvas.drawRect(body, solid_paint(context.palette.surface_background));
        SkPaint border { solid_paint(context.palette.tooltip_border) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.0f * scale);
        context.canvas.drawRect(body, border);

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
        return { .role = access_role::menu, .name = tooltip() };
    }
} // namespace luil
