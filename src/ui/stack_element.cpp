#include "luil/ui/stack_element.h"

#include <utility>

namespace luil {
    stack_element::stack_element(const ui_element_id id, stack_config config)
        : ui_element { id }
        , config_ { std::move(config) }
    {}

    void stack_element::add(std::unique_ptr<ui_element> child, const stack_item item)
    {
        entries_.push_back(entry { child.get(), item });
        add_child(std::move(child));
    }

    void stack_element::add(std::unique_ptr<ui_element> child, const float length)
    {
        add(std::move(child), stack_item { .length = length });
    }

    void stack_element::add_flexible(std::unique_ptr<ui_element> child, const float weight)
    {
        add(std::move(child), stack_item { .weight = weight > 0.0f ? weight : 1.0f });
    }

    void stack_element::add_gap(const float length)
    {
        entries_.push_back(entry { nullptr, stack_item { .length = length } });
    }

    void stack_element::add_flexible_gap(const float weight)
    {
        entries_.push_back(entry { nullptr, stack_item { .weight = weight > 0.0f ? weight : 1.0f } });
    }

    void stack_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        if (entries_.empty())
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const bool row { config_.direction == stack_direction::row };
        const float left { context.slot.x + config_.padding.left * scale };
        const float top { context.slot.y + config_.padding.top * scale };
        const float inner_width { context.slot.width - (config_.padding.left + config_.padding.right) * scale };
        const float inner_height { context.slot.height - (config_.padding.top + config_.padding.bottom) * scale };
        const float main_total { row ? inner_width : inner_height };
        const float cross_total { row ? inner_height : inner_width };

        // 고정 길이와 간격을 뺀 나머지를 비율대로 나눈다.
        const float spacing { config_.spacing * scale };
        float fixed { spacing * static_cast<float>(entries_.size() - 1) };
        float weights { 0.0f };
        for (const entry& current : entries_)
            if (current.item.weight > 0.0f)
                weights += current.item.weight;
            else
                fixed += current.item.length * scale;
        float remaining { main_total - fixed };
        if (remaining < 0.0f)
            remaining = 0.0f;

        // 남는 자리는 유연 항목이 다 가져가므로, 유연 항목이 하나도 없을 때만
        // 정렬이 뜻을 갖는다.
        //  - `remaining`은 위에서 이미 0으로 잘렸으므로 `free`는 음수가 되지 않는다.
        //    넘칠 때 가운데·끝 정렬이 여백 밖으로 새는 경계가 아예 없다.
        const float free { weights > 0.0f ? 0.0f : remaining };
        float offset { 0.0f };
        if (config_.main_alignment == stack_main_alignment::center)
            offset = free / 2.0f;
        else if (config_.main_alignment == stack_main_alignment::end)
            offset = free;

        for (const entry& current : entries_)
        {
            float length { current.item.weight > 0.0f ? (weights > 0.0f ? remaining * (current.item.weight / weights) : 0.0f) : current.item.length * scale };
            // 하한은 아래로만 민다. 그만큼 넘치고, 다른 유연 항목의 몫은 줄지 않는다.
            if (current.item.weight > 0.0f && length < current.item.minimum * scale)
                length = current.item.minimum * scale;
            if (current.child != nullptr)
            {
                // 교차축 길이를 정하지 않았으면 남는 폭을 다 채운다.
                const float cross_length { current.item.cross_length > 0.0f ? current.item.cross_length * scale : cross_total };
                float cross_offset { 0.0f };
                if (config_.cross_alignment == stack_alignment::center)
                    cross_offset = (cross_total - cross_length) / 2.0f;
                else if (config_.cross_alignment == stack_alignment::end)
                    cross_offset = cross_total - cross_length;

                const rect_f slot {
                    row ? left + offset : left + cross_offset,
                    row ? top + cross_offset : top + offset,
                    row ? length : cross_length,
                    row ? cross_length : length,
                };
                current.child->arrange(context.for_child(slot));
            }
            offset += length + spacing;
        }
    }

    void stack_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }
} // namespace luil
