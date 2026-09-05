#pragma once

#include "luil/ui/caption_metrics.h"

#include <cstdint>

namespace luil::win32 {
    struct caption_layout
    {
        int height { 0 };
        int button_width { 0 };
        int minimize_left { 0 };
        int maximize_left { 0 };
        int close_left { 0 };
        int right { 0 };
    };

    enum class caption_hit
    {
        client,
        drag,
        minimize,
        maximize,
        close,
    };

    // 버튼은 오른쪽 끝에서 닫기 → 최대화 → 최소화 순으로 쌓이고, **빠진 버튼은
    // 자리를 차지하지 않는다** — 남은 버튼이 오른쪽으로 당겨진다.
    // 없는 버튼의 왼쪽 좌표는 그 시점의 오른쪽 끝과 같게 두므로
    // `hit_test_caption`은 그대로 성립한다 (caption-button-design.md).
    [[nodiscard]] caption_layout make_caption_layout(
        int client_width, std::uint32_t dpi, const caption_ui_metrics& metrics = default_caption_ui_metrics, const caption_buttons& buttons = default_caption_buttons) noexcept;
    // `x < right`를 먼저 거르므로 왼쪽 좌표가 오른쪽 끝과 같은 버튼은 맞지 않는다.
    [[nodiscard]] caption_hit hit_test_caption(const caption_layout& layout, int x, int y) noexcept;
} // namespace luil::win32
