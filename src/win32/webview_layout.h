#pragma once

#include "luil/ui/ui_element.h"

#include <optional>

namespace luil::win32 {
    // 웹뷰 하나가 이번 frame에 차지할 자리와, 그 자리에 구멍을 뚫을지다.
    //
    // 자리는 표면 client의 **물리 픽셀**이다 — tree의 좌표가 이미 배율을 곱한
    // 값이라 여기서 다시 곱하지 않는다.
    struct webview_layout
    {
        int x { 0 };
        int y { 0 };
        int width { 0 };
        int height { 0 };
        // 이 frame에서 해당 영역을 알파 0으로 남길지 나타낸다.
        // luil visual 아래의 웹뷰는 투명 영역을 통해 보인다.
        // modal로 웹뷰를 가릴 때는 구멍을 막는다. 구멍 위에 반투명 scrim만 그리면
        // 아래 웹뷰가 계속 비치기 때문이다.
        bool punch_hole { false };
        // 이 자리가 **배치된 배율**이다 (CSS 픽셀 = 물리 픽셀 / 이 값).
        //
        // 자리와 함께 실리는 이유: 둘은 같은 tree에서 나온 한 쌍이라야 한다.
        // 표면의 배율을 따로 물으면 `WM_DPICHANGED` 직후 아직 옛 배율로 배치된
        // tree의 자리에 새 배율이 붙어, 페이지가 좁은 뷰포트로 한 번 리플로하고
        // 새 tree가 오면 다시 한 번 한다. 자리표가 배치될 때의 배율을 그대로
        // 실으면 어긋난 쌍이 생길 길이 없다 (webview-composition-design.md).
        float scale { 1.0f };

        // 그릴 자리가 있는가다. 거짓이면 웹뷰를 감춘다.
        [[nodiscard]] bool visible() const noexcept
        {
            return width > 0 && height > 0;
        }

        [[nodiscard]] bool operator==(const webview_layout&) const noexcept = default;
    };

    // 자리표의 보이는 사각형과 창 크기로 이번 frame의 자리를 정한다.
    //
    // `visible`은 `ui_tree::visible_bounds`가 답한 값이다 — 조상의 잘라내기가
    // 이미 반영되어 있어 흘리는 창 안의 웹뷰가 저절로 잘린다. 값이 없으면
    // 자리표가 tree에 없거나 통째로 잘린 것이다.
    //
    // `occluded`는 그 자리 위에 **불투명한 것이 있는가**다. 판정은 tree가 하고
    // (웹뷰 자리에서 `hit_test`가 자리표 자신을 돌려주는가) 이 함수는 그 답을
    // 받기만 한다 — 그래야 이 함수가 tree도 창도 모른 채로 test된다.
    //
    // 가려져도 **자리는 그대로 둔다.** 웹뷰 visual은 우리 아래에 있어 덮이기만
    // 하고, 자리를 흔들면 페이지가 리플로를 한 번 더 한다. 바뀌는 것은
    // `punch_hole` 하나다.
    //  - 1차는 **전부 아니면 전무**다. 토스트가 모서리만 덮어도 통째로 안 뚫는다.
    //    걸치는 것을 rect 차집합으로 빼는 것은 오버레이가 여럿이면 rect 목록의
    //    차집합이 되고, 그것이 곧 layer다.
    //
    // `scale`은 자리표가 배치된 배율이다 — 그 자리를 낸 tree의 것이지 표면의
    // 것이 아니다 (`webview_layout::scale`). 0 이하는 1로 본다.
    [[nodiscard]] webview_layout plan_webview_layout(const std::optional<rect_f>& visible, int client_width, int client_height, bool occluded, float scale) noexcept;

    // 표면 client의 포인터 하나를 웹뷰 자리에 비추어 본 것이다.
    struct webview_pointer
    {
        // 포인터가 웹뷰의 표시 영역에 있는지 나타낸다.
        // 크기 0인 웹뷰 자식 창 대신 부모 창이 마우스를 받아 SendMouseInput으로 중계한다.
        // 중계한 포인터 입력은 luil tree에 중복 전달하지 않는다.
        bool inside { false };
        // **웹뷰 왼쪽 위 기준** 좌표다 (물리 픽셀).
        // `inside`가 거짓이면 뜻이 없다.
        int x { 0 };
        int y { 0 };
    };

    // 표면 client 좌표를 웹뷰 자리 기준으로 옮긴다.
    //
    // 자리가 없으면(`layout.visible()`이 거짓) 언제나 바깥이다 — 감춰진 웹뷰가
    // 포인터를 먹지 않는다.
    // 누름을 시작한 웹뷰는 캡처 동안 사각형 밖의 이동과 뗌도 받는다.
    [[nodiscard]] webview_pointer translate_webview_pointer(const webview_layout& layout, int client_x, int client_y, bool captured = false) noexcept;
} // namespace luil::win32
