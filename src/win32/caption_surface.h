#pragma once

#include "luil/ui/caption_element.h"
#include "win32/window_surface.h"

namespace luil::win32 {
    // custom caption 창의 스타일이다.
    // 시스템 캡션(`WS_CAPTION`)만 떼고 크기 조절·시스템 메뉴는 남긴다 —
    // 캡션을 직접 그리더라도 창 관리 기능은 OS의 것이어야 한다.
    inline constexpr DWORD retained_window_styles { WS_THICKFRAME | WS_SYSMENU };

    // 이 버튼 집합으로 만들 창 스타일이다.
    // 버튼을 빼면 그 **시스템 기능도 함께** 뺀다 — 캡션 더블클릭·Win+↑·시스템
    // 메뉴까지 한 뜻이 된다 (caption-button-design.md).
    //  - 크기 조절과 시스템 메뉴는 버튼과 무관하므로 언제나 남긴다.
    [[nodiscard]] constexpr DWORD window_style_for(const caption_buttons& buttons) noexcept
    {
        DWORD style { WS_OVERLAPPEDWINDOW };
        if (buttons.minimize == false)
            style &= ~static_cast<DWORD>(WS_MINIMIZEBOX);
        if (buttons.maximize == false)
            style &= ~static_cast<DWORD>(WS_MAXIMIZEBOX);
        return style;
    }

    // 시스템 캡션을 뗀 뒤의 스타일이다.
    // **만들 때 쓴 스타일과 크기를 계산할 때 쓴 스타일이 같아야** 프레임 두께가 맞는다.
    [[nodiscard]] constexpr DWORD custom_window_style_for(const caption_buttons& buttons) noexcept
    {
        return window_style_for(buttons) & ~static_cast<DWORD>(WS_CAPTION);
    }

    static_assert((custom_window_style_for({}) & WS_CAPTION) == 0);
    static_assert((custom_window_style_for({}) & retained_window_styles) == retained_window_styles);
    static_assert((window_style_for({}) & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) == (WS_MINIMIZEBOX | WS_MAXIMIZEBOX));
    static_assert((window_style_for({ .minimize = false, .maximize = false, .close = true }) & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) == 0);
    static_assert((window_style_for({ .minimize = false, .maximize = false, .close = true }) & retained_window_styles) == retained_window_styles);

    // custom caption 창의 비클라이언트 판정이다.
    // 크기 조절 테두리·모서리 → 시스템 메뉴 → 캡션 버튼 → 끌기 → client 순이다.
    [[nodiscard]] LRESULT caption_hit_test(HWND window, LPARAM long_parameter, std::uint32_t dpi, const caption_config& caption, const window_config& config);

    // 최소 크기와 최대화 크기를 함께 정한다.
    // 최소 크기는 client 기준 값을 창 크기로 바꿔 넣는다 (프레임 두께 포함).
    void apply_size_limits(MINMAXINFO* information, HWND window, std::uint32_t dpi, DWORD style, DWORD extended_style, int minimum_client_width, int minimum_client_height) noexcept;

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
        static void apply_dwm_frame(HWND window) noexcept;

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

        caption_config caption_ {};
        float minimum_client_width_ { 0.0f };
        float minimum_client_height_ { 0.0f };
        DWORD extended_style_ { 0 };
        caption_button_hover hovered_caption_button_ { caption_button_hover::none };
        std::chrono::steady_clock::time_point nc_hover_since_ {};
        bool tracking_non_client_mouse_ { false };
    };
} // namespace luil::win32
