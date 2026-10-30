#include "luil/ui/layer_element.h"

#include <algorithm>
#include <utility>

namespace luil {
    layer_element::layer_element(ui_element_id id, const edge_insets padding)
        : ui_element { std::move(id) }
        , padding_ { padding }
    {
    }

    void layer_element::add(std::unique_ptr<ui_element> child, const layer_item item)
    {
        if (child == nullptr)
            return;
        entries_.push_back({ child.get(), item });
        add_child(std::move(child));
    }

    void layer_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const measure_context measure { context.measure() };
        for (const entry& current : entries_)
        {
            const layer_item& item { current.item };
            // 이 자식의 자리다 (논리 픽셀, 이 컨테이너의 원점 기준).
            const float left { padding_.left + item.margin.left };
            const float top { padding_.top + item.margin.top };
            const float available_width { std::max(0.0f, context.slot.width / scale - left - padding_.right - item.margin.right) };
            const float available_height { std::max(0.0f, context.slot.height / scale - top - padding_.bottom - item.margin.bottom) };
            float width { item.width > 0.0f ? item.width : available_width };
            float height { item.height > 0.0f ? item.height : available_height };
            const bool fit_width { item.width <= 0.0f && item.horizontal != layout_alignment::stretch };
            const bool fit_height { item.height <= 0.0f && item.vertical != layout_alignment::stretch };
            if (fit_width || fit_height)
            {
                if (const std::optional<size_f> size { current.child->preferred_size(measure, fit_width ? available_width : width) }; size.has_value())
                {
                    if (fit_width)
                        width = std::min(available_width, size->width);
                    if (fit_height)
                        height = std::min(available_height, size->height);
                }
            }
            const float x { align_within(left, available_width, width, item.horizontal) };
            const float y { align_within(top, available_height, height, item.vertical) };
            current.child->arrange(context.for_child({ context.slot.x + x * scale, context.slot.y + y * scale, width * scale, height * scale }));
        }
    }

    void layer_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }

    std::optional<size_f> layer_element::preferred_size(const measure_context& context, const float available_width) const
    {
        const float inner_width { available_width - padding_.left - padding_.right };
        float width { 0.0f };
        float height { 0.0f };
        bool answered { false };
        for (const entry& current : entries_)
        {
            const layer_item& item { current.item };
            const float margin_width { item.margin.left + item.margin.right };
            const float margin_height { item.margin.top + item.margin.bottom };
            const std::optional<size_f> size { current.child->preferred_size(context, item.width > 0.0f ? item.width : std::max(0.0f, inner_width - margin_width)) };
            if (size.has_value() == false && item.width <= 0.0f && item.height <= 0.0f)
                continue;
            answered = true;
            width = std::max(width, (item.width > 0.0f ? item.width : (size.has_value() ? size->width : 0.0f)) + margin_width);
            height = std::max(height, (item.height > 0.0f ? item.height : (size.has_value() ? size->height : 0.0f)) + margin_height);
        }
        if (answered == false)
            return std::nullopt;
        return size_f { width + padding_.left + padding_.right, height + padding_.top + padding_.bottom };
    }
}
