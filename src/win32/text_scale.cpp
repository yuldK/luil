#include "win32/text_scale.h"

#include <roapi.h>
#include <winstring.h>
#include <windows.foundation.h>
#include <windows.ui.viewmanagement.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <new>
#include <utility>

namespace luil::win32 {
    namespace {
        using Microsoft::WRL::ComPtr;
        using settings_type = ABI::Windows::UI::ViewManagement::IUISettings2;
        // `UISettings.TextScaleFactorChanged`의 처리기다. 알림이 배경 thread에서 오므로 thread를 가리지 않는(agile)
        // 처리기로 짓는다 — `FtmBase`가 그 표시다.
        using changed_handler = ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::ViewManagement::UISettings*, IInspectable*>;

        [[nodiscard]] ComPtr<settings_type> open_settings() noexcept
        {
            HSTRING_HEADER header {};
            HSTRING name { nullptr };
            // 클래스 이름은 SDK 헤더의 배열 상수다.
            const wchar_t* const class_name { RuntimeClass_Windows_UI_ViewManagement_UISettings };
            if (FAILED(WindowsCreateStringReference(class_name, static_cast<UINT32>(std::wcslen(class_name)), &header, &name)))
                return {};
            ComPtr<IInspectable> instance {};
            if (FAILED(RoActivateInstance(name, &instance)) || instance == nullptr)
                return {};
            ComPtr<settings_type> settings {};
            if (FAILED(instance.As(&settings)))
                return {};
            return settings;
        }
    }

    float read_text_scale_factor() noexcept
    {
        const ComPtr<settings_type> settings { open_settings() };
        if (settings == nullptr)
            return 1.0f;
        DOUBLE factor { 1.0 };
        if (FAILED(settings->get_TextScaleFactor(&factor)) || std::isfinite(factor) == false)
            return 1.0f;
        // 설정이 고를 수 있는 범위다 (100%~225%). 그 밖의 값은 믿지 않는다.
        return std::clamp(static_cast<float>(factor), 1.0f, 2.25f);
    }

    struct text_scale_watcher::subscription
    {
        ComPtr<settings_type> settings {};
        EventRegistrationToken token {};
    };

    text_scale_watcher::text_scale_watcher(const HWND window, const UINT message) noexcept
    {
        ComPtr<settings_type> settings { open_settings() };
        if (settings == nullptr || window == nullptr)
            return;
        const ComPtr<changed_handler> handler {
            Microsoft::WRL::Callback<Microsoft::WRL::Implements<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, changed_handler, Microsoft::WRL::FtmBase>>(
                [window, message](ABI::Windows::UI::ViewManagement::IUISettings*, IInspectable*) -> HRESULT {
                    // 창이 이미 없으면 게시가 실패할 뿐이다.
                    static_cast<void>(PostMessageW(window, message, 0, 0));
                    return S_OK;
                }),
        };
        // 장부를 먼저 만든다. 구독한 뒤에 만들지 못하면 풀 길이 없는 구독이 남는다.
        std::unique_ptr<subscription> created { new (std::nothrow) subscription {} };
        if (handler == nullptr || created == nullptr)
            return;
        if (FAILED(settings->add_TextScaleFactorChanged(handler.Get(), &created->token)))
            return;
        created->settings = std::move(settings);
        subscription_ = std::move(created);
    }

    text_scale_watcher::~text_scale_watcher()
    {
        if (subscription_ != nullptr && subscription_->settings != nullptr)
            static_cast<void>(subscription_->settings->remove_TextScaleFactorChanged(subscription_->token));
    }
}
