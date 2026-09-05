#include "win32/caption_surface.h"

#include "win32/caption_layout.h"
#include "win32/utf8.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <utility>

namespace luil::win32 {
    LRESULT caption_hit_test(const HWND window, const LPARAM long_parameter, const std::uint32_t dpi, const caption_config& caption, const window_config& config)
    {
        LRESULT dwm_result { 0 };
        if (DwmDefWindowProc(window, WM_NCHITTEST, 0, long_parameter, &dwm_result))
            return dwm_result;

        RECT window_rectangle {};
        GetWindowRect(window, &window_rectangle);
        const int x { GET_X_LPARAM(long_parameter) - window_rectangle.left };
        const int y { GET_Y_LPARAM(long_parameter) - window_rectangle.top };
        const int width { window_rectangle.right - window_rectangle.left };
        const int height { window_rectangle.bottom - window_rectangle.top };
        const auto scaled = [dpi](const int value) { return MulDiv(value, static_cast<int>(dpi), 96); };

        if (IsZoomed(window) == FALSE)
        {
            // 시스템 기본 테두리(보통 8px)는 가장자리에
            // 붙은 스크롤 막대를 잡기 어렵게 한다.
            // 조절 두께를 좁히고 모서리만 넉넉히 둔다.
            const int border { std::max(1, scaled(config.resize_border_thickness)) };
            const int corner { std::max(border, scaled(config.resize_corner_thickness)) };
            const bool corner_left { x < corner };
            const bool corner_right { x >= width - corner };
            const bool corner_top { y < corner };
            const bool corner_bottom { y >= height - corner };
            if (corner_top && corner_left)
                return HTTOPLEFT;
            if (corner_top && corner_right)
                return HTTOPRIGHT;
            if (corner_bottom && corner_left)
                return HTBOTTOMLEFT;
            if (corner_bottom && corner_right)
                return HTBOTTOMRIGHT;
            if (x < border)
                return HTLEFT;
            if (x >= width - border)
                return HTRIGHT;
            if (y < border)
                return HTTOP;
            if (y >= height - border)
                return HTBOTTOM;
        }

        if (y >= 0 && y < scaled(caption.metrics.height) && x >= 0 && x < scaled(caption.metrics.application_icon_slot_width))
            return HTSYSMENU;
        const caption_layout layout { make_caption_layout(width, dpi, caption.metrics, caption.buttons) };
        switch (hit_test_caption(layout, x, y))
        {
        case caption_hit::drag:
            return HTCAPTION;
        case caption_hit::minimize:
            return HTMINBUTTON;
        case caption_hit::maximize:
            return HTMAXBUTTON;
        case caption_hit::close:
            return HTCLOSE;
        case caption_hit::client:
            return HTCLIENT;
        }
        return HTCLIENT;
    }

    void apply_size_limits(MINMAXINFO* const information, const HWND window, const std::uint32_t dpi, const DWORD style, const DWORD extended_style, const int minimum_client_width,
        const int minimum_client_height) noexcept
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

    void caption_surface::apply_dwm_frame(const HWND window) noexcept
    {
        const MARGINS margins { 0, 0, 1, 0 };
        DwmExtendFrameIntoClientArea(window, &margins);
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
        if (buttons_changed)
        {
            SetWindowLongPtrW(window_, GWL_STYLE, static_cast<LONG_PTR>(custom_window_style_for(caption_.buttons)));
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
            return caption_hit_test(window_, long_parameter, dpi_, caption_, context_.config());
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
        case WM_GETMINMAXINFO:
            apply_size_limits(reinterpret_cast<MINMAXINFO*>(long_parameter), window_, dpi_, custom_window_style_for(caption_.buttons), extended_style_,
                MulDiv(static_cast<int>(minimum_client_width_), static_cast<int>(dpi_), 96), MulDiv(static_cast<int>(minimum_client_height_), static_cast<int>(dpi_), 96));
            return LRESULT { 0 };
        case WM_DWMCOMPOSITIONCHANGED:
            apply_dwm_frame(window_);
            return LRESULT { 0 };
        default:
            return std::nullopt;
        }
    }

    void caption_surface::prepare_frame(frame_state& state)
    {
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
