#include "raster_probe.h"

#include "include/core/SkCanvas.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace luil::testing {
    namespace {
        // 실수 자리를 픽셀 칸으로 접는다.
        // 반올림이 아니라 **덮는 칸 전부**다 — 경계에 걸친 획을 세는 자리라
        // 잘라 내면 있는 색을 없다고 답한다.
        [[nodiscard]] int floor_of(const float value) noexcept
        {
            return static_cast<int>(std::floor(value));
        }

        [[nodiscard]] int ceil_of(const float value) noexcept
        {
            return static_cast<int>(std::ceil(value));
        }
    } // namespace

    raster_frame::raster_frame(const int width, const int height, ui_color_palette palette, const float scale)
        : palette_ { std::move(palette) }
        , scale_ { scale > 0.0f ? scale : 1.0f }
    {
        // 색 공간을 주지 않는다. 주면 그린 색과 읽은 색이 변환을 한 번 지나
        // 팔레트 값과 정확히 같지 않게 된다 — 이 축은 바이트가 같은지를 묻는다.
        pixels_.allocN32Pixels(std::max(1, width), std::max(1, height));
    }

    void raster_frame::draw(const ui_tree& tree, const interaction_snapshot& interaction, const std::chrono::steady_clock::time_point now)
    {
        SkCanvas canvas { pixels_ };
        canvas.clear(palette_.window_background);
        draw_context context {
            .canvas = canvas,
            .palette = palette_,
            .scale = scale_,
            .now = now,
        };
        tree.draw(context, interaction);
    }

    const ui_color_palette& raster_frame::palette() const noexcept
    {
        return palette_;
    }

    ui_color raster_frame::pixel_at(const int x, const int y) const
    {
        if (x < 0 || y < 0 || x >= pixels_.width() || y >= pixels_.height())
            return 0;
        return static_cast<ui_color>(pixels_.getColor(x, y));
    }

    int raster_frame::count_color(const rect_f& area, const ui_color color) const
    {
        const int left { std::max(0, floor_of(area.x)) };
        const int top { std::max(0, floor_of(area.y)) };
        const int right { std::min(pixels_.width(), ceil_of(area.x + area.width)) };
        const int bottom { std::min(pixels_.height(), ceil_of(area.y + area.height)) };
        int found { 0 };
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x)
                if (static_cast<ui_color>(pixels_.getColor(x, y)) == color)
                    ++found;
        return found;
    }

    bool raster_frame::contains_color(const rect_f& area, const ui_color color) const
    {
        return count_color(area, color) > 0;
    }
} // namespace luil::testing
