#include "host/font_registry.h"

#include "luil/text/fonts.h"
#include "luil/text/utf8_text.h"

#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace luil {
    namespace {
        // 가족 이름 → typeface cache와 현재 설정을 함께 지키는 자물쇠다.
        // 잠금 구간은 map 조회 하나라 입력 thread의 글자 폭 측정에 부담이 되지 않는다.
        struct font_registry
        {
            std::mutex mutex {};
            sk_sp<SkFontMgr> manager {};
            bool manager_ready { false };
            // 관리자를 지금까지 몇 번 만들었는지다.
            // 무효화가 관리자까지 정말 놓았는지는 밖에서 볼 수 없어 이 값으로 잰다.
            std::size_t generation { 0 };
            std::map<std::u8string, sk_sp<SkTypeface>, std::less<>> cache {};
            // 글자 하나를 그릴 대체 typeface다.
            // 찾지 못한 codepoint도 nullptr로 남겨 같은 글자를 매 frame 다시 뒤지지 않는다.
            std::map<char32_t, sk_sp<SkTypeface>> fallback_cache {};
            std::u8string ui_family {};
            std::u8string code_family {};
            sk_sp<SkTypeface> ui_typeface {};
            sk_sp<SkTypeface> code_typeface {};
        };

        font_registry& registry()
        {
            static font_registry instance {};
            return instance;
        }

        // 기본 UI 글꼴 선호 목록과 자물쇠다.
        // 시작 시 한 번 지정하고 이후에는 읽기만 하므로 잠금 구간이 짧다.
        //  - registry와 자물쇠를 나누는 이유는 registry가 자물쇠를 쥔 채 내장 글꼴을 찾기
        //    때문이다. 한 자물쇠를 두 번 쥐지 않도록 순서는 언제나 registry → 선호 목록이다.
        //  - 빈 목록은 플랫폼의 기본 가족만 찾는다는 뜻이다.
        struct ui_typeface_preference
        {
            std::mutex mutex {};
            std::vector<std::u8string> families {};
        };

        ui_typeface_preference& preference()
        {
            static ui_typeface_preference instance {};
            return instance;
        }

        // 대체 글꼴을 고를 사용자 언어다. 비어 있으면 아직 읽지 않았다.
        //  - registry가 자물쇠를 쥔 채 읽으므로 순서는 언제나 registry → 언어다.
        struct user_language_cache
        {
            std::mutex mutex {};
            std::optional<std::string> language {};
        };

        user_language_cache& language_cache()
        {
            static user_language_cache instance {};
            return instance;
        }

        // 자물쇠를 이미 쥔 상태에서 부른다.
        // 만드는 것은 첫 조회까지 미룬다 — 목록을 다시 읽는 값이 비싸다.
        SkFontMgr* manager_locked(font_registry& state)
        {
            if (state.manager_ready == false)
            {
                state.manager_ready = true;
                state.manager = platform_font_source().make_system_manager();
                ++state.generation;
            }
            return state.manager.get();
        }

        sk_sp<SkTypeface> resolve_locked(font_registry& state, const std::u8string_view family)
        {
            // 내장 글꼴도 cache에 남긴다.
            // 매번 새로 만들어 돌려주면 호출자가 raw 포인터로
            // 받는 순간(미리 보기 해석기) 수명이 끊긴다.
            if (const auto found { state.cache.find(family) }; found != state.cache.end())
                return found->second;
            if (family.empty())
            {
                sk_sp<SkTypeface> embedded { load_ui_typeface() };
                state.cache.emplace(std::u8string {}, embedded);
                return embedded;
            }

            sk_sp<SkTypeface> typeface {};
            if (SkFontMgr* const manager { manager_locked(state) }; manager != nullptr)
            {
                const std::string name { reinterpret_cast<const char*>(family.data()), family.size() };
                typeface = manager->matchFamilyStyle(name.c_str(), SkFontStyle::Normal());
            }
            // 찾지 못한 이름도 cache에 넣는다.
            // 같은 이름으로 매 frame 다시 뒤지지 않는다.
            state.cache.emplace(std::u8string { family }, typeface);
            return typeface;
        }

        [[nodiscard]] sk_sp<SkTypeface> match_family(SkFontMgr& manager, const std::u8string_view family)
        {
            const std::string name { reinterpret_cast<const char*>(family.data()), family.size() };
            return manager.matchFamilyStyle(name.c_str(), SkFontStyle::Normal());
        }
    } // namespace

    std::vector<std::u8string> installed_font_families()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        SkFontMgr* const manager { manager_locked(state) };
        if (manager == nullptr)
            return {};

        std::vector<std::u8string> families {};
        const int count { manager->countFamilies() };
        families.reserve(static_cast<std::size_t>(count > 0 ? count : 0));
        for (int index = 0; index < count; ++index)
        {
            SkString name {};
            manager->getFamilyName(index, &name);
            if (name.isEmpty())
                continue;

            // 관리자에 따라 길이에 끝의 NUL이 들어 있다 (GDI 경로의 `tchar_to_skstring`).
            // 남겨 두면 이름 뒤에 빈 글리프가 하나 붙는다.
            std::u8string family { reinterpret_cast<const char8_t*>(name.c_str()), name.size() };
            while (family.empty() == false && family.back() == u8'\0')
                family.pop_back();
            // 관리자를 바꿔도 방어는 남긴다.
            // 잘못된 UTF-8 이름은 목록에 넣지 않는다.
            //  - 그리는 순간 Skia가 abort하고, 어차피 해석도 되지 않는다.
            if (family.empty() || text::utf8_is_valid(family) == false)
                continue;
            families.push_back(std::move(family));
        }
        std::ranges::sort(families);
        families.erase(std::ranges::unique(families).begin(), families.end());
        return families;
    }

    void set_ui_typeface_families(std::vector<std::u8string> families)
    {
        {
            ui_typeface_preference& state { preference() };
            const std::lock_guard<std::mutex> lock { state.mutex };
            state.families = std::move(families);
        }
        clear_font_caches();
    }

    sk_sp<SkTypeface> load_ui_typeface()
    {
        const font_source& source { platform_font_source() };
        sk_sp<SkFontMgr> font_manager { source.make_uncached_manager() };
        if (font_manager == nullptr)
            return nullptr;

        std::vector<std::u8string> families {};
        {
            ui_typeface_preference& state { preference() };
            const std::lock_guard<std::mutex> lock { state.mutex };
            families = state.families;
        }

        for (const std::u8string& family : families)
        {
            if (family.empty() || family.find(u8'\0') != std::u8string::npos)
                continue;
            if (sk_sp<SkTypeface> typeface { match_family(*font_manager, family) }; typeface != nullptr)
                return typeface;
        }
        return match_family(*font_manager, source.default_ui_family());
    }

    std::string user_ui_language()
    {
        // 한 번 읽어 둔다. Windows는 언어를 바꾸면 앱을 다시 켜는 것이 OS의 규칙이다.
        // Android는 프로세스를 남긴 채 Activity만 다시 세우므로 host가 `refresh_user_ui_language`로 거둔다.
        user_language_cache& cache { language_cache() };
        const std::lock_guard<std::mutex> lock { cache.mutex };
        if (cache.language.has_value() == false)
            cache.language = platform_font_source().read_user_language();
        return *cache.language;
    }

    void refresh_user_ui_language()
    {
        {
            user_language_cache& cache { language_cache() };
            const std::lock_guard<std::mutex> lock { cache.mutex };
            cache.language.reset();
        }
        // 대체 글꼴은 언어로 고른 것이라 함께 낡는다.
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        state.fallback_cache.clear();
    }

    void set_configured_fonts(const std::u8string_view ui_family, const std::u8string_view code_family)
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        if (state.ui_family != ui_family || state.ui_typeface == nullptr)
        {
            state.ui_family = ui_family;
            state.ui_typeface = resolve_locked(state, ui_family);
            // 목록에 없는 이름은 조용히 내장 글꼴로 되돌아간다.
            // 설정 파일은 그대로 둔다.
            if (state.ui_typeface == nullptr)
                state.ui_typeface = load_ui_typeface();
        }
        if (state.code_family != code_family || state.code_typeface == nullptr)
        {
            state.code_family = code_family;
            state.code_typeface = resolve_locked(state, code_family);
            if (state.code_typeface == nullptr)
                state.code_typeface = load_ui_typeface();
        }
    }

    sk_sp<SkTypeface> configured_ui_typeface()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        if (state.ui_typeface == nullptr)
            state.ui_typeface = resolve_locked(state, state.ui_family);
        if (state.ui_typeface == nullptr)
            state.ui_typeface = load_ui_typeface();
        return state.ui_typeface;
    }

    sk_sp<SkTypeface> configured_code_typeface()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        if (state.code_typeface == nullptr)
            state.code_typeface = resolve_locked(state, state.code_family);
        if (state.code_typeface == nullptr)
            state.code_typeface = load_ui_typeface();
        return state.code_typeface;
    }

    void clear_font_caches()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        state.cache.clear();
        state.fallback_cache.clear();
        // 설정된 글꼴도 놓는다. 다음 조회가 새 목록으로 다시 해석한다.
        //  - 이름은 남긴다. 앱이 고른 것은 그대로이고 낡은 것은 해석 결과뿐이다.
        state.ui_typeface = nullptr;
        state.code_typeface = nullptr;
        // **관리자도 놓는다.** DirectWrite 관리자는 만들 때 얻은 font collection을 그대로
        // 들고 있어, 이것을 남기면 cache를 아무리 비워도 다시 해석한 답이 똑같이 낡아 있다.
        //  - 다시 만드는 것은 다음 조회 때다. 목록을 새로 읽는 데 수백 ms까지 걸려
        //    자물쇠를 쥔 이 자리에서 치르면 `WM_FONTCHANGE`가 UI를 멈춰 세운다.
        state.manager = nullptr;
        state.manager_ready = false;
    }

    std::size_t fallback_cache_size()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        return state.fallback_cache.size();
    }

    std::size_t font_manager_generation()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        // 여기서 만들지 않는다. 세는 쪽이 만들면 무엇을 재는지가 흐려진다.
        return state.generation;
    }

    sk_sp<SkTypeface> family_typeface(const std::u8string_view family)
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        return resolve_locked(state, family);
    }

    sk_sp<SkTypeface> fallback_typeface(const char32_t codepoint)
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        if (const auto found { state.fallback_cache.find(codepoint) }; found != state.fallback_cache.end())
            return found->second;

        sk_sp<SkTypeface> typeface {};
        if (SkFontMgr* const manager { manager_locked(state) }; manager != nullptr)
        {
            // 가족은 지정하지 않는다 — 시스템이 그 글자를 담은 글꼴을 고르게 두는
            // 것이 브라우저·편집기와 같은 동작이다. GDI font manager는 이 진입점이
            // 언제나 nullptr이라 대체 자체가 불가능했다.
            //
            // **언어는 지정한다.** 한자는 한국어·일본어·중국어에서 **자형이 다르고**
            // (直·骨·steps 같은 글자가 눈에 띄게 갈린다), 언어를 주지 않으면 시스템이
            // 임의로 고른다 — 한국어 Windows에서 일본어 자형이 나오는 식이다.
            // 사용자 UI 언어를 한 번 읽어 그대로 넘긴다.
            const std::string language { user_ui_language() };
            const char* bcp47[] { language.c_str() };
            const int count { language.empty() ? 0 : 1 };
            typeface = manager->matchFamilyStyleCharacter(nullptr, SkFontStyle::Normal(), count > 0 ? bcp47 : nullptr, count, static_cast<SkUnichar>(codepoint));
        }
        // 상한에 닿으면 캐시를 비운다.
        // 조회 결과가 sk_sp로 소유권을 유지하므로 이미 전달한 typeface는 살아 있다.
        // 새 항목을 넣기 전에 비워 해당 항목이 바로 제거되지 않게 한다.
        if (state.fallback_cache.size() >= fallback_cache_limit)
            state.fallback_cache.clear();
        state.fallback_cache.emplace(codepoint, typeface);
        return typeface;
    }
} // namespace luil
