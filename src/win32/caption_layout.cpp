#include "win32/caption_layout.h"

#include "luil/ui/ui_tree.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace luil::win32 {
    namespace {
        int scale_for_dpi(const int value, const std::uint32_t dpi) noexcept
        {
            return static_cast<int>((static_cast<std::uint64_t>(value) * dpi + 48U) / 96U);
        }

        // 그리기 순서의 첫 caption element다. `hidden`은 그것이나 조상이 숨어 있는가다.
        //  - 숨은 가지도 내려간다. 숨긴 caption과 caption이 없는 tree는 답이 다르다.
        [[nodiscard]] const ui_element* find_caption(const ui_element& node, const bool hidden_above, bool& hidden) noexcept
        {
            const bool node_hidden { hidden_above || node.visible() == false };
            if (node.id().kind == ui_element_kind::caption)
            {
                hidden = node_hidden;
                return &node;
            }
            for (const std::unique_ptr<ui_element>& child : node.children())
                if (const ui_element* const found { find_caption(*child, node_hidden, hidden) }; found != nullptr)
                    return found;
            return nullptr;
        }
    } // namespace

    caption_band default_caption_band(const int client_width, const std::uint32_t dpi, const caption_ui_metrics& metrics) noexcept
    {
        return caption_band { 0, 0, std::max(0, client_width), scale_for_dpi(metrics.height, dpi) };
    }

    caption_band caption_band_in(const ui_tree* const tree, const int client_width, const std::uint32_t dpi, const caption_ui_metrics& metrics) noexcept
    {
        const ui_element* const root { tree != nullptr ? tree->root() : nullptr };
        bool hidden { false };
        const ui_element* const caption { root != nullptr ? find_caption(*root, false, hidden) : nullptr };
        if (caption == nullptr)
            return default_caption_band(client_width, dpi, metrics);
        if (hidden)
            return {};

        // 가장자리를 각각 반올림한다. 폭을 따로 반올림하면 오른쪽 끝(창 버튼의 자리)이
        // 그려진 자리에서 1px 밀릴 수 있다.
        const rect_f& bounds { caption->bounds() };
        const int left { static_cast<int>(std::lround(bounds.x)) };
        const int top { static_cast<int>(std::lround(bounds.y)) };
        const int right { static_cast<int>(std::lround(bounds.x + bounds.width)) };
        const int bottom { static_cast<int>(std::lround(bounds.y + bounds.height)) };
        return caption_band { left, top, std::max(0, right - left), std::max(0, bottom - top) };
    }

    caption_layout make_caption_layout(const caption_band& band, const std::uint32_t dpi, const caption_ui_metrics& metrics, const caption_buttons& buttons) noexcept
    {
        caption_layout layout {};
        layout.left = band.left;
        layout.top = band.top;
        layout.height = std::max(0, band.height);
        layout.button_width = scale_for_dpi(metrics.button_width, dpi);
        layout.right = band.left + std::max(0, band.width);

        // 있는 버튼만 오른쪽부터 한 칸씩 뗀다.
        // 없는 버튼은 그 시점의 끝에 붙여 두어 어떤 x로도 맞지 않게 한다.
        int edge { layout.right };
        const auto take = [&edge, left = layout.left, width = layout.button_width](const bool present) {
            if (present)
                edge = std::max(left, edge - width);
            return edge;
        };
        layout.close_left = take(buttons.close);
        layout.maximize_left = take(buttons.maximize);
        layout.minimize_left = take(buttons.minimize);
        return layout;
    }

    caption_layout make_caption_layout(const int client_width, const std::uint32_t dpi, const caption_ui_metrics& metrics, const caption_buttons& buttons) noexcept
    {
        return make_caption_layout(default_caption_band(client_width, dpi, metrics), dpi, metrics, buttons);
    }

    caption_hit hit_test_caption(const caption_layout& layout, const int x, const int y) noexcept
    {
        if (x < layout.left || x >= layout.right || y < layout.top || y >= layout.top + layout.height)
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
