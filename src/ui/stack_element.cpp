#include "luil/ui/stack_element.h"

#include "ui/flexible_lengths.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        // 맞춤 항목이 원하는 크기다 (논리 픽셀). 답하지 않는 자식은 0이다.
        [[nodiscard]] size_f fitted_size(const ui_element& child, const measure_context& context, const float available_width)
        {
            const std::optional<size_f> size { child.preferred_size(context, available_width) };
            return size.has_value() ? *size : size_f {};
        }
    }

    stack_element::stack_element(const ui_element_id id, stack_config config)
        : ui_element { id }
        , config_ { std::move(config) }
    {
    }

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

    void stack_element::add_fit(std::unique_ptr<ui_element> child)
    {
        add(std::move(child), stack_item { .fit = true });
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

        // 맞춤 항목에게 원하는 크기를 한 번씩 묻는다. 맞춤 항목이 없으면 아무에게도 묻지 않는다.
        //  - 가로 stack의 폭은 제한 없이, 세로 stack의 높이는 교차 폭에서 묻는다 (여러 줄 글이 그 폭에서 접힌다).
        std::vector<size_f> fitted(entries_.size());
        const measure_context measure { context.measure() };
        const float fit_width { row ? std::numeric_limits<float>::infinity() : std::max(0.0f, cross_total / scale) };
        for (std::size_t index = 0; index < entries_.size(); ++index)
            if (entries_[index].item.fit && entries_[index].child != nullptr)
                fitted[index] = fitted_size(*entries_[index].child, measure, fit_width);
        const auto flexible = [this](const std::size_t index) { return entries_[index].item.fit == false && entries_[index].item.weight > 0.0f; };

        // 고정 길이와 간격을 뺀 나머지를 비율대로 나눈다.
        const float spacing { config_.spacing * scale };
        float fixed { spacing * static_cast<float>(entries_.size() - 1) };
        float weights { 0.0f };
        for (std::size_t index = 0; index < entries_.size(); ++index)
            if (flexible(index))
                weights += entries_[index].item.weight;
            else if (entries_[index].item.fit)
                fixed += (row ? fitted[index].width : fitted[index].height) * scale;
            else
                fixed += entries_[index].item.length * scale;
        float remaining { main_total - fixed };
        if (remaining < 0.0f)
            remaining = 0.0f;

        // 유연 항목은 하한·상한을 지키며 남은 자리를 비율대로 나눈다 (`distribute_flexible`).
        //  - 하한에 걸린 항목의 부족분은 다른 유연 항목의 몫에서 나온다. 하한들의 합이 남은 자리를 넘을 때만 넘친다.
        //  - 상한에 걸린 항목이 남긴 자리는 걸리지 않은 항목이 나눈다.
        //  - 제한이 없으면 몫은 지금까지의 식(`remaining × weight / weights`) 그대로다.
        std::vector<float> lengths(entries_.size(), 0.0f);
        std::vector<flexible_share> shares {};
        std::vector<std::size_t> owners {};
        for (std::size_t index = 0; index < entries_.size(); ++index)
        {
            if (flexible(index) == false)
                continue;
            const stack_item& item { entries_[index].item };
            shares.push_back({ .weight = item.weight, .minimum = item.minimum * scale, .maximum = item.maximum * scale });
            owners.push_back(index);
        }
        const std::vector<float> resolved { distribute_flexible(shares, remaining) };
        float flexible_total { 0.0f };
        for (std::size_t share = 0; share < owners.size(); ++share)
        {
            lengths[owners[share]] = resolved[share];
            flexible_total += resolved[share];
        }

        // 남는 자리는 유연 항목이 다 가져가므로, 유연 항목이 하나도 없거나 모두 상한에 걸렸을 때만
        // 정렬이 뜻을 갖는다.
        //  - 음수로 내려가지 않는다. 하한들이 넘칠 때 가운데·끝 정렬이 여백 밖으로 새는 경계가 아예 없다.
        float free { weights > 0.0f ? std::max(0.0f, remaining - flexible_total) : remaining };
        float offset { 0.0f };
        if (config_.main_alignment == stack_main_alignment::center)
            offset = free / 2.0f;
        else if (config_.main_alignment == stack_main_alignment::end)
            offset = free;

        for (std::size_t index = 0; index < entries_.size(); ++index)
        {
            const entry& current { entries_[index] };
            float length { lengths[index] };
            if (current.item.fit)
                length = (row ? fitted[index].width : fitted[index].height) * scale;
            else if (flexible(index) == false)
                length = current.item.length * scale;
            if (current.child != nullptr)
            {
                // 교차축 길이를 정하지 않았으면 남는 폭을 다 채운다.
                //  - 맞춤 항목은 정렬이 `stretch`가 아니면 원하는 교차 길이에 선다.
                float cross_length { current.item.cross_length > 0.0f ? current.item.cross_length * scale : cross_total };
                if (current.item.fit && current.item.cross_length <= 0.0f && config_.cross_alignment != stack_alignment::stretch)
                    cross_length = (row ? fitted[index].height : fitted[index].width) * scale;
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

    std::optional<size_f> stack_element::preferred_size(const measure_context& context, const float available_width) const
    {
        const bool row { config_.direction == stack_direction::row };
        const float padding_width { config_.padding.left + config_.padding.right };
        const float padding_height { config_.padding.top + config_.padding.bottom };
        // 세로 stack의 자식은 여백을 뺀 폭에서 높이를 정한다. 가로 stack의 자식은 폭에 제한이 없다.
        const float child_width { row ? std::numeric_limits<float>::infinity() : std::max(0.0f, available_width - padding_width) };
        float main { entries_.empty() ? 0.0f : config_.spacing * static_cast<float>(entries_.size() - 1) };
        float cross { 0.0f };
        bool answered { false };
        for (const entry& current : entries_)
        {
            std::optional<size_f> size {};
            if (current.child != nullptr)
                size = current.child->preferred_size(context, child_width);
            if (current.item.fit)
            {
                answered = true;
                if (size.has_value())
                    main += row ? size->width : size->height;
            }
            else if (current.item.weight > 0.0f)
                main += current.item.minimum;
            else
                main += current.item.length;
            if (size.has_value())
                answered = true;
            const float wanted_cross { current.item.cross_length > 0.0f ? current.item.cross_length : (size.has_value() ? (row ? size->height : size->width) : 0.0f) };
            cross = std::max(cross, wanted_cross);
        }
        if (answered == false)
            return std::nullopt;
        return row ? size_f { main + padding_width, cross + padding_height } : size_f { cross + padding_width, main + padding_height };
    }
}
