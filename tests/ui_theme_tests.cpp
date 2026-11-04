#include "luil/theme/ui_theme.h"

#include "luil/theme/ui_style.h"

#include "text_languages_guard.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {
    // 불투명(알파 255)인지다.
    // 팔레트 색은 그리는 쪽이 알파를 낮춰 쓰므로 표의 값 자체는 항상 불투명해야 한다.
    bool opaque(const luil::ui_color value) noexcept
    {
        return (value >> 24U) == 0xFFU;
    }

    int channel(const luil::ui_color value, const int shift) noexcept
    {
        return static_cast<int>((value >> static_cast<unsigned>(shift)) & 0xFFU);
    }

    // 두 색의 채널 차가 전부 허용 안인지다.
    // 합성은 실수 연산이라 표의 손값과 한두 단계 어긋날 수 있다.
    bool close(const luil::ui_color left, const luil::ui_color right, const int tolerance) noexcept
    {
        for (const int shift : { 16, 8, 0 })
            if (channel(left, shift) - channel(right, shift) > tolerance || channel(right, shift) - channel(left, shift) > tolerance)
                return false;
        return true;
    }

    // 밝기 비교용 근사값이다.
    // 같은 색상에서 밝기만 다른 두 색이면 채널 합으로도 순서가 잡힌다.
    int channel_sum(const luil::ui_color value) noexcept
    {
        return channel(value, 16) + channel(value, 8) + channel(value, 0);
    }

    // 실행 시점 항목이 test 밖으로 새지 않게 끝나면 거둔다.
    struct system_accent_scope
    {
        ~system_accent_scope()
        {
            luil::set_system_accent(std::nullopt);
        }
    };
}

namespace {
    [[nodiscard]] float contrast_ratio(const luil::ui_color first, const luil::ui_color second) noexcept
    {
        const float a { luil::relative_luminance(first) };
        const float b { luil::relative_luminance(second) };
        return (std::max(a, b) + 0.05f) / (std::min(a, b) + 0.05f);
    }
}

TEST_CASE("Text on a filled accent or error keeps WCAG AA contrast in every accent and theme", "[theme]")
{
    // 기본 단추의 글자, 체크 표시, 켜진 스위치의 손잡이, 강조 배지의 글자가 채운 강조색 위에 선다.
    // 밝은 테마에서 옅은 바탕용 글자색을 그 자리에 쓰면 1.5:1이라 읽히지 않았다.
    std::vector<luil::accent_definition> accents { luil::accent_catalog().begin(), luil::accent_catalog().end() };
    for (const luil::ui_color key : { luil::make_ui_color(0x00, 0x78, 0xd4), luil::make_ui_color(0xe8, 0x11, 0x23), luil::make_ui_color(0xff, 0xb9, 0x00) })
        accents.push_back(luil::make_system_accent(key));
    for (const luil::accent_definition& accent : accents)
    {
        for (const luil::color_theme theme : { luil::color_theme::light, luil::color_theme::dark })
        {
            const luil::ui_color_palette palette { luil::color_palette_for(theme, accent) };
            const std::string name { reinterpret_cast<const char*>(accent.id.data()), accent.id.size() };
            INFO(name << (theme == luil::color_theme::light ? " light" : " dark"));
            REQUIRE(contrast_ratio(palette.accent_foreground, palette.accent) >= 4.5f);
            REQUIRE(contrast_ratio(palette.error_foreground, palette.error_accent) >= 4.5f);
        }
    }
}

TEST_CASE("A soft accent button keeps its label readable in high contrast", "[theme]")
{
    // 고른 토글과 대화 상자의 강조 단추가 옅은 강조 버튼이다. 검정 바탕과 흰 바탕 고대비 모두에서
    // 글자가 바탕과 같은 색이면 버튼이 통째로 사라진다.
    luil::high_contrast_colors white {};
    white.window_background = luil::make_ui_color(255, 255, 255);
    white.window_foreground = luil::make_ui_color(0, 0, 0);
    white.highlight_background = luil::make_ui_color(0, 0, 0);
    white.highlight_foreground = luil::make_ui_color(255, 255, 255);
    white.button_background = luil::make_ui_color(255, 255, 255);
    white.button_foreground = luil::make_ui_color(0, 0, 0);
    for (const luil::high_contrast_colors& colors : { luil::high_contrast_colors {}, white })
    {
        const luil::ui_color_palette palette { luil::high_contrast_palette_for(colors) };
        REQUIRE(palette.soft_button_background != palette.accent_emphasis_foreground);
        REQUIRE(palette.soft_button_hover_background != palette.accent_emphasis_foreground);
    }
}

