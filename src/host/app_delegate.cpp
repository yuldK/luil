#include "luil/app/app_delegate.h"

#include "host/system_appearance_observer.h"

#include <cmath>
#include <mutex>

namespace luil {
    namespace {
        std::mutex appearance_mutex {};
        system_appearance latest_appearance {};
    }

    system_appearance current_system_appearance() noexcept
    {
        const std::lock_guard lock { appearance_mutex };
        return latest_appearance;
    }

    app_message system_appearance_observer::observe(system_appearance value, app_delegate* const delegate)
    {
        if (value.high_contrast == false)
            value.contrast_colors = {};
        if (last_ == value)
            return {};
        last_ = value;
        {
            const std::lock_guard lock { appearance_mutex };
            latest_appearance = value;
            set_system_accent(value.accent);
        }
        return delegate != nullptr ? delegate->make_system_appearance_message(value) : app_message {};
    }

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
