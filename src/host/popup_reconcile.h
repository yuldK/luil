#pragma once

#include "luil/theme/ui_style.h"

#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace luil {
    // popup 하나의 자리다 (물리 픽셀, 앵커 표면 client 기준).
    struct popup_placement
    {
        std::u8string id {};
        int x { 0 };
        int y { 0 };
        int width { 0 };
        int height { 0 };
        // 이 popup이 붙는 표면이다 (비어 있으면 주 창).
        // 자리의 기준이자 창의 소유자다.
        std::u8string anchor {};
        // OS가 둥글게 잘라 내는 창인가다 (`popup_shape::rounded`). 그림자가 창 클래스의 것이라 만든 뒤 바꿀 수 없다.
        bool rounded { false };

        [[nodiscard]] bool operator==(const popup_placement&) const = default;
    };

    // popup 창 하나의 모양이다 — 모서리, 그림자, 경계를 누가 긋는가.
    //  - 둥근 창: OS가 모서리를 자르고 같은 모양의 그림자와 테두리를 얹는다. 표면은 사각 선을 긋지 않는다.
    //  - 직각 창: 처음부터의 모양이다. 창 클래스의 drop shadow와 표면의 1px 선이다.
    struct popup_shape
    {
        // 실제 모서리다. `system`은 남지 않는다 — 이 OS의 답으로 풀린다.
        popup_corner corner { popup_corner::square };
        // 창 클래스의 drop shadow(`CS_DROPSHADOW`)를 쓴다. 둥근 창 밑에 깔면 사각 그림자가 모서리 밖으로 보인다.
        bool drop_shadow { true };
        // 표면이 tree 위에 1px 선을 긋는다 (`frame_state::border`).
        bool line { true };
        // OS 테두리를 팔레트 색으로 긋는다. 거짓인 둥근 창은 OS 테두리를 없앤다 (`DWMWA_COLOR_NONE`).
        bool system_border { false };

        [[nodiscard]] bool rounded() const noexcept
        {
            return corner != popup_corner::square;
        }

        [[nodiscard]] bool operator==(const popup_shape&) const noexcept = default;
    };

    // 고른 모서리를 이 OS의 실제 모서리로 푼다. 둥근 모서리를 못 쓰면 무엇이든 직각이다.
    [[nodiscard]] popup_corner resolve_popup_corner(popup_corner requested, bool rounded_supported) noexcept;
    // popup 하나의 모양을 정한다. `border`는 `ui_popup::border`다.
    [[nodiscard]] popup_shape resolve_popup_shape(popup_corner requested, bool border, bool rounded_supported) noexcept;
    // 풀린 모서리의 반지름이다 (논리 픽셀, Windows 11의 DWM 값 — 둥글게 8, 작게 4, 직각 0).
    [[nodiscard]] float popup_corner_logical_radius(popup_corner resolved) noexcept;

    // 부모가 먼저인 유효 popup 순서다. 없는 앵커·순환·중복 id는 내보내지 않는다.
    [[nodiscard]] std::vector<std::size_t> order_popup_placements(std::span<const popup_placement> wanted, std::span<const std::u8string> roots);
    [[nodiscard]] bool popup_is_descendant(std::span<const popup_placement> placements, const std::u8string& child, const std::u8string& ancestor) noexcept;

    // 살아 있는 popup 창과 frame이 원하는 popup을 id로 대조한 결과다.
    // create·move의 index는 원하는 목록(wanted)의 자리다.
    struct popup_reconcile_result
    {
        std::vector<std::size_t> create {};
        std::vector<std::size_t> move {};
        std::vector<std::u8string> destroy {};
    };

    // frame이 올 때마다 창을 만들고·옮기고·없앨 대상을 정한다.
    // 크기가 0 이하인 wanted 항목은 없는 것으로 본다 (살아 있으면 destroy로 간다).
    // 같은 id에 같은 자리면 아무 일도 내지 않는다.
    // 같은 id라도 **앵커가 다르면** 옮기지 않고 없앴다가 다시 만든다 —
    // Win32의 창 소유자는 만든 뒤 바꾸는 것이 정식 경로가 아니다
    // (popup-anchor-design.md).
    [[nodiscard]] popup_reconcile_result reconcile_popups(std::span<const popup_placement> alive, std::span<const popup_placement> wanted);

    // 살아 있는 보조 창과 frame이 원하는 보조 창을 id로 대조한 결과다.
    // create의 index는 원하는 목록(wanted)의 자리다.
    // 배치는 창을 만들 때만 적용하므로(multi-window-design.md) popup과 달리 move가 없다.
    struct window_reconcile_result
    {
        std::vector<std::size_t> create {};
        std::vector<std::u8string> destroy {};
    };

    // frame이 올 때마다 만들고 없앨 보조 창을 정한다.
    // 없던 id는 만들고 사라진 id는 없앤다. 크기 유효성은 호출자가 미리 거른다.
    [[nodiscard]] window_reconcile_result reconcile_windows(std::span<const std::u8string> alive, std::span<const std::u8string> wanted);

    // 웹뷰 하나의 **수명**이다.
    //
    // 자리가 없는 것이 이 타입의 요점이다 — 웹뷰의 자리는 목록이 아니라 tree의
    // 자리표(`webview_element`)가 정한다. 앱은 `arrange` 결과를 미리 모르고,
    // 흘리는 창 안의 잘림은 `ui_tree::visible_bounds`만 아는 답이기 때문이다
    // (webview-composition-design.md).
    struct webview_placement
    {
        std::u8string id {};
        // 이 웹뷰가 앉는 표면이다 (`ui_window::id`와 같은 이름 공간).
        // 비어 있으면 주 창이다.
        std::u8string anchor {};

        [[nodiscard]] bool operator==(const webview_placement&) const = default;
    };

    // 살아 있는 웹뷰와 frame이 원하는 웹뷰를 대조한 결과다.
    // create의 index는 원하는 목록(wanted)의 자리다.
    // 자리를 목록이 갖지 않으므로 `move`가 없다 — popup이 아니라 보조 창과 같은 모양이다.
    struct webview_reconcile_result
    {
        std::vector<std::size_t> create {};
        std::vector<std::u8string> destroy {};
    };

    // frame이 올 때마다 만들고 없앨 웹뷰를 정한다.
    // 없던 id는 만들고 사라진 id는 없앤다.
    //  - 같은 id라도 **앵커가 다르면** 없앴다가 다시 만든다. 웹뷰는 그 표면의
    //    합성 visual과 `parentWindow`에 함께 매여 있어, 옮기려면 둘을 같이 옮겨야
    //    한다 — popup의 소유자와 같은 판정이다 (popup-anchor-design.md).
    //  - 페이지 상태가 날아가는 것이 그 대가다. 표면을 옮기는 것은 앱이 목록에서
    //    앵커를 바꿔 적었을 때뿐이라 드물고, 조용히 옮기는 것보다 낫다.
    [[nodiscard]] webview_reconcile_result reconcile_webviews(std::span<const webview_placement> alive, std::span<const webview_placement> wanted);

    // 화면의 한 영역이다 (물리 픽셀, 반열림).
    struct screen_area
    {
        int left { 0 };
        int top { 0 };
        int right { 0 };
        int bottom { 0 };
    };

    // popup 왼쪽 위 좌표를 영역 안으로 민다.
    // popup이 영역보다 크면 왼쪽 위를 맞춰 시작 부분이 보이게 한다.
    [[nodiscard]] std::pair<int, int> clamp_popup_position(int x, int y, int width, int height, const screen_area& area) noexcept;
} // namespace luil
