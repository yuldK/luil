#include "win32/caption_surface.h"

#include "win32/utf8.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <optional>
#include <utility>

namespace luil::win32 {
    namespace {
        void update_custom_window_style(const HWND window, const caption_buttons& buttons, const window_display_mode mode)
        {
            const auto current { static_cast<window_style_bits>(GetWindowLongPtrW(window, GWL_STYLE)) };
            SetWindowLongPtrW(window, GWL_STYLE, static_cast<LONG_PTR>(updated_custom_window_style(current, buttons, mode)));
        }
    } // namespace

    LRESULT caption_hit_test(
        const HWND window, const LPARAM long_parameter, const std::uint32_t dpi, const caption_config& caption, const window_config& config, const window_display_mode mode)
    {
        // 전체 화면에서는 DWM에게도 묻지 않는다.
        // 시스템 캡션 버튼을 대신 판정해 주는 자리라 `HTMAXBUTTON`을 돌려줄 수 있고,
        // 그러면 "어디를 눌러도 client"라는 답이 그 한 지점에서만 깨진다.
        if (mode == window_display_mode::fullscreen)
            return HTCLIENT;

        LRESULT dwm_result { 0 };
        if (DwmDefWindowProc(window, WM_NCHITTEST, 0, long_parameter, &dwm_result))
            return dwm_result;

        RECT window_rectangle {};
        GetWindowRect(window, &window_rectangle);
        const window_frame_metrics metrics {
            caption.metrics,
            caption.buttons,
            config.resize_border_thickness,
            config.resize_corner_thickness,
        };
        const window_hit hit {
            hit_test_window(metrics, mode, window_rectangle.right - window_rectangle.left, window_rectangle.bottom - window_rectangle.top, dpi,
                GET_X_LPARAM(long_parameter) - window_rectangle.left, GET_Y_LPARAM(long_parameter) - window_rectangle.top),
        };
        switch (hit)
        {
        case window_hit::resize_top_left:
            return HTTOPLEFT;
        case window_hit::resize_top_right:
            return HTTOPRIGHT;
        case window_hit::resize_bottom_left:
            return HTBOTTOMLEFT;
        case window_hit::resize_bottom_right:
            return HTBOTTOMRIGHT;
        case window_hit::resize_left:
            return HTLEFT;
        case window_hit::resize_right:
            return HTRIGHT;
        case window_hit::resize_top:
            return HTTOP;
        case window_hit::resize_bottom:
            return HTBOTTOM;
        case window_hit::system_menu:
            return HTSYSMENU;
        case window_hit::caption_drag:
            return HTCAPTION;
        case window_hit::minimize_button:
            return HTMINBUTTON;
        case window_hit::maximize_button:
            return HTMAXBUTTON;
        case window_hit::close_button:
            return HTCLOSE;
        case window_hit::client:
            break;
        }
        return HTCLIENT;
    }

