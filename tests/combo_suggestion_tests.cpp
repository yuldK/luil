#include "luil/ui/combo_box_element.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Combo suggestions reject stale and duplicate responses", "[ui][combo][async]")
{
    luil::combo_suggestion_state state {};
    const auto first { luil::begin_combo_query(state, u8"a") };
    const auto second { luil::begin_combo_query(state, u8"ab") };
    auto items { std::make_shared<const std::vector<luil::combo_suggestion>>(std::vector<luil::combo_suggestion> { { u8"ab", u8"About" } }) };
    REQUIRE(luil::accept_combo_suggestions(state, first, items) == false);
    REQUIRE(state.loading);
    REQUIRE(luil::accept_combo_suggestions(state, second, items));
    REQUIRE(state.loading == false);
    REQUIRE(state.items == items);
    REQUIRE(luil::accept_combo_suggestions(state, second, items) == false);
}