TEST_CASE("UI color themes provide semantic caption colors", "[theme]")
{
    const auto dark { luil::color_palette_for(luil::color_theme::dark) };
    const auto high_contrast { luil::color_palette_for(luil::color_theme::high_contrast) };

    REQUIRE(dark.caption.background != dark.window_background);
    REQUIRE(dark.caption.button_hover_background != dark.caption.close_button_hover_background);
    REQUIRE(high_contrast.caption.button_hover_background == high_contrast.caption.close_button_hover_background);
    REQUIRE(high_contrast.caption.button_hover_foreground != high_contrast.caption.button_hover_background);
}

TEST_CASE("The built-in accent catalog is well formed", "[theme][accent]")
{
    const auto catalog { luil::accent_catalog() };
    REQUIRE(catalog.empty() == false);

    std::set<std::u8string> identifiers {};
    for (const luil::accent_definition& accent : catalog)
    {
        REQUIRE(accent.id.empty() == false);
        REQUIRE(accent.label.empty() == false);
        // id는 유일해야 고른 값을 되찾을 수 있다.
        REQUIRE(identifiers.insert(std::u8string { accent.id }).second);

        REQUIRE(opaque(accent.swatch));
        for (const luil::accent_color_set& colors : { accent.dark, accent.light })
        {
            REQUIRE(opaque(colors.accent));
            REQUIRE(opaque(colors.hover));
            REQUIRE(opaque(colors.soft));
            REQUIRE(opaque(colors.emphasis_foreground));
            // 역할이 전부 같은 값이면 hover·강조가 눈에 띄지 않는다.
            REQUIRE(colors.accent != colors.hover);
        }
    }

    // 기본 색은 물러설 곳이라 반드시 있어야 한다.
    REQUIRE(luil::accent_exists(luil::default_accent_id));
    REQUIRE(luil::accent_exists(u8"blue"));
    REQUIRE(luil::accent_exists(u8"없는색") == false);
}

TEST_CASE("Unknown accent ids fall back to the default", "[theme][accent]")
{
    REQUIRE(luil::accent_for(u8"blue").id == u8"blue");
    REQUIRE(luil::accent_for(u8"없는색").id == luil::default_accent_id);
    REQUIRE(luil::accent_for(u8"").id == luil::default_accent_id);
}

TEST_CASE("A system accent synthesized from a catalog key lands beside it", "[theme][accent]")
{
    // 내장 blue의 어두운 accent를 키로 넣으면 그 사다리 옆자리에 서야 한다.
    // 실행 시점 합성이 표와 같은 규칙(고정 밝기·색상 보존)이라는 것을 표 자체로 잡는다.
    const luil::accent_definition blue { luil::accent_for(u8"blue") };
    const luil::accent_definition synthesized { luil::make_system_accent(blue.dark.accent) };
    REQUIRE(close(synthesized.dark.accent, blue.dark.accent, 4));
    REQUIRE(close(synthesized.dark.hover, blue.dark.hover, 4));
    REQUIRE(close(synthesized.dark.soft, blue.dark.soft, 4));
    REQUIRE(close(synthesized.dark.emphasis_foreground, blue.dark.emphasis_foreground, 4));
}

