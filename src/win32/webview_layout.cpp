#include "win32/webview_layout.h"

#include <algorithm>
#include <cmath>

namespace luil::win32 {
    namespace {
        // 왼쪽·위뿐 아니라 오른쪽·아래도 각각 반올림한다.
        // 반올림한 위치에 폭을 더하지 않아 이동에 따른 1픽셀 크기 흔들림을 막는다.
        // dpi_scale.h와 같은 반올림 규칙을 적용한다.
        [[nodiscard]] int rounded(const float value) noexcept
        {
            return static_cast<int>(std::lround(static_cast<double>(value)));
        }
    } // namespace

    webview_layout plan_webview_layout(const std::optional<rect_f>& visible, const int client_width, const int client_height, const bool occluded, const float scale) noexcept
    {
        if (visible.has_value() == false)
            return {};

        const int left { std::clamp(rounded(visible->x), 0, std::max(0, client_width)) };
        const int top { std::clamp(rounded(visible->y), 0, std::max(0, client_height)) };
        const int right { std::clamp(rounded(visible->x + visible->width), 0, std::max(0, client_width)) };
        const int bottom { std::clamp(rounded(visible->y + visible->height), 0, std::max(0, client_height)) };
        if (right <= left || bottom <= top)
            return {};

        return webview_layout {
            .x = left,
            .y = top,
            .width = right - left,
            .height = bottom - top,
            // 가려졌으면 자리는 그대로 두고 구멍만 뚫지 않는다 (헤더의 이유).
            .punch_hole = occluded == false,
            // 자리와 같은 tree의 배율이다 (헤더의 이유).
            .scale = scale > 0.0f ? scale : 1.0f,
        };
    }

    webview_pointer translate_webview_pointer(const webview_layout& layout, const int client_x, const int client_y, const bool captured) noexcept
    {
        if (layout.visible() == false || layout.punch_hole == false)
            return {};
        // 반열림이다 — 오른쪽·아래 가장자리는 바깥이다. 자리를 잇대어 놓아도
        // 한 점이 두 웹뷰의 것이 되지 않는다 (`screen_area`와 같은 규칙).
        if (captured == false && (client_x < layout.x || client_x >= layout.x + layout.width))
            return {};
        if (captured == false && (client_y < layout.y || client_y >= layout.y + layout.height))
            return {};

        return webview_pointer {
            .inside = true,
            .x = client_x - layout.x,
            .y = client_y - layout.y,
        };
    }
} // namespace luil::win32
