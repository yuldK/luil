#include "win32/popup_surface.h"

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
        return popup_placement { id_, x_, y_, width_, height_, anchor_ };
    }

    void popup_surface::set_placement(const popup_placement& placement) noexcept
    {
        x_ = placement.x;
        y_ = placement.y;
        width_ = placement.width;
        height_ = placement.height;
    }

    bool popup_surface::adopt(const ui_popup& source)
    {
        dismiss_ = source.dismiss;
        dismiss_requested_ = false;
        // 테두리가 바뀐 것도 내용이 바뀐 것이다 — 다시 그려야 화면에 닿는다.
        const bool border_changed { border_ != source.border };
        border_ = source.border;
        return set_tree(source.tree) || border_changed;
    }

    std::optional<input_action> popup_surface::take_dismiss_action(const popup_dismiss_reason reason)
    {
        return take_popup_dismiss_action(dismiss_, dismiss_requested_, reason);
    }
} // namespace luil::win32