TEST_CASE("The accent name follows the library language and falls back to its label", "[theme][accent][i18n]")
{
    const luil_test::text_languages_guard guard {};
    const luil::accent_definition blue { luil::accent_for(u8"blue") };
    REQUIRE(blue.id == u8"blue");

    // 기본은 한국어이고 `label`은 언어를 바꿔도 `ko` 글 그대로다.
    CHECK(blue.label == u8"블루");
    CHECK(luil::accent_label(blue) == u8"블루");
    for (const luil::accent_definition& accent : luil::accent_catalog())
        CHECK(luil::accent_label(accent) == accent.label);

    guard.use(u8"en-GB");
    CHECK(luil::accent_label(blue) == u8"Blue");
    CHECK(luil::accent_label(luil::accent_for(u8"skyblue")) == u8"Sky blue");
    CHECK(blue.label == u8"블루");
    // 저장소 카탈로그는 모든 항목이 영어 이름을 든다.
    for (const luil::accent_definition& accent : luil::accent_catalog())
    {
        CAPTURE(std::string { accent.id.begin(), accent.id.end() });
        CHECK(luil::accent_label(accent).empty() == false);
        CHECK(luil::accent_label(accent) != accent.label);
    }

    // 시스템 항목은 합성할 때 고른 언어를 `label`이 이미 든다. 카탈로그 밖 항목은 `label`이다.
    const luil::accent_definition system { luil::make_system_accent(luil::make_ui_color(0, 120, 212)) };
    CHECK(system.label == u8"System");
    CHECK(luil::accent_label(system) == u8"System");
    luil::accent_definition custom {};
    custom.id = u8"brand";
    custom.label = u8"Brand";
    CHECK(luil::accent_label(custom) == u8"Brand");

    guard.use(u8"ko");
    CHECK(luil::accent_label(blue) == u8"블루");
    CHECK(luil::make_system_accent(luil::make_ui_color(0, 120, 212)).label == u8"시스템");
}

TEST_CASE("A system accent keeps the roles on the lightness ladder", "[theme][accent]")
{
    // Windows 기본 accent(파랑)다. 실제 입력이 오는 자리와 같은 모양의 값이다.
    const luil::accent_definition value { luil::make_system_accent(luil::make_ui_color(0, 120, 212)) };
    REQUIRE(value.id == luil::system_accent_id);
    REQUIRE(value.label.empty() == false);
    // 대표색은 어두운 테마의 soft다 — 내장 표와 같은 규칙이다.
    REQUIRE(value.swatch == value.dark.soft);

    for (const luil::accent_color_set& colors : { value.dark, value.light })
    {
        REQUIRE(opaque(colors.accent));
        REQUIRE(opaque(colors.hover));
        REQUIRE(opaque(colors.soft));
        REQUIRE(opaque(colors.emphasis_foreground));
        REQUIRE(colors.accent != colors.hover);
    }

    // 어두운 테마는 바탕이 어두우니 역할이 밝아지는 사다리,
    // 밝은 테마는 그 반대다. soft와 강조 글자는 밝은 쪽에서 한 값이다.
    REQUIRE(channel_sum(value.dark.accent) < channel_sum(value.dark.hover));
    REQUIRE(channel_sum(value.dark.hover) < channel_sum(value.dark.soft));
    REQUIRE(channel_sum(value.dark.soft) < channel_sum(value.dark.emphasis_foreground));
    REQUIRE(channel_sum(value.light.accent) > channel_sum(value.light.hover));
    REQUIRE(channel_sum(value.light.hover) > channel_sum(value.light.soft));
    REQUIRE(value.light.soft == value.light.emphasis_foreground);

    // 같은 입력은 같은 답이다.
    const luil::accent_definition again { luil::make_system_accent(luil::make_ui_color(0, 120, 212)) };
    REQUIRE(again.dark.accent == value.dark.accent);
    REQUIRE(again.light.accent == value.light.accent);
}

TEST_CASE("A neutral system key stays neutral", "[theme][accent]")
{
    // 회색 키는 색상 벡터가 없다.
    // 채도를 발명하면 "무채색 accent"라는 사용자의 선택이 사라진다.
    const luil::accent_definition value { luil::make_system_accent(luil::make_ui_color(128, 128, 128)) };
    for (const luil::ui_color color : { value.dark.accent, value.dark.soft, value.light.accent, value.light.soft })
    {
        REQUIRE(channel(color, 16) == channel(color, 8));
        REQUIRE(channel(color, 8) == channel(color, 0));
    }
}

