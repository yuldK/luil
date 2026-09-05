#include "luil/ui/wrap_element.h"

#include <utility>

namespace luil {
    wrap_element::wrap_element(const ui_element_id id, wrap_config config)
        : ui_element { id }
        , config_ { config }
    {}

    void wrap_element::add(std::unique_ptr<ui_element> child)
    {
        add_child(std::move(child));
    }

    void wrap_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        // 담는 쪽이 높이를 잡을 때 부른 것과 **같은 함수**로 센다.
        // 열 수를 세는 식이 둘이면 잡은 높이와 실제 줄 수가 어긋나 마지막 줄이 잘린다.
        const std::size_t columns { wrap_columns_for(config_, context.slot.width / scale) };

        const float item_width { config_.item_width * scale };
        const float item_height { config_.item_height * scale };
        const float step_x { item_width + config_.spacing * scale };
        const float step_y { item_height + config_.line_spacing * scale };

        std::size_t index { 0 };
        for (const std::unique_ptr<ui_element>& child : children())
        {
            const rect_f slot {
                context.slot.x + static_cast<float>(index % columns) * step_x,
                context.slot.y + static_cast<float>(index / columns) * step_y,
                item_width,
                item_height,
            };
            child->arrange(context.for_child(slot));
            ++index;
        }
    }

    void wrap_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }
} // namespace luil
