#pragma once

#include "luil/ui/caption_element.h"
#include "win32/window_mode.h"
#include "win32/window_surface.h"

#include <optional>
#include <type_traits>

namespace luil::win32 {
    // 스타일 계산은 창을 모르는 자리(window_mode.h)에 있다.
    // 그 자리가 쓰는 비트가 실제로 `WS_*`와 같은 값·같은 타입이라는 것만 windows.h를
    // 든 여기서 못 박는다 — 값이 갈리면 컴파일이 서지 않으므로, 창 없이 재는 test와
    // 실제 창의 스타일이 어긋난 채로 굳을 길이 없다.
    static_assert(std::is_same_v<window_style_bits, DWORD>);
    static_assert(style_caption == WS_CAPTION);
    static_assert(style_system_menu == WS_SYSMENU);
    static_assert(style_thick_frame == WS_THICKFRAME);
    static_assert(style_minimize_box == WS_MINIMIZEBOX);
    static_assert(style_maximize_box == WS_MAXIMIZEBOX);
    static_assert(style_maximize == WS_MAXIMIZE);
    static_assert(style_overlapped_window == WS_OVERLAPPEDWINDOW);
    static_assert(extended_style_window_edge == WS_EX_WINDOWEDGE);

    // custom caption 창의 비클라이언트 판정이다.
    // 판정 자체는 `hit_test_window`가 하고 여기서는 창에서 좌표를 읽어 Win32 `HT*`로
    // 옮긴다. 모드를 받는 이유는 그 판정이 모드마다 다르기 때문이다 —
    // 전체 화면이면 어디를 눌러도 `HTCLIENT`다 (window_mode.h).
    [[nodiscard]] LRESULT caption_hit_test(HWND window, LPARAM long_parameter, std::uint32_t dpi, const caption_config& caption, const window_config& config, window_display_mode mode);

    // 최소 크기와 최대화 크기를 함께 정한다.
    // 최소 크기는 client 기준 값을 창 크기로 바꿔 넣는다 (프레임 두께 포함).
    //  - 최대화 크기를 작업 영역으로 자르는 것은 **전체 화면이 아닐 때만**이다.
    //    덮으라고 준 자리를 작업 표시줄 두께만큼 스스로 비우게 된다.
    void apply_size_limits(MINMAXINFO* information, HWND window, std::uint32_t dpi, DWORD style, DWORD extended_style, int minimum_client_width, int minimum_client_height,
        window_display_mode mode) noexcept;

    // custom caption을 가진 표면이다 (주 창·보조 창).
    // 시스템 캡션을 떼고 비클라이언트를 client로 만든 뒤, 캡션 자리·버튼·
    // 크기 조절 테두리를 직접 판정한다 (multi-window-design.md).
    //  - 캡션을 **그리는** 것은 앱이 tree에 담은 `caption_element`다.
    //    여기 있는 것은 OS가 비클라이언트로 묻는 것에 답하는 부분뿐이다.
    class caption_surface : public window_surface
    {
    public:
        explicit caption_surface(surface_context& context, std::u8string id = {}) noexcept;

        // 시스템 캡션을 뗀다 (창을 만든 직후 한 번).
        [[nodiscard]] static bool remove_system_caption(HWND window, std::u8string& error);
        // 캡션 자리에 DWM 그림자·테두리를 남긴다.
        // 모드를 받는 이유는 그 테두리의 한 줄이 전체 화면에서는 화면 맨 위의
        // 이음매로 보이기 때문이다 (`dwm_frame_top_margin_for`).
        //  - `WM_DWMCOMPOSITIONCHANGED`가 이것을 다시 적용하므로, 그 자리도 지금
        //    모드를 넘겨야 전체 화면 중의 합성 재시작이 이음매를 되살리지 않는다.
        static void apply_dwm_frame(HWND window, window_display_mode mode) noexcept;

