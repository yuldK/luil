#include "macos/window_rules.h"

#include "luil/ui/caption_element.h"
#include "luil/ui/ui_tree.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace luil::macos {
    namespace {
        // `win32::placement_in_screen_range`와 같은 규칙이다 (그 몸은 플랫폼 층에 있어 core test가 링크하지 않는다).
        [[nodiscard]] bool in_screen_range(const window_placement& placement) noexcept
        {
            constexpr long long lowest { std::numeric_limits<std::int32_t>::min() };
            constexpr long long highest { std::numeric_limits<std::int32_t>::max() };
            const long long right { static_cast<long long>(placement.x) + placement.width };
            const long long bottom { static_cast<long long>(placement.y) + placement.height };
            return right >= lowest && right <= highest && bottom >= lowest && bottom <= highest;
        }
    }

    std::optional<window_placement> accepted_initial_placement(
        const window_placement& placement, const int minimum_width, const int minimum_height, const std::span<const display_info> displays) noexcept
    {
        if (placement.valid() == false || in_screen_range(placement) == false)
            return std::nullopt;
        if (placement.width < minimum_width || placement.height < minimum_height)
            return std::nullopt;
        const screen_rect bounds { placement.x, placement.y, placement.x + placement.width, placement.y + placement.height };
        const auto overlaps = [&bounds](const display_info& display) {
            const screen_rect& screen { display.bounds };
            return bounds.left < screen.right && screen.left < bounds.right && bounds.top < screen.bottom && screen.top < bounds.bottom;
        };
        if (std::ranges::none_of(displays, overlaps))
            return std::nullopt;
        return placement;
    }

    caption_hit custom_caption_hit(const ui_tree* const tree, const float x, const float y, const int client_width, const float scale) noexcept
    {
        if (tree == nullptr)
            return caption_hit::client;
        const auto* const caption { dynamic_cast<const caption_element*>(tree->find(ui_element_id { ui_element_kind::caption })) };
        if (caption == nullptr)
            return caption_hit::client;
        const caption_config& config { caption->config() };
        const std::uint32_t dpi { static_cast<std::uint32_t>(std::lround(std::max(scale, 0.01f) * 96.0f)) };
        const caption_band band { caption_band_in(tree, client_width, dpi, config.metrics) };
        const caption_layout layout { make_caption_layout(band, dpi, config.metrics, config.buttons) };
        const int pixel_x { static_cast<int>(std::floor(x)) };
        const int pixel_y { static_cast<int>(std::floor(y)) };
        const caption_hit hit { hit_test_caption(layout, pixel_x, pixel_y) };
        if (hit == caption_hit::drag && tree->caption_client_at(x, y))
            return caption_hit::client;
        return hit;
    }

    title_double_click parse_title_double_click(const std::u8string_view value) noexcept
    {
        if (value == u8"Minimize")
            return title_double_click::minimize;
        if (value == u8"None")
            return title_double_click::none;
        return title_double_click::zoom;
    }

    screen_rect dragged_window_bounds(const screen_rect& start, const screen_point pressed, const screen_point pointer) noexcept
    {
        const int dx { pointer.x - pressed.x };
        const int dy { pointer.y - pressed.y };
        return { start.left + dx, start.top + dy, start.right + dx, start.bottom + dy };
    }

    screen_rect moving_bounds_kept_size(const screen_rect& proposed, const screen_rect& adjusted) noexcept
    {
        return { adjusted.left, adjusted.top, adjusted.left + proposed.width(), adjusted.top + proposed.height() };
    }

    popup_screen_frame place_popup_on_screen(
        const screen_point origin, const int offset_x, const int offset_y, const int width, const int height, const screen_area& work, const bool anchor_is_popup) noexcept
    {
        int x { origin.x + offset_x };
        const int y { origin.y + offset_y };
        const int visible { std::min(height, std::max(1, work.bottom - work.top)) };
        // 하위 메뉴가 오른쪽 끝을 넘으면 앵커 popup의 왼쪽에 선다.
        if (anchor_is_popup && x + width > work.right)
            x = origin.x - width;
        const auto [clamped_x, clamped_y] { clamp_popup_position(x, y, width, visible, work) };
        return { clamped_x, clamped_y, width, visible };
    }
}