TEST_CASE("The system accent stands as a runtime entry and comes down", "[theme][accent]")
{
    const system_accent_scope scope {};
    // 서기 전의 `system`은 없는 id와 같다 — 조회는 기본색으로 물러선다.
    REQUIRE(luil::system_accent().has_value() == false);
    REQUIRE(luil::accent_exists(luil::system_accent_id) == false);
    REQUIRE(luil::accent_for(luil::system_accent_id).id == luil::default_accent_id);

    const luil::ui_color key { luil::make_ui_color(0, 120, 212) };
    luil::set_system_accent(key);
    REQUIRE(luil::accent_exists(luil::system_accent_id));
    const std::optional<luil::accent_definition> entry { luil::system_accent() };
    REQUIRE(entry.has_value());
    REQUIRE(entry->id == luil::system_accent_id);

    // 목록 표시(system_accent)와 그리기 조회(accent_for)가 한 값에서 나온다.
    const luil::accent_definition looked_up { luil::accent_for(luil::system_accent_id) };
    REQUIRE(looked_up.id == luil::system_accent_id);
    REQUIRE(looked_up.dark.accent == entry->dark.accent);
    REQUIRE(looked_up.dark.accent == luil::make_system_accent(key).dark.accent);

    // 정적 목록은 그대로다 — 실행 시점 항목은 곁에 선다.
    for (const luil::accent_definition& accent : luil::accent_catalog())
        REQUIRE(accent.id != luil::system_accent_id);

    // 거두면 다시 없는 id다.
    luil::set_system_accent(std::nullopt);
    REQUIRE(luil::system_accent().has_value() == false);
    REQUIRE(luil::accent_for(luil::system_accent_id).id == luil::default_accent_id);
}

TEST_CASE("Palettes compose the chosen accent, and high contrast ignores it", "[theme][accent]")
{
    const luil::accent_definition blue { luil::accent_for(u8"blue") };
    const auto dark { luil::color_palette_for(luil::color_theme::dark, blue) };

    REQUIRE(dark.accent == blue.dark.accent);
    REQUIRE(dark.accent_hover == blue.dark.hover);
    REQUIRE(dark.accent_soft == blue.dark.soft);
    REQUIRE(dark.accent_emphasis_foreground == blue.dark.emphasis_foreground);
    // 중립 색은 키 컬러와 무관하게 같다.
    REQUIRE(dark.window_background == luil::color_palette_for(luil::color_theme::dark).window_background);
    REQUIRE(dark.accent != luil::color_palette_for(luil::color_theme::dark).accent);
    // 파생 역할은 고른 키 컬러에서 나온다 — 색조가 accent와 같고 알파만 tone이다.
    REQUIRE((dark.accent_pressed & 0x00FFFFFFu) == (blue.dark.accent & 0x00FFFFFFu));
    REQUIRE((dark.row_selection_background & 0x00FFFFFFu) == (blue.dark.soft & 0x00FFFFFFu));
    REQUIRE((dark.drop_target_background & 0x00FFFFFFu) == (blue.dark.accent & 0x00FFFFFFu));
    REQUIRE(dark.accent_pressed != luil::color_palette_for(luil::color_theme::dark).accent_pressed);
    REQUIRE((dark.accent_pressed >> 24U) < 255u);

    // 고대비는 가독성이 우선이라 키 컬러를 쓰지 않는다.
    // 강조·선택은 시스템 색(hotlight·highlight 짝)이 맡는다.
    const auto high_contrast { luil::color_palette_for(luil::color_theme::high_contrast, blue) };
    REQUIRE(high_contrast.accent != blue.dark.accent);
    REQUIRE(high_contrast.accent_soft != blue.dark.soft);
    // 선택 바탕과 글자는 짝이라 서로 달라야 읽힌다.
    REQUIRE(high_contrast.accent_emphasis_foreground != high_contrast.accent_soft);
}

