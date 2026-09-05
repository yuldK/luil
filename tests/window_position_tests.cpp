#include "luil/win32/win32_window.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE("A window position parses two decimal integers", "[win32][window]")
{
    const auto plain { luil::win32::parse_window_position(u8"120,40") };
    REQUIRE(plain.has_value());
    REQUIRE(plain->x == 120);
    REQUIRE(plain->y == 40);

    // 가상 화면 좌표라 왼쪽·위 모니터에서는 음수다.
    const auto negative { luil::win32::parse_window_position(u8"-2500,690") };
    REQUIRE(negative.has_value());
    REQUIRE(negative->x == -2500);
    REQUIRE(negative->y == 690);

    const auto signed_plus { luil::win32::parse_window_position(u8"+10,-20") };
    REQUIRE(signed_plus.has_value());
    REQUIRE(signed_plus->x == 10);
    REQUIRE(signed_plus->y == -20);
}

TEST_CASE("A window position rejects what it cannot read", "[win32][window]")
{
    // 쉼표가 없으면 자리가 아니다.
    REQUIRE(luil::win32::parse_window_position(u8"120").has_value() == false);
    // 숫자가 아닌 글자가 섞이면 실패다 (조용히 앞부분만 읽지 않는다).
    REQUIRE(luil::win32::parse_window_position(u8"12a,40").has_value() == false);
    REQUIRE(luil::win32::parse_window_position(u8"12,4 0").has_value() == false);
    // 빈 자리도 실패다.
    REQUIRE(luil::win32::parse_window_position(u8"").has_value() == false);
    REQUIRE(luil::win32::parse_window_position(u8",40").has_value() == false);
    REQUIRE(luil::win32::parse_window_position(u8"12,").has_value() == false);
    REQUIRE(luil::win32::parse_window_position(u8"-,40").has_value() == false);
    // 화면 좌표를 넘는 값은 오타로 본다.
    REQUIRE(luil::win32::parse_window_position(u8"99999999,0").has_value() == false);
}

TEST_CASE("A placement inside the screen coordinate range passes", "[win32][window]")
{
    constexpr int maximum { std::numeric_limits<int>::max() };
    constexpr int minimum { std::numeric_limits<int>::min() };

    REQUIRE(luil::win32::placement_in_screen_range({ 120, 40, 800, 600 }));
    // 왼쪽·위 모니터의 음수 좌표도 화면 자리다.
    REQUIRE(luil::win32::placement_in_screen_range({ -2500, -1400, 800, 600 }));
    // 끝이 정확히 상한에 닿는 것까지가 범위 안이다.
    REQUIRE(luil::win32::placement_in_screen_range({ maximum - 800, maximum - 600, 800, 600 }));
    // 아주 음수인 좌표라도 더한 끝이 범위 안이면 통과다.
    REQUIRE(luil::win32::placement_in_screen_range({ minimum, minimum, 800, 600 }));
}

TEST_CASE("A placement overflowing the screen coordinate range is rejected", "[win32][window]")
{
    constexpr int maximum { std::numeric_limits<int>::max() };

    // 손상된 저장값의 최악 사례다 — int 덧셈이면 여기서 넘친다.
    REQUIRE(luil::win32::placement_in_screen_range({ maximum, 0, 800, 600 }) == false);
    REQUIRE(luil::win32::placement_in_screen_range({ 0, maximum, 800, 600 }) == false);
    // 상한을 한 칸만 지나도 거른다.
    REQUIRE(luil::win32::placement_in_screen_range({ maximum - 799, 0, 800, 600 }) == false);
    REQUIRE(luil::win32::placement_in_screen_range({ 0, maximum - 599, 800, 600 }) == false);
    // 좌표는 평범해도 크기가 손상되면 끝이 넘친다.
    REQUIRE(luil::win32::placement_in_screen_range({ 100, 100, maximum, 600 }) == false);
    REQUIRE(luil::win32::placement_in_screen_range({ 100, 100, 800, maximum }) == false);
}
