#include "host/font_registry.h"

#include "luil/text/fonts.h"
#include "luil/ui/text_layout.h"
#include "luil/text/utf8_text.h"
#include "ui/font_synthesis.h"

#include "include/core/SkFontArguments.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace luil {
    namespace {
        // 굵기를 바꾼 face를 찾는 열쇠다. 바탕 face와 요청한 모양이다.
        struct styled_key
        {
            SkTypefaceID base { 0 };
            int weight { 0 };
            font_slant slant { font_slant::upright };

            [[nodiscard]] auto operator<=>(const styled_key&) const noexcept = default;
        };

        // 바탕 face를 함께 쥔다. 놓으면 그 id가 다른 face에 다시 쓰여 엉뚱한 답이 맞는다.
        struct styled_entry
        {
            sk_sp<SkTypeface> base {};
            styled_typeface result {};
        };

        struct fallback_key
        {
            char32_t codepoint { 0 };
            int weight { 0 };
            font_slant slant { font_slant::upright };

            [[nodiscard]] auto operator<=>(const fallback_key&) const noexcept = default;
        };

        // 등록부가 시스템 관리자에서 얻어 내준 face와 그 가족 이름이다.
        // 굵기를 바꿀 때 관리자에 다시 물을 이름이다 — 이 표에 없는 face(앱이 파일로 읽은 것)는 관리자에 묻지 않는다.
        struct system_origin
        {
            sk_sp<SkTypeface> face {};
            std::u8string family {};
        };

        // 굵기 해석 cache의 상한이다. 앱이 굵기를 아무 수로나 요청해도 한없이 자라지 않는다.
        constexpr std::size_t styled_cache_limit { 1024 };

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
            // 글자 하나를 그릴 대체 typeface다. 굵기·기울임마다 따로 고른다.
            // 찾지 못한 글자도 nullptr로 남겨 같은 글자를 매 frame 다시 뒤지지 않는다.
            std::map<fallback_key, sk_sp<SkTypeface>> fallback_cache {};
            std::map<SkTypefaceID, system_origin> system_origins {};
            std::map<styled_key, styled_entry> styled_cache {};
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

        // 앱이 등록한 가족이다 (`register_font_family`).
        //  - registry가 자물쇠를 쥔 채 읽으므로 순서는 언제나 registry → 앱 가족이다. 선호 목록과 함께 쥐지 않는다.
        struct app_font_families
        {
            std::mutex mutex {};
            std::map<std::u8string, std::vector<sk_sp<SkTypeface>>, std::less<>> families {};
        };

        app_font_families& app_families()
        {
            static app_font_families instance {};
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

        [[nodiscard]] bool usable_family_name(const std::u8string_view family) noexcept
        {
            return family.empty() == false && family.find(u8'\0') == std::u8string_view::npos;
        }

        [[nodiscard]] SkFontStyle skia_style(const font_style style) noexcept
        {
            return SkFontStyle {
                font_weight_value(style.weight)
                , SkFontStyle::kNormal_Width
                , style.slant == font_slant::italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant
            };
        }

        [[nodiscard]] std::vector<sk_sp<SkTypeface>> app_family_faces(const std::u8string_view family)
        {
            app_font_families& state { app_families() };
            const std::lock_guard<std::mutex> lock { state.mutex };
            if (const auto found { state.families.find(family) }; found != state.families.end())
                return found->second;
            return {};
        }

        // `face`가 든 앱 가족의 face 목록이다. 어느 가족에도 없으면 빈 목록이다.
        [[nodiscard]] std::vector<sk_sp<SkTypeface>> app_family_containing(const SkTypefaceID face)
        {
            app_font_families& state { app_families() };
            const std::lock_guard<std::mutex> lock { state.mutex };
            for (const auto& [name, faces] : state.families)
            {
                if (std::ranges::any_of(faces, [face](const sk_sp<SkTypeface>& item) { return item->uniqueID() == face; }))
                    return faces;
            }
            return {};
        }

        // face가 `wanted` 굵기에서 얼마나 먼가다. 가변 글꼴은 축 범위 안이면 0이다.
        [[nodiscard]] int weight_distance(const SkTypeface& face, const int wanted)
        {
            float minimum { 0.0f };
            float maximum { 0.0f };
            if (weight_axis_range(face, minimum, maximum))
            {
                const float reachable { std::clamp(static_cast<float>(wanted), minimum, maximum) };
                return static_cast<int>(std::lround(std::abs(reachable - static_cast<float>(wanted))));
            }
            return std::abs(achieved_weight(face) - wanted);
        }

        // 가족 안에서 `style`에 가장 가까운 face다. 기울임이 맞는 것을 먼저, 그 안에서 굵기가 가까운 것을 고른다.
        //  - 굵기가 같은 거리면 요청이 regular 이상일 때 더 굵은 쪽, 아니면 더 가는 쪽이다 (CSS의 글꼴 고르기와 같은 쪽).
        [[nodiscard]] sk_sp<SkTypeface> closest_face(const std::vector<sk_sp<SkTypeface>>& faces, const font_style style)
        {
            const int wanted { font_weight_value(style.weight) };
            const bool heavier_first { wanted >= font_weight_value(font_weight::regular) };
            const bool italic { style.slant == font_slant::italic };
            sk_sp<SkTypeface> best {};
            int best_score { 0 };
            int best_weight { 0 };
            for (const sk_sp<SkTypeface>& face : faces)
            {
                const bool leaning { face->fontStyle().slant() != SkFontStyle::kUpright_Slant };
                const int score { (leaning == italic ? 0 : 10000) + weight_distance(*face, wanted) };
                const int weight { achieved_weight(*face) };
                const bool tie_wins { score == best_score && (heavier_first ? weight > best_weight : weight < best_weight) };
                if (best == nullptr
                    || score < best_score
                    || tie_wins)
                {
                    best = face;
                    best_score = score;
                    best_weight = weight;
                }
            }
            return best;
        }

        // face에 `wght` 축이 있으면 `wanted`(축 범위로 자른 값)로 clone한다. 다른 축은 지금 자리를 잇는다 —
        // 이 축만 넘기면 Skia가 나머지 축을 기본값으로 되돌린다.
        [[nodiscard]] sk_sp<SkTypeface> with_weight_axis(sk_sp<SkTypeface> face, const int wanted)
        {
            float minimum { 0.0f };
            float maximum { 0.0f };
            if (face == nullptr || weight_axis_range(*face, minimum, maximum) == false)
                return face;
            const float target { std::clamp(static_cast<float>(wanted), minimum, maximum) };
            if (std::lround(target) == achieved_weight(*face))
                return face;

            using coordinate = SkFontArguments::VariationPosition::Coordinate;
            std::vector<coordinate> position {};
            if (const int count { face->getVariationDesignPosition({}) }; count > 0)
            {
                position.resize(static_cast<std::size_t>(count));
                if (face->getVariationDesignPosition(position) != count)
                    position.clear();
            }
            const auto weight_axis { std::ranges::find(position, coordinate::wght, &coordinate::axis) };
            if (weight_axis != position.end())
                weight_axis->value = target;
            else
                position.push_back({ coordinate::wght, target });

            SkFontArguments arguments {};
            arguments.setVariationDesignPosition({ position.data(), static_cast<int>(position.size()) });
            sk_sp<SkTypeface> clone { face->makeClone(arguments) };
            return clone != nullptr ? clone : face;
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

        void remember_system_locked(font_registry& state, const sk_sp<SkTypeface>& face, const std::u8string_view family)
        {
            if (face == nullptr || family.empty())
                return;
            state.system_origins.try_emplace(face->uniqueID(), system_origin { face, std::u8string { family } });
        }

        [[nodiscard]] sk_sp<SkTypeface> match_family(SkFontMgr& manager, const std::u8string_view family, const font_style style = {})
        {
            const std::string name { reinterpret_cast<const char*>(family.data()), family.size() };
            return manager.matchFamilyStyle(name.c_str(), skia_style(style));
        }

        // 기본 UI 글꼴로 고른 face와 그 출처다.
        struct ui_typeface_choice
        {
            sk_sp<SkTypeface> face {};
            std::u8string family {};
            // 시스템 관리자에서 찾았다. 거짓이면 앱 가족의 face다.
            bool system { false };
        };

        // 선호 목록을 앞에서부터 찾는다. 같은 이름이면 앱 가족이 설치된 가족보다 앞선다 — 앱이 실어 온 글꼴이
        // 사용자 PC에 깔린 다른 판에 가려지지 않는다.
        [[nodiscard]] ui_typeface_choice choose_ui_typeface()
        {
            std::vector<std::u8string> families {};
            {
                ui_typeface_preference& state { preference() };
                const std::lock_guard<std::mutex> lock { state.mutex };
                families = state.families;
            }

            const font_source& source { platform_font_source() };
            const sk_sp<SkFontMgr> font_manager { source.make_uncached_manager() };
            for (const std::u8string& family : families)
            {
                if (usable_family_name(family) == false)
                    continue;
                if (const auto faces { app_family_faces(family) }; faces.empty() == false)
                    return { closest_face(faces, {}), family, false };
                if (font_manager == nullptr)
                    continue;
                if (sk_sp<SkTypeface> typeface { match_family(*font_manager, family) }; typeface != nullptr)
                    return { std::move(typeface), family, true };
            }
            if (font_manager == nullptr)
                return {};
            const std::u8string_view fallback { source.default_ui_family() };
            return { match_family(*font_manager, fallback), std::u8string { fallback }, true };
        }

        // 등록부 안에서 내장 UI 글꼴을 얻는다. 시스템에서 찾았으면 출처를 남겨 굵기를 바꿀 수 있게 한다.
        [[nodiscard]] sk_sp<SkTypeface> load_ui_typeface_locked(font_registry& state)
        {
            ui_typeface_choice choice { choose_ui_typeface() };
            if (choice.system)
                remember_system_locked(state, choice.face, choice.family);
            return std::move(choice.face);
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
                sk_sp<SkTypeface> embedded { load_ui_typeface_locked(state) };
                state.cache.emplace(std::u8string {}, embedded);
                return embedded;
            }

            sk_sp<SkTypeface> typeface {};
            if (const auto faces { app_family_faces(family) }; faces.empty() == false)
                typeface = closest_face(faces, {});
            else if (SkFontMgr* const manager { manager_locked(state) }; manager != nullptr)
            {
                typeface = match_family(*manager, family);
                remember_system_locked(state, typeface, family);
            }
            // 찾지 못한 이름도 cache에 넣는다.
            // 같은 이름으로 매 frame 다시 뒤지지 않는다.
            state.cache.emplace(std::u8string { family }, typeface);
            return typeface;
        }
    }

    std::vector<std::u8string> installed_font_families()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        std::vector<std::u8string> families {};
        // 앱 가족도 목록에 둔다. 설정의 글꼴 고르기가 이 목록을 그대로 보여 준다.
        {
            app_font_families& apps { app_families() };
            const std::lock_guard<std::mutex> app_lock { apps.mutex };
            for (const auto& [name, faces] : apps.families)
                families.push_back(name);
        }

        if (SkFontMgr* const manager { manager_locked(state) }; manager != nullptr)
        {
            const int count { manager->countFamilies() };
            families.reserve(families.size() + static_cast<std::size_t>(count > 0 ? count : 0));
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

    void register_font_family(std::u8string family, std::vector<sk_sp<SkTypeface>> faces)
    {
        if (usable_family_name(family) == false || text::utf8_is_valid(family) == false)
            return;
        std::erase(faces, nullptr);
        {
            app_font_families& state { app_families() };
            const std::lock_guard<std::mutex> lock { state.mutex };
            if (faces.empty())
                state.families.erase(family);
            else
                state.families.insert_or_assign(std::move(family), std::move(faces));
        }
        // 자물쇠를 놓은 뒤에 비운다. 비우기는 registry 자물쇠를 쥐고, 순서는 registry → 앱 가족이다.
        clear_font_caches();
    }

    void unregister_font_family(const std::u8string_view family)
    {
        {
            app_font_families& state { app_families() };
            const std::lock_guard<std::mutex> lock { state.mutex };
            const auto found { state.families.find(family) };
            if (found == state.families.end())
                return;
            state.families.erase(found);
        }
        clear_font_caches();
    }

    sk_sp<SkTypeface> load_ui_typeface()
    {
        return choose_ui_typeface().face;
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
        invalidate_text_layouts();
    }

    void set_configured_fonts(const std::u8string_view ui_family, const std::u8string_view code_family)
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        const bool changed { state.ui_family != ui_family || state.code_family != code_family };
        if (state.ui_family != ui_family || state.ui_typeface == nullptr)
        {
            state.ui_family = ui_family;
            state.ui_typeface = resolve_locked(state, ui_family);
            // 목록에 없는 이름은 조용히 내장 글꼴로 되돌아간다.
            // 설정 파일은 그대로 둔다.
            if (state.ui_typeface == nullptr)
                state.ui_typeface = load_ui_typeface_locked(state);
        }
        if (state.code_family != code_family || state.code_typeface == nullptr)
        {
            state.code_family = code_family;
            state.code_typeface = resolve_locked(state, code_family);
            if (state.code_typeface == nullptr)
                state.code_typeface = load_ui_typeface_locked(state);
        }
        if (changed)
            invalidate_text_layouts();
    }

    std::u8string configured_ui_family()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        return state.ui_family;
    }

    std::u8string configured_code_family()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        return state.code_family;
    }

    sk_sp<SkTypeface> configured_ui_typeface()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        if (state.ui_typeface == nullptr)
            state.ui_typeface = resolve_locked(state, state.ui_family);
        if (state.ui_typeface == nullptr)
            state.ui_typeface = load_ui_typeface_locked(state);
        return state.ui_typeface;
    }

    sk_sp<SkTypeface> configured_code_typeface()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        if (state.code_typeface == nullptr)
            state.code_typeface = resolve_locked(state, state.code_family);
        if (state.code_typeface == nullptr)
            state.code_typeface = load_ui_typeface_locked(state);
        return state.code_typeface;
    }

    void clear_font_caches()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        state.cache.clear();
        state.fallback_cache.clear();
        state.styled_cache.clear();
        state.system_origins.clear();
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
        // 비운 뒤에 올린다. 먼저 올리면 그 사이 배치가 새 revision으로 낡은 대체 글꼴을 cache에 넣는다.
        invalidate_text_layouts();
    }

    std::size_t fallback_cache_size()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        return state.fallback_cache.size();
    }

    std::size_t styled_cache_size()
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        return state.styled_cache.size();
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
        return fallback_typeface(codepoint, font_style {});
    }

    sk_sp<SkTypeface> fallback_typeface(const char32_t codepoint, const font_style style)
    {
        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        const fallback_key key { codepoint, font_weight_value(style.weight), style.slant };
        if (const auto found { state.fallback_cache.find(key) }; found != state.fallback_cache.end())
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
            //
            // **굵기·기울임도 넘긴다.** 넘기지 않으면 굵은 글 속 한글이 보통 굵기의 대체 face로 와 한 줄의 굵기가 갈린다.
            const std::string language { user_ui_language() };
            const char* bcp47[] { language.c_str() };
            const int count { language.empty() ? 0 : 1 };
            typeface = manager->matchFamilyStyleCharacter(nullptr, skia_style(style), count > 0 ? bcp47 : nullptr, count, static_cast<SkUnichar>(codepoint));
            // 대체 face도 시스템 출처다. 범위 강조가 이 face의 굵기를 다시 바꿀 수 있게 가족 이름을 남긴다.
            if (typeface != nullptr)
            {
                SkString name {};
                typeface->getFamilyName(&name);
                const std::u8string family { reinterpret_cast<const char8_t*>(name.c_str()), name.size() };
                if (text::utf8_is_valid(family))
                    remember_system_locked(state, typeface, family);
            }
        }
        // 상한에 닿으면 캐시를 비운다.
        // 조회 결과가 sk_sp로 소유권을 유지하므로 이미 전달한 typeface는 살아 있다.
        // 새 항목을 넣기 전에 비워 해당 항목이 바로 제거되지 않게 한다.
        if (state.fallback_cache.size() >= fallback_cache_limit)
            state.fallback_cache.clear();
        state.fallback_cache.emplace(key, typeface);
        return typeface;
    }

    styled_typeface style_typeface(SkTypeface* const base, const font_style style)
    {
        if (base == nullptr)
            return {};
        if (style == font_style {})
            return { sk_ref_sp(base) };

        font_registry& state { registry() };
        const std::lock_guard<std::mutex> lock { state.mutex };
        const int wanted { font_weight_value(style.weight) };
        const styled_key key { base->uniqueID(), wanted, style.slant };
        if (const auto found { state.styled_cache.find(key) }; found != state.styled_cache.end())
            return found->second.result;

        sk_sp<SkTypeface> chosen {};
        std::u8string system_family {};
        if (const auto faces { app_family_containing(base->uniqueID()) }; faces.empty() == false)
            chosen = closest_face(faces, style);
        else if (const auto origin { state.system_origins.find(base->uniqueID()) }; origin != state.system_origins.end())
        {
            system_family = origin->second.family;
            if (SkFontMgr* const manager { manager_locked(state) }; manager != nullptr)
                chosen = match_family(*manager, system_family, style);
        }
        if (chosen == nullptr)
            chosen = sk_ref_sp(base);
        chosen = with_weight_axis(std::move(chosen), wanted);
        // 시스템 face에서 얻은 face도 시스템 출처다. 범위 강조가 이 face를 다시 바꿀 때 같은 가족에 묻는다.
        remember_system_locked(state, chosen, system_family);

        const font_synthesis synthesis { synthesis_for(*chosen, style) };
        styled_typeface result { std::move(chosen), synthesis.embolden, synthesis.skew };
        if (state.styled_cache.size() >= styled_cache_limit)
            state.styled_cache.clear();
        state.styled_cache.emplace(key, styled_entry { sk_ref_sp(base), result });
        return result;
    }
}
