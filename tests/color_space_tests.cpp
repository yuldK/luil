#include "luil/ui/color_space.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

// 색 고르기가 쓰는 색 계산이다 (docs/color-space-design.md). 모두 순수 함수다.
namespace {
    using luil::hex_color_problem;

    [[nodiscard]] luil::ui_color rgba(const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue, const std::uint8_t alpha = 255)
    {
        return luil::make_ui_color(red, green, blue, alpha);
    }

    [[nodiscard]] bool near(const float first, const float second)
    {
        return std::abs(first - second) <= 0.0005f;
    }
}

TEST_CASE("Every 8-bit color comes back from its HSV unchanged", "[ui][color-space]")
{
    // 채널마다 5 걸음 격자와 끝값(0·255)이다. 왕복 약속은 8비트에서 HSV를 거쳐 8비트로 가는 한 방향이다.
    const luil::hsv_color keeps[] { {}, { 200.0f, 0.7f, 0.4f, 0.5f } };
    int checked { 0 };
    for (int red { 0 }; red <= 255; red += 5)
        for (int green { 0 }; green <= 255; green += 5)
            for (int blue { 0 }; blue <= 255; blue += 5)
                for (const luil::hsv_color& keep : keeps)
                {
                    const luil::ui_color color { rgba(static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green), static_cast<std::uint8_t>(blue), static_cast<std::uint8_t>((red + blue) % 256)) };
                    REQUIRE(luil::color_from_hsv(luil::hsv_from_color(color, keep)) == color);
                    ++checked;
                }
    // 255가 5의 배수라 0, 5, ..., 255의 52개가 끝값을 품는다.
    REQUIRE(checked == 52 * 52 * 52 * 2);
}

TEST_CASE("A gray keeps the hue and a black keeps the hue and saturation", "[ui][color-space]")
{
    const luil::hsv_color keep { 220.0f, 0.8f, 1.0f, 1.0f };
    // 회색은 색상이 정해지지 않는다. 채도 0과 명도는 색에서 온다.
    const luil::hsv_color gray { luil::hsv_from_color(rgba(128, 128, 128), keep) };
    REQUIRE(gray.hue == 220.0f);
    REQUIRE(gray.saturation == 0.0f);
    REQUIRE(near(gray.value, 128.0f / 255.0f));
    // 검정은 색상과 채도가 정해지지 않는다.
    const luil::hsv_color black { luil::hsv_from_color(rgba(0, 0, 0, 128), keep) };
    REQUIRE(black.hue == 220.0f);
    REQUIRE(black.saturation == 0.8f);
    REQUIRE(black.value == 0.0f);
    REQUIRE(near(black.alpha, 128.0f / 255.0f));
    // keep이 없으면 0이다.
    REQUIRE(luil::hsv_from_color(rgba(255, 255, 255)).hue == 0.0f);
    // keep도 범위로 잘라 쓴다.
    REQUIRE(luil::hsv_from_color(rgba(10, 10, 10), { 500.0f, 0.0f, 0.0f, 1.0f }).hue == 360.0f);
}

TEST_CASE("The primary colors read their textbook hue and the hue 360 is the hue 0", "[ui][color-space]")
{
    REQUIRE(luil::hsv_from_color(rgba(255, 0, 0)) == luil::hsv_color { 0.0f, 1.0f, 1.0f, 1.0f });
    REQUIRE(near(luil::hsv_from_color(rgba(0, 255, 0)).hue, 120.0f));
    REQUIRE(near(luil::hsv_from_color(rgba(0, 0, 255)).hue, 240.0f));
    REQUIRE(near(luil::hsv_from_color(rgba(255, 0, 255)).hue, 300.0f));
    REQUIRE(luil::color_from_hsv({ 360.0f, 1.0f, 1.0f, 1.0f }) == luil::color_from_hsv({ 0.0f, 1.0f, 1.0f, 1.0f }));
    REQUIRE(luil::color_from_hsv({ 360.0f, 1.0f, 1.0f, 1.0f }) == rgba(255, 0, 0));
    // 220도, 채도 80%, 명도 100%, 알파 50%는 #3366FF80이다 (lround(0.5 * 255)는 128).
    REQUIRE(luil::color_from_hsv({ 220.0f, 0.8f, 1.0f, 0.5f }) == rgba(0x33, 0x77, 0xFF, 0x80));
    REQUIRE(luil::color_from_hsv({ 225.0f, 0.8f, 1.0f, 0.5f }) == rgba(0x33, 0x66, 0xFF, 0x80));
}

TEST_CASE("Clamping cuts each channel into range and reads NaN as zero or opaque", "[ui][color-space]")
{
    const float nan { std::numeric_limits<float>::quiet_NaN() };
    REQUIRE(luil::clamp_hsv({ nan, nan, nan, nan }) == luil::hsv_color { 0.0f, 0.0f, 0.0f, 1.0f });
    REQUIRE(luil::clamp_hsv({ -10.0f, -1.0f, 2.0f, 9.0f }) == luil::hsv_color { 0.0f, 0.0f, 1.0f, 1.0f });
    REQUIRE(luil::clamp_hsv({ 400.0f, 0.5f, 0.5f, -1.0f }) == luil::hsv_color { 360.0f, 0.5f, 0.5f, 0.0f });
    const float infinity { std::numeric_limits<float>::infinity() };
    REQUIRE(luil::clamp_hsv({ infinity, infinity, -infinity, infinity }) == luil::hsv_color { 360.0f, 1.0f, 0.0f, 1.0f });
    // 범위 밖도 잘라서 색을 짓는다.
    REQUIRE(luil::color_from_hsv({ -50.0f, 3.0f, 3.0f, 3.0f }) == rgba(255, 0, 0));
}

