#include "win32/caption_layout.h"

#include <algorithm>

namespace luil::win32 {
    namespace {
        int scale_for_dpi(const int value, const std::uint32_t dpi) noexcept
        {
            return static_cast<int>((static_cast<std::uint64_t>(value) * dpi + 48U) / 96U);
        }
    } // namespace

    caption_layout make_caption_layout(const int client_width, const std::uint32_t dpi, const caption_ui_metrics& metrics, const caption_buttons& buttons) noexcept
    {
        caption_layout layout {};
        layout.height = scale_for_dpi(metrics.height, dpi);
        layout.button_width = scale_for_dpi(metrics.button_width, dpi);
        layout.right = std::max(0, client_width);

        // 있는 버튼만 오른쪽부터 한 칸씩 뗀다.
        // 없는 버튼은 그 시점의 끝에 붙여 두어 어떤 x로도 맞지 않게 한다.
        int edge { layout.right };
        const auto take = [&edge, width = layout.button_width](const bool present) {
            if (present)
                edge = std::max(0, edge - width);
            return edge;
        };
        layout.close_left = take(buttons.close);
        layout.maximize_left = take(buttons.maximize);
        layout.minimize_left = take(buttons.minimize);
        return layout;
    }

    caption_hit hit_test_caption(const caption_layout& layout, const int x, const int y) noexcept
    {
        if (x < 0 || x >= layout.right || y < 0 || y >= layout.height)
            return caption_hit::client;
        if (x >= layout.close_left)
            return caption_hit::close;
        if (x >= layout.maximize_left)
            return caption_hit::maximize;
        if (x >= layout.minimize_left)
            return caption_hit::minimize;
        return caption_hit::drag;
    }
} // namespace luil::win32
