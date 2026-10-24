#include "luil/ui/text_document.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

TEST_CASE("A 20 MB document queries only its visible styles from half a million spans", "[ui][document][scale]")
{
    const std::u8string document(20'000'000, u8'x');
    std::vector<luil::text_span> spans {};
    spans.reserve(500'000);
    for (std::size_t offset { 0 }; offset < document.size(); offset += 40)
        spans.push_back({ offset, offset + 8, luil::text_color_role::accent });
    const auto visible { luil::text_spans_in_range(spans, 19'998'000, document.size()) };
    REQUIRE(visible.size() == 50u);
    REQUIRE(visible.data() == spans.data() + 499'950);
    REQUIRE(luil::text_spans_in_range(spans, 8, 40).empty());
    REQUIRE(luil::text_spans_in_range(spans, 10, 10).empty());
    REQUIRE(luil::text_spans_in_range({}, 0, 10).empty());
}

TEST_CASE("Search highlights use half open byte ranges", "[ui][document]")
{
    const std::vector<luil::text_highlight> matches { { 0, 3 }, { 10, 20, true }, { 30, 40 } };
    const auto visible { luil::text_highlights_in_range(matches, 3, 30) };
    REQUIRE(visible.size() == 1u);
    REQUIRE(visible.front().current);
    REQUIRE(visible.front().begin == 10u);
}

TEST_CASE("An empty row finds the ranges over its line break by asking through that byte", "[ui][document]")
{
    // "a\n\nb"의 빈 줄은 [2, 2)이고 그 줄 바꿈은 offset 2다. 빈 범위는 아무것도 걸지 않으므로 칸은 줄 바꿈까지 묻는다.
    const std::vector<luil::text_span> spans { { 0, 4, luil::text_color_role::accent } };
    REQUIRE(luil::text_spans_in_range(spans, 2, 2).empty());
    REQUIRE(luil::text_spans_in_range(spans, 2, 3).size() == 1u);
    // 줄 바꿈 하나뿐인 검색 결과는 그것으로 끝나는 첫 줄 [0, 1)의 줄 바꿈 칸에서 걸린다.
    const std::vector<luil::text_highlight> newline_only { { 1, 2 } };
    REQUIRE(luil::text_highlights_in_range(newline_only, 0, 1).empty());
    REQUIRE(luil::text_highlights_in_range(newline_only, 0, 2).size() == 1u);
}
