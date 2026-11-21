#pragma once

#include "host/popup_reconcile.h"
#include "luil/app/app_host.h"

#include <optional>

namespace luil {
    // 주 창이 지금 어떤 모습으로 서 있는가다.
    //
    // 창의 모습에 딸린 결정(Win32라면 창 스타일·비클라이언트 판정·최대화 크기 한계·DWM 프레임)이
    // **모두 이 한 값을 본다.** 셋을 따로 세우면 그중 하나만 갱신되는 frame이 반드시 생긴다.
    // 플랫폼마다 그 값으로 무엇을 정하는지는 플랫폼 계층에 있다 (win32/window_mode.h).
    //
    // **전체 화면은 테두리 없는 창(borderless)이다.** 모니터 사각형을 그대로 덮는 창이
    // 여기서 뜻하는 fullscreen이다 (docs/concepts/window.md).
    //  - Win32에서는 그것이 유일하게 가능한 길이다. 제시가 DirectComposition(합성 스왑체인)을
    //    지나므로 독점 전체 화면(exclusive fullscreen)은 애초에 설 수 없다 — 합성 스왑체인은
    //    `SetFullscreenState`를 받지 못한다.
    enum class window_display_mode
    {
        normal,
        maximized,
        fullscreen,
    };

    // OS가 답한 최대화 표식과 우리가 든 전체 화면 상태를 모드 하나로 합친다.
    // **전체 화면이 이긴다.** 최대화된 창에서 전체 화면으로 들어가면 OS의 최대화 표식이
    // 남는데(Win32의 `WS_MAXIMIZE`와 `IsZoomed`), 그때 최대화로 보면 크기 한계가 작업
    // 영역으로 잘려 창이 모니터를 덮지 못한다.
    [[nodiscard]] constexpr window_display_mode window_mode_of(const bool zoomed, const bool fullscreen) noexcept
    {
        if (fullscreen)
            return window_display_mode::fullscreen;
        return zoomed ? window_display_mode::maximized : window_display_mode::normal;
    }

    // 창 하나가 설 자리다 (물리 픽셀).
    struct window_bounds
    {
        int x { 0 };
        int y { 0 };
        int width { 0 };
        int height { 0 };

        [[nodiscard]] bool operator==(const window_bounds&) const noexcept = default;
    };

    // 이 모니터를 덮는 전체 화면 창의 자리다.
    // **작업 영역이 아니라 모니터 전체다** (Win32의 rcWork가 아니라 rcMonitor) — 작업 표시줄
    // 위까지 덮는 것이 곧 전체 화면이다. 다른 계산(popup 다듬기·보조 창 초기 배치·최대화 한계)이
    // 전부 작업 영역을 쓰므로 여기만 다르다는 것을 이름으로 남긴다.
    //
    // **들어갈 때 한 번 재고 마는 값이 아니다.** 모니터 사각형은 배율이나 해상도·모니터 구성이
    // 바뀌면 함께 바뀌는데(Win32의 `WM_DPICHANGED`·`WM_DISPLAYCHANGE`), 그때 창만 옛 사각형에
    // 남으면 화면을 덮는다는 뜻이 그 순간부터 거짓이 된다 — 그 창은 여전히 테두리가 없고
    // 판정도 어디나 client라 사용자가 되돌릴 길이 전체 화면 해제뿐이다. 플랫폼은 그 알림마다
    // 이 계산을 다시 부른다 (Win32는 `caption_surface::reapply_fullscreen_bounds`).
    [[nodiscard]] window_bounds fullscreen_bounds_for(const screen_area& monitor) noexcept;

    // 앱에 알릴 창 배치다.
    //
    // `observed`는 OS가 방금 답한 값이다 (Win32라면 `WINDOWPLACEMENT::rcNormalPosition`과
    // `IsZoomed`). `restore`는 전체 화면에 들어가기 **전에** 갈무리해 둔 배치이고,
    // **비어 있다는 것이 곧 "전체 화면이 아니다"이다** — 곁에 bool을 두지 않는다.
    //
    // 이 함수가 있는 이유는 전체 화면이 관측값을 통째로 못 쓰게 만들기 때문이다. Win32의 경우다.
    //  - 전체 화면에 들어갈 때 쓰는 `SetWindowPos`가 `rcNormalPosition`을 **모니터
    //    사각형으로 덮어쓴다.** 그대로 보고하면 다음 실행은 "전체 화면을 벗어나면
    //    모니터를 덮는 창"으로 복원된다 — 돌아갈 자리가 사라진다.
    //  - 같은 순간의 `WM_SIZE`는 `SIZE_RESTORED`로 와서 최대화 표식도 함께 지운다.
    //    최대화 창에서 들어갔다가 나오면 창이 복원 크기로 앉는다.
    // 그래서 전체 화면 동안의 답은 관측값이 아니라 **기억해 둔 것**이고, 거기에
    // `fullscreen = true`만 얹는다. 저장했다가 그대로 다시 넣으면 전체 화면으로
    // 열리면서 돌아갈 자리도 함께 산다 (app_host.h의 `window_placement`).
    [[nodiscard]] window_placement placement_to_report(const window_placement& observed, const std::optional<window_placement>& restore) noexcept;
}
