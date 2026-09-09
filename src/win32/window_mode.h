#pragma once

#include "luil/ui/caption_metrics.h"
#include "luil/win32/app_host.h"
#include "win32/caption_layout.h"
#include "win32/popup_reconcile.h"

#include <cstdint>
#include <optional>

namespace luil::win32 {
    // 주 창이 지금 어떤 모습으로 서 있는가다.
    //
    // 창 스타일·비클라이언트 판정·최대화 크기 한계·DWM 프레임이 **모두 이 한 값을
    // 본다.** 셋을 따로 세우면 그중 하나만 갱신되는 frame이 반드시 생긴다 —
    // `caption_surface::set_caption`도 버튼 집합이 바뀌면 같은 모드 계산을 쓴다.
    // 기존 창에 적용할 때는 프레임 비트만 바꾸어 표시·최소화 상태를 보존한다.
    //
    // **전체 화면은 테두리 없는 창(borderless)이다.** 제시가
    // DirectComposition(합성 스왑체인)을 지나므로 독점 전체 화면(exclusive
    // fullscreen)은 애초에 설 수 없다 — 합성 스왑체인은 `SetFullscreenState`를
    // 받지 못한다. 모니터 rcMonitor를 그대로 덮는 창이 유일하게 가능한 길이고
    // 그것이 여기서 뜻하는 fullscreen이다 (docs/concepts/window.md).
    enum class window_display_mode
    {
        normal,
        maximized,
        fullscreen,
    };

    // OS가 답한 최대화 표식과 우리가 든 전체 화면 상태를 모드 하나로 합친다.
    // **전체 화면이 이긴다.** 최대화된 창에서 전체 화면으로 들어가면 `WS_MAXIMIZE`가
    // 남아 `IsZoomed`가 계속 참을 답하는데, 그때 최대화로 보면 크기 한계가 작업
    // 영역으로 잘려 창이 모니터를 덮지 못한다.
    [[nodiscard]] constexpr window_display_mode window_mode_of(const bool zoomed, const bool fullscreen) noexcept
    {
        if (fullscreen)
            return window_display_mode::fullscreen;
        return zoomed ? window_display_mode::maximized : window_display_mode::normal;
    }

    // Win32 창 스타일 비트다.
    // `DWORD`와 같은 타입이고 값도 `WS_*`와 같다 — 그 둘이 실제로 같다는 것은
    // windows.h를 든 자리(caption_surface.h)의 `static_assert`가 잇는다.
    //  - 여기서 windows.h를 들이지 않는 것이 **창 없이 test가 서는 조건**이다
    //    (surface_invalidate.h가 `USER_TIMER_MAXIMUM` 값을 직접 적은 것과 같은
    //    규칙이다). 스타일 계산은 이 모듈이 답해야 하는 결정 중 가장 자주 틀리는
    //    것이라, 창을 띄우지 않고 못 박을 수 있어야 한다.
    using window_style_bits = unsigned long;

    inline constexpr window_style_bits style_caption { 0x00C00000UL };
    inline constexpr window_style_bits style_system_menu { 0x00080000UL };
    inline constexpr window_style_bits style_thick_frame { 0x00040000UL };
    inline constexpr window_style_bits style_minimize_box { 0x00020000UL };
    inline constexpr window_style_bits style_maximize_box { 0x00010000UL };
    // 창이 **지금** 최대화되어 있다는 표식이다 (`WS_MAXIMIZE`).
    // 스타일 계산에는 들지 않는다 — 최대화는 우리가 세우는 모습이 아니라
    // `showCmd`가 정하는 상태이고, `IsZoomed`가 읽는 것이 이 비트다. 그런데도
    // 이름을 두는 이유는 계산이 이 비트를 **반드시 빼고 나온다는 것**이 전체
    // 화면에서 나올 때의 자리를 정하기 때문이다 (caption_surface.cpp의
    // `set_fullscreen` — 아래 `static_assert`가 그 성질을 못 박는다).
    inline constexpr window_style_bits style_maximize { 0x01000000UL };
    // `WS_OVERLAPPED`는 0이라 더할 것이 없다.
    inline constexpr window_style_bits style_overlapped_window { style_caption | style_system_menu | style_thick_frame | style_minimize_box | style_maximize_box };
    // 확장 스타일 중 전체 화면이 손대는 유일한 비트다 (`WS_EX_WINDOWEDGE`).
    inline constexpr window_style_bits extended_style_window_edge { 0x00000100UL };

