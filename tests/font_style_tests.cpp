#include "host/font_registry.h"
#include "luil/text/font_style.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/ui_element.h"
#include "ui/font_synthesis.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

// 굵기·기울임의 규칙 중 글꼴 관리자 없이 서는 것이다. 진짜 face를 찾는지는 Win32 test가 잰다
// (font_style_native_tests.cpp). core test의 관리자는 비어 있어 여기서는 모든 해석이 메움으로 끝난다.
TEST_CASE("Font weights are clamped to the OpenType range", "[text][font-style]")
{
    REQUIRE(luil::font_weight_value(luil::font_weight::regular) == 400);
    REQUIRE(luil::font_weight_value(static_cast<luil::font_weight>(350)) == 350);
    REQUIRE(luil::font_weight_value(static_cast<luil::font_weight>(0)) == 1);
    REQUIRE(luil::font_weight_value(static_cast<luil::font_weight>(5000)) == 1000);
    REQUIRE(luil::font_style {} == luil::font_style { luil::font_weight::regular, luil::font_slant::upright });
}

// 덧칠 문턱은 semibold다. 그보다 가는 요청을 덧칠하면 Bold가 되어 요청보다 굵어진다.
TEST_CASE("Synthesis fills only what the face cannot draw", "[text][font-style]")
{
    const sk_sp<SkTypeface> empty { SkTypeface::MakeEmpty() };
    REQUIRE(luil::synthesis_for(*empty, { luil::font_weight::bold }).embolden);
    REQUIRE(luil::synthesis_for(*empty, { luil::font_weight::semibold }).embolden);
    REQUIRE_FALSE(luil::synthesis_for(*empty, { luil::font_weight::medium }).embolden);
    REQUIRE(luil::synthesis_for(*empty, { luil::font_weight::regular, luil::font_slant::italic }).skew);
    REQUIRE_FALSE(luil::synthesis_for(*empty, {}).skew);
}

// face가 어디서 왔는지 모르면 그 face 그대로 두고 메운다. 관리자가 비어도 죽지 않는다.
TEST_CASE("Styling a face of unknown origin keeps the face and synthesizes", "[text][font-style]")
{
    const sk_sp<SkTypeface> empty { SkTypeface::MakeEmpty() };
    const luil::styled_typeface heavy { luil::style_typeface(empty.get(), { luil::font_weight::bold, luil::font_slant::italic }) };
    REQUIRE(heavy.typeface.get() == empty.get());
    REQUIRE(heavy.embolden);
    REQUIRE(heavy.skew);
    REQUIRE(luil::style_typeface(nullptr, { luil::font_weight::bold }).typeface == nullptr);

    SkFont font { empty, 12.0f };
    luil::apply_font_style(font, { luil::font_weight::bold, luil::font_slant::italic });
    REQUIRE(font.isEmbolden());
    REQUIRE(font.getSkewX() == luil::synthetic_skew);
    REQUIRE(luil::font_style_of(font) == luil::font_style { luil::font_weight::bold, luil::font_slant::italic });

    // 기본 모양으로 되돌리면 메움도 지운다.
    luil::apply_font_style(font, {});
    REQUIRE_FALSE(font.isEmbolden());
    REQUIRE(font.getSkewX() == 0.0f);
    REQUIRE(luil::font_style_of(font) == luil::font_style {});
}

// face가 없는 글꼴도 요청한 모양을 통째로 흉내 낸다.
TEST_CASE("A font without a typeface synthesizes the whole style", "[text][font-style]")
{
    SkFont font {};
    font.setTypeface(nullptr);
    luil::apply_font_style(font, { luil::font_weight::semibold });
    REQUIRE(font.isEmbolden());
    luil::apply_font_style(font, { luil::font_weight::light });
    REQUIRE_FALSE(font.isEmbolden());
}

// 넣은 측정기(test의 고정 폭)는 굵기보다 앞선다. 굵기를 반영하는 것은 측정기의 몫이다.
TEST_CASE("An injected measurer wins over the requested weight", "[text][font-style]")
{
    const luil::text_measurer half_size { [](const std::u8string_view text, const float size) { return static_cast<float>(text.size()) * size / 2.0f; } };
    const luil::measure_context context { .scale = 2.0f, .measurer = &half_size };
    const std::u8string_view text { u8"weight" };
    REQUIRE(context.text_width(text, 10.0f, { luil::font_weight::bold }) == context.text_width(text, 10.0f));
}