TEST_CASE("High contrast palettes carry the chosen system colors verbatim", "[theme]")
{
    // "고대비 흰색" 사용자 — 바탕이 희고 글자가 검다.
    // 검정 바탕을 하드코딩하면 이 사용자에게 정반대 화면이 나온다.
    luil::high_contrast_colors white {};
    white.window_background = luil::make_ui_color(255, 255, 255);
    white.window_foreground = luil::make_ui_color(0, 0, 0);
    white.highlight_background = luil::make_ui_color(55, 0, 110);
    white.highlight_foreground = luil::make_ui_color(255, 255, 255);
    white.emphasis = luil::make_ui_color(0, 0, 159);
    white.button_background = luil::make_ui_color(255, 255, 255);
    white.button_foreground = luil::make_ui_color(0, 0, 0);

    white.disabled_foreground = luil::make_ui_color(96, 96, 96);

    const auto palette { luil::high_contrast_palette_for(white) };
    // 고대비는 알파를 섞지 않는다 — 구분선·보조 글자도 온전한 전경색이고
    // 비활성만 시스템의 GRAYTEXT를 그대로 쓴다.
    REQUIRE(palette.divider == white.window_foreground);
    REQUIRE(palette.secondary_foreground == white.window_foreground);
    REQUIRE(palette.disabled_foreground == white.disabled_foreground);
    REQUIRE(palette.input_border == white.window_foreground);
    REQUIRE(palette.window_background == white.window_background);
    REQUIRE(palette.primary_foreground == white.window_foreground);
    REQUIRE(palette.accent == white.emphasis);
    REQUIRE(palette.accent_soft == white.highlight_background);
    REQUIRE(palette.accent_emphasis_foreground == white.highlight_foreground);
    REQUIRE(palette.button_hover_background == white.highlight_background);
    REQUIRE(palette.button_hover_foreground == white.highlight_foreground);
    REQUIRE(palette.caption.background == white.button_background);
    REQUIRE(palette.caption.foreground == white.button_foreground);
    // 새 중립 역할도 온전한 전경색이다.
    REQUIRE(palette.control_border == white.window_foreground);
    REQUIRE(palette.group_border == white.window_foreground);
    // 파생 역할은 알파 없이 highlight 짝으로 접힌다.
    REQUIRE(palette.accent_pressed == white.highlight_background);
    // 옅은 강조 버튼의 글자가 highlight 글자이므로 바탕도 highlight다.
    REQUIRE(palette.soft_button_background == white.highlight_background);
    REQUIRE(palette.soft_button_hover_background == white.highlight_background);
    REQUIRE(palette.active_toggle_background == white.highlight_background);
    REQUIRE(palette.selection_background == white.highlight_background);
    REQUIRE(palette.selection_foreground == white.highlight_foreground);
    REQUIRE(palette.drop_target_background == white.highlight_background);
    REQUIRE(palette.danger_button_background == white.button_background);
    REQUIRE(palette.danger_button_hover_background == white.highlight_background);
    REQUIRE(palette.danger_button_pressed_background == white.highlight_background);
    // 고른 행의 채움만은 접는다 — 행의 글자는 앱이 그려 짝을 맞출 수 없다.
    REQUIRE((palette.row_selection_background >> 24U) == 0u);

    // 기본값은 OS를 읽을 수 없을 때 물러설 검정 바탕이다.
    const auto fallback { luil::high_contrast_palette_for({}) };
    REQUIRE(fallback.window_background == luil::make_ui_color(0, 0, 0));
    REQUIRE(fallback.primary_foreground == luil::make_ui_color(255, 255, 255));
}

TEST_CASE("Neutral palettes define the derived roles as translucent foreground", "[theme]")
{
    // 구분선·비활성·보조 글자·입력칸은 element가 알파를 발명하지 않도록
    // 팔레트가 역할로 내준다. 전경색의 낮은 알파라 테마가 뒤집혀도 성립한다.
    for (const auto theme : { luil::color_theme::dark, luil::color_theme::light })
    {
        const auto palette { luil::color_palette_for(theme) };
        const auto alpha_of { [](const luil::ui_color color) { return color >> 24U; } };
        REQUIRE(alpha_of(palette.divider) < alpha_of(palette.disabled_foreground));
        REQUIRE(alpha_of(palette.disabled_foreground) < alpha_of(palette.secondary_foreground));
        REQUIRE(alpha_of(palette.secondary_foreground) < 255u);
        REQUIRE(alpha_of(palette.input_background) < alpha_of(palette.input_border));
        // 그룹 틀은 구분선보다 진하고, 컨트롤 윤곽은 입력칸 테두리보다 진하다 —
        // 칸의 경계가 아니라 표시 자체의 윤곽이라 눈에 띄어야 한다.
        REQUIRE(alpha_of(palette.divider) < alpha_of(palette.group_border));
        REQUIRE(alpha_of(palette.input_border) < alpha_of(palette.control_border));
        REQUIRE(alpha_of(palette.control_border) < alpha_of(palette.secondary_foreground));
        // 색조는 전경색과 같다 (알파만 다르다).
        REQUIRE((palette.divider & 0x00FFFFFFu) == (palette.primary_foreground & 0x00FFFFFFu));
        REQUIRE((palette.control_border & 0x00FFFFFFu) == (palette.primary_foreground & 0x00FFFFFFu));
        REQUIRE((palette.group_border & 0x00FFFFFFu) == (palette.primary_foreground & 0x00FFFFFFu));
    }
}