    // 전체 화면이 아닐 때 버튼 집합과 무관하게 남는 스타일이다.
    // 캡션을 직접 그리더라도 크기 조절과 시스템 메뉴는 OS의 것이어야 한다.
    inline constexpr window_style_bits retained_window_styles { style_thick_frame | style_system_menu };

    // 이 버튼 집합과 이 모드로 세울 창 스타일이다 (시스템 캡션 포함).
    // 버튼을 빼면 그 **시스템 기능도 함께** 뺀다 — 캡션 더블클릭·Win+↑·시스템
    // 메뉴까지 한 뜻이 된다 (caption-button-design.md).
    //
    // 전체 화면은 여기에 둘을 더 뺀다.
    //  - `WS_THICKFRAME`: 크기 조절이 **어느 길로도** 닿지 않아야 한다. 비클라이언트
    //    판정은 우리가 client로 답해 막지만, Alt+Space의 시스템 메뉴에 있는 "크기
    //    조정"은 그 판정을 지나지 않는다.
    //  - `WS_MAXIMIZEBOX`: Win+↑ 한 번에 최대화가 끼어들면 창은 작업 영역으로
    //    줄고 우리가 든 전체 화면 상태만 참으로 남는다 — 화면과 상태가 갈라진다.
    // 최소화(`WS_MINIMIZEBOX`)와 시스템 메뉴(`WS_SYSMENU`)는 남긴다. Alt+Tab으로
    // 빠져나가고 Alt+F4로 닫는 길까지 막을 이유가 없다.
    //
    // **전체 화면에 들어갈 때와 나올 때가 이 계산 하나다.** 나올 때만 갈무리해 둔
    // 비트를 되돌리면 전체 화면인 동안 앱이 뺀 버튼이 그 순간 되살아난다 — 캡션은
    // 최대화 버튼을 그리지 않는데 Win+↑·캡션 더블클릭·시스템 메뉴의 최대화는 도로
    // 사는, 이 함수가 막으려던 바로 그 어긋남이다.
    [[nodiscard]] constexpr window_style_bits window_style_for(const caption_buttons& buttons, const window_display_mode mode) noexcept
    {
        window_style_bits style { style_overlapped_window };
        if (buttons.minimize == false)
            style &= ~style_minimize_box;
        if (buttons.maximize == false)
            style &= ~style_maximize_box;
        if (mode == window_display_mode::fullscreen)
            style &= ~(style_thick_frame | style_maximize_box);
        return style;
    }

    // 시스템 캡션을 뗀 뒤의 스타일이다.
    // **만들 때 쓴 스타일과 크기를 계산할 때 쓴 스타일이 같아야** 프레임 두께가 맞는다.
    [[nodiscard]] constexpr window_style_bits custom_window_style_for(const caption_buttons& buttons, const window_display_mode mode) noexcept
    {
        return window_style_for(buttons, mode) & ~style_caption;
    }

    // 이미 존재하는 창은 표시·비활성·최소화 등 나머지 상태를 보존한다.
    // 최대화 표식은 모드에 맞춘다. 전체 화면 해제 시 normal을 넘겨 표식을
    // 지운 뒤 SetWindowPlacement가 저장된 최대화 배치를 다시 적용하게 한다.
    [[nodiscard]] constexpr window_style_bits updated_custom_window_style(
        const window_style_bits current, const caption_buttons& buttons, const window_display_mode mode) noexcept
    {
        const window_style_bits retained { current & ~(style_overlapped_window | style_maximize) };
        return retained | custom_window_style_for(buttons, mode) | (mode == window_display_mode::maximized ? style_maximize : 0UL);
    }

