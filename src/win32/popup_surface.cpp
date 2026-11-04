#include "win32/popup_surface.h"

#include "luil/theme/ui_theme.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace luil::win32 {
    namespace {
        // 화살표 칸 위에 머무는 동안 흘리는 timer다.
        //  - 3은 표면의 커서 숨김 timer다 (window_surface.cpp). 같은 창에 걸리므로 겹치지 않게 둔다.
        constexpr UINT_PTR strip_scroll_timer_id { 5 };
        constexpr UINT strip_scroll_interval_milliseconds { 40 };
        // 화살표 칸의 높이와 그 칸이 timer 한 번에 흘리는 양, 휠 한 칸이 흘리는 양이다 (논리 픽셀).
        //  - 휠 한 칸은 메뉴 항목 셋이다 (Windows의 기본 휠 줄 수와 같다).
        constexpr int strip_logical_height { 16 };
        constexpr int strip_logical_step { 8 };
        constexpr int wheel_logical_step { 78 };
    }

    popup_surface::popup_surface(surface_context& context, std::u8string id, std::u8string anchor) noexcept
        : window_surface { context, std::move(id) }
        , anchor_ { std::move(anchor) }
    {
    }

    LRESULT CALLBACK popup_surface::static_procedure(const HWND window, const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        auto* self { reinterpret_cast<popup_surface*>(GetWindowLongPtrW(window, GWLP_USERDATA)) };
        if (message == WM_NCCREATE)
        {
            const auto* creation { reinterpret_cast<const CREATESTRUCTW*>(long_parameter) };
            self = static_cast<popup_surface*>(creation->lpCreateParams);
            self->attach_window(window);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self != nullptr)
            return self->procedure(message, word_parameter, long_parameter);
        return DefWindowProcW(window, message, word_parameter, long_parameter);
    }

    LRESULT popup_surface::procedure(const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        if (const std::optional<LRESULT> scrolled { handle_scroll_message(message, word_parameter, long_parameter) }; scrolled.has_value())
            return *scrolled;
        switch (message)
        {
        case WM_MOUSEACTIVATE:
            // popup은 초점을 뺏지 않는다.
            return MA_NOACTIVATE;
        case WM_DESTROY:
            // 소유 관계로 주 창과 함께 파괴되는 경로다.
            // 표면 자체는 남고 소유자가 다음 대조에서 걷어낸다 —
            // 자기 procedure 안에서 자기를 지우면 돌아올 자리가 없다.
            detach_window();
            return 0;
        default:
            break;
        }
        // popup의 입력은 popup id를 표식으로 달고 같은 pump로 합류한다.
        if (const std::optional<LRESULT> handled { handle_surface_message(message, word_parameter, long_parameter) }; handled.has_value())
            return *handled;
        return DefWindowProcW(window_, message, word_parameter, long_parameter);
    }

    popup_placement popup_surface::placement() const
    {
        return popup_placement { id_, x_, y_, width_, height_, anchor_, shape_.rounded() };
    }

    void popup_surface::set_placement(const popup_placement& placement) noexcept
    {
        x_ = placement.x;
        y_ = placement.y;
        width_ = placement.width;
        height_ = placement.height;
        scroll_ = std::clamp(scroll_, 0, maximum_scroll());
    }

    void popup_surface::set_viewport_height(const int height) noexcept
    {
        viewport_height_ = height;
        scroll_ = std::clamp(scroll_, 0, maximum_scroll());
        if (scrollable() == false)
            stop_strip_scroll();
    }

    bool popup_surface::scrollable() const noexcept
    {
        return viewport_height_ > 0 && height_ > viewport_height_;
    }

    int popup_surface::maximum_scroll() const noexcept
    {
        return scrollable() ? height_ - viewport_height_ : 0;
    }

    int popup_surface::strip_height() const noexcept
    {
        return MulDiv(strip_logical_height, static_cast<int>(dpi_), 96);
    }

    int popup_surface::strip_at(const int y) const noexcept
    {
        if (scrollable() == false)
            return 0;
        const int strip { strip_height() };
        if (scroll_ > 0 && y < strip)
            return -1;
        if (scroll_ < maximum_scroll() && y >= viewport_height() - strip)
            return 1;
        return 0;
    }

    bool popup_surface::scroll_by(const int delta)
    {
        const int next { std::clamp(scroll_ + delta, 0, maximum_scroll()) };
        if (next == scroll_)
            return false;
        scroll_ = next;
        if (window_ == nullptr)
            return true;
        InvalidateRect(window_, nullptr, FALSE);
        // 포인터가 그대로여도 그 밑의 내용이 바뀌었다. 같은 자리의 이동을 다시 알려 hover가 새 항목에 선다.
        //  - 칸 위면 알리지 않는다. 칸 밑의 항목은 가려져 있다.
        POINT cursor {};
        if (GetCursorPos(&cursor) != FALSE && WindowFromPoint(cursor) == window_ && ScreenToClient(window_, &cursor) != FALSE && strip_at(cursor.y) == 0)
            static_cast<void>(post_pointer_message(WM_MOUSEMOVE, 0, MAKELPARAM(cursor.x, cursor.y)));
        return true;
    }

    void popup_surface::stop_strip_scroll() noexcept
    {
        if (strip_direction_ == 0)
            return;
        strip_direction_ = 0;
        if (window_ != nullptr)
            KillTimer(window_, strip_scroll_timer_id);
    }

    std::optional<LRESULT> popup_surface::handle_scroll_message(const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        if (message == WM_TIMER && word_parameter == strip_scroll_timer_id)
        {
            // 칸을 떠났거나 끝에 닿았으면 멈춘다.
            POINT cursor {};
            const bool over { GetCursorPos(&cursor) != FALSE && WindowFromPoint(cursor) == window_ && ScreenToClient(window_, &cursor) != FALSE };
            const int direction { strip_direction_ };
            if (over == false || strip_at(cursor.y) != direction || scroll_by(direction * MulDiv(strip_logical_step, static_cast<int>(dpi_), 96)) == false)
                stop_strip_scroll();
            return LRESULT { 0 };
        }
        if (message == WM_MOUSELEAVE)
        {
            stop_strip_scroll();
            return std::nullopt;
        }
        if (scrollable() == false)
            return std::nullopt;
        switch (message)
        {
        case WM_MOUSEWHEEL: {
            // 메뉴 자체는 휠을 쓰지 않는다. 흘릴 것이 남은 쪽이면 popup이 흘리고, 끝이면 지금처럼 tree에 간다.
            const int notches { GET_WHEEL_DELTA_WPARAM(word_parameter) };
            const int delta { -MulDiv(notches, MulDiv(wheel_logical_step, static_cast<int>(dpi_), 96), WHEEL_DELTA) };
            if (scroll_by(delta))
                return LRESULT { 0 };
            return std::nullopt;
        }
        case WM_MOUSEMOVE: {
            const int direction { strip_at(GET_Y_LPARAM(long_parameter)) };
            if (direction == 0)
            {
                stop_strip_scroll();
                return std::nullopt;
            }
            // 칸 위다. 그 쪽으로 흘리기 시작하고, 칸 밑에 가려진 항목의 hover는 거둔다.
            track_mouse_leave();
            if (strip_direction_ != direction)
            {
                strip_direction_ = direction;
                SetTimer(window_, strip_scroll_timer_id, strip_scroll_interval_milliseconds, nullptr);
            }
            if (app_host* const host { context_.host() }; host != nullptr)
                host->post_raw_input(pointer_left_event { id_ });
            return LRESULT { 0 };
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_RBUTTONUP: {
            // 칸 위의 누름은 칸의 것이다 — 가려진 항목을 고르지 않는다. 누르면 한 칸 흘린다.
            const int direction { strip_at(GET_Y_LPARAM(long_parameter)) };
            if (direction == 0)
                return std::nullopt;
            if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK)
                static_cast<void>(scroll_by(direction * MulDiv(strip_logical_height, static_cast<int>(dpi_), 96)));
            return LRESULT { 0 };
        }
        default:
            return std::nullopt;
        }
    }

    void popup_surface::follow_keyboard(const interaction_snapshot& interaction)
    {
        ui_element_id target { interaction.menu_highlight };
        if (target == ui_element_id {} && interaction.focus_visible)
            target = interaction.focused;
        if (target == followed_)
            return;
        followed_ = target;
        // 포인터가 고른 강조는 이미 보이는 자리다. 따라가면 포인터 밑의 내용이 움직인다.
        if (target == ui_element_id {} || target == interaction.hovered || tree_ == nullptr)
            return;
        const ui_element* const element { tree_->find(target) };
        if (element == nullptr)
            return;
        // 항목 위아래에 칸 높이만큼 여유를 둔다 — 칸 밑에 가리지 않게.
        const rect_f box { element->bounds() };
        const int strip { strip_height() };
        const int top { static_cast<int>(std::floor(box.y)) - strip };
        const int bottom { static_cast<int>(std::ceil(box.y + box.height)) + strip };
        int next { scroll_ };
        if (top < next)
            next = top;
        else if (bottom > next + viewport_height())
            next = bottom - viewport_height();
        scroll_ = std::clamp(next, 0, maximum_scroll());
    }

    bool popup_surface::adopt(const ui_popup& source, const popup_shape& shape)
    {
        dismiss_ = source.dismiss;
        dismiss_requested_ = false;
        // 효과는 소유자가 OS 사정과 함께 맞춘다 (`refresh_backdrop`). 창을 다시 만들 일은 없다 — 표시 방식이 그대로다.
        request_backdrop(source.backdrop);
        // 경계·모서리가 바뀐 것도 내용이 바뀐 것이다 — 다시 그려야 화면에 닿는다.
        const bool shape_changed { shape_ != shape };
        const bool corner_changed { shape_.corner != shape.corner };
        shape_ = shape;
        if (corner_changed)
            apply_corner();
        if (shape_changed)
            dwm_theme_.reset();
        return set_tree(source.tree) || shape_changed;
    }

    void popup_surface::apply_corner() noexcept
    {
        if (window_ == nullptr)
            return;
        DWM_WINDOW_CORNER_PREFERENCE preference { DWMWCP_DONOTROUND };
        if (shape_.corner == popup_corner::round)
            preference = DWMWCP_ROUND;
        else if (shape_.corner == popup_corner::round_small)
            preference = DWMWCP_ROUNDSMALL;
        // 직각 창도 걸어 둔다 — Windows 11이 popup을 스스로 둥글게 하는 판이 와도 직각을 고른 말풍선이 그대로다.
        // 모서리 선택이 없는 OS는 실패를 돌려준다. 그때 둥근 모양이었으면 직각의 경계(1px 선)로 물러선다 — 창 그림자는
        // 이미 없는 클래스라 선이 경계를 맡는다.
        const HRESULT result { DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference)) };
        if (FAILED(result) && shape_.rounded())
        {
            shape_.line = shape_.system_border || shape_.line;
            shape_.system_border = false;
            shape_.corner = popup_corner::square;
        }
    }

    void popup_surface::prepare_frame(frame_state& state)
    {
        state.border = shape_.line;
        state.popup = true;
        // 내용이 창보다 길면 흘린 만큼 위로 옮겨 그리고 화살표 칸을 얹는다.
        if (scrollable())
        {
            follow_keyboard(state.interaction);
            state.origin_y -= scroll_;
            state.scroll_up_strip = scroll_ > 0 ? strip_height() : 0;
            state.scroll_down_strip = scroll_ < maximum_scroll() ? strip_height() : 0;
        }
        if (window_ == nullptr)
            return;
        // 둥근 창의 OS 테두리를 팔레트에 맞춘다. 끈 창과 직각 창은 OS 테두리를 없앤다 — 직각 창의 경계는 표면의 선이라
        // OS 선이 겹치면 두 겹이 된다. 고대비는 OS의 색이 맞다 (`DWMWA_COLOR_DEFAULT`).
        const ui_color_palette palette { frame_palette(state) };
        COLORREF color { DWMWA_COLOR_NONE };
        if (shape_.system_border)
        {
            if (state.theme == color_theme::high_contrast)
                color = DWMWA_COLOR_DEFAULT;
            else
            {
                // DWM은 알파를 받지 않으므로 창 바탕 위에 얹은 불투명 결과를 보낸다 (주 창 테두리와 같은 규칙).
                const auto channel = [](const ui_color value, const int shift) { return static_cast<float>((value >> shift) & 0xFFu); };
                const float alpha { channel(palette.tooltip_border, 24) / 255.0f };
                const auto blend
                    = [&](const int shift) { return static_cast<BYTE>(channel(palette.window_background, shift) * (1.0f - alpha) + channel(palette.tooltip_border, shift) * alpha + 0.5f); };
                color = RGB(blend(16), blend(8), blend(0));
            }
        }
        // 어두운 바탕이면 그림자와 테두리도 어두운 모드의 것이다. 명암은 테마 이름이 아니라 바탕의 밝기가 정한다.
        const BOOL dark { is_dark_background(palette.window_background) ? TRUE : FALSE };
        // accent acrylic의 색 막도 같은 명암을 따른다. 효과가 없으면 아무것도 하지 않는다.
        set_backdrop_dark(dark != FALSE);
        if (dwm_theme_.has_value() && dwm_theme_->first == color && dwm_theme_->second == dark)
            return;
        dwm_theme_ = std::pair { color, dark };
        static_cast<void>(DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)));
        // 지원하지 않는 OS에서는 실패하고 아무 일도 없다 — 그 OS의 popup은 직각이라 표면의 선이 경계다.
        static_cast<void>(DwmSetWindowAttribute(window_, DWMWA_BORDER_COLOR, &color, sizeof(color)));
    }

    std::optional<input_action> popup_surface::take_dismiss_action(const popup_dismiss_reason reason)
    {
        return take_popup_dismiss_action(dismiss_, dismiss_requested_, reason);
    }

    void popup_surface::commit_composition_before_press()
    {
        // 앵커 칸에서 조합하다 popup의 후보를 눌러도 같다 — 확정 글이 먼저 가야 고른 후보가 그 위에 선다.
        const window_surface* const anchor { context_.surface_by_id(anchor_) };
        tsf_input_session* const session { anchor != nullptr ? anchor->text_input() : nullptr };
        if (session != nullptr && session->composing())
            session->commit_composition();
    }
}
