#include "win32/win32_fonts.h"
#include "luil/text/fonts.h"

#include "luil/text/utf8_text.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {
    // 실제 시스템 대체를 그리기 계층에 붙인다.
    // 앱에서는 `application_window`가 한다.
    class registry_fallback final : public luil::font_fallback
    {
    public:
        [[nodiscard]] sk_sp<SkTypeface> for_codepoint(const char32_t codepoint) const override
        {
            return luil::win32::fallback_typeface(codepoint);
        }
    };

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
} // namespace

// 설치된 글꼴 이름은 미리 보기에서 그대로 Skia로 그려진다.
// UTF-8이 아닌 이름이 하나라도 섞이면 `SkFont::countText`가 -1을 돌려주고 그 값이
// `AutoSTArray::reset`의 `SkASSERT(count >= 0)`에 걸려 **프로세스를 죽인다**.
// GDI font manager가 정확히 그랬다.
//  - 한국어 Windows에서 `맑은 고딕`을 CP949 바이트로 돌려주었다.
TEST_CASE("Installed font family names are valid UTF-8", "[win32][fonts]")
{
    const std::vector<std::u8string> families { luil::installed_font_families() };
    REQUIRE_FALSE(families.empty());

    const SkFont font {};
    for (const std::u8string& family : families)
    {
        INFO("family byte size: " << family.size());
        REQUIRE(luil::text::utf8_is_valid(family));
        // 이름 끝의 NUL은 빈 글리프 하나로 그려진다.
        REQUIRE(family.find(char8_t { 0 }) == std::u8string::npos);
        // 실제로 재 본다.
        // 고쳐지기 전에는 여기에서 abort했다.
        static_cast<void>(luil::measure_text(family, font));
    }
}

// 대체 글꼴을 고를 때 넘기는 언어다.
// 한자는 한국어·일본어·중국어에서 자형이 달라, 이 값이 없으면 시스템이 임의로 고른다.
TEST_CASE("The user UI language is read as a BCP-47 tag", "[win32][fonts]")
{
    const std::string_view language { luil::win32::user_ui_language() };
    // 이름을 읽지 못하는 환경도 있다 — 그때는 지금까지처럼 언어 없이 고른다.
    if (language.empty())
        SKIP("The OS did not report a user locale name.");

    // `ko-KR`·`en-US` 꼴이다: ASCII이고 끝에 NUL이 붙지 않는다.
    REQUIRE(language.size() >= 2u);
    REQUIRE(language.find(char { 0 }) == std::string_view::npos);
    for (const char character : language)
        REQUIRE(static_cast<unsigned char>(character) < 0x80u);

    // 두 번 물어도 같은 값이다 (한 번만 읽는다).
    REQUIRE(luil::win32::user_ui_language() == language);
}

// 대체가 실제로 해석되는지다.
// GDI font manager는 이 진입점이 언제나 nullptr라 `Cascadia Code`에서 한글이 빈 네모가 되었다.
TEST_CASE("The system resolves a fallback typeface for Hangul and emoji", "[win32][fonts]")
{
    const sk_sp<SkTypeface> hangul { luil::win32::fallback_typeface(U'한') };
    REQUIRE(hangul != nullptr);
    REQUIRE(hangul->unicharToGlyph(U'한') != 0);

    const sk_sp<SkTypeface> emoji { luil::win32::fallback_typeface(U'\U0001F600') };
    REQUIRE(emoji != nullptr);
    REQUIRE(emoji->unicharToGlyph(static_cast<SkUnichar>(U'\U0001F600')) != 0);

    // 두 번째 조회는 cache에서 온다.
    //  - 같은 객체여야 한다.
    REQUIRE(luil::win32::fallback_typeface(U'한').get() == hangul.get());
}

// cache가 무한히 자라지 않고, 글꼴이 바뀌면 비워지는지다.
// 비운 뒤에도 **이미 건네진 typeface는 살아 있어야 한다** — 그 계약이 없으면
// 상한도 무효화도 둘 수 없다.
TEST_CASE("The fallback cache is bounded and can be cleared", "[win32][fonts]")
{
    luil::win32::clear_font_caches();
    REQUIRE(luil::win32::fallback_cache_size() == 0u);

    // 찾지 못한 글자도 자리를 차지한다 (같은 글자를 매 frame 다시 뒤지지 않으려는 것이다).
    const sk_sp<SkTypeface> hangul { luil::win32::fallback_typeface(U'한') };
    REQUIRE(luil::win32::fallback_cache_size() == 1u);
    static_cast<void>(luil::win32::fallback_typeface(U'한'));
    REQUIRE(luil::win32::fallback_cache_size() == 1u);

    // 비워도 이미 받아 둔 것은 쓸 수 있다.
    luil::win32::clear_font_caches();
    REQUIRE(luil::win32::fallback_cache_size() == 0u);
    if (hangul != nullptr)
        REQUIRE(hangul->unicharToGlyph(U'한') != 0);

    // 관리자를 **다시 만든 뒤에도** 그대로다.
    // 무효화가 관리자까지 놓으므로 다음 조회가 새로 만든다 — 그 자리에서 낡은 typeface가
    // 함께 무너지면 그리기 thread가 이미 들고 있는 글꼴이 죽는다.
    // DirectWrite typeface는 관리자로 되돌아가는 참조를 들지 않아 안전하다.
    const std::size_t generation { luil::win32::font_manager_generation() };
    static_cast<void>(luil::win32::fallback_typeface(U'글'));
    REQUIRE(luil::win32::font_manager_generation() > generation);
    if (hangul != nullptr)
        REQUIRE(hangul->unicharToGlyph(U'한') != 0);

    // 상한을 넘기면 통째로 비우고 새 항목부터 다시 담는다.
    // 사용하지 않는 사적 사용 영역(U+E000~)으로 채워 시스템 조회를 가볍게 한다.
    for (std::size_t index = 0; index <= luil::win32::fallback_cache_limit; ++index)
        static_cast<void>(luil::win32::fallback_typeface(static_cast<char32_t>(0xE000u + index)));
    REQUIRE(luil::win32::fallback_cache_size() <= luil::win32::fallback_cache_limit);
    REQUIRE(luil::win32::fallback_cache_size() > 0u);

    luil::win32::clear_font_caches();
}

