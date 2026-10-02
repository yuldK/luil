#include "host/fail_fast.h"
#include "host/font_registry.h"

#include "include/core/SkFontMgr.h"

#include <cstdlib>
#include <string>

// core test 실행 파일은 플랫폼 계층을 링크하지 않는다 — 그래야 어느 플랫폼에서든 같은
// 실행 파일이 선다. core가 링크로 묶어 부르는 두 hook은 플랫폼이 정의하는 것이 계약이라
// (docs/concepts/threading-model.md), 여기서 test용으로 하나씩 정의한다.
//  - 글꼴은 빈 관리자다. 시스템 글꼴을 보는 test는 플랫폼 test 실행 파일에 둔다.
//  - 즉시 종료는 표준 abort다. 이 경로를 밟는 test는 없고, 밟으면 실패로 드러난다.
namespace luil {
    namespace {
        class empty_font_source final : public font_source
        {
        public:
            [[nodiscard]] sk_sp<SkFontMgr> make_system_manager() const override
            {
                return SkFontMgr::RefEmpty();
            }

            [[nodiscard]] sk_sp<SkFontMgr> make_uncached_manager() const override
            {
                return SkFontMgr::RefEmpty();
            }

            [[nodiscard]] std::u8string_view default_ui_family() const override
            {
                return {};
            }

            [[nodiscard]] std::string read_user_language() const override
            {
                return {};
            }
        };
    } // namespace

    const font_source& platform_font_source() noexcept
    {
        static const empty_font_source source {};
        return source;
    }

    void platform_fail_fast() noexcept
    {
        std::abort();
    }
} // namespace luil
