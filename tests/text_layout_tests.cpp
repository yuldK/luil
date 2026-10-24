#include "luil/ui/text_layout.h"

#include "luil/text/grapheme.h"
#include "ui/text_layout_internal.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <atomic>
#include <thread>

namespace {
    const luil::text_measurer cells { [](const std::u8string_view text, float) {
        return static_cast<float>(luil::text::grapheme_boundaries(text).size() - 1) * 10.0f;
    }
    };
}

TEST_CASE("A text layout owns its source and keeps every grapheme caret round trip", "[ui][text-layout]")
{
    luil::text_layout_request request { .text = u8"e\u0301각🇰🇷👩🏽‍💻x", .line_height = 20.0f };
    const auto layout { luil::make_text_layout(request, cells) };
    request.text.clear();
    REQUIRE(layout.backend() == luil::text_layout_backend::measured);
    REQUIRE(layout.text() == u8"e\u0301각🇰🇷👩🏽‍💻x");
    REQUIRE(layout.size().width == 50.0f);
    REQUIRE(layout.lines().size() == 1);
    const auto boundaries { luil::text::grapheme_boundaries(layout.text()) };
    REQUIRE(layout.caret_stops().size() == boundaries.size());
    for (const auto at : boundaries)
    {
        const auto caret { layout.caret_rect(at) };
        REQUIRE(layout.hit_test({ caret.x, caret.y + 1.0f }).offset == at);
    }
    REQUIRE(layout.caret_rect(1).x == 0.0f);
    REQUIRE(layout.hit_test({ -100.0f, 1.0f }).offset == 0);
    REQUIRE(layout.hit_test({ 10000.0f, 1.0f }).offset == layout.text().size());
}

TEST_CASE("Soft wraps distinguish upstream carets and split selections across the same rows", "[ui][text-layout]")
{
    const auto layout { luil::make_text_layout({ .text = u8"abcde", .width = 20.0f, .wrap = true, .line_height = 10.0f }, cells) };
    REQUIRE(layout.lines().size() == 3);
    REQUIRE(layout.lines()[0].end == 2);
    REQUIRE(layout.lines()[1].begin == 2);
    REQUIRE(layout.caret_rect(2, true).x == 20.0f);
    REQUIRE(layout.caret_rect(2, true).y == 0.0f);
    REQUIRE(layout.caret_rect(2).x == 0.0f);
    REQUIRE(layout.caret_rect(2).y == 10.0f);
    REQUIRE(layout.hit_test({ 20.0f, 1.0f }) == luil::text_caret_position { 2, true });
    REQUIRE(layout.hit_test({ 0.0f, 11.0f }) == luil::text_caret_position { 2, false });
    const auto selection { layout.selection_rects(1, 4) };
    REQUIRE(selection.size() == 2);
    REQUIRE(selection[0].x == 10.0f);
    REQUIRE(selection[0].width == 10.0f);
    REQUIRE(selection[1].x == 0.0f);
    REQUIRE(selection[1].width == 20.0f);
}

TEST_CASE("CRLF and a trailing newline preserve empty logical rows and byte offsets", "[ui][text-layout]")
{
    const auto layout { luil::make_text_layout({ .text = u8"a\r\n\r\nb\n", .line_height = 12.0f }, cells) };
    REQUIRE(layout.lines().size() == 4);
    REQUIRE(layout.lines()[0].begin == 0);
    REQUIRE(layout.lines()[0].end == 1);
    REQUIRE(layout.lines()[1].begin == 3);
    REQUIRE(layout.lines()[1].end == 3);
    REQUIRE(layout.lines()[2].begin == 5);
    REQUIRE(layout.lines()[3].begin == 7);
    REQUIRE(layout.lines()[3].logical_line == 3);
    REQUIRE(layout.line_at(2) == 0);
    REQUIRE(layout.hit_test({ 2.0f, 1000.0f }).offset == 7);
    REQUIRE(layout.selection_rects(1, 3).front().width > 0.0f);
    const auto empty { luil::make_text_layout({}, cells) };
    REQUIRE(empty.valid());
    REQUIRE(empty.lines().size() == 1);
    REQUIRE(empty.hit_test({ 100.0f, 1.0f }).offset == 0);
    REQUIRE_FALSE(luil::text_layout {}.valid());
}

TEST_CASE("Wrapped rows hang trailing spaces outside their width", "[ui][text-layout]")
{
    const auto layout { luil::make_text_layout({ .text = u8"abc    def", .width = 30.0f, .wrap = true, .line_height = 10.0f }, cells) };
    REQUIRE(layout.lines().size() == 2);
    // 공백은 접은 줄에 남아 caret을 둘 수 있지만 폭은 보이는 글자까지다.
    REQUIRE(layout.lines()[0].end == 7);
    REQUIRE(layout.lines()[0].width == 30.0f);
    REQUIRE(layout.lines()[1].begin == 7);
    REQUIRE(layout.size().width == 30.0f);
    // 접지 않은 마지막 줄의 공백은 입력 칸의 caret 자리라 폭에 남는다.
    REQUIRE(luil::make_text_layout({ .text = u8"ab  ", .width = 100.0f, .wrap = true }, cells).size().width == 40.0f);
}

