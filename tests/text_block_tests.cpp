#include "luil/ui/text_block.h"

#include "luil/text/utf8_text.h"
#include "luil/ui/label_element.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

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
}

TEST_CASE("A text block lays out one line at its natural width until it overflows", "[ui][text-block]")
{
    const luil::text_block_style style {};
    // 12px 글 열 글자는 60px다.
    const luil::text_block_layout fits { luil::measure_text_block(context_at(1.0f), u8"0123456789", style, 100.0f) };
    REQUIRE(fits.lines.size() == 1u);
    REQUIRE(fits.lines[0].end == 10u);
    REQUIRE(fits.truncated == false);
    REQUIRE(fits.width == 60.0f);
    REQUIRE(fits.height == luil::text_block_line_height(style));

    // 넘치면 받은 폭이고 그리기가 줄인다.
    const luil::text_block_layout narrow { luil::measure_text_block(context_at(1.0f), u8"0123456789", style, 40.0f) };
    REQUIRE(narrow.truncated);
    REQUIRE(narrow.width == 40.0f);

    // 빈 글은 줄도 높이도 없다.
    const luil::text_block_layout empty { luil::measure_text_block(context_at(1.0f), u8"", style, 100.0f) };
    REQUIRE(empty.lines.empty());
    REQUIRE(empty.height == 0.0f);
}

TEST_CASE("A text block wraps like a label and keeps its sizes in logical pixels", "[ui][text-block]")
{
    luil::text_block_style style {};
    style.wrap = true;
    style.max_lines = 2;
    const std::u8string text { u8"alpha beta gamma delta epsilon zeta" };
    const luil::label_config label { .text = text, .font_size = 12.0f, .wrap = true, .max_lines = 2 };

    // 기본 줄 높이는 label의 한 줄이고, 배율과 상관없이 같은 자리에서 바꾼다.
    REQUIRE(luil::text_block_line_height(style) == luil::label_element::height_for(label));
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        const luil::text_block_layout block { luil::measure_text_block(context_at(scale), text, style, 70.0f) };
        const luil::label_wrap wrapped { luil::label_element::measure_wrap(label, context_at(scale), 70.0f) };
        REQUIRE(block.lines.size() == 2u);
        REQUIRE(block.truncated);
        REQUIRE(block.lines.back().end == text.size());
        REQUIRE(block.width == 70.0f);
        REQUIRE(block.height == 2.0f * luil::text_block_line_height(style));
        REQUIRE(wrapped.lines.size() == block.lines.size());
        REQUIRE(wrapped.lines[0].end == block.lines[0].end);
    }

    // 줄 높이를 정하면 그 높이로 쌓는다.
    style.line_height = 30.0f;
    REQUIRE(luil::measure_text_block(context_at(1.0f), text, style, 70.0f).height == 60.0f);
}

TEST_CASE("A label's padding is left out of the width its lines wrap in", "[ui][text-block][label]")
{
    // 여백 10이 양옆이라 글은 70px에서 바뀐다 — 여백 없이 70px을 받은 글과 같은 자리다.
    const luil::label_config padded { .text = u8"alpha beta gamma delta", .font_size = 12.0f, .padding = 10.0f, .wrap = true };
    luil::text_block_style style {};
    style.wrap = true;
    const luil::label_wrap wrapped { luil::label_element::measure_wrap(padded, context_at(1.0f), 90.0f) };
    const luil::text_block_layout block { luil::measure_text_block(context_at(1.0f), padded.text, style, 70.0f) };
    REQUIRE(wrapped.lines.size() == 2u);
    REQUIRE(wrapped.lines[1].begin == block.lines[1].begin);
}

TEST_CASE("Drawing a text block with stale line ranges stays inside the text", "[ui][text-block]")
{
    // 글꼴이 없는 core test라 글자는 서지 않는다. 앱이 건넨 줄 구간이 글보다 길어도 글 밖을 읽지 않는다.
    SkBitmap bitmap {};
    bitmap.allocN32Pixels(64, 32);
    SkCanvas canvas { bitmap };
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::draw_context context { .canvas = canvas, .palette = palette };
    luil::text_block_style style {};
    style.wrap = true;
    style.vertical = luil::text_vertical_align::top;
    luil::text_block_layout stale {};
    stale.lines.push_back({ 0, 40 });
    stale.lines.push_back({ 50, 60 });
    const luil::rect_f drawn { luil::draw_text_block(context, u8"short", { 0.0f, 0.0f, 64.0f, 32.0f }, style, SkPaint {}, stale) };
    REQUIRE(drawn.y == 0.0f);
    REQUIRE(drawn.height == 2.0f * luil::text_block_line_height(style));

    // 빈 글은 상자의 왼쪽 위에 선 빈 자리다.
    const luil::rect_f empty { luil::draw_text_block(context, u8"", { 4.0f, 6.0f, 64.0f, 32.0f }, style, SkPaint {}) };
    REQUIRE(empty.x == 4.0f);
    REQUIRE(empty.y == 6.0f);
    REQUIRE(empty.width == 0.0f);
}
