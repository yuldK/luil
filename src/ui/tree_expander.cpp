#include "ui/tree_expander.h"

#include "luil/generated/codicons.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkTypeface.h"

#include <utility>

namespace luil {
    tree_expander_element::tree_expander_element(ui_element_id id, std::u8string key, const bool expanded, tree_expand_message_factory set_expanded, ui_element_id focus_target)
        : ui_element { std::move(id) }
        , key_ { std::move(key) }
        , expanded_ { expanded }
        , set_expanded_ { std::move(set_expanded) }
    {
        set_tab_stop(false);
        set_pointer_focus_target(std::move(focus_target));
        if (set_expanded_ == nullptr)
            return;
        set_cursor(ui_cursor::hand);
        set_action(ui_trigger::left_click,
            [key = key_, target = expanded_ == false, set_expanded = set_expanded_](const ui_action_context&) -> std::vector<input_action> { return { set_expanded(key, target) }; });
    }

    std::optional<std::vector<input_action>> tree_expander_element::access_actions(const access_request& request) const
    {
        if (request.command != access_command::expand && request.command != access_command::collapse)
            return ui_element::access_actions(request);
        if (set_expanded_ == nullptr)
            return std::nullopt;
        return std::vector<input_action> { set_expanded_(key_, request.command == access_command::expand) };
    }

    void tree_expander_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void tree_expander_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 13.0f * scale };
        const ui_color color { interaction.hovered == id() ? context.palette.primary_foreground : context.palette.secondary_foreground };
        draw_centered_glyph(context.canvas, expanded_ ? codicons::icon_chevron_down : codicons::icon_chevron_right, bounds(), glyph_font, solid_paint(color));
    }
}
