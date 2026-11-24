#include "host/font_registry.h"

#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_mac_ct.h"

#include <CoreFoundation/CoreFoundation.h>

#include <string>

namespace luil {
    namespace {
        // 사용자의 첫 언어다 (BCP-47, 예: `ko-KR`). 글자 대체가 이 언어의 글꼴을 먼저 고른다.
        [[nodiscard]] std::string read_preferred_language()
        {
            std::string language {};
            const CFArrayRef languages { CFLocaleCopyPreferredLanguages() };
            if (languages == nullptr)
                return language;
            if (CFArrayGetCount(languages) > 0)
            {
                const auto first { static_cast<CFStringRef>(CFArrayGetValueAtIndex(languages, 0)) };
                char buffer[64] {};
                if (first != nullptr && CFStringGetCString(first, buffer, sizeof(buffer), kCFStringEncodingUTF8))
                    language = buffer;
            }
            CFRelease(languages);
            return language;
        }

        // macOS의 글꼴 자원이다. cache·설정·대체 규칙은 core의 registry가 갖고, 여기는 CoreText 관리자와 언어만 공급한다
        // (host/font_registry.h). 관리자는 CoreText가 아는 시스템 글꼴 전체를 본다 (skia-prep의 feature probe와 같은 조합).
        class macos_font_source final : public font_source
        {
        public:
            [[nodiscard]] sk_sp<SkFontMgr> make_system_manager() const override
            {
                return SkFontMgr_New_CoreText(nullptr);
            }

            // 실행 중 글꼴 설치는 CoreText가 스스로 반영하므로 관리자 하나를 함께 쓴다.
            [[nodiscard]] sk_sp<SkFontMgr> make_uncached_manager() const override
            {
                static const sk_sp<SkFontMgr> manager { make_system_manager() };
                return manager;
            }

            // 시스템 UI 글꼴(San Francisco)이다. CoreText가 이 이름을 시스템 글꼴로 푼다.
            [[nodiscard]] std::u8string_view default_ui_family() const override
            {
                return u8".AppleSystemUIFont";
            }

            [[nodiscard]] std::string read_user_language() const override
            {
                return read_preferred_language();
            }
        };
    }

    const font_source& platform_font_source() noexcept
    {
        static const macos_font_source source {};
        return source;
    }
}
