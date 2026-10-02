#include "luil/ui/button_element.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    constexpr luil::ui_element_kind kind_button { luil::application_element_kind(0) };

    // 테마 하나를 골라 고정한다.
    // 색 값을 적어 두지 않고 **팔레트의 어느 자리에서 길어 오는지**를 비교하므로
    // 테마 색이 바뀌어도 이 test는 그대로 서 있다.
    const luil::ui_color_palette dark { luil::color_palette_for(luil::color_theme::dark) };
} // namespace

TEST_CASE("An icon button reads every colour from a palette role", "[ui][button]")
{
    SECTION("도구 막대는 일반 버튼 역할을 쓴다")
    {
        const luil::button_colors colors { luil::button_colors_for(luil::button_config {}, dark) };
        REQUIRE(colors.foreground == dark.primary_foreground);
        REQUIRE(colors.hover_background == dark.button_hover_background);
        REQUIRE(colors.hover_foreground == dark.button_hover_foreground);
        REQUIRE(colors.pressed_background == dark.button_pressed_background);
        // 쉬는 동안에는 아무것도 깔지 않는다 — 그 자리를 채우는 역할이 아예 없다.
        REQUIRE(colors.rest_background == 0u);
    }

    SECTION("창 캡션의 버튼은 캡션 팔레트를 쓴다")
    {
        const luil::button_colors colors { luil::button_colors_for(luil::button_config { .role = luil::button_visual_role::caption }, dark) };
        REQUIRE(colors.foreground == dark.caption.foreground);
        REQUIRE(colors.hover_background == dark.caption.button_hover_background);
        REQUIRE(colors.hover_foreground == dark.caption.button_hover_foreground);
        // 캡션 버튼은 눌림에 따로 색을 두지 않는다 — hover와 같은 자리다.
        REQUIRE(colors.pressed_background == dark.caption.button_hover_background);
        REQUIRE(colors.rest_background == 0u);
    }

    SECTION("캡션의 닫기는 hover에서만 붉어진다")
    {
        const luil::button_colors colors { luil::button_colors_for(luil::button_config { .role = luil::button_visual_role::caption_close }, dark) };
        // 쉬는 동안의 글리프는 다른 캡션 버튼과 같다. 자리와 모양이 이미 뜻을 말한다.
        REQUIRE(colors.foreground == dark.caption.foreground);
        REQUIRE(colors.hover_background == dark.caption.close_button_hover_background);
        REQUIRE(colors.hover_foreground == dark.caption.close_button_hover_foreground);
        REQUIRE(colors.pressed_background == dark.caption.close_button_hover_background);
    }

    SECTION("되돌릴 수 없는 동작은 쉬는 동안에도 오류색이다")
    {
        const luil::button_colors colors { luil::button_colors_for(luil::button_config { .role = luil::button_visual_role::danger }, dark) };
        // 도구 막대의 아이콘 하나는 눌러 보기 전에 무엇이 사라지는지 말하지 않는다.
        // 그래서 캡션의 닫기와 달리 글리프가 먼저 붉다.
        REQUIRE(colors.foreground == dark.error_accent);
        REQUIRE(colors.hover_background == dark.error_accent);
        // 채운 오류색 위의 글자는 그 바탕을 위해 만든 역할이다.
        REQUIRE(colors.hover_foreground == dark.error_foreground);
        REQUIRE(colors.pressed_background == dark.error_accent);
        // 붉은 것은 색이지 바탕이 아니다 — 쉬는 동안 깔리는 판은 여전히 없다.
        REQUIRE(colors.rest_background == 0u);
    }
}

TEST_CASE("A toggled-on icon button keeps its state visible at rest", "[ui][button]")
{
    const luil::button_config toggled { .role = luil::button_visual_role::toolbar, .active = true };
    const luil::button_colors colors { luil::button_colors_for(toggled, dark) };

    SECTION("켜진 토글은 쉬는 동안의 두 색으로 말한다")
    {
        // 켜진 토글의 바탕은 팔레트의 파생 역할이다 — element가 알파를 발명하지 않는다.
        REQUIRE(colors.rest_background == dark.active_toggle_background);
        REQUIRE(colors.foreground == dark.accent_emphasis_foreground);
    }

    SECTION("hover와 눌림은 켜져 있어도 자기 색으로 답한다")
    {
        // 상태 표시가 반응을 덮으면 눌린 것인지 알 수 없다.
        REQUIRE(colors.hover_background == dark.button_hover_background);
        REQUIRE(colors.hover_foreground == dark.button_hover_foreground);
        REQUIRE(colors.pressed_background == dark.button_pressed_background);
    }

    SECTION("꺼진 토글은 지금까지의 버튼 그대로다")
    {
        REQUIRE(luil::button_colors_for(luil::button_config {}, dark) == luil::button_colors_for(luil::button_config { .active = false }, dark));
    }
}

