#include "luil/ui/draw_primitives.h"

#include "win32/embedded_assets.h"
#include "win32/win32_fonts.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // 고른 codepoint에만 대체를 내주는 가짜 해석기다.
    // 실제 시스템 글꼴에 기대지 않고 run 경계만 확인한다.
    class scripted_fallback final : public luil::font_fallback
    {
    public:
        scripted_fallback(SkTypeface* const typeface, std::set<char32_t> covered)
            : typeface_ { typeface }
            , covered_ { std::move(covered) }
        {}

        [[nodiscard]] sk_sp<SkTypeface> for_codepoint(const char32_t codepoint) const override
        {
            return covered_.contains(codepoint) ? sk_ref_sp(typeface_) : sk_sp<SkTypeface> {};
        }

    private:
        SkTypeface* typeface_ { nullptr };
        std::set<char32_t> covered_ {};
    };

    // 등록은 전역이므로 test가 끝나면 반드시 되돌린다.
    class installed_fallback_scope
    {
    public:
        explicit installed_fallback_scope(const luil::font_fallback& fallback)
        {
            luil::set_font_fallback(&fallback);
        }

        installed_fallback_scope(const installed_fallback_scope&) = delete;
        installed_fallback_scope(installed_fallback_scope&&) = delete;
        installed_fallback_scope& operator=(const installed_fallback_scope&) = delete;
        installed_fallback_scope& operator=(installed_fallback_scope&&) = delete;

        ~installed_fallback_scope()
        {
            luil::set_font_fallback(nullptr);
        }
    };

    [[nodiscard]] std::u8string joined(const std::vector<luil::text_run>& runs)
    {
        std::u8string result {};
        for (const luil::text_run& run : runs)
            result.append(run.text);
        return result;
    }
} // namespace

TEST_CASE("Text runs stay whole when no fallback is installed", "[ui][fonts]")
{
    // 대체 해석기가 없으면 입력 전체를 별도 typeface 없는 단일 run으로 반환한다.
    const SkFont font {};
    const std::vector<luil::text_run> runs { luil::split_text_runs(u8"한글 abc 😀", font) };

    REQUIRE(runs.size() == 1u);
    REQUIRE(runs.front().typeface.get() == nullptr);
    REQUIRE(runs.front().text == u8"한글 abc 😀");
}

TEST_CASE("Text runs split only where the chosen font has no glyph", "[ui][fonts]")
{
    // Codicons는 아이콘 전용이라 한글도 라틴도 담지 않는다.
    // 그래서 "기본 글꼴에 없는 글자"를 시스템 글꼴 구성과 무관하게 만들 수 있다.
    const sk_sp<SkTypeface> codicons { luil::win32::load_codicon_typeface() };
    REQUIRE(codicons != nullptr);

    const SkFont font { codicons, 12.0f };
    REQUIRE(font.unicharToGlyph(U'한') == 0);

    const sk_sp<SkTypeface> substitute { luil::win32::configured_ui_typeface() };
    REQUIRE(substitute != nullptr);

    // `한`만 대체가 있고 `A`는 없다.
    // `A`도 Codicons에는 없지만 대체를 찾지 못하므로 기본 글꼴로 남는다.
    //  - 빈 네모가 보이는 편이 침묵보다 낫다.
    const scripted_fallback fallback { substitute.get(), { U'한' } };
    const installed_fallback_scope scope { fallback };

    const std::vector<luil::text_run> runs { luil::split_text_runs(u8"A한A", font) };

    REQUIRE(runs.size() == 3u);
    REQUIRE(runs[0].text == u8"A");
    REQUIRE(runs[0].typeface.get() == nullptr);
    REQUIRE(runs[1].text == u8"한");
    REQUIRE(runs[1].typeface.get() == substitute.get());
    // 대체가 필요했던 글자 뒤에 원래 글꼴로 **돌아온다**.
    REQUIRE(runs[2].text == u8"A");
    REQUIRE(runs[2].typeface.get() == nullptr);
}

TEST_CASE("Text runs cover the input exactly", "[ui][fonts]")
{
    const sk_sp<SkTypeface> codicons { luil::win32::load_codicon_typeface() };
    REQUIRE(codicons != nullptr);
    const sk_sp<SkTypeface> substitute { luil::win32::configured_ui_typeface() };
    REQUIRE(substitute != nullptr);

    const SkFont font { codicons, 12.0f };
    // BMP 밖 문자를 포함한다.
    // 경계가 codepoint 단위라 4바이트도 쪼개지지 않는다.
    const scripted_fallback fallback { substitute.get(), { U'가', U'\U0001F600' } };
    const installed_fallback_scope scope { fallback };

    constexpr std::u8string_view text { u8"ab 가나 😀 cd" };
    const std::vector<luil::text_run> runs { luil::split_text_runs(text, font) };

    REQUIRE(runs.empty() == false);
    REQUIRE(joined(runs) == text);
    for (const luil::text_run& run : runs)
        REQUIRE(run.text.empty() == false);
}

TEST_CASE("Measuring adds up the run widths", "[ui][fonts]")
{
    const sk_sp<SkTypeface> codicons { luil::win32::load_codicon_typeface() };
    REQUIRE(codicons != nullptr);
    const sk_sp<SkTypeface> substitute { luil::win32::configured_ui_typeface() };
    REQUIRE(substitute != nullptr);

    const SkFont font { codicons, 12.0f };
    constexpr std::u8string_view text { u8"한글" };

    const float without_fallback { luil::measure_text(text, font) };

    const scripted_fallback fallback { substitute.get(), { U'한', U'글' } };
    const installed_fallback_scope scope { fallback };
    const float with_fallback { luil::measure_text(text, font) };

    // 대체 글꼴로 실제 글자를 그리므로 폭이 `.notdef` 두 개와 같을 수 없다.
    // 측정이 run을 따라가지 않으면 이 값이 그대로였을 것이고, caret 자리가 글자와 어긋났을 것이다.
    REQUIRE(with_fallback > 0.0f);
    REQUIRE(with_fallback != without_fallback);
}