namespace {
    // 위 색을 아래 바탕에 얹은 불투명 색이다. 입력 칸이 실제로 칠하는 바탕을 만든다.
    [[nodiscard]] luil::ui_color composite_over(const luil::ui_color top, const luil::ui_color bottom) noexcept
    {
        const int alpha { static_cast<int>(top >> 24U) };
        const auto mix = [&](const int shift) { return (channel(top, shift) * alpha + channel(bottom, shift) * (255 - alpha) + 127) / 255; };
        return luil::make_ui_color(static_cast<std::uint8_t>(mix(16)), static_cast<std::uint8_t>(mix(8)), static_cast<std::uint8_t>(mix(0)));
    }

    constexpr luil::ui_color luil::ui_color_palette::* syntax_roles[] {
        &luil::ui_color_palette::syntax_keyword,
        &luil::ui_color_palette::syntax_string,
        &luil::ui_color_palette::syntax_comment,
        &luil::ui_color_palette::syntax_number,
        &luil::ui_color_palette::syntax_type,
        &luil::ui_color_palette::syntax_function,
    };
}

TEST_CASE("Syntax colors keep WCAG AA contrast on every surface a code view draws on", "[theme][syntax]")
{
    // 코드 칸의 바탕은 창, 표면, 그 위에 입력 칸의 옅은 바탕을 얹은 색이다. 마지막이 입력 칸이 실제로 칠하는 색이다.
    for (const luil::color_theme theme : { luil::color_theme::dark, luil::color_theme::light })
    {
        const luil::ui_color_palette palette { luil::color_palette_for(theme) };
        const luil::ui_color backgrounds[] {
            palette.window_background,
            palette.surface_background,
            composite_over(palette.input_background, palette.window_background),
            composite_over(palette.input_background, palette.surface_background),
        };
        for (const auto role : syntax_roles)
        {
            REQUIRE(opaque(palette.*role));
            REQUIRE(palette.*role != palette.primary_foreground);
            for (const luil::ui_color background : backgrounds)
            {
                INFO("color " << (palette.*role) << " on " << background << (theme == luil::color_theme::light ? " light" : " dark"));
                REQUIRE(contrast_ratio(palette.*role, background) >= 4.5f);
            }
        }
    }
}

TEST_CASE("Syntax colors tell the six roles apart in each theme and do not follow the key color", "[theme][syntax]")
{
    for (const luil::color_theme theme : { luil::color_theme::dark, luil::color_theme::light })
    {
        const luil::ui_color_palette palette { luil::color_palette_for(theme, luil::accent_for(u8"blue")) };
        std::set<luil::ui_color> distinct {};
        for (const auto role : syntax_roles)
            distinct.insert(palette.*role);
        REQUIRE(distinct.size() == std::size(syntax_roles));
        // 키 컬러는 경고·오류색처럼 구문 색을 바꾸지 못한다.
        const luil::ui_color_palette other { luil::color_palette_for(theme, luil::accent_for(u8"amber")) };
        for (const auto role : syntax_roles)
            REQUIRE(palette.*role == other.*role);
    }
}

TEST_CASE("High contrast folds the six syntax roles into the window foreground", "[theme][syntax]")
{
    luil::high_contrast_colors white {};
    white.window_background = luil::make_ui_color(255, 255, 255);
    white.window_foreground = luil::make_ui_color(0, 0, 0);
    white.highlight_background = luil::make_ui_color(55, 0, 110);
    white.highlight_foreground = luil::make_ui_color(255, 255, 255);
    for (const luil::high_contrast_colors& colors : { luil::high_contrast_colors {}, white })
    {
        const luil::ui_color_palette palette { luil::high_contrast_palette_for(colors) };
        for (const auto role : syntax_roles)
            REQUIRE(palette.*role == colors.window_foreground);
        // 본문과 같은 색이라 바탕 위의 대비가 본문 글자와 같다.
        REQUIRE(contrast_ratio(palette.syntax_keyword, palette.window_background) >= 7.0f);
    }
}

