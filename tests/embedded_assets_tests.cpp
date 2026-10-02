#include "win32/embedded_assets.h"
#include "win32/resource_ids.h"

#include "host/font_registry.h"
#include "luil/generated/codicons.h"
#include "luil/text/fonts.h"

#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>

TEST_CASE("Public UI font preferences skip missing families and refresh cached defaults", "[assets][fonts]")
{
    struct restore_preferences
    {
        ~restore_preferences() { luil::set_ui_typeface_families({}); }
    } restore {};
    const auto expected { luil::family_typeface(u8"Arial") };
    REQUIRE(expected != nullptr);
    luil::set_ui_typeface_families({ u8"", u8"luil-missing-font-family", u8"Arial", u8"Segoe UI" });
    const auto actual { luil::configured_ui_typeface() };
    REQUIRE(actual != nullptr);
    SkString expected_name {};
    SkString actual_name {};
    expected->getFamilyName(&expected_name);
    actual->getFamilyName(&actual_name);
    REQUIRE(actual_name == expected_name);
    luil::set_ui_typeface_families({});
    REQUIRE(luil::configured_ui_typeface() != nullptr);
}

TEST_CASE("Executable resources contain the Codicons font and licenses", "[assets]")
{
    std::u8string error;
    REQUIRE(luil::win32::verify_embedded_resources(error));

    const auto typeface = luil::win32::load_codicon_typeface();
    REQUIRE(typeface != nullptr);
    constexpr std::array required_glyphs {
        luil::codicons::icon_source_control,
        luil::codicons::icon_chrome_minimize,
        luil::codicons::icon_chrome_restore,
        luil::codicons::icon_chrome_maximize,
        luil::codicons::icon_chrome_close,
    };
    for (const char32_t codepoint : required_glyphs)
        REQUIRE(typeface->unicharToGlyph(static_cast<SkUnichar>(codepoint)) != 0);

    const auto notice = luil::win32::find_embedded_resource(IDR_THIRD_PARTY_NOTICES);
    REQUIRE(notice.data != nullptr);
    const std::string_view notice_text(static_cast<const char*>(notice.data), notice.size);
    // 고지는 실행 파일에 실제로 포함되는 구성 요소만 담는다
    // Catch2는 test 실행 파일에만 링크되므로 대상이 아니다.
    REQUIRE(notice_text.find("Component: Skia") != std::string_view::npos);
    REQUIRE(notice_text.find("Component: SPIRV-Cross") != std::string_view::npos);
    REQUIRE(notice_text.find("Codicons") != std::string_view::npos);
    // 웹뷰 로더는 정적으로 링크되므로 그 코드가 이 실행 파일에 들어 있다.
    // 웹뷰를 끈 구성(LUIL_ENABLE_WEBVIEW=OFF)에는 로더가 없으므로 고지도 없어야 한다.
#if LUIL_TESTS_WEBVIEW
    REQUIRE(notice_text.find("Component: Microsoft Edge WebView2 SDK") != std::string_view::npos);
#else
    REQUIRE(notice_text.find("Component: Microsoft Edge WebView2 SDK") == std::string_view::npos);
#endif
}
