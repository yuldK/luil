#include "host/overlay_input.h"

#include <algorithm>
#include <utility>
#include <variant>

namespace luil {
    void overlay_input_router::set_areas(std::vector<overlay_area> areas)
    {
        areas_ = std::move(areas);
    }

    bool overlay_input_router::has_areas() const noexcept
    {
        return areas_.empty() == false;
    }

    const overlay_area* overlay_input_router::hit(const float x, const float y) const noexcept
    {
        for (auto area { areas_.rbegin() }; area != areas_.rend(); ++area)
            if (area->bounds.contains(x, y))
                return &*area;
        return nullptr;
    }

    overlay_input_router::capture* overlay_input_router::find(const pointer_device device, const std::uint32_t pointer_id) noexcept
    {
        const auto found { std::ranges::find_if(captures_, [&](const capture& value) { return value.device == device && value.pointer_id == pointer_id; }) };
        return found != captures_.end() ? &*found : nullptr;
    }

    void overlay_input_router::hover(const pointer_device device, const std::u8string& surface, std::vector<raw_input_event>& events)
    {
        // 터치에는 호버가 없다.
        if (device == pointer_device::touch)
            return;
        if (hovering_ && (hover_surface_ != surface || hover_device_ != device))
            events.push_back(raw_input_event { pointer_left_event { hover_surface_, hover_device_ } });
        hovering_ = true;
        hover_device_ = device;
        hover_surface_ = surface;
    }

    routed_input overlay_input_router::route(raw_input_event event)
    {
        routed_input result {};
        const auto place = [](auto& value, const std::u8string& surface, const float origin_x, const float origin_y) {
            value.surface = surface;
            value.x -= origin_x;
            value.y -= origin_y;
        };
        const auto area_surface = [](const overlay_area* const area) { return area != nullptr ? area->id : std::u8string {}; };

        if (auto* const pressed { std::get_if<pointer_pressed_event>(&event) }; pressed != nullptr)
        {
            capture* held { find(pressed->device, pressed->pointer_id) };
            if (held == nullptr)
            {
                const overlay_area* const area { hit(pressed->x, pressed->y) };
                result.pressed_outside = area == nullptr && areas_.empty() == false;
                captures_.push_back(capture { pressed->device, pressed->pointer_id, area_surface(area), area != nullptr ? area->bounds.x : 0.0f, area != nullptr ? area->bounds.y : 0.0f, 0 });
                held = &captures_.back();
            }
            ++held->buttons;
            const capture target { *held };
            hover(pressed->device, target.surface, result.events);
            place(*pressed, target.surface, target.origin_x, target.origin_y);
        }
        else if (auto* const released { std::get_if<pointer_released_event>(&event) }; released != nullptr)
        {
            if (capture* const held { find(released->device, released->pointer_id) }; held != nullptr)
            {
                const capture target { *held };
                if (--held->buttons <= 0)
                    std::erase_if(captures_, [&](const capture& value) { return value.device == target.device && value.pointer_id == target.pointer_id; });
                place(*released, target.surface, target.origin_x, target.origin_y);
            }
            else
            {
                const overlay_area* const area { hit(released->x, released->y) };
                place(*released, area_surface(area), area != nullptr ? area->bounds.x : 0.0f, area != nullptr ? area->bounds.y : 0.0f);
            }
        }
        else if (auto* const cancelled { std::get_if<pointer_cancelled_event>(&event) }; cancelled != nullptr)
        {
            if (const capture* const held { find(cancelled->device, cancelled->pointer_id) }; held != nullptr)
            {
                cancelled->surface = held->surface;
                const capture target { *held };
                std::erase_if(captures_, [&](const capture& value) { return value.device == target.device && value.pointer_id == target.pointer_id; });
            }
        }
        else if (auto* const moved { std::get_if<pointer_moved_event>(&event) }; moved != nullptr)
        {
            if (const capture* const held { find(moved->device, moved->pointer_id) }; held != nullptr)
            {
                const capture target { *held };
                place(*moved, target.surface, target.origin_x, target.origin_y);
            }
            else
            {
                const overlay_area* const area { hit(moved->x, moved->y) };
                hover(moved->device, area_surface(area), result.events);
                place(*moved, area_surface(area), area != nullptr ? area->bounds.x : 0.0f, area != nullptr ? area->bounds.y : 0.0f);
            }
        }
        else if (auto* const left { std::get_if<pointer_left_event>(&event) }; left != nullptr)
        {
            // 창을 떠났다. 지금 호버하던 표면의 이탈이다.
            if (hovering_ == false || hover_device_ != left->device)
                return result;
            left->surface = hover_surface_;
            hovering_ = false;
            hover_surface_.clear();
        }
        else if (auto* const wheel { std::get_if<mouse_wheel_event>(&event) }; wheel != nullptr)
        {
            const overlay_area* const area { hit(wheel->x, wheel->y) };
            result.wheel_outside = area == nullptr && areas_.empty() == false;
            place(*wheel, area_surface(area), area != nullptr ? area->bounds.x : 0.0f, area != nullptr ? area->bounds.y : 0.0f);
        }
        result.events.push_back(std::move(event));
        return result;
    }
} // namespace luil
