#include "win32/dpi_scale.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("DPI turns into a scale", "[win32][dpi]")
{
    STATIC_REQUIRE(luil::win32::dpi_scale(96) == 1.0f);
    STATIC_REQUIRE(luil::win32::dpi_scale(144) == 1.5f);
    STATIC_REQUIRE(luil::win32::dpi_scale(192) == 2.0f);
}

TEST_CASE("Logical pixels round instead of truncating", "[win32][dpi]")
{
    constexpr float scale { 1.5f };

    // 잘라내던 자리가 이 값들에서 한 픽셀씩 어긋났다.
    REQUIRE(luil::win32::scaled_pixels(100.0f, scale) == 150);
    REQUIRE(luil::win32::scaled_pixels(101.0f, scale) == 152); // 151.5 → 152 (잘라내면 151)
    REQUIRE(luil::win32::scaled_pixels(0.9f, scale) == 1);     // 1.35 → 1 (잘라내도 1)
    REQUIRE(luil::win32::scaled_pixels(1.0f, scale) == 2);     // 1.5 → 2 (잘라내면 1)

    // **음수에서 방향이 갈린다.** 잘라내기는 0 쪽으로 가므로 양수는 내려가고 음수는
    // 올라간다 — 같은 논리 값이 화면의 어느 쪽에 있느냐로 다르게 놓인다.
    // 가상 화면 좌표는 왼쪽·위 모니터에서 음수다.
    REQUIRE(luil::win32::scaled_pixels(-1.0f, scale) == -2);     // 잘라내면 -1
    REQUIRE(luil::win32::scaled_pixels(-101.0f, scale) == -152); // 잘라내면 -151
    // 그래서 부호를 뒤집어도 크기가 같다.
    REQUIRE(luil::win32::scaled_pixels(-101.0f, scale) == -luil::win32::scaled_pixels(101.0f, scale));

    // 배율 1에서는 있으나 없으나 같은 값이다 — 그래서 이 잘못이 오래 숨어 있었다.
    REQUIRE(luil::win32::scaled_pixels(101.0f, 1.0f) == 101);
    REQUIRE(luil::win32::scaled_pixels(-101.0f, 1.0f) == -101);
}
