#include "luil/chart.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

// 차트 축의 순수 계산이다 (docs/chart-design.md). 요소도 글꼴도 없이 잰다.
namespace {
    using Catch::Approx;

    [[nodiscard]] double last_tick(const luil::chart_ticks& ticks)
    {
        return ticks.first + static_cast<double>(ticks.count - 1u) * ticks.step;
    }

    // 범위를 덮고, 칸이 상한 안이고, 걸음이 1·2·5 x 10^k인가.
    void require_covers(const luil::chart_range range, const luil::chart_ticks& ticks, const std::size_t target)
    {
        REQUIRE(ticks.count >= 2u);
        REQUIRE(ticks.count - 1u <= target);
        const double margin { ticks.step * 1e-9 };
        REQUIRE(ticks.first <= range.minimum + margin);
        REQUIRE(last_tick(ticks) >= range.maximum - margin);
        const double scaled { ticks.step / std::pow(10.0, std::floor(std::log10(ticks.step) + 1e-12)) };
        const bool round_step { scaled == Approx(1.0) || scaled == Approx(2.0) || scaled == Approx(5.0) };
        REQUIRE(round_step);
    }
}

TEST_CASE("Nice ticks cover the range with round steps and stay under the interval limit", "[chart][scale]")
{
    // 문서의 사례다.
    const luil::chart_ticks ticks { luil::nice_ticks({ 0.0, 97.0 }, 5u) };
    REQUIRE(ticks.first == 0.0);
    REQUIRE(ticks.step == 20.0);
    REQUIRE(ticks.count == 6u);
    REQUIRE(ticks.decimals == 0);
    REQUIRE(last_tick(ticks) == 100.0);

    for (const luil::chart_range range : { luil::chart_range { 0.0, 1.0 }, luil::chart_range { 3.0, 120.0 }, luil::chart_range { -40.0, 75.0 }, luil::chart_range { 1000.0, 1010.0 }, luil::chart_range { -1e6, 1e6 }, luil::chart_range { 0.1, 0.7 } })
    {
        for (std::size_t target = 2u; target <= 10u; ++target)
            require_covers(range, luil::nice_ticks(range, target), target);
    }
}

TEST_CASE("Nice ticks handle negative ranges and swapped ends", "[chart][scale]")
{
    const luil::chart_ticks negative { luil::nice_ticks({ -35.0, -2.0 }, 5u) };
    REQUIRE(negative.first == -40.0);
    REQUIRE(negative.step == 10.0);
    REQUIRE(negative.count == 5u);

    // 0을 가로지르는 범위는 칸 둘로도 덮는다. 칸 상한이 가장 작은 2일 때 끝나는지 본다.
    const luil::chart_ticks straddling { luil::nice_ticks({ -3.0, 97.0 }, 2u) };
    REQUIRE(straddling.count == 3u);
    REQUIRE(straddling.first == -100.0);
    REQUIRE(straddling.step == 100.0);

    const luil::chart_ticks swapped { luil::nice_ticks({ 97.0, 0.0 }, 5u) };
    REQUIRE(swapped.first == 0.0);
    REQUIRE(swapped.step == 20.0);
    REQUIRE(swapped.count == 6u);
}

TEST_CASE("Nice ticks fold the target count into two to ten intervals", "[chart][scale]")
{
    const luil::chart_ticks too_few { luil::nice_ticks({ 0.0, 97.0 }, 0u) };
    REQUIRE(too_few.count - 1u <= 2u);
    REQUIRE(too_few.step == 50.0);

    const luil::chart_ticks too_many { luil::nice_ticks({ 0.0, 97.0 }, 1000u) };
    REQUIRE(too_many.count - 1u <= 10u);
    REQUIRE(too_many.step == 10.0);
}

