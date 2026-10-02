#include "luil/theme/ui_style.h"

#include "host/frame_state.h"
#include "luil/theme/ui_theme.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace {
    [[nodiscard]] std::uint32_t alpha_of(const luil::ui_color color) noexcept
    {
        return color >> 24U;
    }

    // 내장값과 확실히 다른 값으로 세운 스타일이다.
    // 바탕·글자·tone·치수를 한 벌 바꿔, 어느 하나라도 새지 않는지 팔레트에서 본다.
    [[nodiscard]] luil::ui_style custom_style()
    {
        luil::ui_style style { luil::default_ui_style() };
        style.dark.window_background = luil::make_ui_color(26, 27, 38);
        style.dark.surface_background = luil::make_ui_color(36, 40, 59);
        style.dark.primary_foreground = luil::make_ui_color(192, 202, 245);
        style.dark.caption.background = luil::make_ui_color(22, 22, 30);
        style.light.window_background = luil::make_ui_color(250, 250, 255);
        style.light.error_accent = luil::make_ui_color(200, 0, 0);
        style.tones.row_selection = 0.5f;
        style.tones.pressed = 0.9f;
        style.metrics.body_font_size = 14.0f;
        style.metrics.control_corner_radius = 0.0f;
        return style;
    }
} // namespace

TEST_CASE("The built-in style is the generated one and carries the documented defaults", "[theme][style]")
{
    const luil::ui_style& style { luil::default_ui_style() };
    // 중립 색은 JSON(assets/style.json)에서 온다 — 전에 코드에 박혀 있던 값 그대로다.
    REQUIRE(style.dark.window_background == luil::make_ui_color(30, 30, 30));
    REQUIRE(style.dark.surface_background == luil::make_ui_color(45, 45, 48));
    REQUIRE(style.dark.primary_foreground == luil::make_ui_color(255, 255, 255));
    REQUIRE(style.dark.caption.close_button_hover_background == luil::make_ui_color(196, 43, 28));
    REQUIRE(style.light.window_background == luil::make_ui_color(248, 248, 248));
    REQUIRE(style.light.primary_foreground == luil::make_ui_color(31, 31, 31));
    REQUIRE(style.light.caption.background == luil::make_ui_color(240, 240, 240));
    // 새 중립 역할은 전경색의 알파 변형이다 (전에 element가 발명하던 0.55·0.25).
    REQUIRE(style.dark.control_border == luil::with_alpha(style.dark.primary_foreground, 0.55f));
    REQUIRE(style.dark.group_border == luil::with_alpha(style.dark.primary_foreground, 0.25f));
    REQUIRE(style.light.control_border == luil::with_alpha(style.light.primary_foreground, 0.55f));
    REQUIRE(style.light.group_border == luil::with_alpha(style.light.primary_foreground, 0.25f));
    // JSON의 tone·치수는 struct의 기본값과 같다 — 둘이 갈리면 "채우지 않은 context"와
    // "내장 스타일"이 다른 그림을 낸다.
    REQUIRE(style.tones == luil::accent_tones {});
    REQUIRE(style.metrics == luil::ui_metrics {});
    REQUIRE(luil::ui_metrics {}.body_font_size == 12.0f);
    REQUIRE(luil::ui_metrics {}.small_font_size == 11.0f);
    REQUIRE(luil::ui_metrics {}.control_corner_radius == 3.0f);
    REQUIRE(luil::ui_metrics {}.row_corner_radius == 4.0f);
}

TEST_CASE("A style answers the neutral palette by theme and folds high contrast onto dark", "[theme][style]")
{
    const luil::ui_style style { custom_style() };
    REQUIRE(style.neutral_for(luil::color_theme::dark) == style.dark);
    REQUIRE(style.neutral_for(luil::color_theme::light) == style.light);
    // 고대비는 중립 색을 쓰지 않는다. 답이 비지 않도록 dark를 돌려줄 뿐이다.
    REQUIRE(style.neutral_for(luil::color_theme::high_contrast) == style.dark);
}

