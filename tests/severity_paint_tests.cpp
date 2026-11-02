#include "ui/severity_paint.h"

#include "luil/generated/codicons.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/severity.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <set>

namespace {
    constexpr std::array<luil::ui_severity, 4> severities { luil::ui_severity::info, luil::ui_severity::success, luil::ui_severity::warning, luil::ui_severity::error };
}

TEST_CASE("Each severity paints the palette role the toast always used", "[ui][severity]")
{
    // 토스트의 익명 함수에 있던 값 그대로다. info는 중립이라 강조하지 않고 success가 정상의 키 컬러를 쓴다.
    for (const luil::color_theme theme : { luil::color_theme::dark, luil::color_theme::light })
    {
        const luil::ui_color_palette palette { luil::color_palette_for(theme) };
        REQUIRE(luil::severity_color(palette, luil::ui_severity::info) == palette.secondary_foreground);
        REQUIRE(luil::severity_color(palette, luil::ui_severity::success) == palette.accent);
        REQUIRE(luil::severity_color(palette, luil::ui_severity::warning) == palette.warning_accent);
        REQUIRE(luil::severity_color(palette, luil::ui_severity::error) == palette.error_accent);
    }
}

TEST_CASE("Each severity has its own codicon glyph", "[ui][severity]")
{
    REQUIRE(luil::severity_glyph(luil::ui_severity::info) == luil::codicons::icon_info);
    REQUIRE(luil::severity_glyph(luil::ui_severity::success) == luil::codicons::icon_pass);
    REQUIRE(luil::severity_glyph(luil::ui_severity::warning) == luil::codicons::icon_warning);
    REQUIRE(luil::severity_glyph(luil::ui_severity::error) == luil::codicons::icon_error);

    std::set<char32_t> glyphs {};
    for (const luil::ui_severity severity : severities)
        glyphs.insert(luil::severity_glyph(severity));
    REQUIRE(glyphs.size() == severities.size());
}

TEST_CASE("High contrast folds info, warning and error to one color and the glyph keeps the meaning", "[ui][severity]")
{
    luil::high_contrast_colors colors {};
    colors.window_background = luil::make_ui_color(0, 0, 0);
    colors.window_foreground = luil::make_ui_color(255, 255, 255);
    colors.emphasis = luil::make_ui_color(0x37, 0xDC, 0xFF);
    const luil::ui_color_palette palette { luil::high_contrast_palette_for(colors) };

    // 고대비는 경고색과 오류색을 창 전경색으로 접는다. 색만으로는 셋을 가를 수 없다.
    const luil::ui_color info { luil::severity_color(palette, luil::ui_severity::info) };
    REQUIRE(info == colors.window_foreground);
    REQUIRE(luil::severity_color(palette, luil::ui_severity::warning) == info);
    REQUIRE(luil::severity_color(palette, luil::ui_severity::error) == info);
    // success만 hotlight라 따로 선다.
    REQUIRE(luil::severity_color(palette, luil::ui_severity::success) == colors.emphasis);
    REQUIRE(luil::severity_color(palette, luil::ui_severity::success) != info);

    // 그래서 글리프는 네 심각도가 모두 다르다. 색이 접혀도 뜻이 남는다.
    std::set<char32_t> glyphs {};
    for (const luil::ui_severity severity : severities)
        glyphs.insert(luil::severity_glyph(severity));
    REQUIRE(glyphs.size() == 4u);
}