TEST_CASE("Nice ticks step one unit of the value's digit around a range without width", "[chart][scale]")
{
    const luil::chart_ticks five { luil::nice_ticks({ 5.0, 5.0 }, 5u) };
    REQUIRE(five.first == 4.0);
    REQUIRE(five.step == 1.0);
    REQUIRE(five.count == 3u);

    const luil::chart_ticks zero { luil::nice_ticks({ 0.0, 0.0 }, 5u) };
    REQUIRE(zero.first == -1.0);
    REQUIRE(zero.step == 1.0);
    REQUIRE(zero.count == 3u);

    const luil::chart_ticks negative { luil::nice_ticks({ -3.0, -3.0 }, 5u) };
    REQUIRE(negative.first == -4.0);
    REQUIRE(negative.count == 3u);

    // 자릿수 단위로 앞뒤를 덮으니 값이 걸음의 배수가 아니어도 덮는다.
    const luil::chart_ticks hundreds { luil::nice_ticks({ 123.4, 123.4 }, 5u) };
    REQUIRE(hundreds.step == 100.0);
    REQUIRE(hundreds.first == 0.0);
    REQUIRE(hundreds.count == 4u);

    // 값의 크기에 견줘 폭이 없는 범위도 같은 길이다. 눈금 글끼리 구별되지 않을 걸음을 고르지 않는다.
    const luil::chart_ticks almost { luil::nice_ticks({ 5.0, 5.0 + 1e-13 }, 5u) };
    REQUIRE(almost.step == 1.0);
    REQUIRE(almost.count == 3u);

    REQUIRE(luil::nice_ticks({ 0.5, 0.5 }, 5u).decimals == 1);
}

TEST_CASE("Nice ticks hold at very small and very large scales", "[chart][scale]")
{
    const luil::chart_ticks small { luil::nice_ticks({ 0.0, 1e-9 }, 5u) };
    REQUIRE(small.first == 0.0);
    REQUIRE(small.step == Approx(2e-10));
    REQUIRE(small.count == 6u);
    REQUIRE(small.decimals == 10);

    const luil::chart_ticks large { luil::nice_ticks({ 0.0, 1.2e12 }, 5u) };
    REQUIRE(large.first == 0.0);
    REQUIRE(large.step == Approx(5e11));
    REQUIRE(large.count == 4u);
    REQUIRE(large.decimals == 0);

    // 눈금 하나가 덜 서거나 더 서지 않는다. 0.3 / 0.1은 정확히 3이 아니다.
    const luil::chart_ticks tenths { luil::nice_ticks({ 0.3, 0.7 }, 4u) };
    REQUIRE(tenths.step == Approx(0.1));
    REQUIRE(tenths.first == Approx(0.3));
    REQUIRE(tenths.count == 5u);
    REQUIRE(tenths.decimals == 1);

    // 정규 수의 끝자락과 상한 근처도 멈추고 범위를 덮는다.
    const double tiny { std::numeric_limits<double>::denorm_min() };
    const luil::chart_ticks subnormal { luil::nice_ticks({ 0.0, tiny }, 5u) };
    REQUIRE(subnormal.count == 3u);
    REQUIRE(subnormal.step == 1.0);
    const luil::chart_ticks huge { luil::nice_ticks({ -1e300, 1e300 }, 5u) };
    REQUIRE(huge.count >= 2u);
    REQUIRE(huge.first <= -1e300 * (1.0 - 1e-9));
    REQUIRE(last_tick(huge) >= 1e300 * (1.0 - 1e-9));
}

TEST_CASE("Nice ticks read an unusable range as zero to one", "[chart][scale]")
{
    const double not_a_number { std::numeric_limits<double>::quiet_NaN() };
    const double infinity { std::numeric_limits<double>::infinity() };
    for (const luil::chart_range range : { luil::chart_range { not_a_number, 1.0 }, luil::chart_range { 0.0, not_a_number }, luil::chart_range { -infinity, infinity }, luil::chart_range { 1e308, 1e308 } })
    {
        const luil::chart_ticks ticks { luil::nice_ticks(range, 5u) };
        REQUIRE(ticks.first == 0.0);
        REQUIRE(ticks.step == Approx(0.2));
        REQUIRE(ticks.count == 6u);
        REQUIRE(ticks.decimals == 1);
    }
}

