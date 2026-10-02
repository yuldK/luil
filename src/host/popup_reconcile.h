#pragma once

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

        [[nodiscard]] bool operator==(const popup_placement&) const = default;
    };

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
