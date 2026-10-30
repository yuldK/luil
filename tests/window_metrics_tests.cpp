#include "luil/app/app_delegate.h"
#include "luil/ui/layout_metrics.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {
    struct metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 0.0f };
    };

    // 세 값짜리만 넓힌 기존 앱의 delegate다.
    class old_delegate final : public luil::app_delegate
    {
    public:
        [[nodiscard]] luil::app_message make_window_metrics_message(const float width, const float height, const float scale) override
        {
            return luil::app_message { metrics_intent { width, height, scale } };
        }
    };

    // 폭 등급을 받는 새 짝을 넓힌 delegate다.
    class class_delegate final : public luil::app_delegate
    {
    public:
        [[nodiscard]] luil::app_message make_metrics_message(const luil::window_metrics& metrics) override
        {
            return luil::app_message { metrics };
        }
    };
}

TEST_CASE("Window width classes break at 600 and 840 logical pixels", "[app][window][width_class]")
{
    using luil::classify_window_width;
    using luil::window_width_class;
    REQUIRE(classify_window_width(0.0f) == window_width_class::compact);
    REQUIRE(classify_window_width(599.9f) == window_width_class::compact);
    REQUIRE(classify_window_width(600.0f) == window_width_class::medium);
    REQUIRE(classify_window_width(839.9f) == window_width_class::medium);
    REQUIRE(classify_window_width(840.0f) == window_width_class::expanded);
    REQUIRE(classify_window_width(2000.0f) == window_width_class::expanded);
    REQUIRE(classify_window_width(std::numeric_limits<float>::quiet_NaN()) == window_width_class::compact);
    REQUIRE(classify_window_width(std::numeric_limits<float>::infinity()) == window_width_class::expanded);
    // 휴대폰 세로 384dp·가로 823dp, 펼친 접는 기기 673dp, 태블릿 가로 1280dp다.
    REQUIRE(classify_window_width(384.0f) == window_width_class::compact);
    REQUIRE(classify_window_width(823.0f) == window_width_class::medium);
    REQUIRE(classify_window_width(673.0f) == window_width_class::medium);
    REQUIRE(classify_window_width(1280.0f) == window_width_class::expanded);
    static_assert(classify_window_width(840.0f) == window_width_class::expanded);
}

TEST_CASE("Window width class breakpoints can be moved", "[app][window][width_class]")
{
    using luil::classify_window_width;
    using luil::window_width_class;
    constexpr luil::window_width_breakpoints wide_panes { .medium = 480.0f, .expanded = 1024.0f };
    REQUIRE(classify_window_width(479.0f, wide_panes) == window_width_class::compact);
    REQUIRE(classify_window_width(480.0f, wide_panes) == window_width_class::medium);
    REQUIRE(classify_window_width(1023.0f, wide_panes) == window_width_class::medium);
    REQUIRE(classify_window_width(1024.0f, wide_panes) == window_width_class::expanded);
    // 경계가 뒤집히면 expanded는 서지 않는다.
    constexpr luil::window_width_breakpoints flipped { .medium = 900.0f, .expanded = 700.0f };
    REQUIRE(classify_window_width(800.0f, flipped) == window_width_class::compact);
    REQUIRE(classify_window_width(950.0f, flipped) == window_width_class::medium);
}

TEST_CASE("Window metrics carry the width class of the logical width", "[app][window][width_class]")
{
    // 물리 폭을 배율로 나눈 논리 폭의 등급이다.
    REQUIRE(luil::make_window_metrics(1080.0f, 2241.0f, 2.8125f).width_class == luil::window_width_class::compact);
    REQUIRE(luil::make_window_metrics(2316.0f, 1080.0f, 2.8125f).width_class == luil::window_width_class::medium);
    REQUIRE(luil::make_window_metrics(1920.0f, 1080.0f, 1.5f).width_class == luil::window_width_class::expanded);
    // 배율을 모르면 1로 본다.
    const luil::window_metrics unknown { luil::make_window_metrics(700.0f, 400.0f, 0.0f) };
    REQUIRE(unknown.scale == 1.0f);
    REQUIRE(unknown.width_class == luil::window_width_class::medium);
}

TEST_CASE("Delegates written for three values keep working with the class hook", "[app][window][width_class]")
{
    const luil::window_metrics metrics { luil::make_window_metrics(2316.0f, 1080.0f, 2.8125f) };
    // 세 값짜리만 넓힌 앱에는 host가 부르는 새 훅이 그대로 이어 준다.
    old_delegate old {};
    luil::app_delegate& old_base { old };
    const luil::app_message forwarded { old_base.make_metrics_message(metrics) };
    REQUIRE(forwarded.get<metrics_intent>() != nullptr);
    REQUIRE(forwarded.get<metrics_intent>()->width == 2316.0f);
    REQUIRE(forwarded.get<metrics_intent>()->scale == 2.8125f);
    // 새 훅을 넓힌 앱은 등급까지 받는다.
    class_delegate fresh {};
    luil::app_delegate& fresh_base { fresh };
    const luil::app_message classified { fresh_base.make_metrics_message(metrics) };
    REQUIRE(classified.get<luil::window_metrics>() != nullptr);
    REQUIRE(classified.get<luil::window_metrics>()->width_class == luil::window_width_class::medium);
    // 아무것도 넓히지 않은 delegate는 메시지가 없다.
    luil::app_delegate quiet {};
    REQUIRE(quiet.make_metrics_message(metrics).empty());
}

TEST_CASE("The window surface reaches past the safe area", "[app][metrics]")
{
    // 안전 영역 밖이 없으면 root 자리 그대로다 (Windows).
    luil::window_metrics metrics { luil::make_window_metrics(1080.0f, 2199.0f, 2.81f) };
    REQUIRE(metrics.outside == luil::edge_insets {});
    luil::rect_f surface { luil::window_surface(metrics) };
    REQUIRE(surface.x == 0.0f);
    REQUIRE(surface.y == 0.0f);
    REQUIRE(surface.width == 1080.0f);
    REQUIRE(surface.height == 2199.0f);
    // 세로 Android: 위 75, 아래 42. root가 (0, 0)에서 시작하므로 위 몫은 음수다.
    metrics.outside = { 0.0f, 75.0f, 0.0f, 42.0f };
    surface = luil::window_surface(metrics);
    REQUIRE(surface.y == -75.0f);
    REQUIRE(surface.height == 2316.0f);
    // 가로: 왼쪽 컷아웃 75, 위 84, 아래 42.
    metrics = luil::make_window_metrics(2241.0f, 954.0f, 2.81f);
    metrics.outside = { 75.0f, 84.0f, 0.0f, 42.0f };
    surface = luil::window_surface(metrics);
    REQUIRE(surface.x == -75.0f);
    REQUIRE(surface.y == -84.0f);
    REQUIRE(surface.width == 2316.0f);
    REQUIRE(surface.height == 1080.0f);
}
