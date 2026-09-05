#include "luil/ui/badge_element.h"

#include "luil/ui/label_element.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    constexpr luil::ui_element_kind kind_badge { luil::application_element_kind(0) };
} // namespace

TEST_CASE("A badge is as tall as a label of the same size", "[ui][badge]")
{
    // 같은 줄에 나란히 서는 것이 흔하므로 두 사이저가 같은 식이어야 한다.
    STATIC_REQUIRE(luil::badge_element::height_for(luil::badge_config {}) == 18.0f);
    STATIC_REQUIRE(luil::badge_element::height_for(luil::badge_config { .font_size = 13.0f }) == 20.0f);
    REQUIRE(luil::badge_element::height_for(luil::badge_config { .font_size = 11.0f }) == luil::label_element::height_for(luil::label_config { .font_size = 11.0f }));
}

TEST_CASE("A badge says severity with a glyph, not with colour alone", "[ui][badge]")
{
    // 고대비는 경고색과 오류색을 같은 전경색으로 접는다.
    // 글리프가 tone에 딸려 오므로 "글리프 없는 경고 배지"를 만들 수 없다.
    STATIC_REQUIRE(luil::badge_element::shows_glyph(luil::badge_tone::warning));
    STATIC_REQUIRE(luil::badge_element::shows_glyph(luil::badge_tone::error));
    // 심각도가 아닌 tone은 글리프를 두지 않는다 — 뜻이 색에 있지 않아서다.
    STATIC_REQUIRE(luil::badge_element::shows_glyph(luil::badge_tone::neutral) == false);
    STATIC_REQUIRE(luil::badge_element::shows_glyph(luil::badge_tone::accent) == false);
}

TEST_CASE("A badge is not interactive", "[ui][badge]")
{
    // 표시뿐이다. 누를 수 있는 chip이 필요해지면 닫기 버튼을 가진 별 element가 선다.
    const luil::badge_element badge { luil::ui_element_id { kind_badge }, luil::badge_config { .text = u8"새것", .tone = luil::badge_tone::accent } };
    REQUIRE(badge.interactive() == false);
    REQUIRE(badge.tab_stop() == false);
    REQUIRE(badge.cursor() == luil::ui_cursor::inherit);
}

TEST_CASE("A badge takes the width it is given", "[ui][badge]")
{
    // 내용에 맞춰 줄지 않는다 — 배치 시점에 글자 폭을 잴 수 없기 때문이다.
    luil::badge_element badge { luil::ui_element_id { kind_badge }, luil::badge_config { .text = u8"아주 긴 배지 글" } };
    badge.arrange({ { 40.0f, 10.0f, 60.0f, 18.0f }, 1.0f });
    REQUIRE(badge.bounds().x == 40.0f);
    REQUIRE(badge.bounds().width == 60.0f);
    REQUIRE(badge.bounds().height == 18.0f);
}
