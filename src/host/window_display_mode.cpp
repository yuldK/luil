#include "host/window_display_mode.h"

#include <algorithm>

namespace luil {
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
}
