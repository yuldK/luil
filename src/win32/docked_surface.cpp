#include "win32/docked_surface.h"

#include "win32/utf8.h"

#include <shellscalingapi.h>

#include <utility>

namespace luil::win32 {
    namespace {
        [[nodiscard]] screen_rect screen_rect_of(const RECT& rectangle) noexcept
        {
            return screen_rect { rectangle.left, rectangle.top, rectangle.right, rectangle.bottom };
        }

        BOOL CALLBACK collect_display(const HMONITOR monitor, HDC, LPRECT, const LPARAM context)
        {
            auto* const displays { reinterpret_cast<std::vector<display_info>*>(context) };
            MONITORINFOEXW information {};
            information.cbSize = sizeof(information);
            if (GetMonitorInfoW(monitor, &information) == FALSE)
                return TRUE;
            display_info display {};
            if (auto converted { utf16_to_utf8(information.szDevice) }; converted.value.has_value())
                display.id = std::move(*converted.value);
            display.bounds = screen_rect_of(information.rcMonitor);
            display.work_area = screen_rect_of(information.rcWork);
            display.primary = (information.dwFlags & MONITORINFOF_PRIMARY) != 0;
            UINT dpi_x { 96 };
            UINT dpi_y { 96 };
            if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)))
                display.scale = static_cast<float>(dpi_x) / 96.0f;
            displays->push_back(std::move(display));
            return TRUE;
        }
    }

    DWORD docked_window_style() noexcept
    {
        return WS_POPUP;
    }

    DWORD docked_extended_style() noexcept
    {
        return WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
    }

    std::vector<display_info> enumerate_displays()
    {
        std::vector<display_info> displays {};
        if (EnumDisplayMonitors(nullptr, nullptr, &collect_display, reinterpret_cast<LPARAM>(&displays)) == FALSE)
            return {};
        return displays;
    }

    docked_surface::docked_surface(surface_context& context, std::u8string id) noexcept
        : window_surface { context, std::move(id) }
    {
    }

    LRESULT CALLBACK docked_surface::static_procedure(const HWND window, const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        auto* self { reinterpret_cast<docked_surface*>(GetWindowLongPtrW(window, GWLP_USERDATA)) };
        if (message == WM_NCCREATE)
        {
            const auto* creation { reinterpret_cast<const CREATESTRUCTW*>(long_parameter) };
            self = static_cast<docked_surface*>(creation->lpCreateParams);
            self->attach_window(window);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self != nullptr)
            return self->procedure(message, word_parameter, long_parameter);
        return DefWindowProcW(window, message, word_parameter, long_parameter);
    }

    LRESULT docked_surface::procedure(const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        switch (message)
        {
        case WM_MOUSEACTIVATE:
            // 눌러도 활성화되지 않는다. 다른 앱이 쥔 앞 창과 키보드를 빼앗지 않는다 (드래그 중의 원본 앱도 그대로다).
            return MA_NOACTIVATE;
        case WM_ACTIVATE:
            // 잠깐 활성화했던 표면이 다시 비활성이 됐다 (사용자가 다른 곳을 눌렀다). 그 사이 연 메뉴는 이 계기로 닫는다.
            //  - 활성이 **될** 때는 닫지 않는다. 메뉴를 열며 활성화를 함께 청한 앱의 메뉴가 서자마자 닫힌다.
            if (LOWORD(word_parameter) == WA_INACTIVE)
                static_cast<void>(context_.dismiss_popups(popup_dismiss_reason::activation_changed));
            break;
        case WM_DPICHANGED:
            // **제안 사각형을 쓰지 않는다.** 자리와 크기는 앱의 것이다 — 가장자리 자리는 새 배율로 소유자가 다시 풀고, 물리
            // 사각형은 그대로다. 배율이 바뀐 것만 알린다 (크기가 그대로면 WM_SIZE가 오지 않는다).
            set_dpi(HIWORD(word_parameter));
            post_metrics();
            context_.displays_changed();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            // 모니터·작업 영역 변화는 주 창도 받는다. 주 창이 그 모니터에 없어도 이 표면이 받으므로 함께 알린다 (견주어 거른다).
            context_.displays_changed();
            break;
        case WM_GETMINMAXINFO: {
            // 몇 픽셀 두께의 띠도 된다. 기본 처리가 채운 최소 추적 크기를 거둔다.
            const LRESULT result { DefWindowProcW(window_, message, word_parameter, long_parameter) };
            auto* const information { reinterpret_cast<MINMAXINFO*>(long_parameter) };
            information->ptMinTrackSize = POINT { 1, 1 };
            return result;
        }
        case WM_MOVE:
            // 이 표면에 붙은 popup이 따라 옮겨진다.
            //  - **닫힘 계기로 알리지 않는다.** 도킹 표면의 이동·크기 변경은 사용자의 손이 아니라 앱이 frame에 실은 결정이다.
            //    보조 창처럼 `surface_moved`·`surface_resized`로 알리면, 위젯을 늘리며 메뉴를 연 앱의 메뉴가 그 자리에서 닫히고
            //    다른 창의 popup까지 함께 닫힌다. 닫을지는 앱이 같은 frame에서 정한다.
            context_.surface_scrolled(id_);
            return 0;
        case WM_SIZE:
            if (word_parameter != SIZE_MINIMIZED)
            {
                std::u8string error {};
                if (resize_renderer(static_cast<int>(LOWORD(long_parameter)), static_cast<int>(HIWORD(long_parameter)), error) == false)
                    context_.report_error(error);
                post_metrics();
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_CLOSE:
            // 활성화된 동안의 Alt+F4다. 표면을 없애는 것은 앱이 목록에서 빼는 것뿐이다.
            return 0;
        case WM_DESTROY:
            detach_window();
            return 0;
        default:
            break;
        }

        if (const std::optional<LRESULT> handled { handle_surface_message(message, word_parameter, long_parameter) }; handled.has_value())
            return *handled;
        return DefWindowProcW(window_, message, word_parameter, long_parameter);
    }

    bool docked_surface::adopt(const ui_docked_surface& source)
    {
        metrics_ = source.metrics;
        if (source.title != title_)
        {
            title_ = source.title;
            if (window_ != nullptr)
                if (const auto converted { utf8_to_utf16(title_) }; converted.value.has_value())
                    SetWindowTextW(window_, converted.value->c_str());
        }
        return set_tree(source.tree);
    }

    void docked_surface::apply_bounds(const std::optional<screen_rect>& bounds)
    {
        if (window_ == nullptr || bounds == bounds_)
            return;
        bounds_ = bounds;
        if (bounds.has_value() == false)
        {
            ShowWindow(window_, SW_HIDE);
            return;
        }
        SetWindowPos(window_, HWND_TOPMOST, bounds->left, bounds->top, bounds->width(), bounds->height(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    void docked_surface::post_metrics() const noexcept
    {
        app_host* const host { context_.host() };
        if (host == nullptr || metrics_ == nullptr || window_ == nullptr)
            return;
        RECT client {};
        if (GetClientRect(window_, &client) == FALSE)
            return;
        const float scale { static_cast<float>(dpi_) / 96.0f };
        // 앱의 factory는 noexcept가 아니다. 창 메시지 처리 밖으로 새지 않게 여기서 거둔다.
        try
        {
            if (app_message message { metrics_(static_cast<float>(client.right - client.left), static_cast<float>(client.bottom - client.top), scale) }; message.empty() == false)
                host->post_app_message(std::move(message));
        }
        catch (...)
        {
            context_.report_error(u8"The docked surface metrics factory threw an exception.");
        }
    }

    void docked_surface::prepare_frame(frame_state& state)
    {
        context_.schedule_update_repaint(id_, state.interaction);
    }
}
