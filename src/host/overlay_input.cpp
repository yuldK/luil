#include "host/overlay_input.h"

#include "luil/ui/layout_metrics.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/ui_tree.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

namespace luil {
    namespace {
        constexpr float wheel_units_per_notch { 120.0f };
        constexpr float wheel_rows { 3.0f };

        [[nodiscard]] float origin_y(const overlay_area* const area) noexcept
        {
            return area != nullptr ? area->bounds.y - area->scroll_y : 0.0f;
        }

        [[nodiscard]] bool tree_owns_touch(const ui_tree& tree, const point_f at)
        {
            const ui_element* const hit { tree.hit_test(at.x, at.y) };
            if (hit != nullptr && hit->enabled())
            {
                if (const ink_source* const ink { hit->ink() }; ink != nullptr && ink->touch && ink->on_ink)
                    return true;
                if (const pointer_drag_target* const handle { hit->pointer_drag() }; handle != nullptr && handle->start_distance <= 0.0f)
                    return true;
            }
            const ui_element* const zoom { zoom_owner_at(tree, at.x, at.y) };
            return zoom != nullptr && zoom->zoom()->pan_by && (hit == nullptr || (hit->enabled() && hit->pointer_drag() == nullptr && hit->drag() == nullptr));
        }
    }

    float overlay_scroll_to_show(const float offset, const float maximum, const float height, const rect_f& target) noexcept
    {
        return std::clamp(offset + scroll_delta_to_reveal(target.y, target.height, offset, height), 0.0f, maximum);
    }

    void overlay_input_router::set_areas(std::vector<overlay_area> areas)
    {
        areas_ = std::move(areas);
        // 캡처는 같은 표면에 남고, 그 표면이 움직이면 다음 접촉 좌표도 따라간다.
        for (capture& held : captures_)
        {
            if (const overlay_area* const area { area_for(held.surface) }; area != nullptr)
            {
                held.origin_x = area->bounds.x;
                held.origin_y = origin_y(area);
            }
            else if (held.pan.has_value())
            {
                // 한번 사라진 popup에는 같은 id를 다시 붙여도 앞의 끌기를 잇지 않는다.
                held.pan.reset();
                if (held.surface_scrolling)
                    held.suppressed = true;
            }
        }
    }

