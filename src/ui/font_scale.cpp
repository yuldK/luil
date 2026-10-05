#include "luil/ui/font_scale.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <utility>

namespace luil {
    namespace {
        std::shared_ptr<const std::vector<float>> font_sizes {};
        std::mutex font_sizes_mutex {};
        std::atomic<bool> has_font_sizes { false };
    } // namespace

    float scaled_font_size(const float logical_size, const float display_scale) noexcept
    {
        if (has_font_sizes.load() == false)
            return logical_size * display_scale;
        std::shared_ptr<const std::vector<float>> sizes {};
        {
            const std::lock_guard lock { font_sizes_mutex };
            sizes = font_sizes;
        }
        if (sizes == nullptr || logical_size <= 0.0f || std::isfinite(logical_size) == false)
            return logical_size * display_scale;
        const float limit { static_cast<float>(sizes->size() - 1) };
        if (logical_size >= limit)
            return sizes->back() * (logical_size / limit) * display_scale;
        const auto index { static_cast<std::size_t>(logical_size) };
        const float fraction { logical_size - static_cast<float>(index) };
        return std::lerp((*sizes)[index], (*sizes)[index + 1], fraction) * display_scale;
    }

    float text_line_height(const float logical_size, const float padding) noexcept
    {
        return scaled_font_size(logical_size) + padding;
    }

    bool set_system_font_sizes(std::vector<float> sizes)
    {
        if (sizes.empty())
        {
            const std::lock_guard lock { font_sizes_mutex };
            font_sizes.reset();
            has_font_sizes.store(false);
            return true;
        }
        if (sizes.size() < 2 || sizes.front() != 0.0f)
            return false;
        for (std::size_t index { 1 }; index < sizes.size(); ++index)
            if (std::isfinite(sizes[index]) == false || sizes[index] <= sizes[index - 1])
                return false;
        const auto next { std::make_shared<const std::vector<float>>(std::move(sizes)) };
        const std::lock_guard lock { font_sizes_mutex };
        font_sizes = next;
        has_font_sizes.store(true);
        return true;
    }
} // namespace luil