    void apply_size_limits(MINMAXINFO* const information, const HWND window, const std::uint32_t dpi, const DWORD style, const DWORD extended_style, const int minimum_client_width,
        const int minimum_client_height, const window_display_mode mode) noexcept
    {
        RECT minimum { 0, 0, minimum_client_width, minimum_client_height };
        if (AdjustWindowRectExForDpi(&minimum, style, FALSE, extended_style, dpi) != FALSE)
        {
            information->ptMinTrackSize.x = minimum.right - minimum.left;
            information->ptMinTrackSize.y = minimum.bottom - minimum.top;
        }
        else
        {
            information->ptMinTrackSize.x = minimum_client_width;
            information->ptMinTrackSize.y = minimum_client_height;
        }

        // 전체 화면은 최대화 크기를 손대지 않는다.
        // 목표가 rcMonitor인데 그 한계가 rcWork로 잘려 있으면, OS가 창을 작업 표시줄
        // 위에서 멈춰 세워 모니터를 덮지 못한다.
        if (maximum_size_follows_work_area(mode) == false)
            return;

        const HMONITOR monitor { MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) };
        MONITORINFO monitor_information {};
        monitor_information.cbSize = sizeof(monitor_information);
        if (GetMonitorInfoW(monitor, &monitor_information) == FALSE)
            return;
        const RECT work { monitor_information.rcWork };
        const RECT monitor_bounds { monitor_information.rcMonitor };
        information->ptMaxPosition.x = work.left - monitor_bounds.left;
        information->ptMaxPosition.y = work.top - monitor_bounds.top;
        information->ptMaxSize.x = work.right - work.left;
        information->ptMaxSize.y = work.bottom - work.top;
    }

    caption_surface::caption_surface(surface_context& context, std::u8string id) noexcept
        : window_surface { context, std::move(id) }
    {}

    bool caption_surface::remove_system_caption(const HWND window, std::u8string& error)
    {
        const LONG_PTR current_style { GetWindowLongPtrW(window, GWL_STYLE) };
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previous_style {
            SetWindowLongPtrW(window, GWL_STYLE, current_style & ~static_cast<LONG_PTR>(WS_CAPTION)),
        };

        if (previous_style == 0 && GetLastError() != ERROR_SUCCESS)
        {
            error = u8"Failed to remove the Win32 system caption.";
            return false;
        }

        if (SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER) == FALSE)
        {
            error = u8"Failed to recalculate the Win32 custom frame.";
            return false;
        }
        return true;
    }

    void caption_surface::apply_dwm_frame(const HWND window, const window_display_mode mode) noexcept
    {
        const MARGINS margins { 0, 0, dwm_frame_top_margin_for(mode), 0 };
        DwmExtendFrameIntoClientArea(window, &margins);
    }

    window_display_mode caption_surface::display_mode() const noexcept
    {
        return window_mode_of(window_ != nullptr && IsZoomed(window_) != FALSE, fullscreen());
    }

    std::optional<window_placement> caption_surface::placement_before_fullscreen() const noexcept
    {
        if (before_fullscreen_.has_value() == false)
            return std::nullopt;
        const RECT& normal { before_fullscreen_->placement.rcNormalPosition };
        return window_placement {
            normal.left,
            normal.top,
            normal.right - normal.left,
            normal.bottom - normal.top,
            before_fullscreen_->placement.showCmd == static_cast<UINT>(SW_SHOWMAXIMIZED),
            true,
        };
    }

    std::optional<window_bounds> caption_surface::fullscreen_bounds_of_monitor() const noexcept
    {
        if (window_ == nullptr)
            return std::nullopt;
        // 덮을 모니터는 창이 지금 있는 그것이다.
        MONITORINFO information {};
        information.cbSize = sizeof(information);
        if (GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &information) == FALSE)
            return std::nullopt;
        const screen_area monitor { information.rcMonitor.left, information.rcMonitor.top, information.rcMonitor.right, information.rcMonitor.bottom };
        return fullscreen_bounds_for(monitor);
    }

    bool caption_surface::reapply_fullscreen_bounds()
    {
        if (fullscreen() == false)
            return false;
        const std::optional<window_bounds> bounds { fullscreen_bounds_of_monitor() };
        if (bounds.has_value() == false)
            return false;
        // 스타일과 갈무리는 그대로다 — 여기서 달라진 것은 **모니터 사각형뿐**이다.
        SetWindowPos(window_, nullptr, bounds->x, bounds->y, bounds->width, bounds->height, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        return true;
    }

    bool caption_surface::set_fullscreen(const bool enter)
    {
        if (window_ == nullptr || enter == before_fullscreen_.has_value())
            return false;

        if (enter == false)
        {
            // 갈무리를 **먼저 놓는다.** 아래의 `SetWindowPlacement`가 동기로 보내는
            // `WM_SIZE`가 이미 "전체 화면이 아닌" 창을 보아야, 그 자리에서 나가는
            // 배치를 알릴 때 돌아갈 자리가 아니라 지금 자리가 실린다.
            const saved_window_frame saved { *before_fullscreen_ };
            before_fullscreen_.reset();
            // 스타일은 되돌리는 것이 아니라 **다시 계산한다.** 들어갈 때가 버튼
            // 집합과 모드를 함께 받는 한 계산이므로 나올 때도 같은 계산이어야
            // 한다 — 두 방향이 다른 식을 보면 그 사이에 생긴 변화가 한쪽에서만
            // 산다. 전체 화면인 동안 앱이 최대화 버튼을 뺐다면(전체 화면 앱이
            // 흔히 하는 일이고 `set_caption`이 그 자리에서 스타일을 다시 쓴다)
            // 갈무리는 그보다 낡아 있고, 그것을 그대로 넣으면 `WS_MAXIMIZEBOX`가
            // 되살아나 캡션은 최대화 버튼을 그리지 않는데 Win+↑·캡션 더블클릭·
            // 시스템 메뉴의 최대화만 사는 창이 된다 — "버튼을 빼면 그 시스템
            // 기능도 함께 뺀다"가 깨지는 자리다 (caption-button-design.md).
            // `set_caption`은 버튼 집합이 **바뀔 때만** 스타일을 다시 쓰므로 그
            // 어긋남은 저절로 지워지지도 않는다.
            //  - 다시 계산한 값에는 `WS_MAXIMIZE`가 없다는 것이 나올 때의 자리를
            //    결정적으로 만든다. 최대화된 창에서 들어가면 그 표식은 전체 화면
            //    내내 남고(자리를 직접 준 `SetWindowPos`는 지우지 않는다), 남은
            //    채로 아래의 `SetWindowPlacement`에 `SW_SHOWMAXIMIZED`를 주면 OS는
            //    이미 최대화된 창으로 보아 사각형을 다시 세우지 않을 수 있다 —
            //    창이 화면을 덮은 크기 그대로 앉는다. 표식을 먼저 지워 OS가 반드시
            //    다시 세우게 한다 (window_mode.h의 `style_maximize`).
            update_custom_window_style(window_, caption_.buttons, window_display_mode::normal);
            // 확장 스타일과 배치는 갈무리한 것을 그대로 되돌린다.
            // 그 둘은 우리가 계산하는 값이 아니라 창이 들어오기 전에 갖고 있던 것이다.
            SetWindowLongPtrW(window_, GWL_EXSTYLE, static_cast<LONG_PTR>(saved.extended_style));
            apply_dwm_frame(window_, display_mode());
            // 자리와 `showCmd`를 함께 되돌린다 — 최대화된 창에서 들어갔으면
            // 최대화된 창으로 나온다.
            WINDOWPLACEMENT placement { saved.placement };
            static_cast<void>(SetWindowPlacement(window_, &placement));
            SetWindowPos(window_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return true;
        }

        // 모니터를 **먼저** 읽는다. 아래 갈무리와 마찬가지로, 읽지 못하면
        // 아무것도 건드리지 않고 들어가지 않는다.
        const std::optional<window_bounds> bounds { fullscreen_bounds_of_monitor() };
        if (bounds.has_value() == false)
            return false;
        saved_window_frame saved {};
        saved.placement.length = sizeof(saved.placement);
        // 갈무리에 실패하면 **들어가지 않는다.** 나올 자리를 모르는 전체 화면은
        // 사용자가 창을 되찾을 길이 없는 상태다.
        if (GetWindowPlacement(window_, &saved.placement) == FALSE)
            return false;
        saved.extended_style = static_cast<window_style_bits>(GetWindowLongPtrW(window_, GWL_EXSTYLE));

        // 상태를 **스타일보다 먼저** 세운다. 아래 호출들이 `display_mode()`로 지금
        // 모습을 되묻고, `SetWindowPos`가 동기로 보내는 `WM_SIZE`·`WM_NCCALCSIZE`도
        // 이미 전체 화면인 창을 보아야 한다.
        before_fullscreen_ = saved;
        const window_display_mode mode { window_display_mode::fullscreen };
        update_custom_window_style(window_, caption_.buttons, mode);
        SetWindowLongPtrW(window_, GWL_EXSTYLE, static_cast<LONG_PTR>(extended_window_style_for(saved.extended_style, mode)));
        apply_dwm_frame(window_, mode);
        SetWindowPos(window_, nullptr, bounds->x, bounds->y, bounds->width, bounds->height, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        return true;
    }

    void caption_surface::set_caption(const caption_config& caption)
    {
        // OS를 부를 일이 있는지는 **바꾸기 전에** 가린다.
        const bool buttons_changed { (caption_.buttons == caption.buttons) == false };
        const bool title_changed { caption_.title != caption.title };
        caption_ = caption;
        // 창을 만들기 전이면 값만 싣는다 (생성 인자가 스타일과 제목을 맡는다).
        if (window_ == nullptr || (buttons_changed == false && title_changed == false))
            return;

        // 버튼을 빼면 그 시스템 기능도 함께 뺀다는 규칙이 창 스타일에 있다.
        // 시스템 캡션은 이미 떼었으므로 그것을 뺀 스타일로 다시 세운다 —
        // `remove_system_caption`이 남긴 것과 같은 값이어야 프레임 두께가 맞는다.
        // 현재 모드로 프레임 비트만 다시 계산하고 표시·최소화 상태는 보존한다.
        if (buttons_changed)
        {
            update_custom_window_style(window_, caption_.buttons, display_mode());
            SetWindowPos(window_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        }

        // 우리가 그리는 제목은 tree 안에 있지만 **OS 제목은 거기 없다.**
        // Alt+Tab과 작업 표시줄이 읽는 것이 이쪽이다.
        if (title_changed)
            if (const auto converted { utf8_to_utf16(caption_.title) }; converted.value.has_value())
                SetWindowTextW(window_, converted.value->c_str());
    }

    void caption_surface::set_minimum_client_size(const float width, const float height) noexcept
    {
        minimum_client_width_ = width;
        minimum_client_height_ = height;
    }

    void caption_surface::set_extended_style(const DWORD style) noexcept
    {
        extended_style_ = style;
    }

    std::optional<LRESULT> caption_surface::handle_caption_message(const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        switch (message)
        {
        case WM_NCCALCSIZE:
            // 비클라이언트를 0으로 만든다 (client == window rect).
            if (word_parameter != 0)
                return LRESULT { 0 };
            return std::nullopt;
        case WM_NCHITTEST:
            return caption_hit_test(window_, long_parameter, dpi_, caption_, context_.config(), display_mode());
        case WM_NCMOUSEMOVE:
            track_non_client_mouse_leave();
            update_caption_hover(caption_hover_from_hit(word_parameter));
            return std::nullopt;
        case WM_NCMOUSELEAVE:
            tracking_non_client_mouse_ = false;
            update_caption_hover(caption_button_hover::none);
            return std::nullopt;
        case WM_MOUSEMOVE:
            // client로 들어왔으면 비클라이언트 hover는 없다.
            // 표면 공통 번역이 이어 본다.
            update_caption_hover(caption_button_hover::none);
            return std::nullopt;
        case WM_MOVE:
            // 움직이는 top-level 창은 주 창과 보조 창뿐이라 여기에 둔다.
            // popup 자신이 이 경로를 타면 우리가 옮긴 것이 다시 옮기는 되돌이가 된다.
            context_.surface_moved(id_);
            return std::nullopt;
        case WM_NCLBUTTONDOWN:
            // 캡션·테두리를 잡는 것도 popup 밖 클릭이다.
            if (input_dismisses_popups())
                static_cast<void>(context_.dismiss_popups(popup_dismiss_reason::pointer_press_outside));
            if (is_caption_button(word_parameter))
                return LRESULT { 0 };
            return std::nullopt;
        case WM_NCLBUTTONUP:
            if (execute_caption_button(word_parameter))
                return LRESULT { 0 };
            return std::nullopt;
        case WM_NCRBUTTONUP:
            if (word_parameter == HTCAPTION || word_parameter == HTSYSMENU)
            {
                show_system_menu({ GET_X_LPARAM(long_parameter), GET_Y_LPARAM(long_parameter) });
                return LRESULT { 0 };
            }
            return std::nullopt;
        case WM_SYSKEYDOWN:
            // Alt+Space는 시스템 메뉴다 (bit 29가 Alt다).
            // 나머지 Alt 조합은 앱 단축키라 호출자가 이어 본다.
            if (word_parameter == VK_SPACE && (long_parameter & (1LL << 29)) != 0)
            {
                RECT window_rectangle {};
                GetWindowRect(window_, &window_rectangle);
                show_system_menu({ window_rectangle.left, window_rectangle.top + MulDiv(caption_.metrics.height, static_cast<int>(dpi_), 96) });
                return LRESULT { 0 };
            }
            return std::nullopt;
        case WM_GETMINMAXINFO: {
            const window_display_mode mode { display_mode() };
            apply_size_limits(reinterpret_cast<MINMAXINFO*>(long_parameter), window_, dpi_, custom_window_style_for(caption_.buttons, mode), extended_style_,
                MulDiv(static_cast<int>(minimum_client_width_), static_cast<int>(dpi_), 96), MulDiv(static_cast<int>(minimum_client_height_), static_cast<int>(dpi_), 96), mode);
            return LRESULT { 0 };
        }
        case WM_DWMCOMPOSITIONCHANGED:
            // 합성이 다시 서면 프레임도 다시 얹는다.
            // **지금 모드로** 얹어야 전체 화면 중의 재시작이 위쪽 한 줄을 되살리지 않는다.
            apply_dwm_frame(window_, display_mode());
            return LRESULT { 0 };
        default:
            return std::nullopt;
        }
    }

    void caption_surface::prepare_frame(frame_state& state)
    {
        // 창의 모습은 **한 값에서 함께** 난다.
        // `window_surface::render`가 `IsZoomed`로 세워 둔 최대화 표식을 여기서 다시
        // 쓰는 이유가 그것이다: 최대화된 창에서 전체 화면으로 들어가면 `WS_MAXIMIZE`가
        // 남아 둘이 함께 참이 되고, 그러면 캡션은 화면을 덮은 창에 "복원" 글리프를
        // 그린다. 무엇으로 그릴지는 모드가 정한다 (window_mode.h).
        const window_display_mode mode { display_mode() };
        state.maximized = mode == window_display_mode::maximized;
        state.fullscreen = mode == window_display_mode::fullscreen;

        // caption 버튼의 hover는 비클라이언트 메시지로만 도착하므로
        // UI thread 추적 값을 상호작용 상태에 합친다.
        if (hovered_caption_button_ != caption_button_hover::none)
        {
            state.interaction.hovered = caption_button_element_id(hovered_caption_button_);
            state.interaction.hover_started_at = nc_hover_since_;
            // **표식도 자기 id로 함께 세운다.** 이 hover는 표면 필터를 지난 뒤에
            // 얹히므로 표식이 비어 있으면 "주 창의 hover"라는 거짓말이 되고,
            // 이 상태를 다시 거르는 자리(보조 창의 `schedule_update_repaint`)에서
            // 방금 얹은 것이 도로 지워진다 (multi-window-design.md).
            state.interaction.hovered_surface = id_;
        }
    }

    bool caption_surface::is_caption_button(const WPARAM hit) noexcept
    {
        return hit == HTMINBUTTON || hit == HTMAXBUTTON || hit == HTCLOSE;
    }

    caption_button_hover caption_surface::caption_hover_from_hit(const WPARAM hit) noexcept
    {
        switch (hit)
        {
        case HTMINBUTTON:
            return caption_button_hover::minimize;
        case HTMAXBUTTON:
            return caption_button_hover::maximize;
        case HTCLOSE:
            return caption_button_hover::close;
        default:
            return caption_button_hover::none;
        }
    }

    void caption_surface::update_caption_hover(const caption_button_hover hover) noexcept
    {
        if (hovered_caption_button_ == hover)
            return;
        hovered_caption_button_ = hover;
        // caption tooltip 지연 판정의 기준 시각이다.
        nc_hover_since_ = std::chrono::steady_clock::now();
        InvalidateRect(window_, nullptr, FALSE);
    }

    void caption_surface::track_non_client_mouse_leave() noexcept
    {
        if (tracking_non_client_mouse_)
            return;
        TRACKMOUSEEVENT tracking {
            static_cast<DWORD>(sizeof(TRACKMOUSEEVENT)),
            TME_LEAVE | TME_NONCLIENT,
            window_,
            HOVER_DEFAULT,
        };
        tracking_non_client_mouse_ = TrackMouseEvent(&tracking) != FALSE;
    }

    void caption_surface::show_system_menu(const POINT position) const
    {
        const HMENU menu { GetSystemMenu(window_, FALSE) };
        if (menu == nullptr)
            return;

        const auto command { static_cast<WPARAM>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, position.x, position.y, 0, window_, nullptr)) };

        if (command != 0)
            PostMessageW(window_, WM_SYSCOMMAND, command, 0);
    }
} // namespace luil::win32
