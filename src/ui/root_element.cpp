#include "luil/ui/root_element.h"

#include <utility>

namespace luil {
    root_element::root_element(std::u8string owner)
        : ui_element { ui_element_id { ui_element_kind::root, std::move(owner) } }
    {}

    void root_element::add(std::unique_ptr<ui_element> child)
    {
        add_child(std::move(child));
    }

    void root_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void root_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }
} // namespace luil
