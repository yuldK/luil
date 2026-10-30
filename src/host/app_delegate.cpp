#include "luil/app/app_delegate.h"

#include <cmath>

namespace luil {
    window_metrics make_window_metrics(const float width, const float height, const float scale) noexcept
    {
        const float logical_scale { std::isfinite(scale) && scale > 0.0f ? scale : 1.0f };
        return window_metrics { width, height, logical_scale, classify_window_width(width / logical_scale) };
    }

    rect_f window_surface(const window_metrics& metrics) noexcept
    {
        const edge_insets& outside { metrics.outside };
        return { -outside.left, -outside.top, metrics.width + outside.left + outside.right, metrics.height + outside.top + outside.bottom };
    }
}