    void overlay_input_router::set_touch_config(const touch_gesture_config& config) noexcept
    {
        if (valid_touch_gesture_config(config) == false)
            return;
        if (touch_.pan_enabled && config.pan_enabled == false)
        {
            for (capture& held : captures_)
            {
                if (held.surface_scrolling)
                    held.suppressed = true;
                held.pan.reset();
            }
        }
        touch_ = config;
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

    const overlay_area* overlay_input_router::area_for(const std::u8string& surface) const noexcept
    {
        const auto found { std::ranges::find(areas_, surface, &overlay_area::id) };
        return found != areas_.end() ? &*found : nullptr;
    }

    overlay_input_router::capture* overlay_input_router::find(const pointer_device device, const std::uint32_t pointer_id) noexcept
    {
        const auto found { std::ranges::find_if(captures_, [&](const capture& value) { return value.device == device && value.pointer_id == pointer_id; }) };
        return found != captures_.end() ? &*found : nullptr;
    }

    bool overlay_input_router::take_touch_pan(capture& held, const point_f to, const std::chrono::steady_clock::time_point time, routed_input& result)
    {
        if (held.suppressed)
            return true;
        if (held.pan.has_value() == false)
            return false;
        const overlay_area* const area { area_for(held.surface) };
        pending_pan& pan { *held.pan };
        const float amount { pan.start.y - to.y };
        if (held.surface_scrolling == false)
        {
            if (area == nullptr || time > pan.deadline)
            {
                held.pan.reset();
                return false;
            }
            const float across { to.x - pan.start.x };
            if (std::hypot(across, amount) < pan.distance)
                return false;
            if (std::abs(across) > std::abs(amount) || area->max_scroll_y <= 0.0f
                || std::clamp(area->scroll_y + amount, 0.0f, area->max_scroll_y) == area->scroll_y)
            {
                held.pan.reset();
                return false;
            }

            const ui_tree* const tree { area->tree };
            const auto inside { tree != nullptr ? route_pan(*tree, pan.tree_start.x, pan.tree_start.y, scroll_axis::vertical) : std::nullopt };
            const bool inner_can_scroll { inside.has_value() && (amount > 0.0f ? inside->at_end == false : inside->at_start == false) };
            const ui_element* const handoff { tree != nullptr ? pan_handoff_owner_at(*tree, pan.tree_start.x, pan.tree_start.y, scroll_axis::vertical) : nullptr };
            const bool inner_handoff { handoff != nullptr && (inside.has_value() == false || (inside->at_start && amount < 0.0f && tree->within(handoff->id(), inside->id))) };
            if (inner_can_scroll || inner_handoff)
            {
                held.pan.reset();
                return false;
            }
            // 이미 전달한 누름을 거둔다. 이후 이동과 뗌은 표면이 맡아 메뉴 항목의 탭이 되지 않는다.
            held.surface_scrolling = true;
            for (capture& other : captures_)
                if (&other != &held && other.device == pointer_device::touch)
                    other.suppressed = true;
            result.events.push_back(pointer_cancelled_event { held.device, held.pointer_id, held.surface, time });
        }

        pan.start.y = to.y;
        if (area != nullptr)
        {
            const float offset { std::clamp(area->scroll_y + amount, 0.0f, area->max_scroll_y) };
            if (offset != area->scroll_y)
                result.scroll = overlay_scroll { held.surface, offset };
        }
        return true;
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
                capture target { pressed->device, pressed->pointer_id, area_surface(area), area != nullptr ? area->bounds.x : 0.0f, origin_y(area), 0 };
                if (pressed->device == pointer_device::touch && captures_.empty() == false)
                {
                    if (std::ranges::any_of(captures_, [](const capture& active) { return active.surface_scrolling || active.suppressed; }))
                    {
                        target.suppressed = true;
                        result.pressed_outside = false;
                    }
                }
                else if (pressed->device == pointer_device::touch && area != nullptr && area->max_scroll_y > 0.0f && touch_.pan_enabled)
                {
                    const point_f tree_start { pressed->x - target.origin_x, pressed->y - target.origin_y };
                    if (area->tree == nullptr || tree_owns_touch(*area->tree, tree_start) == false)
                    {
                        const float scale { pressed->scale > 0.0f ? pressed->scale : 1.0f };
                        const auto time_limit { touch_.long_press_enabled ? std::min(touch_.pan_start_time, touch_.long_press_time) : touch_.pan_start_time };
                        target.pan = pending_pan { { pressed->x, pressed->y }, tree_start, touch_.pan_start_distance * scale, pressed->time + time_limit };
                    }
                }
                captures_.push_back(std::move(target));
                held = &captures_.back();
            }
            ++held->buttons;
            if (held->suppressed)
                return result;
            hover(pressed->device, held->surface, result.events);
            place(*pressed, held->surface, held->origin_x, held->origin_y);
        }
        else if (auto* const released { std::get_if<pointer_released_event>(&event) }; released != nullptr)
        {
            if (capture* const held { find(released->device, released->pointer_id) }; held != nullptr)
            {
                const bool consumed { released->cancelled ? held->suppressed || held->surface_scrolling
                    : take_touch_pan(*held, { released->x, released->y }, released->time, result)
                };
                place(*released, held->surface, held->origin_x, held->origin_y);
                if (--held->buttons <= 0)
                    std::erase_if(captures_, [&](const capture& value) { return value.device == released->device && value.pointer_id == released->pointer_id; });
                if (consumed)
                    return result;
            }
            else
            {
                const overlay_area* const area { hit(released->x, released->y) };
                place(*released, area_surface(area), area != nullptr ? area->bounds.x : 0.0f, origin_y(area));
            }
        }
        else if (auto* const cancelled { std::get_if<pointer_cancelled_event>(&event) }; cancelled != nullptr)
        {
            if (const capture* const held { find(cancelled->device, cancelled->pointer_id) }; held != nullptr)
            {
                cancelled->surface = held->surface;
                const bool consumed { held->surface_scrolling || held->suppressed };
                std::erase_if(captures_, [&](const capture& value) { return value.device == cancelled->device && value.pointer_id == cancelled->pointer_id; });
                if (consumed)
                    return result;
            }
        }
        else if (auto* const moved { std::get_if<pointer_moved_event>(&event) }; moved != nullptr)
        {
            if (capture* const held { find(moved->device, moved->pointer_id) }; held != nullptr)
            {
                if (take_touch_pan(*held, { moved->x, moved->y }, moved->time, result))
                    return result;
                place(*moved, held->surface, held->origin_x, held->origin_y);
            }
            else
            {
                const overlay_area* const area { hit(moved->x, moved->y) };
                hover(moved->device, area_surface(area), result.events);
                place(*moved, area_surface(area), area != nullptr ? area->bounds.x : 0.0f, origin_y(area));
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
            place(*wheel, area_surface(area), area != nullptr ? area->bounds.x : 0.0f, origin_y(area));
            if (area != nullptr && area->max_scroll_y > 0.0f && wheel->horizontal == false && wheel->control == false && wheel->delta != 0.0f
                && (area->tree == nullptr || wheel_taken_inside(*area->tree, wheel->x, wheel->y, wheel->delta > 0.0f) == false))
            {
                const float offset { std::clamp(area->scroll_y - wheel->delta / wheel_units_per_notch * wheel_rows * menu_row_height() * area->scale, 0.0f, area->max_scroll_y) };
                if (offset != area->scroll_y)
                {
                    if (hovering_ && hover_surface_ == area->id)
                    {
                        result.events.push_back(pointer_left_event { hover_surface_, hover_device_ });
                        hovering_ = false;
                    }
                    result.scroll = overlay_scroll { area->id, offset };
                    return result;
                }
            }
        }
        result.events.push_back(std::move(event));
        return result;
    }
}
