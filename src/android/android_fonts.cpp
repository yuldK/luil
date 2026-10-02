#include "android/android_fonts.h"

#include "host/font_registry.h"

#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_android.h"
#include "include/ports/SkFontScanner_FreeType.h"

#include <mutex>
#include <string>
#include <utility>

namespace luil {
    namespace {
        // 앱 host가 넣은 사용자 언어다. registry가 한 번 읽는다.
        struct user_language_state
        {
            std::mutex mutex {};
            std::string language {};
        };

        user_language_state& user_language()
        {
            static user_language_state instance {};
            return instance;
        }

        // Android의 글꼴 자원이다. cache·설정·대체 규칙은 core의 registry가 갖고,
        // 여기는 시스템 글꼴 관리자와 언어만 공급한다 (host/font_registry.h).
        //
        // 관리자는 `/system/etc/fonts.xml`을 읽어 시스템 글꼴을 FreeType으로 연다
        // (skia-prep의 tools/android_probe.cpp가 기기에서 확인한 조합이다).
        class android_font_source final : public font_source
        {
        public:
            [[nodiscard]] sk_sp<SkFontMgr> make_system_manager() const override
            {
                return SkFontMgr_New_Android(nullptr, SkFontScanner_Make_FreeType());
            }

            // **시스템 관리자 하나를 함께 쓴다.** Windows가 부를 때마다 새로 만드는 이유는
            // 글꼴 설치(`WM_FONTCHANGE`) 뒤의 낡은 목록이다. Android 앱에는 실행 중 글꼴이
            // 설치되는 길이 없고, 새로 만들 때마다 fonts.xml을 다시 읽는 값만 남는다.
            [[nodiscard]] sk_sp<SkFontMgr> make_uncached_manager() const override
            {
                static const sk_sp<SkFontMgr> manager { make_system_manager() };
                return manager;
            }

            [[nodiscard]] std::u8string_view default_ui_family() const override
            {
                return u8"sans-serif";
            }

            [[nodiscard]] std::string read_user_language() const override
            {
                user_language_state& state { user_language() };
                const std::lock_guard<std::mutex> lock { state.mutex };
                return state.language;
            }
        };
    } // namespace

    const font_source& platform_font_source() noexcept
    {
        static const android_font_source source {};
        return source;
    }

    namespace android {
        void set_user_language(std::string language)
        {
            user_language_state& state { user_language() };
            const std::lock_guard<std::mutex> lock { state.mutex };
            state.language = std::move(language);
        }
    } // namespace android
} // namespace luil
