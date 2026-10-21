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
