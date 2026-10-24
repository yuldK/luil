#include "win32/popup_surface.h"

#include "luil/theme/ui_theme.h"

#include <dwmapi.h>

#include <utility>

namespace luil::win32 {
    popup_surface::popup_surface(surface_context& context, std::u8string id, std::u8string anchor) noexcept
        : window_surface { context, std::move(id) }
        , anchor_ { std::move(anchor) }
    {}

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
    }

    bool popup_surface::adopt(const ui_popup& source, const popup_shape& shape)
    {
        dismiss_ = source.dismiss;
        dismiss_requested_ = false;
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
} // namespace luil::win32
