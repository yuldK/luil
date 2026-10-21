#include "luil/ui/combo_box_element.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Combo navigation and Escape use the candidate and editing state", "[ui][combo]")
{
    luil::combo_box_config config {};
    config.open = true;
    config.suggestions.items = std::make_shared<const std::vector<luil::combo_suggestion>>(
        std::vector<luil::combo_suggestion> { { u8"a", u8"Alpha" }, { u8"b", u8"Beta" } });
    config.move = [](const std::u8string& key) { return luil::input_action { luil::app_message { key } }; };
    config.set_open = [](const bool open) { return luil::input_action { luil::app_message { open } }; };
    luil::combo_box_element combo { { luil::application_element_kind(0), u8"repo" }, {}, config };
    const auto first { combo.on_key({ luil::key_code::home }) };
    const auto last { combo.on_key({ luil::key_code::end }) };
    REQUIRE(first->size() == 1u);
    REQUIRE(last->size() == 1u);
    REQUIRE(*std::get<luil::app_message>(first->front()).get<std::u8string>() == u8"a");
    REQUIRE(*std::get<luil::app_message>(last->front()).get<std::u8string>() == u8"b");
    config.suggestions.items = std::make_shared<const std::vector<luil::combo_suggestion>>(
        std::vector<luil::combo_suggestion> { { u8"a", u8"Alpha" }, { u8"b", u8"Beta" }, { u8"c", u8"Disabled", false } });
    luil::combo_box_element paged { { luil::application_element_kind(0), u8"paged" }, {}, config };
    const auto page { paged.on_key({ luil::key_code::page_down }) };
    REQUIRE(*std::get<luil::app_message>(page->front()).get<std::u8string>() == u8"b");
    const auto close { combo.on_key({ luil::key_code::escape }) };
    REQUIRE(*std::get<luil::app_message>(close->front()).get<bool>() == false);
    luil::text_input_view composing {};
    composing.composing = true;
    composing.composition_text = u8"베";
    std::u8string query {};
    config.request_suggestions = [&query](std::uint64_t, const std::u8string& text) { query = text; return luil::input_action {}; };
    luil::combo_box_element ime { { luil::application_element_kind(0), u8"ime" }, composing, config };
    REQUIRE(ime.on_key({ luil::key_code::escape }).has_value() == false);
    static_cast<void>(ime.suggestion_request(1));
    REQUIRE(query == u8"베");
}