        // 비클라이언트 hit test가 쓰는 캡션 치수다 (논리 픽셀).
        // 이 표면이 그리는 caption의 치수와 버튼 집합이다.
        // 비클라이언트 hit test와 창 스타일이 같은 값을 본다.
        //
        // **창이 붙어 있으면 OS 쪽도 함께 잇는다.** 버튼 집합은 창 스타일을
        // 정하고 제목은 Alt+Tab·작업 표시줄이 읽는데, 그 둘은 tree에 실려 있지
        // 않아 여기서만 갱신될 수 있다. 셋이 어긋나면 "버튼은 없는데 그 자리가
        // 최대화"인 창이 된다 (`caption_buttons`의 주석).
        //  - 바뀐 것이 없으면 OS를 부르지 않는다. 매 frame 오는 길이다.
        //  - 창을 만들기 **전**에 부르는 것이 그대로 남는다. 그때는 창이 없어
        //    값만 실리고, 스타일은 생성 인자가, 제목은 `CreateWindowExW`가 맡는다.
        void set_caption(const caption_config& caption);
        // 마지막으로 실린 캡션이다.
        // 비클라이언트 hit test·창 스타일·크기 한계가 전부 이 값에서 나오므로,
        // "새 frame이 실은 값이 실제로 여기 닿았는가"를 이것으로 묻는다.
        [[nodiscard]] const caption_config& caption() const noexcept
        {
            return caption_;
        }
        // 사용자가 이보다 작게 줄일 수 없는 client 크기다 (논리 픽셀).
        void set_minimum_client_size(float width, float height) noexcept;
        [[nodiscard]] float minimum_client_width() const noexcept
        {
            return minimum_client_width_;
        }
        [[nodiscard]] float minimum_client_height() const noexcept
        {
            return minimum_client_height_;
        }
        // `AdjustWindowRectExForDpi`가 프레임 두께를 셈할 때 쓰는 확장 스타일이다.
        void set_extended_style(DWORD style) noexcept;

        // 이 표면이 지금 서 있는 모습이다.
        // 창 스타일·비클라이언트 판정·크기 한계·DWM 프레임이 **모두 이 값 하나를**
        // 본다. `set_caption`이 버튼 집합에서 스타일을 다시 세울 때도 이 값을 함께
        // 넣으므로, 전체 화면이 스타일을 뒤에서 손보다 캡션 갱신 한 번에 지워지는
        // 길이 없다 (window_mode.h).
        [[nodiscard]] window_display_mode display_mode() const noexcept;
        // 테두리 없는 전체 화면인가다.
        [[nodiscard]] bool fullscreen() const noexcept
        {
            return before_fullscreen_.has_value();
        }
        // 전체 화면으로 넣거나 뺀다.
        // 실제로 상태가 바뀌었으면 참이다 (이미 그 상태이거나 모니터·배치를 읽지
        // 못하면 거짓이고 아무것도 건드리지 않는다).
        //
        // 들어갈 때 갈무리하는 것은 **둘**이다: `GWL_EXSTYLE`와 `WINDOWPLACEMENT`
        // 전부(`rcNormalPosition`과 `showCmd`). 앞의 것은 나머지 비트가 우리 것이
        // 아니라 되돌릴 수밖에 없는 값이고, 뒤의 것은 **전체 화면으로 옮기는
        // `SetWindowPos`가 덮어쓰는** 값이다 — 갈무리하지 않으면 들어서는 순간
        // 돌아갈 자리가 사라진다 (`placement_to_report`).
        //  - **`GWL_STYLE`은 갈무리하지 않는다.** 들어갈 때가 버튼 집합과 모드로
        //    계산해 세우는 값이라 나올 때도 같은 계산이어야 한다. 갈무리한 비트를
        //    되돌리면 전체 화면인 동안 앱이 바꾼 버튼 집합이 그 순간 지워져,
        //    캡션이 그리지 않는 버튼의 시스템 기능만 되살아난다 (window_mode.h의
        //    `window_style_for`).
        bool set_fullscreen(bool enter);
        // 전체 화면 창을 **지금** 모니터의 사각형에 다시 앉힌다.
        // 다시 앉혔으면 참이다 (전체 화면이 아니거나 모니터를 읽지 못하면 거짓이고
        // 아무것도 건드리지 않는다).
        //  - 배율 변화와 화면 변화가 부른다 (`WM_DPICHANGED`·`WM_DISPLAYCHANGE`).
        //    덮을 자리를 정하는 것은 배율도 옛 사각형도 아닌 **모니터**라, 배율
        //    변화에서 OS가 제안하는 사각형(지금 창을 새 배율로 곱한 것)은 전체
        //    화면 창에 쓸 수 없다 (win32_window.cpp).
        bool reapply_fullscreen_bounds();
        // 전체 화면에 들어가기 전에 갈무리해 둔 배치다.
        // **비어 있다는 것이 곧 "전체 화면이 아니다"이다** — 앱에 알릴 배치를 고르는
        // `placement_to_report`가 이 값을 그대로 받는다.
        [[nodiscard]] std::optional<window_placement> placement_before_fullscreen() const noexcept;

