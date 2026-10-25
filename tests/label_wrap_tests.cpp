#include "luil/ui/label_element.h"

#include "luil/text/grapheme.h"
#include "luil/text/utf8_text.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // 코드포인트마다 글자 크기의 절반 폭이다 (12px 글이면 6px). 배율이 2면 폭도 두 배가 되어 바뀌는 자리가 같다.
    [[nodiscard]] float half_size_width(const std::u8string_view text, const float pixel_size)
    {
        std::size_t count { 0 };
        for (std::size_t offset { 0 }; offset < text.size(); offset += luil::text::utf8_decode(text, offset).length)
            ++count;
        return static_cast<float>(count) * pixel_size / 2.0f;
    }

    const luil::text_measurer half_size_measurer { half_size_width };

    [[nodiscard]] luil::measure_context context_at(const float scale)
    {
        return luil::measure_context { .scale = scale, .measurer = &half_size_measurer };
    }

    [[nodiscard]] luil::label_config wrapping(std::u8string text, const std::size_t max_lines = 0)
    {
        return luil::label_config { .text = std::move(text), .font_size = 12.0f, .wrap = true, .max_lines = max_lines };
    }

    [[nodiscard]] std::vector<std::u8string> line_texts(const luil::label_wrap& wrap, const std::u8string_view text)
    {
        std::vector<std::u8string> result {};
        for (const luil::label_wrap::line& line : wrap.lines)
            result.emplace_back(text.substr(line.begin, line.end - line.begin));
        return result;
    }

    // 모든 줄의 경계가 글자소 경계다.
    [[nodiscard]] bool lines_on_grapheme_boundaries(const luil::label_wrap& wrap, const std::u8string_view text)
    {
        const std::vector<std::size_t> boundaries { luil::text::grapheme_boundaries(text) };
        const auto on_boundary = [&boundaries](const std::size_t offset) { return std::find(boundaries.begin(), boundaries.end(), offset) != boundaries.end(); };
        return std::all_of(wrap.lines.begin(), wrap.lines.end(), [&](const luil::label_wrap::line& line) { return on_boundary(line.begin) && on_boundary(line.end); });
    }
} // namespace

TEST_CASE("A wrapping label breaks at spaces first", "[ui][label][wrap]")
{
    const luil::label_config config { wrapping(u8"alpha beta gamma delta") };
    // 70px면 한 줄에 11글자다.
    const luil::label_wrap wrap { luil::label_element::measure_wrap(config, context_at(1.0f), 70.0f) };
    REQUIRE(wrap.lines.size() == 2u);
    REQUIRE(line_texts(wrap, config.text)[1] == u8"gamma delta");
    REQUIRE(wrap.truncated == false);
    REQUIRE(wrap.height == 2.0f * luil::label_element::height_for(config));

    // preferred_size가 접힌 높이를 답한다. 폭은 받은 폭이다.
    const luil::label_element label { luil::ui_element_id { luil::application_element_kind(1), u8"label" }, config };
    const std::optional<luil::size_f> size { label.preferred_size(context_at(1.0f), 70.0f) };
    REQUIRE(size.has_value());
    REQUIRE(size->height == wrap.height);
    REQUIRE(size->width == 70.0f);
    // 넉넉하면 한 줄이고 글의 폭이다.
    const std::optional<luil::size_f> wide { label.preferred_size(context_at(1.0f), 400.0f) };
    REQUIRE(wide->height == luil::label_element::height_for(config));
    REQUIRE(wide->width == 22.0f * 6.0f);
}

TEST_CASE("A line break and max lines are honoured", "[ui][label][wrap]")
{
    const luil::label_config broken { wrapping(u8"first\nsecond") };
    REQUIRE(luil::label_element::measure_wrap(broken, context_at(1.0f), 400.0f).lines.size() == 2u);

    // 넘치는 줄은 마지막 보이는 줄이 남은 글 전체를 맡고 말줄임 표식이 선다.
    const luil::label_config limited { wrapping(u8"alpha beta gamma delta epsilon zeta", 2) };
    const luil::label_wrap wrap { luil::label_element::measure_wrap(limited, context_at(1.0f), 70.0f) };
    REQUIRE(wrap.lines.size() == 2u);
    REQUIRE(wrap.truncated);
    REQUIRE(wrap.lines.back().end == limited.text.size());
    REQUIRE(wrap.height == 2.0f * luil::label_element::height_for(limited));
    // 0은 무제한이다.
    REQUIRE(luil::label_element::measure_wrap(wrapping(limited.text), context_at(1.0f), 70.0f).lines.size() > 2u);
}

TEST_CASE("Long words break on grapheme boundaries, never inside composed Hangul or emoji", "[ui][label][wrap]")
{
    // 조합형(NFD) 한글 "한"은 코드포인트 셋이 글자소 하나다.
    std::u8string hangul {};
    for (int index { 0 }; index < 8; ++index)
        hangul += u8"한";
    const luil::label_wrap hangul_wrap { luil::label_element::measure_wrap(wrapping(hangul), context_at(1.0f), 40.0f) };
    REQUIRE(hangul_wrap.lines.size() > 1u);
    REQUIRE(lines_on_grapheme_boundaries(hangul_wrap, hangul));

    // ZWJ로 이은 가족 이모지와 피부색 수식이다.
    std::u8string emoji {};
    for (int index { 0 }; index < 6; ++index)
        emoji += u8"\U0001F468‍\U0001F469‍\U0001F467\U0001F44D\U0001F3FD";
    const luil::label_wrap emoji_wrap { luil::label_element::measure_wrap(wrapping(emoji), context_at(1.0f), 60.0f) };
    REQUIRE(emoji_wrap.lines.size() > 1u);
    REQUIRE(lines_on_grapheme_boundaries(emoji_wrap, emoji));
}

TEST_CASE("A wrapping label breaks at the same places at any scale", "[ui][label][wrap]")
{
    const luil::label_config config { wrapping(u8"alpha beta gamma delta") };
    const luil::label_wrap single { luil::label_element::measure_wrap(config, context_at(1.0f), 70.0f) };
    const luil::label_wrap doubled { luil::label_element::measure_wrap(config, context_at(2.0f), 70.0f) };
    REQUIRE(line_texts(single, config.text) == line_texts(doubled, config.text));
    // 높이는 논리 픽셀이라 배율과 상관없다.
    REQUIRE(single.height == doubled.height);
}