TEST_CASE("Composition moves the six syntax colors verbatim from the neutral palette", "[theme][syntax]")
{
    luil::neutral_color_palette neutral { luil::default_ui_style().dark };
    neutral.syntax_keyword = luil::make_ui_color(1, 2, 3);
    neutral.syntax_string = luil::make_ui_color(4, 5, 6);
    neutral.syntax_comment = luil::make_ui_color(7, 8, 9);
    neutral.syntax_number = luil::make_ui_color(10, 11, 12);
    neutral.syntax_type = luil::make_ui_color(13, 14, 15);
    neutral.syntax_function = luil::make_ui_color(16, 17, 18);
    const luil::ui_color_palette palette { luil::compose_palette(neutral, luil::accent_for(u8"blue").dark) };
    REQUIRE(palette.syntax_keyword == neutral.syntax_keyword);
    REQUIRE(palette.syntax_string == neutral.syntax_string);
    REQUIRE(palette.syntax_comment == neutral.syntax_comment);
    REQUIRE(palette.syntax_number == neutral.syntax_number);
    REQUIRE(palette.syntax_type == neutral.syntax_type);
    REQUIRE(palette.syntax_function == neutral.syntax_function);
}

TEST_CASE("The theme preference resolves against high contrast and the OS setting", "[theme]")
{
    using luil::color_theme;
    using luil::theme_preference;

    // 고대비는 접근성 설정이라 어떤 선호보다 세다.
    REQUIRE(luil::resolve_color_theme(theme_preference::light, true, true) == color_theme::high_contrast);
    REQUIRE(luil::resolve_color_theme(theme_preference::dark, true, false) == color_theme::high_contrast);

    // 명시 선호는 OS 설정을 무시한다.
    REQUIRE(luil::resolve_color_theme(theme_preference::light, false, false) == color_theme::light);
    REQUIRE(luil::resolve_color_theme(theme_preference::dark, false, true) == color_theme::dark);

    // system은 OS의 앱 모드를 따른다.
    REQUIRE(luil::resolve_color_theme(theme_preference::system, false, true) == color_theme::light);
    REQUIRE(luil::resolve_color_theme(theme_preference::system, false, false) == color_theme::dark);
}

TEST_CASE("The light palette inverts the neutral colors and takes the light accent", "[theme][accent]")
{
    const luil::accent_definition blue { luil::accent_for(u8"blue") };
    const auto light { luil::color_palette_for(luil::color_theme::light, blue) };
    const auto dark { luil::color_palette_for(luil::color_theme::dark, blue) };

    // 밝은 바탕과 어두운 글자다.
    // 알파를 낮춰 겹치는 그리기 코드가 그대로 성립한다.
    REQUIRE(light.window_background != dark.window_background);
    REQUIRE(light.surface_background != dark.surface_background);
    REQUIRE(light.primary_foreground != dark.primary_foreground);
    REQUIRE(light.caption.background != dark.caption.background);

    // 키 컬러는 테마별 정의를 쓴다.
    REQUIRE(light.accent == blue.light.accent);
    REQUIRE(light.accent_hover == blue.light.hover);
    REQUIRE(light.accent_soft == blue.light.soft);
    REQUIRE(light.accent_emphasis_foreground == blue.light.emphasis_foreground);
    REQUIRE(light.accent != dark.accent);
}

TEST_CASE("Theme preference names round trip", "[theme]")
{
    for (const luil::theme_preference value : { luil::theme_preference::system, luil::theme_preference::light, luil::theme_preference::dark })
    {
        luil::theme_preference parsed { luil::theme_preference::system };
        REQUIRE(luil::parse_theme_preference(luil::theme_preference_name(value), parsed));
        REQUIRE(parsed == value);
    }

    luil::theme_preference untouched { luil::theme_preference::dark };
    REQUIRE(luil::parse_theme_preference(u8"neon", untouched) == false);
    REQUIRE(untouched == luil::theme_preference::dark);
}