        // caption 버튼의 hover는 비클라이언트 메시지로만 도착하므로
        // UI thread가 따로 추적한다.
        void update_caption_hover(caption_button_hover hover) noexcept;

        // caption chrome이 맡는 메시지다 (비클라이언트 판정·hover·버튼·
        // 크기 한계·시스템 메뉴·DWM 프레임).
        // 처리했으면 그 결과고, 아니면 nullopt라 호출자가 자기 처리를 이어 본다.
        [[nodiscard]] std::optional<LRESULT> handle_caption_message(UINT message, WPARAM word_parameter, LPARAM long_parameter);

        [[nodiscard]] static bool is_caption_button(WPARAM hit) noexcept;
        [[nodiscard]] static caption_button_hover caption_hover_from_hit(WPARAM hit) noexcept;

    protected:
        // 비클라이언트 캡션 버튼의 클릭이다.
        // 주 창은 tree에 등록된 액션으로, 보조 창은 그 창에 직접 실행한다
        // (multi-window-design.md).
        [[nodiscard]] virtual bool execute_caption_button(WPARAM hit) = 0;

        // 비클라이언트 hover를 상호작용 상태에 합친다.
        void prepare_frame(frame_state& state) override;

        // 주 창·보조 창은 설정을 따라 OS 파일 끌기를 받는다
        // (os-dragdrop-design.md — popup만 기본값 그대로 받지 않는다).
        [[nodiscard]] bool accepts_file_drop() const noexcept override
        {
            return context_.config().accept_file_drop;
        }

    private:
        void track_non_client_mouse_leave() noexcept;
        void show_system_menu(POINT position) const;
        // 이 창이 지금 있는 모니터를 덮는 사각형이다.
        // 모니터를 읽지 못하면 빈 값이라 부른 쪽이 아무것도 하지 않는다.
        //  - 들어갈 때와 다시 앉힐 때가 **같은 유도**를 본다. 둘이 갈리면 "전체
        //    화면은 rcMonitor를 덮는다"가 들어선 순간에만 참인 말이 된다.
        [[nodiscard]] std::optional<window_bounds> fullscreen_bounds_of_monitor() const noexcept;

        // 전체 화면에 들어가기 전의 창이다.
        // 나올 때 이 둘을 그대로 되돌리면 프레임과 자리가 제자리로 온다.
        //  - **창 스타일은 여기 없다.** 그것은 갈무리해 둘 값이 아니라 버튼 집합과
        //    모드에서 다시 나는 값이다 (`set_fullscreen`). 갈무리해 두면 전체 화면
        //    동안의 캡션 갱신보다 낡은 비트가 나올 때 되살아난다.
        struct saved_window_frame
        {
            window_style_bits extended_style { 0 };
            WINDOWPLACEMENT placement {};
        };

        caption_config caption_ {};
        // 전체 화면이면 값이 있고 아니면 비어 있다.
        // **곁에 `bool`을 두지 않는다** — 없음이 곧 상태다 (app_host.h의 "빈 값이 곧
        // 스위치"와 같은 규칙). 둘로 나누면 "전체 화면인데 되돌릴 것이 없는" 상태가
        // 표현 가능해지고, 그 상태로 빠지면 창은 영영 화면에 붙박인다.
        std::optional<saved_window_frame> before_fullscreen_ {};
        float minimum_client_width_ { 0.0f };
        float minimum_client_height_ { 0.0f };
        DWORD extended_style_ { 0 };
        caption_button_hover hovered_caption_button_ { caption_button_hover::none };
        std::chrono::steady_clock::time_point nc_hover_since_ {};
        bool tracking_non_client_mouse_ { false };
    };
} // namespace luil::win32
