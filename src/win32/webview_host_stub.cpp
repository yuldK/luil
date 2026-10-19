#include "win32/webview_host.h"

#include <algorithm>
#include <utility>

// 웹뷰를 끈 구성(LUIL_ENABLE_WEBVIEW=OFF)의 host다.
// WebView2 SDK 없이 컴파일되며 아무것도 세우지 않는다 — frame에 실린 웹뷰는
// 자리표의 placeholder로만 남고, 구멍도 뚫지 않고 포인터도 가져가지 않는다.
// 런타임이 없는 기계에서 실제 host가 보이는 것과 같은 모습이다. 알리는 것도 같다 —
// 실은 웹뷰마다 `creation_failed`를 한 번 낸다 (갈래만 `not_built`로 다르다).
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
        static_cast<void>(composition);
        // 실제 host와 같은 규칙이다 — 앱이 목록에서 뺐다가 다시 실으면 다시 알린다.
        std::erase_if(failed_, [&wanted](const std::u8string& id) {
            for (const webview_target& target : wanted)
                if (target.source != nullptr && target.source->id == id)
                    return false;
            return true;
        });
        for (const webview_target& target : wanted)
        {
            // 실제 host가 만들기를 시작하는 것만 알린다 (앵커 표면·id·프로필 자리가 있는 것).
            if (target.source == nullptr || target.window == nullptr)
                continue;
            const ui_webview& want { *target.source };
            if (want.id.empty() || want.user_data_folder.empty())
                continue;
            if (std::find(failed_.begin(), failed_.end(), want.id) != failed_.end())
                continue;
            failed_.push_back(want.id);
            if (want.on_event == nullptr || deliver_ == nullptr)
                continue;
            webview_event event {};
            event.id = want.id;
            event.kind = webview_event_kind::creation_failed;
            event.error = u8"This build does not include the WebView2 host.";
            event.failure = webview_creation_failure::not_built;
            deliver_(want.on_event(std::move(event)));
        }
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