TEST_CASE("Chart numbers are fixed, locale free, and never negative zero", "[chart][scale]")
{
    REQUIRE(luil::format_chart_number(-1250.5, 1) == u8"-1250.5");
    REQUIRE(luil::format_chart_number(2.0, 2) == u8"2.00");
    REQUIRE(luil::format_chart_number(120.0, 0) == u8"120");
    // 반올림이다. 1.005는 double로 1.00499...라 내려간다.
    REQUIRE(luil::format_chart_number(3.7, 0) == u8"4");
    REQUIRE(luil::format_chart_number(1.005, 2) == u8"1.00");
    // 부호는 값이 0으로 반올림되면 뗀다.
    REQUIRE(luil::format_chart_number(-0.0, 0) == u8"0");
    REQUIRE(luil::format_chart_number(-0.001, 2) == u8"0.00");
    REQUIRE(luil::format_chart_number(-0.4, 0) == u8"0");
    REQUIRE(luil::format_chart_number(-0.6, 0) == u8"-1");
    // 큰 값은 지수 꼴도 천 단위 구분도 없다.
    REQUIRE(luil::format_chart_number(1e15, 0) == u8"1000000000000000");
    // 가장 큰 수에 소수 자리 상한까지 담는다.
    REQUIRE(luil::format_chart_number(std::numeric_limits<double>::max(), 20).size() == 330u);
    // 소수 자리는 0~20으로 접는다.
    REQUIRE(luil::format_chart_number(3.7, -2) == u8"4");
    REQUIRE(luil::format_chart_number(0.5, 100).size() == 22u);
    // 유한하지 않으면 빈 글이다.
    REQUIRE(luil::format_chart_number(std::numeric_limits<double>::quiet_NaN(), 2).empty());
    REQUIRE(luil::format_chart_number(std::numeric_limits<double>::infinity(), 2).empty());
    REQUIRE(luil::format_chart_number(-std::numeric_limits<double>::infinity(), 2).empty());
}

TEST_CASE("Values map onto pixels and back along a plain or flipped axis", "[chart][scale]")
{
    const luil::chart_range range { 0.0, 100.0 };
    REQUIRE(luil::chart_value_to_pixel(0.0, range, 10.0f, 210.0f) == Approx(10.0f));
    REQUIRE(luil::chart_value_to_pixel(50.0, range, 10.0f, 210.0f) == Approx(110.0f));
    REQUIRE(luil::chart_value_to_pixel(100.0, range, 10.0f, 210.0f) == Approx(210.0f));

    // y축은 위가 작은 픽셀이라 end가 start보다 작다.
    REQUIRE(luil::chart_value_to_pixel(0.0, range, 200.0f, 0.0f) == Approx(200.0f));
    REQUIRE(luil::chart_value_to_pixel(75.0, range, 200.0f, 0.0f) == Approx(50.0f));
    REQUIRE(luil::chart_value_to_pixel(100.0, range, 200.0f, 0.0f) == Approx(0.0f));

    // 픽셀에서 되돌려 값이 같다.
    const luil::chart_range negative { -40.0, 60.0 };
    for (const double value : { -40.0, -12.5, 0.0, 33.0, 60.0 })
    {
        const float pixel { luil::chart_value_to_pixel(value, negative, 300.0f, 20.0f) };
        const double back { negative.minimum + (static_cast<double>(pixel) - 300.0) / (20.0 - 300.0) * (negative.maximum - negative.minimum) };
        REQUIRE(back == Approx(value).margin(1e-3));
    }

    // 범위 밖의 값은 자르지 않는다.
    REQUIRE(luil::chart_value_to_pixel(150.0, range, 0.0f, 100.0f) == Approx(150.0f));
    REQUIRE(luil::chart_value_to_pixel(-50.0, range, 0.0f, 100.0f) == Approx(-50.0f));
}

TEST_CASE("A range without a usable width puts every value in the middle", "[chart][scale]")
{
    REQUIRE(luil::chart_value_to_pixel(5.0, { 5.0, 5.0 }, 0.0f, 100.0f) == Approx(50.0f));
    REQUIRE(luil::chart_value_to_pixel(9.0, { 5.0, 5.0 }, 100.0f, 0.0f) == Approx(50.0f));
    const double infinity { std::numeric_limits<double>::infinity() };
    REQUIRE(luil::chart_value_to_pixel(1.0, { 0.0, infinity }, 0.0f, 100.0f) == Approx(50.0f));
    REQUIRE(std::isnan(luil::chart_value_to_pixel(std::numeric_limits<double>::quiet_NaN(), { 0.0, 1.0 }, 0.0f, 100.0f)));
}
