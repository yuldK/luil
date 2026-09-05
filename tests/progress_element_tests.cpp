#include "luil/ui/progress_element.h"

#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    constexpr luil::ui_element_kind kind_progress { luil::application_element_kind(0) };
} // namespace

TEST_CASE("A progress bar reports its height from the thickness", "[ui][progress]")
{
    // 정적 사이저는 논리 픽셀이다 — 담는 쪽이 tree를 짓기 전에 부른다.
    STATIC_REQUIRE(luil::progress_element::height_for(luil::progress_config {}) == 4.0f);
    STATIC_REQUIRE(luil::progress_element::height_for(luil::progress_config { .value = 0.5f, .thickness = 10.0f }) == 10.0f);
    // 두께가 없으면 높이도 0이다 (음수 두께로 위로 자라지 않는다).
    STATIC_REQUIRE(luil::progress_element::height_for(luil::progress_config { .value = 0.5f, .thickness = -3.0f }) == 0.0f);
}

TEST_CASE("A progress bar clamps its value to the closed unit range", "[ui][progress]")
{
    // 자르는 식이 하나여야 그리기와 test가 어긋나지 않는다.
    STATIC_REQUIRE(luil::progress_element::clamp_value(0.0f) == 0.0f);
    STATIC_REQUIRE(luil::progress_element::clamp_value(0.25f) == 0.25f);
    STATIC_REQUIRE(luil::progress_element::clamp_value(1.0f) == 1.0f);
    STATIC_REQUIRE(luil::progress_element::clamp_value(-2.0f) == 0.0f);
    STATIC_REQUIRE(luil::progress_element::clamp_value(7.5f) == 1.0f);
}

TEST_CASE("A progress bar is not interactive", "[ui][progress]")
{
    // 표시뿐이다. 누를 수 있는 진행률이라는 조합은 만들지 않는다 —
    // 자리를 눌러 옮기는 것은 slider의 일이다.
    const luil::progress_element bar { luil::ui_element_id { kind_progress }, luil::progress_config { .value = 0.5f } };
    REQUIRE(bar.interactive() == false);
    REQUIRE(bar.tab_stop() == false);
    REQUIRE(bar.cursor() == luil::ui_cursor::inherit);
}

TEST_CASE("A progress bar takes the slot it is given", "[ui][progress]")
{
    luil::progress_element bar { luil::ui_element_id { kind_progress }, luil::progress_config { .value = 0.4f } };
    bar.arrange({ { 10.0f, 20.0f, 200.0f, 12.0f }, 2.0f });
    REQUIRE(bar.bounds().x == 10.0f);
    REQUIRE(bar.bounds().y == 20.0f);
    REQUIRE(bar.bounds().width == 200.0f);
    REQUIRE(bar.bounds().height == 12.0f);
}
