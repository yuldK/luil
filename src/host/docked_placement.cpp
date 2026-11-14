#include "host/docked_placement.h"

#include <algorithm>
#include <cmath>

namespace luil {
    namespace {
        [[nodiscard]] int physical(const float logical, const float scale) noexcept
        {
            return static_cast<int>(std::lround(logical * scale));
        }

        [[nodiscard]] const display_info* find_display(const std::u8string& monitor, const std::span<const display_info> displays) noexcept
        {
            for (const display_info& display : displays)
                if (monitor.empty() ? display.primary : display.id == monitor)
                    return &display;
            // 주 모니터 표식이 없는 목록(test·OS가 답하지 않음)에서는 첫 모니터가 주 모니터다.
            if (monitor.empty() && displays.empty() == false)
                return &displays.front();
            return nullptr;
        }

        [[nodiscard]] std::optional<screen_rect> resolve_edge(const docked_edge_placement& placement, const std::span<const display_info> displays) noexcept
        {
            const display_info* const display { find_display(placement.monitor, displays) };
            if (display == nullptr)
                return std::nullopt;
            const screen_rect area { placement.work_area ? display->work_area : display->bounds };
            if (area.empty())
                return std::nullopt;
            const float scale { display->scale > 0.0f ? display->scale : 1.0f };
            const int thickness { physical(placement.thickness, scale) };
            if (thickness <= 0)
                return std::nullopt;
            const int inset { physical(placement.inset, scale) };
            const int offset { physical(placement.offset, scale) };
            const bool horizontal { placement.edge == screen_edge::top || placement.edge == screen_edge::bottom };
            const int span_start { horizontal ? area.left : area.top };
            const int span_end { horizontal ? area.right : area.bottom };
            const int span { span_end - span_start };
            const int length { placement.length > 0.0f ? std::min(physical(placement.length, scale), span) : span };
            if (length <= 0)
                return std::nullopt;

            int along { span_start + offset };
            if (placement.alignment == edge_alignment::end)
                along = span_end - offset - length;
            else if (placement.alignment == edge_alignment::center)
                along = span_start + (span - length) / 2 + offset;

            screen_rect bounds {};
            switch (placement.edge)
            {
            case screen_edge::top:
                bounds = { along, area.top + inset, along + length, area.top + inset + thickness };
                break;
            case screen_edge::bottom:
                bounds = { along, area.bottom - inset - thickness, along + length, area.bottom - inset };
                break;
            case screen_edge::left:
                bounds = { area.left + inset, along, area.left + inset + thickness, along + length };
                break;
            case screen_edge::right:
                bounds = { area.right - inset - thickness, along, area.right - inset, along + length };
                break;
            }
            return bounds;
        }
    }

    std::optional<screen_rect> resolve_docked_bounds(const std::variant<screen_rect, docked_edge_placement>& placement, const std::span<const display_info> displays) noexcept
    {
        if (const screen_rect* const rectangle { std::get_if<screen_rect>(&placement) }; rectangle != nullptr)
        {
            if (rectangle->empty())
                return std::nullopt;
            return *rectangle;
        }
        return resolve_edge(std::get<docked_edge_placement>(placement), displays);
    }
}
