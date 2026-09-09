#include "win32/window_mode.h"

#include <algorithm>

namespace luil::win32 {
    namespace {
        // 논리 픽셀을 이 배율의 물리 픽셀로 옮긴다.
        // 반올림까지 `caption_layout.cpp`와 같은 식이어야 한다 — 캡션 높이를 두 곳에서
        // 재는데 1px이 갈리면 판정 경계에서 답이 어긋난다.
        [[nodiscard]] int scale_for_dpi(const int value, const std::uint32_t dpi) noexcept
        {
            return static_cast<int>((static_cast<std::uint64_t>(value) * dpi + 48U) / 96U);
        }
    } // namespace

    window_hit hit_test_window(
        const window_frame_metrics& metrics, const window_display_mode mode, const int width, const int height, const std::uint32_t dpi, const int x, const int y) noexcept
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

        if (y >= 0 && y < scale_for_dpi(metrics.caption.height, dpi) && x >= 0 && x < scale_for_dpi(metrics.caption.application_icon_slot_width, dpi))
            return window_hit::system_menu;

        const caption_layout layout { make_caption_layout(width, dpi, metrics.caption, metrics.buttons) };
        switch (hit_test_caption(layout, x, y))
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

    window_bounds fullscreen_bounds_for(const screen_area& monitor) noexcept
    {
        return window_bounds {
            monitor.left,
            monitor.top,
            std::max(0, monitor.right - monitor.left),
            std::max(0, monitor.bottom - monitor.top),
        };
    }

    window_placement placement_to_report(const window_placement& observed, const std::optional<window_placement>& restore) noexcept
    {
        // 전체 화면이 아니면 OS가 답한 그대로다.
        // 표식만은 관측값을 믿지 않고 여기서 세운다 — OS는 전체 화면을 알지 못하므로
        // `observed`에 실려 온 참은 어디선가 흘러든 값이다.
        if (restore.has_value() == false)
        {
            window_placement value { observed };
            value.fullscreen = false;
            return value;
        }
        // 전체 화면이면 **돌아갈 자리**를 알린다.
        // 관측값의 자리·크기·최대화는 전체 화면에 들어서는 순간 전부 덮였다.
        window_placement value { *restore };
        value.fullscreen = true;
        return value;
    }
} // namespace luil::win32