// 무효화가 cache만이 아니라 글꼴 관리자까지 놓는지다.
// `SkFontMgr_win_dw`는 만들 때 얻은 font collection을 수명 내내 들고 있어, 관리자를
// 남겨 두면 `WM_FONTCHANGE` 뒤 다시 해석한 답도 똑같이 낡은 목록에서 온다.
//  - 그러면 무효화 전체가 헛돈다.
TEST_CASE("Clearing the font caches drops the font manager", "[win32][fonts]")
{
    // 앞선 test가 남긴 상태와 무관하게 시작한다.
    luil::win32::clear_font_caches();
    // 이름이 비어 있지 않으면 해석이 관리자를 거친다.
    //  - 그 이름이 실제로 설치돼 있는지는 상관없다. 여기서 재는 것은 관리자다.
    static_cast<void>(luil::win32::family_typeface(u8"Cascadia Code"));
    const std::size_t created { luil::win32::font_manager_generation() };
    REQUIRE(created > 0u);

    // 비우는 것만으로는 아직 만들지 않는다.
    // 목록을 새로 읽는 데 수백 ms까지 걸려 `WM_FONTCHANGE` 처리 안에서 치를 수 없다.
    luil::win32::clear_font_caches();
    REQUIRE(luil::win32::font_manager_generation() == created);

    // 다음 조회가 새 관리자를 만든다.
    static_cast<void>(luil::win32::family_typeface(u8"Cascadia Code"));
    REQUIRE(luil::win32::font_manager_generation() == created + 1u);
}

// 관리자를 다시 만든 뒤에도 설정한 가족이 그 글꼴로 해석되는지다.
// `WM_FONTCHANGE` 한 번에 화면 글꼴이 내장 글꼴로 주저앉으면 무효화가 고침이 아니라 손해다.
TEST_CASE("A rebuilt manager still resolves the configured family", "[win32][fonts]")
{
    const sk_sp<SkTypeface> code { luil::win32::family_typeface(u8"Cascadia Code") };
    if (code == nullptr)
        SKIP("Cascadia Code is not installed on this machine.");

    // UI는 빈 이름(내장 글꼴)으로 남긴다.
    // 다른 test가 `configured_ui_typeface`를 보고 있어 흔들지 않는다.
    luil::win32::set_configured_fonts(u8"", u8"Cascadia Code");
    const sk_sp<SkTypeface> before { luil::win32::configured_code_typeface() };
    REQUIRE(before != nullptr);
    REQUIRE(before->unicharToGlyph(U'A') != 0);

    // `WM_FONTCHANGE`가 하는 일이다: cache도 관리자도 놓는다.
    luil::win32::clear_font_caches();
    const std::size_t dropped { luil::win32::font_manager_generation() };

    // platform은 앱이 고른 **같은 이름**을 다시 세운다.
    luil::win32::set_configured_fonts(u8"", u8"Cascadia Code");
    // 새 관리자로 해석해야 한다 — 낡은 것을 그대로 썼다면 계수기가 늘지 않는다.
    REQUIRE(luil::win32::font_manager_generation() > dropped);

    const sk_sp<SkTypeface> after { luil::win32::configured_code_typeface() };
    REQUIRE(after != nullptr);
    REQUIRE(after->unicharToGlyph(U'A') != 0);
    // 해석에 실패하면 조용히 내장 글꼴로 되돌아가므로 그것과 갈라야 한다.
    // 같은 이름의 조회와 **같은 객체**여야 한다 — 둘 다 같은 cache 항목에서 온다.
    REQUIRE(after.get() == luil::win32::family_typeface(u8"Cascadia Code").get());

    // 설정을 뒤따르는 test에 남기지 않는다.
    luil::win32::set_configured_fonts(u8"", u8"");
    luil::win32::clear_font_caches();
}

// 대체가 붙은 뒤 실제로 폭이 달라지는지다.
// 한글이 `.notdef`로 그려지고 있다면 폭이 그대로였을 것이다.
TEST_CASE("A code font without Hangul still measures Hangul through fallback", "[win32][fonts]")
{
    const sk_sp<SkTypeface> code { luil::win32::family_typeface(u8"Cascadia Code") };
    if (code == nullptr)
        SKIP("Cascadia Code is not installed on this machine.");

    // 전제: 그 글꼴에는 한글 글리프가 없다.
    REQUIRE(code->unicharToGlyph(U'한') == 0);

    const registry_fallback fallback {};
    const installed_fallback_scope scope { fallback };

    const SkFont font { code, 12.0f };
    const std::vector<luil::text_run> runs { luil::split_text_runs(u8"code 한글", font) };
    REQUIRE(runs.size() == 2u);
    REQUIRE(runs[0].text == u8"code ");
    REQUIRE(runs[0].typeface.get() == nullptr);
    REQUIRE(runs[1].text == u8"한글");
    REQUIRE(runs[1].typeface.get() != nullptr);
    REQUIRE(runs[1].typeface->unicharToGlyph(U'한') != 0);
}