    // 이 모드에서 창이 가질 확장 스타일이다.
    // 지금 값에서 시작하는 이유는 확장 스타일의 나머지 비트가 **우리 것이 아니기**
    // 때문이다 — `WS_EX_APPWINDOW`는 앱이 주고 `WS_EX_WINDOWEDGE`는 OS가 스타일을
    // 보고 얹는다. 전체 화면에서는 그 테두리 비트만 뗀다.
    [[nodiscard]] constexpr window_style_bits extended_window_style_for(const window_style_bits current, const window_display_mode mode) noexcept
    {
        if (mode == window_display_mode::fullscreen)
            return current & ~extended_style_window_edge;
        return current;
    }

    static_assert((custom_window_style_for({}, window_display_mode::normal) & style_caption) == 0);
    static_assert((custom_window_style_for({}, window_display_mode::normal) & retained_window_styles) == retained_window_styles);
    static_assert((window_style_for({}, window_display_mode::normal) & (style_minimize_box | style_maximize_box)) == (style_minimize_box | style_maximize_box));
    static_assert((window_style_for({ .minimize = false, .maximize = false, .close = true }, window_display_mode::normal) & (style_minimize_box | style_maximize_box)) == 0);
    static_assert((window_style_for({ .minimize = false, .maximize = false, .close = true }, window_display_mode::normal) & retained_window_styles) == retained_window_styles);
    static_assert((window_style_for({}, window_display_mode::fullscreen) & (style_thick_frame | style_maximize_box)) == 0);
    static_assert((window_style_for({}, window_display_mode::fullscreen) & (style_minimize_box | style_system_menu)) == (style_minimize_box | style_system_menu));
    // 이 계산은 **최대화 표식을 만들지 않는다.**
    // 전체 화면에서 나올 때 그 비트가 남아 있으면 `SetWindowPlacement`가 이미
    // 최대화된 창을 다시 최대화하지 못해 창이 화면을 덮은 크기 그대로 앉는다.
    static_assert((window_style_for({}, window_display_mode::normal) & style_maximize) == 0);
    static_assert((window_style_for({}, window_display_mode::fullscreen) & style_maximize) == 0);

    // 캡션 자리에 남길 DWM 프레임의 위쪽 두께다 (`MARGINS::cyTopHeight`).
    // 통상 1px은 창 위 테두리와 그림자를 살리지만, 모니터를 덮는 창에서는 그 한 줄이
    // 화면 맨 위의 **이음매**로 보인다. 전체 화면에서만 0으로 두고 나올 때 되돌린다.
    [[nodiscard]] constexpr int dwm_frame_top_margin_for(const window_display_mode mode) noexcept
    {
        return mode == window_display_mode::fullscreen ? 0 : 1;
    }

    // 이 모드가 OS에 맡기는 최대화 크기가 작업 영역(rcWork)인가다.
    // 전체 화면만 거짓이다 — 그때의 목표는 rcMonitor라, 작업 표시줄 두께만큼
    // 잘리면 덮으라고 준 자리를 스스로 비운다.
    [[nodiscard]] constexpr bool maximum_size_follows_work_area(const window_display_mode mode) noexcept
    {
        return mode != window_display_mode::fullscreen;
    }

    // 비클라이언트 판정의 답이다.
    // Win32 `HT*` 값으로 옮기는 것은 창을 든 자리(caption_surface.cpp)가 한다 —
    // 이 열거가 windows.h를 모르는 것이 판정을 창 없이 재는 조건이다.
    enum class window_hit
    {
        client,
        caption_drag,
        system_menu,
        minimize_button,
        maximize_button,
        close_button,
        resize_left,
        resize_right,
        resize_top,
        resize_bottom,
        resize_top_left,
        resize_top_right,
        resize_bottom_left,
        resize_bottom_right,
    };

    // 판정이 보는 창의 치수다 (논리 픽셀).
    // 캡션 쪽은 그 표면이 그리는 값 그대로이고(`caption_config`), 테두리 두께는
    // 앱 설정(`window_config`)의 값이다.
    struct window_frame_metrics
    {
        caption_ui_metrics caption { default_caption_ui_metrics };
        caption_buttons buttons { default_caption_buttons };
        // 창 가장자리에서 크기 조절로 잡히는 두께다.
        // 모서리는 가장자리보다 넓어야 잡기 쉽다.
        int resize_border_thickness { 4 };
        int resize_corner_thickness { 10 };
    };

