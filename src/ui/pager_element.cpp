#include "luil/ui/pager_element.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace luil {
    namespace {
        float bounded_offset(const float offset, const std::size_t count) noexcept
        {
            return std::clamp(std::isfinite(offset) ? offset : 0.0f, 0.0f, count == 0 ? 0.0f : static_cast<float>(count - 1));
        }
    }

    float settled_pager_offset(const float offset, const float velocity, const float page_width, const std::size_t count) noexcept
    {
        const float current { bounded_offset(offset, count) };
        const float projected { page_width > 0.0f && std::isfinite(velocity) ? std::clamp(velocity * 0.18f / page_width, -1.0f, 1.0f) : 0.0f };
        return bounded_offset(std::round(current + projected), count);
    }

    float apply_pager_command(const float offset, const std::size_t count, const pager_command& command) noexcept
    {
        const float current { bounded_offset(offset, count) };
        const float delta { std::isfinite(command.delta) ? command.delta : 0.0f };
        switch (command.operation)
        {
        case pager_operation::drag: return bounded_offset(current + delta, count);
        case pager_operation::step: return bounded_offset(std::round(current) + delta, count);
        case pager_operation::settle: return settled_pager_offset(current + delta, command.velocity, 1.0f, count);
        case pager_operation::select: return bounded_offset(static_cast<float>(command.page), count);
        }
        return current;
    }

    pager_element::pager_element(const ui_element_id id, pager_config config)
        : ui_element { id }
        , config_ { std::move(config) }
    {
        set_clip_children(true);
        set_access_name(config_.name);
        set_tab_stop(config_.change != nullptr);
    }

    void pager_element::add_page(std::unique_ptr<ui_element> page)
    {
        if (page != nullptr)
            add_child(std::move(page));
    }

    void pager_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        config_.offset = bounded_offset(config_.offset, children().size());
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float width { context.slot.width / scale };
        if (config_.change != nullptr && width > 0.0f && children().size() > 1)
        {
            const auto change = [this](const int steps) {
                return config_.change({ .operation = pager_operation::step, .delta = static_cast<float>(steps) });
            };
            scroll_source source {};
            source.axis = scroll_axis::horizontal;
            source.scale = scale;
            source.at_start = config_.offset <= 0.0f;
            source.at_end = config_.offset >= static_cast<float>(children().size() - 1);
            source.scroll = [this, width](const float delta) { return config_.change({ .delta = delta / width }); };
            source.step = change;
            source.settle = [this, width](const float delta, const float velocity) {
                return config_.change({ .operation = pager_operation::settle, .delta = delta / width, .velocity = velocity / width });
            };
            set_scroll_source(std::move(source));
            key_step_target keys {};
            keys.axis = focus_axis::horizontal;
            keys.on_step = [this, change](const value_step step) -> std::optional<std::vector<input_action>> {
                if (step == value_step::minimum)
                    return std::vector<input_action> { config_.change({ .operation = pager_operation::select, .page = 0 }) };
                if (step == value_step::maximum)
                    return std::vector<input_action> { config_.change({ .operation = pager_operation::select, .page = children().size() - 1 }) };
                return std::vector<input_action> { change(step == value_step::decrease || step == value_step::decrease_page ? -1 : 1) };
            };
            set_key_step_target(std::move(keys));
        }
        else
        {
            set_scroll_source(std::nullopt);
            set_key_step_target(std::nullopt);
        }
        for (std::size_t index { 0 }; index < children().size(); ++index)
        {
            const rect_f slot { context.slot.x + (static_cast<float>(index) - config_.offset) * context.slot.width, context.slot.y, context.slot.width, context.slot.height };
            children()[index]->arrange(context.for_child(slot));
        }
    }

    void pager_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }

    access_info pager_element::accessibility() const
    {
        return { .role = access_role::pane, .name = config_.name };
    }
}
