#include "win32/webview_host.h"

#include <utility>

// 웹뷰를 끈 구성(LUIL_ENABLE_WEBVIEW=OFF)의 host다.
// WebView2 SDK 없이 컴파일되며 아무것도 세우지 않는다 — frame에 실린 웹뷰는
// 자리표의 placeholder로만 남고, 구멍도 뚫지 않고 포인터도 가져가지 않는다.
// 런타임이 없는 기계에서 실제 host가 보이는 것과 같은 모습이다.
namespace luil::win32 {
    // 헤더가 unique_ptr로 드는 타입이다. 소멸자가 완전한 타입을 요구하므로 빈 채로 둔다.
    struct webview_host::entry
    {
    };

    struct webview_host::environment
    {
    };

    webview_host::webview_host() = default;
    webview_host::~webview_host() = default;

    void webview_host::set_deliver(std::function<void(app_message)> deliver)
    {
        deliver_ = std::move(deliver);
    }

    void webview_host::set_focus_reporter(std::function<void(const std::u8string&, const std::u8string&, webview_focus_signal)> reporter)
    {
        focus_reporter_ = std::move(reporter);
    }

    void webview_host::set_default_background(const ui_color color)
    {
        default_background_ = color;
    }

    void webview_host::apply_default_background(entry& target) const
    {
        static_cast<void>(target);
    }

    void webview_host::synchronize(const std::span<const webview_target> wanted, IDCompositionDevice* const composition)
    {
        static_cast<void>(wanted);
        static_cast<void>(composition);
    }

    void webview_host::apply_layout(const std::u8string& id, const webview_layout& layout, IDCompositionVisual* const underlay)
    {
        static_cast<void>(id);
        static_cast<void>(layout);
        static_cast<void>(underlay);
    }

    bool webview_host::standing(const std::u8string& id) const noexcept
    {
        static_cast<void>(id);
        return false;
    }

    bool webview_host::move_focus_in(const std::u8string& id, const bool backward)
    {
        static_cast<void>(id);
        static_cast<void>(backward);
        return false;
    }

    bool webview_host::relay_pointer(const std::u8string& anchor, const UINT message, const WPARAM word_parameter, const int client_x, const int client_y)
    {
        static_cast<void>(anchor);
        static_cast<void>(message);
        static_cast<void>(word_parameter);
        static_cast<void>(client_x);
        static_cast<void>(client_y);
        return false;
    }

    void webview_host::relay_pointer_left(const std::u8string& anchor)
    {
        static_cast<void>(anchor);
    }

    void webview_host::cancel_pointer(const std::u8string& anchor)
    {
        static_cast<void>(anchor);
    }

    bool webview_host::relay_pointer_input(const std::u8string& anchor, const webview_pointer_input& input)
    {
        static_cast<void>(anchor);
        static_cast<void>(input);
        return false;
    }

    void webview_host::cancel_pointer_input(const std::u8string& anchor, const std::uint32_t pointer_id)
    {
        static_cast<void>(anchor);
        static_cast<void>(pointer_id);
    }

    void webview_host::shutdown() noexcept
    {
    }
} // namespace luil::win32