    // custom caption 창의 비클라이언트 판정이다 (좌표는 창 왼쪽 위 기준 물리 픽셀).
    // 크기 조절 테두리·모서리 → 시스템 메뉴 → 캡션 버튼 → 끌기 → client 순이고,
    // 캡션 쪽 자리는 `make_caption_layout`이 정한 것을 그대로 쓴다 — 그리기와 판정이
    // 같은 계산을 보는 것이 caption 계약의 핵심이라 여기서 다시 재지 않는다.
    //
    // 모드가 답을 두 번 자른다.
    //  - **전체 화면은 어디를 눌러도 client다.** 화면을 덮은 창에 크기 조절
    //    가장자리가 남아 있으면 사용자는 화면 끝을 노려 무언가를 누르다가 창 크기를
    //    바꾼다. 캡션 끌기 띠도 마찬가지다 — 끌면 전체 화면이 통째로 딸려 나간다.
    //    HTSYSMENU도 같은 이유로 없다.
    //  - 최대화 창은 가장자리만 없다. 이미 작업 영역에 맞물려 있어 조절할 것이 없고,
    //    캡션은 그대로 살아 있어야 끌어 내려 복원할 수 있다.
    [[nodiscard]] window_hit hit_test_window(const window_frame_metrics& metrics, window_display_mode mode, int width, int height, std::uint32_t dpi, int x, int y) noexcept;

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
    // **작업 영역(rcWork)이 아니라 모니터 전체(rcMonitor)다** — 이 코드베이스에서
    // rcMonitor를 목표로 삼는 첫 자리이고, 작업 표시줄 위까지 덮는 것이 곧 전체
    // 화면이다. 다른 계산(popup 다듬기·보조 창 초기 배치·최대화 한계)이 전부 rcWork를
    // 쓰므로 여기만 다르다는 것을 이름으로 남긴다.
    //
    // **들어갈 때 한 번 재고 마는 값이 아니다.** 모니터 사각형은 배율이 바뀌거나
    // (`WM_DPICHANGED`) 해상도·모니터 구성이 바뀌면(`WM_DISPLAYCHANGE`) 함께
    // 바뀌는데, 그때 창만 옛 사각형에 남으면 화면을 덮는다는 뜻이 그 순간부터
    // 거짓이 된다 — 그 창은 여전히 테두리가 없고 판정도 어디나 client라 사용자가
    // 되돌릴 길이 전체 화면 해제뿐이다. 두 메시지가 이 계산을 다시 부른다
    // (`caption_surface::reapply_fullscreen_bounds`).
    [[nodiscard]] window_bounds fullscreen_bounds_for(const screen_area& monitor) noexcept;

    // 앱에 알릴 창 배치다.
    //
    // `observed`는 OS가 방금 답한 값이다 (`WINDOWPLACEMENT::rcNormalPosition`과
    // `IsZoomed`). `restore`는 전체 화면에 들어가기 **전에** 갈무리해 둔 배치이고,
    // **비어 있다는 것이 곧 "전체 화면이 아니다"이다** — 곁에 bool을 두지 않는다.
    //
    // 이 함수가 있는 이유는 전체 화면이 관측값을 통째로 못 쓰게 만들기 때문이다.
    //  - 전체 화면에 들어갈 때 쓰는 `SetWindowPos`가 `rcNormalPosition`을 **모니터
    //    사각형으로 덮어쓴다.** 그대로 보고하면 다음 실행은 "전체 화면을 벗어나면
    //    모니터를 덮는 창"으로 복원된다 — 돌아갈 자리가 사라진다.
    //  - 같은 순간의 `WM_SIZE`는 `SIZE_RESTORED`로 와서 최대화 표식도 함께 지운다.
    //    최대화 창에서 들어갔다가 나오면 창이 복원 크기로 앉는다.
    // 그래서 전체 화면 동안의 답은 관측값이 아니라 **기억해 둔 것**이고, 거기에
    // `fullscreen = true`만 얹는다. 저장했다가 그대로 다시 넣으면 전체 화면으로
    // 열리면서 돌아갈 자리도 함께 산다 (app_host.h의 `window_placement`).
    [[nodiscard]] window_placement placement_to_report(const window_placement& observed, const std::optional<window_placement>& restore) noexcept;
} // namespace luil::win32