TEST_CASE("A custom style's neutral colors reach the palette and the accent stays the user's", "[theme][style]")
{
    const luil::ui_style style { custom_style() };
    const luil::accent_definition blue { luil::accent_for(u8"blue") };

    SECTION("dark")
    {
        const luil::ui_color_palette palette { luil::color_palette_for(style, luil::color_theme::dark, blue) };
        REQUIRE(palette.window_background == style.dark.window_background);
        REQUIRE(palette.surface_background == style.dark.surface_background);
        REQUIRE(palette.primary_foreground == style.dark.primary_foreground);
        REQUIRE(palette.caption.background == style.dark.caption.background);
        // 키 컬러는 스타일이 아니라 사용자 설정이다.
        REQUIRE(palette.accent == blue.dark.accent);
        REQUIRE(palette.accent_soft == blue.dark.soft);
        // tone도 스타일의 것이다.
        REQUIRE(palette.row_selection_background == luil::with_alpha(blue.dark.soft, 0.5f));
        REQUIRE(palette.accent_pressed == luil::with_alpha(blue.dark.accent, 0.9f));
    }

    SECTION("light")
    {
        const luil::ui_color_palette palette { luil::color_palette_for(style, luil::color_theme::light, blue) };
        REQUIRE(palette.window_background == style.light.window_background);
        REQUIRE(palette.error_accent == style.light.error_accent);
        REQUIRE(palette.danger_button_background == luil::with_alpha(style.light.error_accent, style.tones.danger_button));
        REQUIRE(palette.accent == blue.light.accent);
    }

    SECTION("내장 스타일 경로와 스타일 경로는 같은 답이다")
    {
        for (const luil::color_theme theme : { luil::color_theme::dark, luil::color_theme::light, luil::color_theme::high_contrast })
            REQUIRE(luil::color_palette_for(theme, blue) == luil::color_palette_for(luil::default_ui_style(), theme, blue));
    }
}

TEST_CASE("High contrast ignores the style", "[theme][style]")
{
    // 접근성 설정은 앱의 스타일보다 세다 — 스타일이 무엇이든 시스템 색 fallback이다.
    const luil::ui_style style { custom_style() };
    const luil::accent_definition blue { luil::accent_for(u8"blue") };
    const luil::ui_color_palette palette { luil::color_palette_for(style, luil::color_theme::high_contrast, blue) };
    REQUIRE(palette == luil::high_contrast_palette_for(luil::high_contrast_colors {}));
    REQUIRE(palette.window_background != style.dark.window_background);
}

TEST_CASE("compose_palette folds neutral colors, the accent and the tones into one palette", "[theme][style]")
{
    const luil::neutral_color_palette& neutral { luil::default_ui_style().dark };
    const luil::accent_color_set accent { luil::accent_for(u8"blue").dark };

    SECTION("기본 tone은 element가 쓰던 숫자 그대로다")
    {
        const luil::ui_color_palette palette { luil::compose_palette(neutral, accent) };
        REQUIRE(palette.accent_pressed == luil::with_alpha(accent.accent, 0.70f));
        REQUIRE(palette.soft_button_background == luil::with_alpha(accent.soft, 0.25f));
        REQUIRE(palette.soft_button_hover_background == luil::with_alpha(accent.hover, 0.35f));
        REQUIRE(palette.active_toggle_background == luil::with_alpha(accent.soft, 0.22f));
        REQUIRE(palette.selection_background == luil::with_alpha(accent.soft, 0.35f));
        REQUIRE(palette.row_selection_background == luil::with_alpha(accent.soft, 0.18f));
        REQUIRE(palette.drop_target_background == luil::with_alpha(accent.accent, 0.30f));
        REQUIRE(palette.danger_button_background == luil::with_alpha(neutral.error_accent, 0.20f));
        REQUIRE(palette.danger_button_hover_background == luil::with_alpha(neutral.error_accent, 0.32f));
        REQUIRE(palette.danger_button_pressed_background == luil::with_alpha(neutral.error_accent, 0.42f));
        // 옅은 선택 위의 글자는 본문 글자다.
        REQUIRE(palette.selection_foreground == neutral.primary_foreground);
        // 중립 역할은 그대로 지나간다.
        REQUIRE(palette.control_border == neutral.control_border);
        REQUIRE(palette.group_border == neutral.group_border);
        REQUIRE(palette.caption == neutral.caption);
        REQUIRE(palette.notice_background == neutral.notice_background);
    }

    SECTION("tone을 바꾸면 파생 역할만 따라온다")
    {
        luil::accent_tones tones {};
        tones.drop_target = 1.0f;
        tones.active_toggle = 0.0f;
        const luil::ui_color_palette palette { luil::compose_palette(neutral, accent, tones) };
        REQUIRE(palette.drop_target_background == accent.accent);
        REQUIRE(alpha_of(palette.active_toggle_background) == 0u);
        REQUIRE(palette.accent == accent.accent);
        REQUIRE(palette.window_background == neutral.window_background);
    }

    SECTION("동적 합성과 내장 경로는 같은 규칙이다")
    {
        const luil::accent_definition blue { luil::accent_for(u8"blue") };
        REQUIRE(luil::compose_palette(neutral, accent) == luil::color_palette_for(luil::color_theme::dark, blue));
    }
}

