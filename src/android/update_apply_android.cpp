#include "luil/app/platform_services.h"
#include "luil/app/updater.h"
#include "net/update_apply.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>

// 자동 업데이트 적용의 Android 쪽이다 (docs/auto-update-apply-design.md의 Android 절).
//  - 적용은 스토어 페이지를 여는 것이 전부다. 설치와 그 결과는 스토어의 몫이라 앱은 모른다.
//  - Java도 새 JNI도 없다. `open_url`이 `ACTION_VIEW` Intent를 앱 context로 띄운다.
//  - APK를 직접 설치하는 길은 없다. 설치 확인의 답을 받으려면 `onNewIntent`를 재정의한 Activity(Java)가 든다.
namespace luil {
    update_apply_mode platform_update_apply_mode() noexcept
    {
        return update_apply_mode::open_store;
    }

    std::optional<int> run_update_helper(const std::span<const std::u8string> arguments)
    {
        // 프로세스를 띄우지 못하는 플랫폼이라 도우미 실행이 없다.
        static_cast<void>(arguments);
        return std::nullopt;
    }

    namespace net {
        bool hand_off_update(const update_handoff& handoff, std::u8string& error)
        {
            open_url_options options {};
            options.schemes = handoff.store_schemes;
            const platform_result opened { open_url(handoff.store_url, options) };
            if (opened.ok())
                return true;
            error = opened.error.empty() ? std::u8string { u8"No app opened the store address." } : opened.error;
            return false;
        }

        bool update_target_writable(std::u8string& error)
        {
            static_cast<void>(error);
            return true;
        }

        std::filesystem::path update_target_path()
        {
            return {};
        }
    }
}
