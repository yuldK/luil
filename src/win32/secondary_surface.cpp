#include "win32/secondary_surface.h"

#include <algorithm>
#include <utility>

namespace luil::win32 {
    secondary_surface::secondary_surface(surface_context& context, std::u8string id) noexcept
        : caption_surface { context, std::move(id) }
    {}

    LRESULT CALLBACK secondary_surface::static_procedure(const HWND window, const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        auto* self { reinterpret_cast<secondary_surface*>(GetWindowLongPtrW(window, GWLP_USERDATA)) };
        if (message == WM_NCCREATE)
        {
            const auto* creation { reinterpret_cast<const CREATESTRUCTW*>(long_parameter) };
            self = static_cast<secondary_surface*>(creation->lpCreateParams);
            self->attach_window(window);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self != nullptr)
            return self->procedure(message, word_parameter, long_parameter);
        return DefWindowProcW(window, message, word_parameter, long_parameter);
    }

    LRESULT secondary_surface::procedure(const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        switch (message)
        {
        case WM_ACTIVATE:
            // 보조 창 사이를 오갈 때는 주 창이 이미 inactive라
            // 주 창의 WM_ACTIVATE가 오지 않는다. 활성 전환은 popup 밖
            // 상호작용이므로 여기서도 닫는다.
            if (LOWORD(word_parameter) != WA_INACTIVE)
                static_cast<void>(context_.dismiss_popups(popup_dismiss_reason::activation_changed));
            break;
        case WM_DPICHANGED: {
            // 보조 창은 실제로 다른 배율의 모니터로 옮겨 다닌다.
            set_dpi(HIWORD(word_parameter));
            const auto* suggested { reinterpret_cast<const RECT*>(long_parameter) };
            SetWindowPos(window_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOACTIVATE | SWP_NOZORDER);
            // 새 크기는 이어지는 WM_SIZE가 renderer로 옮긴다. **배율은 여기서 직접
            // 알린다** — 크기가 그대로인 배율 변화(최대화된 창에서 모니터 배율을 바꿀
            // 때)에는 WM_SIZE가 오지 않아, WM_SIZE에만 맡기면 앱이 새 배율을 영영
            // 모르고 tree가 옛 배율로 남는다. 주 창이 하는 것과 같은 순서다.
            post_metrics();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        case WM_SIZE:
            if (word_parameter != SIZE_MINIMIZED)
            {
                // 크기 변경은 popup의 닫힘 계기다 (주 창과 같은 규칙).
                static_cast<void>(context_.dismiss_popups(popup_dismiss_reason::surface_resized));
                std::u8string error {};
                if (resize_renderer(static_cast<int>(LOWORD(long_parameter)), static_cast<int>(HIWORD(long_parameter)), error) == false)
                    context_.report_error(error);
                // 앱이 이 크기·배율로 tree를 다시 배치한다.
                post_metrics();
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_CLOSE:
            // Alt+F4도 캡션 close처럼 닫자는 메시지만 낸다.
            // 창을 없애는 것은 다음 frame의 대조다.
            request_close();
            return 0;
        case WM_DESTROY:
            // 소유 관계로 주 창과 함께 파괴되는 경로다.
            // 표면 자체는 남고 소유자가 다음 대조에서 걷어낸다.
            detach_window();
            return 0;
        default:
            break;
        }

        if (const std::optional<LRESULT> handled { handle_caption_message(message, word_parameter, long_parameter) }; handled.has_value())
            return *handled;
        if (const std::optional<LRESULT> handled { handle_surface_message(message, word_parameter, long_parameter) }; handled.has_value())
            return *handled;
        return DefWindowProcW(window_, message, word_parameter, long_parameter);
    }

    bool secondary_surface::adopt(const ui_window& source)
    {
        metrics_ = source.metrics;
        close_ = source.close;
        close_requested_ = false;
        // 캡션과 최소 크기는 **창을 만들 때 한 번만** 실렸다. 그런데 그 값을 읽는
        // 자리는 매번 오는 OS 질문이다 — 비클라이언트 hit test(`WM_NCHITTEST`)와
        // 크기 한계(`WM_GETMINMAXINFO`)가 그것이다.
        //  - 그래서 앱이 다음 frame에서 캡션을 바꾸면 tree는 새 모습을 그리는데
        //    hit 판정·창 스타일·OS 제목은 옛 값에 남는다. `caption_buttons`의
        //    주석이 경고한 "버튼은 없는데 그 자리가 최대화"인 창이 정확히 이것이다.
        //  - 다시 그리는 것과는 다른 문제다. 보이는 캡션은 앱이 tree에 담은
        //    `caption_element`가 그리므로, 캡션만 바뀌고 tree가 그대로면 픽셀도
        //    그대로다 — `surfaces_to_repaint`가 건너뛰는 것이 옳다.
        set_caption(source.caption);
        set_minimum_client_size(source.minimum_width, source.minimum_height);
        return set_tree(source.tree);
    }

    void secondary_surface::post_metrics() const noexcept
    {
        app_host* const host { context_.host() };
        if (host == nullptr || metrics_ == nullptr)
            return;
        RECT client {};
        if (GetClientRect(window_, &client) == FALSE)
            return;
        const float scale { static_cast<float>(dpi_) / 96.0f };
        if (app_message message { metrics_(static_cast<float>(client.right - client.left), static_cast<float>(client.bottom - client.top), scale) }; message.empty() == false)
            host->post_app_message(std::move(message));
    }

    bool secondary_surface::execute_caption_button(const WPARAM hit)
    {
        switch (hit)
        {
        case HTMINBUTTON:
            PostMessageW(window_, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            return true;
        case HTMAXBUTTON:
            PostMessageW(window_, WM_SYSCOMMAND, IsZoomed(window_) ? SC_RESTORE : SC_MAXIMIZE, 0);
            return true;
        case HTCLOSE:
            request_close();
            return true;
        default:
            return false;
        }
    }

    void secondary_surface::prepare_frame(frame_state& state)
    {
        caption_surface::prepare_frame(state);
        // 이 창의 caption tooltip 지연도 update timer에 실리도록 여기서도 예고를 다시 건다.
        // 넘기는 것은 **이 표면의 것**이라 자기 id를 함께 준다 — 나머지 표면은
        // 발행본을 각자의 것으로 다시 거른다 (multi-window-design.md).
        context_.schedule_update_repaint(id_, state.interaction);
    }

    void secondary_surface::request_close()
    {
        if (close_ == nullptr || close_requested_)
            return;
        close_requested_ = true;
        context_.dispatch_action(close_());
    }
} // namespace luil::win32