TEST_CASE("Wrap cuts stay on grapheme boundaries after a space with a combining mark", "[ui][text-layout]")
{
    const std::u8string text { u8"ab ́cdef" };
    const auto layout { luil::make_text_layout({ .text = text, .width = 20.0f, .wrap = true, .line_height = 10.0f }, cells) };
    const auto lines { layout.lines() };
    REQUIRE(lines.size() > 1);
    REQUIRE(lines.front().begin == 0);
    for (std::size_t index { 1 }; index < lines.size(); ++index)
        REQUIRE(lines[index].begin == lines[index - 1].end);
    REQUIRE(lines.back().end == text.size());
}

TEST_CASE("The selected newline marker follows the physical font size", "[ui][text-layout]")
{
    const auto layout { luil::make_text_layout({ .text = u8"a\nb", .font_size = 24.0f, .line_height = 10.0f }, cells) };
    const auto selection { layout.selection_rects(0, 3) };
    REQUIRE(selection.size() == 2);
    REQUIRE(selection[0].width == Catch::Approx(18.0f));
}

TEST_CASE("Synthetic ligature clusters divide carets without measuring prefixes", "[ui][text-layout][ligature]")
{
    int calls { 0 };
    luil::text_layout_request request { .text = u8"ffi" };
    const auto shape = [&calls](const std::u8string_view text, std::size_t) {
        ++calls;
        luil::text_layout_segment segment {};
        segment.height = 18.0f;
        segment.baseline = 12.0f;
        segment.width = text == u8"ffi" ? 12.0f : static_cast<float>(text.size()) * 6.0f;
        segment.clusters.push_back({ 0, text.size(), 0, 0.0f, segment.width });
        segment.runs.push_back({ 0, text.size(), 0, 1 });
        return segment;
    };
    const auto layout { luil::build_text_layout(request, luil::text_layout_backend::harfbuzz, shape) };
    REQUIRE(calls == 1);
    REQUIRE(layout.runs().front().glyph_count == 1);
    REQUIRE(layout.approximate_ligature_carets());
    REQUIRE(layout.caret_rect(1).x == Catch::Approx(4.0f));
    REQUIRE(layout.caret_rect(2).x == Catch::Approx(8.0f));
    REQUIRE(layout.hit_test({ 8.0f, 1.0f }).offset == 2);
    REQUIRE(layout.selection_rects(1, 2).front().width == Catch::Approx(4.0f));
    REQUIRE(calls == 1);
    request.wrap = true;
    request.width = 8.0f;
    const auto wrapped { luil::build_text_layout(request, luil::text_layout_backend::harfbuzz, shape) };
    // 줄을 실제 문맥으로 다시 shape하면 ff가 12px라 한 글자씩 접는다.
    REQUIRE(wrapped.lines().size() == 3);
    REQUIRE(wrapped.lines()[0].width == 6.0f);
}

TEST_CASE("Zero width stops choose the last equal boundary and ties choose the following caret", "[ui][text-layout]")
{
    const auto layout { luil::make_text_layout({ .text = u8"abc" }, [](const std::u8string_view text, float) {
        return text.size() == 1 ? 10.0f : text.size() == 2 ? 10.0f : 20.0f;
    })
    };
    REQUIRE(layout.hit_test({ 10.0f, 1.0f }).offset == 2);
    REQUIRE(layout.hit_test({ 15.0f, 1.0f }).offset == 3);
    REQUIRE(layout.hit_test({ 5.0f, 1.0f }).offset == 2);
}

TEST_CASE("Published layout copies can be queried concurrently without rebuilding", "[ui][text-layout]")
{
    const auto layout { luil::make_text_layout({ .text = u8"immutable layout", .line_height = 20.0f }, cells) };
    const auto copy { layout };
    REQUIRE(copy.caret_stops().data() == layout.caret_stops().data());
    std::atomic<bool> correct { true };
    std::vector<std::thread> readers {};
    for (int reader { 0 }; reader < 4; ++reader)
        readers.emplace_back([layout, &correct] {
            for (int pass { 0 }; pass < 500; ++pass)
                if (layout.hit_test({ layout.caret_rect(6).x, 1.0f }).offset != 6 || layout.selection_rects(2, 8).empty())
                    correct = false;
        });
    for (auto& reader : readers)
        reader.join();
    REQUIRE(correct);
}

TEST_CASE("Bulk grapheme boundaries agree with directional queries including long RI runs", "[domain][grapheme]")
{
    const std::u8string text { u8"e\u0301각🇰🇷🇺🇸🇯🇵👩🏽‍💻\r\n" };
    const auto boundaries { luil::text::grapheme_boundaries(text) };
    REQUIRE(boundaries.front() == 0);
    REQUIRE(boundaries.back() == text.size());
    REQUIRE(std::adjacent_find(boundaries.begin(), boundaries.end()) == boundaries.end());
    for (std::size_t index { 1 }; index < boundaries.size(); ++index)
    {
        REQUIRE(luil::text::grapheme_next(text, boundaries[index - 1]) == boundaries[index]);
        REQUIRE(luil::text::grapheme_previous(text, boundaries[index]) == boundaries[index - 1]);
    }
}