TEST_CASE("with_alpha keeps the color and clamps the alpha", "[theme]")
{
    // 합성이 tone을 얹는 함수와 그리기가 역할색을 겹치는 함수가 같은 것이라 상수식이어야 한다.
    STATIC_REQUIRE(luil::with_alpha(0xFF123456u, 0.0f) == 0x00123456u);
    STATIC_REQUIRE(luil::with_alpha(0x00123456u, 1.0f) == 0xFF123456u);
    STATIC_REQUIRE(luil::with_alpha(0xFF123456u, 1.5f) == 0xFF123456u);
    STATIC_REQUIRE(luil::with_alpha(0xFF123456u, -1.0f) == 0x00123456u);
    STATIC_REQUIRE(luil::with_alpha(0xFF123456u, 0.5f) == 0x80123456u);
}

TEST_CASE("Backgrounds are judged by luminance, not by the theme's name", "[theme][style]")
{
    REQUIRE(luil::relative_luminance(luil::make_ui_color(0, 0, 0)) == 0.0f);
    REQUIRE(luil::relative_luminance(luil::make_ui_color(255, 255, 255)) > 0.999f);
    REQUIRE(luil::relative_luminance(luil::make_ui_color(30, 30, 30)) < luil::relative_luminance(luil::make_ui_color(128, 128, 128)));
    // 알파는 보지 않는다.
    REQUIRE(luil::relative_luminance(luil::make_ui_color(30, 30, 30, 0)) == luil::relative_luminance(luil::make_ui_color(30, 30, 30)));

    // 내장 두 테마는 이름과 값이 일치한다.
    REQUIRE(luil::is_dark_background(luil::default_ui_style().dark.window_background));
    REQUIRE(luil::is_dark_background(luil::default_ui_style().light.window_background) == false);
    REQUIRE(luil::is_dark_background(luil::make_ui_color(0, 0, 0)));
    REQUIRE(luil::is_dark_background(luil::make_ui_color(255, 255, 255)) == false);
    // 중간 회색은 검은 글자가 더 잘 읽히는 쪽이다.
    REQUIRE(luil::is_dark_background(luil::make_ui_color(128, 128, 128)) == false);
    // 앱이 "dark"에 밝은 바탕을 두면 값이 답이다.
    luil::ui_style inverted { luil::default_ui_style() };
    inverted.dark.window_background = luil::make_ui_color(245, 245, 245);
    REQUIRE(luil::is_dark_background(inverted.dark.window_background) == false);
}

TEST_CASE("The frame palette comes from the frame's style and falls back to the built-in one", "[win32][style]")
{
    const luil::accent_definition blue { luil::accent_for(u8"blue") };
    luil::frame_state state {};
    state.theme = luil::color_theme::dark;
    state.accent_id = u8"blue";

    SECTION("스타일이 없으면 내장 스타일이다")
    {
        REQUIRE(&luil::frame_style(state) == &luil::default_ui_style());
        REQUIRE(luil::frame_palette(state) == luil::color_palette_for(luil::color_theme::dark, blue));
    }

    SECTION("실린 스타일은 그리기와 창 테두리가 함께 보는 팔레트가 된다")
    {
        const luil::ui_style style { custom_style() };
        state.style = &style;
        REQUIRE(&luil::frame_style(state) == &style);
        const luil::ui_color_palette palette { luil::frame_palette(state) };
        REQUIRE(palette.window_background == style.dark.window_background);
        REQUIRE(palette.accent == blue.dark.accent);
        state.theme = luil::color_theme::light;
        REQUIRE(luil::frame_palette(state).window_background == style.light.window_background);
    }

    SECTION("고대비는 시스템 색이다 — 스타일이 실려 있어도")
    {
        const luil::ui_style style { custom_style() };
        state.style = &style;
        state.theme = luil::color_theme::high_contrast;
        state.high_contrast.window_background = luil::make_ui_color(255, 255, 255);
        state.high_contrast.window_foreground = luil::make_ui_color(0, 0, 0);
        REQUIRE(luil::frame_palette(state) == luil::high_contrast_palette_for(state.high_contrast));
        REQUIRE(luil::frame_palette(state).window_background == luil::make_ui_color(255, 255, 255));
    }
}
