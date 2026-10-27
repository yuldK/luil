#pragma once

#include "luil/ui/caption_metrics.h"

#include <cstdint>

namespace luil {
    class ui_tree;
} // namespace luil

namespace luil::win32 {
    // caption이 서 있는 자리다 (client 물리 픽셀).
    // 폭이나 높이가 0이면 caption이 없는 것이라 어느 자리도 caption으로 맞지 않는다.
    struct caption_band
    {
        int left { 0 };
        int top { 0 };
        int width { 0 };
        int height { 0 };

        [[nodiscard]] bool operator==(const caption_band&) const noexcept = default;
    };

    // tree가 아직 없을 때의 자리다 — client 맨 위를 가로지르는 띠다.
    [[nodiscard]] caption_band default_caption_band(int client_width, std::uint32_t dpi, const caption_ui_metrics& metrics = default_caption_ui_metrics) noexcept;

    // 게시된 tree에서 `caption_element`가 선 자리다.
    // caption의 자리는 앱이 배치로 정한다 (아래에 두거나, 위로 밀어 올리거나, 창 일부만 덮거나).
    // 그것을 그린 tree에 판정도 물어야 그림과 판정이 한 자리를 본다 — 설정에 앵커를 따로 두면
    // 배치와 어긋날 두 번째 원천이 생긴다 (caption-button-design.md).
    //  - tree가 없거나 tree에 caption element가 없으면 `default_caption_band`다.
    //  - caption이나 그 조상이 숨어 있으면 빈 띠다. 보이지 않는 caption을 잡고 창이 딸려 가면 안 된다.
    [[nodiscard]] caption_band caption_band_in(const ui_tree* tree, int client_width, std::uint32_t dpi, const caption_ui_metrics& metrics = default_caption_ui_metrics) noexcept;

    struct caption_layout
    {
        int left { 0 };
        int top { 0 };
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

    // 버튼은 띠의 오른쪽 끝에서 닫기 → 최대화 → 최소화 순으로 쌓이고, **빠진 버튼은
    // 자리를 차지하지 않는다** — 남은 버튼이 오른쪽으로 당겨진다.
    // 없는 버튼의 왼쪽 좌표는 그 시점의 오른쪽 끝과 같게 두므로
    // `hit_test_caption`은 그대로 성립한다 (caption-button-design.md).
    [[nodiscard]] caption_layout make_caption_layout(
        const caption_band& band, std::uint32_t dpi, const caption_ui_metrics& metrics = default_caption_ui_metrics, const caption_buttons& buttons = default_caption_buttons) noexcept;
    // client 맨 위의 띠(`default_caption_band`)로 잰다.
    [[nodiscard]] caption_layout make_caption_layout(
        int client_width, std::uint32_t dpi, const caption_ui_metrics& metrics = default_caption_ui_metrics, const caption_buttons& buttons = default_caption_buttons) noexcept;
    // 띠 밖은 client다. `x < right`를 먼저 거르므로 왼쪽 좌표가 오른쪽 끝과 같은 버튼은 맞지 않는다.
    [[nodiscard]] caption_hit hit_test_caption(const caption_layout& layout, int x, int y) noexcept;
} // namespace luil::win32
