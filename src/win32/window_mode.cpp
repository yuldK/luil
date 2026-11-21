#include "win32/window_mode.h"

#include <algorithm>

namespace luil::win32 {
    namespace {
        // 논리 픽셀을 이 배율의 물리 픽셀로 옮긴다.
        // 반올림까지 `caption_layout.cpp`와 같은 식이어야 한다 — 시스템 메뉴 칸과 버튼 폭을
        // 두 곳에서 재는데 1px이 갈리면 판정 경계에서 답이 어긋난다.
        [[nodiscard]] int scale_for_dpi(const int value, const std::uint32_t dpi) noexcept
        {
            return static_cast<int>((static_cast<std::uint64_t>(value) * dpi + 48U) / 96U);
        }
    }

    window_hit hit_test_window(const window_frame_metrics& metrics, const window_display_mode mode, const int width, const int height, const std::uint32_t dpi, const int x,
        const int y, const caption_band& band) noexcept
    {
        // 화면을 덮은 창에는 비클라이언트가 없다.
        // 가장자리도 캡션 띠도 시스템 메뉴도 없이 통째로 client다.
        if (mode == window_display_mode::fullscreen)
            return window_hit::client;

        if (mode == window_display_mode::normal)
        {
            // 시스템 기본 테두리(보통 8px)는 가장자리에
            // 붙은 스크롤 막대를 잡기 어렵게 한다.
            // 조절 두께를 좁히고 모서리만 넉넉히 둔다.
            const int border { std::max(1, scale_for_dpi(metrics.resize_border_thickness, dpi)) };
            const int corner { std::max(border, scale_for_dpi(metrics.resize_corner_thickness, dpi)) };
            const bool corner_left { x < corner };
            const bool corner_right { x >= width - corner };
            const bool corner_top { y < corner };
            const bool corner_bottom { y >= height - corner };
            if (corner_top && corner_left)
                return window_hit::resize_top_left;
            if (corner_top && corner_right)
                return window_hit::resize_top_right;
            if (corner_bottom && corner_left)
                return window_hit::resize_bottom_left;
            if (corner_bottom && corner_right)
                return window_hit::resize_bottom_right;
            if (x < border)
                return window_hit::resize_left;
            if (x >= width - border)
                return window_hit::resize_right;
            if (y < border)
                return window_hit::resize_top;
            if (y >= height - border)
                return window_hit::resize_bottom;
        }

        const caption_layout layout { make_caption_layout(band, dpi, metrics.caption, metrics.buttons) };
        const caption_hit hit { hit_test_caption(layout, x, y) };
        // 시스템 메뉴 아이콘 칸은 캡션 띠의 왼쪽 끝이고 버튼보다 먼저다.
        if (hit != caption_hit::client && x < layout.left + scale_for_dpi(metrics.caption.application_icon_slot_width, dpi))
            return window_hit::system_menu;

        switch (hit)
        {
        case caption_hit::drag:
            return window_hit::caption_drag;
        case caption_hit::minimize:
            return window_hit::minimize_button;
        case caption_hit::maximize:
            return window_hit::maximize_button;
        case caption_hit::close:
            return window_hit::close_button;
        case caption_hit::client:
            break;
        }
        return window_hit::client;
    }

    window_hit hit_test_window(
        const window_frame_metrics& metrics, const window_display_mode mode, const int width, const int height, const std::uint32_t dpi, const int x, const int y) noexcept
    {
        return hit_test_window(metrics, mode, width, height, dpi, x, y, default_caption_band(width, dpi, metrics.caption));
    }

    screen_area fullscreen_restore_area(
        const screen_area& normal, const screen_area& visible, const screen_area& monitor, const screen_area& work, const bool maximized) noexcept
    {
        if (maximized)
            return normal;
        const workspace_offset offset { workspace_offset_for(monitor, work) };
        return screen_area { visible.left - offset.x, visible.top - offset.y, visible.right - offset.x, visible.bottom - offset.y };
    }
}