TEST_CASE("A hex color reads with or without the hash, in short or long form", "[ui][color-space][hex]")
{
    const auto read = [](const std::u8string_view text, const bool alpha) { return luil::check_hex_color(text, alpha); };
    REQUIRE(read(u8"#abc", false) == luil::hex_color_check { hex_color_problem::none, rgba(0xAA, 0xBB, 0xCC) });
    REQUIRE(read(u8"abc", false) == luil::hex_color_check { hex_color_problem::none, rgba(0xAA, 0xBB, 0xCC) });
    REQUIRE(read(u8"#AABBCC", false) == luil::hex_color_check { hex_color_problem::none, rgba(0xAA, 0xBB, 0xCC) });
    REQUIRE(read(u8"  #aabbcc\n", false) == luil::hex_color_check { hex_color_problem::none, rgba(0xAA, 0xBB, 0xCC) });
    REQUIRE(read(u8"#3366FF80", true) == luil::hex_color_check { hex_color_problem::none, rgba(0x33, 0x66, 0xFF, 0x80) });
    REQUIRE(read(u8"#36f8", true) == luil::hex_color_check { hex_color_problem::none, rgba(0x33, 0x66, 0xFF, 0x88) });
    // 알파를 켜도 알파 자리가 없으면 불투명이다.
    REQUIRE(read(u8"#3366FF", true) == luil::hex_color_check { hex_color_problem::none, rgba(0x33, 0x66, 0xFF) });
    // 전각 숫자·전각 영문·전각 #과 전각 공백이다 (IME 전각 확정).
    REQUIRE(read(u8"　＃３３６６Ｆｆ", false) == luil::hex_color_check { hex_color_problem::none, rgba(0x33, 0x66, 0xFF) });
    REQUIRE(read(u8"#ａｂｃ", false).color == rgba(0xAA, 0xBB, 0xCC));
}

TEST_CASE("A hex color names why it cannot be read", "[ui][color-space][hex]")
{
    const auto problem = [](const std::u8string_view text, const bool alpha) { return luil::check_hex_color(text, alpha).problem; };
    REQUIRE(problem(u8"", false) == hex_color_problem::empty);
    REQUIRE(problem(u8"   ", true) == hex_color_problem::empty);
    REQUIRE(problem(u8"#12G", false) == hex_color_problem::not_hex);
    REQUIRE(problem(u8"#12 34", false) == hex_color_problem::not_hex);
    REQUIRE(problem(u8"##123", false) == hex_color_problem::not_hex);
    REQUIRE(problem(u8"#ＧＧＧ", false) == hex_color_problem::not_hex);
    // 16진 밖 글자가 자릿수보다 먼저다.
    REQUIRE(problem(u8"#1G", false) == hex_color_problem::not_hex);
    REQUIRE(problem(u8"#", false) == hex_color_problem::wrong_length);
    REQUIRE(problem(u8"#12345", false) == hex_color_problem::wrong_length);
    // 알파를 끄면 8자리와 4자리는 자릿수가 틀린 글이다.
    REQUIRE(problem(u8"#3366FF80", false) == hex_color_problem::wrong_length);
    REQUIRE(problem(u8"#36f8", false) == hex_color_problem::wrong_length);
    REQUIRE(luil::check_hex_color(u8"#12G", false).color.has_value() == false);
    REQUIRE(luil::check_hex_color(u8"#12G", false).valid() == false);
    REQUIRE(luil::check_hex_color(u8"#123", false).valid());
}

TEST_CASE("A hex color is written in upper case with the alpha last", "[ui][color-space][hex]")
{
    REQUIRE(luil::format_hex_color(rgba(0x33, 0x66, 0xFF, 0x80), false) == u8"#3366FF");
    REQUIRE(luil::format_hex_color(rgba(0x33, 0x66, 0xFF, 0x80), true) == u8"#3366FF80");
    REQUIRE(luil::format_hex_color(rgba(0xab, 0xcd, 0xef), true) == u8"#ABCDEFFF");
    REQUIRE(luil::format_hex_color(rgba(0, 0, 0, 0), true) == u8"#00000000");
    // 쓴 글은 다시 같은 색으로 읽힌다.
    REQUIRE(luil::check_hex_color(luil::format_hex_color(rgba(1, 2, 3, 4), true), true).color == rgba(1, 2, 3, 4));
}

TEST_CASE("The hex filter keeps only hex digits and the hash from each piece", "[ui][color-space][hex]")
{
    const luil::text_insert_filter filter { luil::hex_color_filter() };
    REQUIRE(filter(u8"색: #12 34 zz") == u8"#1234");
    REQUIRE(filter(u8"＃ａＢ３") == u8"#aB3");
    REQUIRE(filter(u8"ghij") == u8"");
    // 조각만 본다. 이어 붙여 틀린 글은 검사가 잡는다.
    REQUIRE(filter(u8"##12") == u8"##12");
    // 깨진 바이트는 버린다.
    const std::u8string broken { u8"a", 1 };
    REQUIRE(filter(broken + static_cast<char8_t>(0xFF) + u8"b") == u8"ab");
}