TEST_CASE("A palette selector replaces exactly one slot of an icon button", "[ui][button]")
{
    const luil::button_colors role_colors { luil::button_colors_for(luil::button_config {}, dark) };

    SECTION("선택자를 주지 않으면 역할이 정한 색 그대로다")
    {
        // 다섯 줄이 전부 비어 있는 것이 지금까지의 버튼이고, 그것이 기본값이다.
        REQUIRE(luil::button_colors_for(luil::button_config { .glyph = U'A', .icon_size = 20.0f }, dark) == role_colors);
    }

    SECTION("글리프 색만 바뀐다")
    {
        luil::button_config config {};
        config.foreground = [](const luil::ui_color_palette& palette) { return palette.error_accent; };
        luil::button_colors expected { role_colors };
        expected.foreground = dark.error_accent;
        REQUIRE(luil::button_colors_for(config, dark) == expected);
    }

    SECTION("hover 바탕만 바뀐다")
    {
        luil::button_config config {};
        config.hover_background = [](const luil::ui_color_palette& palette) { return palette.notice_background; };
        luil::button_colors expected { role_colors };
        expected.hover_background = dark.notice_background;
        REQUIRE(luil::button_colors_for(config, dark) == expected);
    }

    SECTION("hover 글리프만 바뀐다")
    {
        luil::button_config config {};
        config.hover_foreground = [](const luil::ui_color_palette& palette) { return palette.accent_emphasis_foreground; };
        luil::button_colors expected { role_colors };
        expected.hover_foreground = dark.accent_emphasis_foreground;
        REQUIRE(luil::button_colors_for(config, dark) == expected);
    }

    SECTION("눌림 바탕만 바뀐다")
    {
        luil::button_config config {};
        config.pressed_background = [](const luil::ui_color_palette& palette) { return palette.accent; };
        luil::button_colors expected { role_colors };
        expected.pressed_background = dark.accent;
        REQUIRE(luil::button_colors_for(config, dark) == expected);
    }

    SECTION("쉬는 동안의 바탕은 선택자가 처음 세운다")
    {
        // 어느 역할도 채우지 않는 자리라, 이 선택자만이 아무것도 없던 곳에 판을 깐다.
        luil::button_config config {};
        config.rest_background = [](const luil::ui_color_palette& palette) { return palette.input_background; };
        luil::button_colors expected { role_colors };
        expected.rest_background = dark.input_background;
        REQUIRE(luil::button_colors_for(config, dark) == expected);
    }

    SECTION("선택자는 켜진 토글보다 세다")
    {
        // 앱이 이름 지어 고른 색보다 라이브러리의 기본값이 세면 escape hatch가 아니다.
        luil::button_config config { .active = true };
        config.rest_background = [](const luil::ui_color_palette& palette) { return palette.input_background; };
        config.foreground = [](const luil::ui_color_palette& palette) { return palette.secondary_foreground; };
        const luil::button_colors colors { luil::button_colors_for(config, dark) };
        REQUIRE(colors.rest_background == dark.input_background);
        REQUIRE(colors.foreground == dark.secondary_foreground);
    }

    SECTION("역할이 무엇이든 선택자가 그 자리를 대신한다")
    {
        // 선택자는 역할의 대체가 아니라 어떤 역할도 이름 붙일 수 없는 모양을 위한 자리다.
        // 그래서 role이 바뀌어도 손댄 자리의 답은 같다.
        luil::button_config config { .role = luil::button_visual_role::danger };
        config.hover_background = [](const luil::ui_color_palette& palette) { return palette.warning_accent; };
        const luil::button_colors colors { luil::button_colors_for(config, dark) };
        REQUIRE(colors.hover_background == dark.warning_accent);
        // 나머지는 여전히 danger가 말한다.
        REQUIRE(colors.foreground == dark.error_accent);
        REQUIRE(colors.pressed_background == dark.error_accent);
    }
}

TEST_CASE("A disabled icon button dims with a palette role, not with its own colours", "[ui][button]")
{
    // 비활성 흐림은 element마다 알파를 발명하지 않고 팔레트 역할 하나
    // (`disabled_foreground`)로 그리는 쪽에서 갈린다. 그래서 설정에도, 이 순수 함수에도
    // 비활성이라는 자리가 없다 — 켜고 끄는 것은 element의 상태이고 색은 그대로 남는다.
    luil::button_element button { luil::ui_element_id { kind_button }, luil::button_config { .role = luil::button_visual_role::danger } };
    REQUIRE(button.enabled());
    button.set_enabled(false);
    REQUIRE(button.enabled() == false);

    // 어느 역할도 비활성 색을 답하지 않는다. 그 색이 화면에 서는 길은 그리기 하나뿐이라,
    // 역할 하나가 우연히 같은 색으로 접히면 꺼진 버튼과 켜진 버튼이 구별되지 않는다.
    REQUIRE(luil::button_colors_for(luil::button_config {}, dark).foreground != dark.disabled_foreground);
    REQUIRE(luil::button_colors_for(luil::button_config { .role = luil::button_visual_role::caption }, dark).foreground != dark.disabled_foreground);
    REQUIRE(luil::button_colors_for(luil::button_config { .role = luil::button_visual_role::caption_close }, dark).foreground != dark.disabled_foreground);
    REQUIRE(luil::button_colors_for(luil::button_config { .role = luil::button_visual_role::danger }, dark).foreground != dark.disabled_foreground);
}
