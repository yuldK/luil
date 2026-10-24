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
    // Home·End는 글 칸의 caret 몫이라 콤보가 받지 않는다. 처음·끝은 Page로 간다.
    REQUIRE(combo.on_key({ luil::key_code::home }).has_value() == false);
    REQUIRE(combo.on_key({ luil::key_code::end }).has_value() == false);
    const auto first { combo.on_key({ luil::key_code::page_up }) };
    const auto last { combo.on_key({ luil::key_code::page_down }) };
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

TEST_CASE("Combo trusts the answered query and filters only stale candidates", "[ui][combo][async]")
{
    luil::combo_box_config config {};
    config.open = true;
    config.suggestions.query = u8"ab";
    config.suggestions.items = std::make_shared<const std::vector<luil::combo_suggestion>>(
        std::vector<luil::combo_suggestion> { { u8"ab", u8"ab" }, { u8"about", u8"About" } });
    config.move = [](const std::u8string& key) { return luil::input_action { luil::app_message { key } }; };
    luil::text_input_view value {};
    value.text = u8"ab";
    // 원천이 "ab"에 "About"을 돌려줬으면 대소문자가 달라도 그대로 보인다.
    luil::combo_box_element answered { { luil::application_element_kind(0), u8"answered" }, value, config };
    const auto last { answered.on_key({ luil::key_code::page_down }) };
    REQUIRE(*std::get<luil::app_message>(last->front()).get<std::u8string>() == u8"about");
    // 받는 중이면 후보가 앞 질의의 것이라 칸의 글로 다시 거른다.
    config.suggestions.loading = true;
    luil::combo_box_element stale { { luil::application_element_kind(0), u8"stale" }, value, config };
    const auto filtered { stale.on_key({ luil::key_code::page_down }) };
    REQUIRE(*std::get<luil::app_message>(filtered->front()).get<std::u8string>() == u8"ab");
}

TEST_CASE("Combo requests suggestions with the query stored for the token", "[ui][combo][async]")
{
    luil::combo_box_config config {};
    config.suggestions.token = 5;
    config.suggestions.query = u8"ab";
    std::u8string query {};
    config.request_suggestions = [&query](std::uint64_t, const std::u8string& text) { query = text; return luil::input_action {}; };
    luil::text_input_view value {};
    value.text = u8"abc";
    luil::combo_box_element combo { { luil::application_element_kind(0), u8"request" }, value, config };
    static_cast<void>(combo.suggestion_request(5));
    REQUIRE(query == u8"ab");
    static_cast<void>(combo.suggestion_request(6));
    REQUIRE(query == u8"abc");
}

TEST_CASE("Combo keeps previous candidates for Enter while suggestions load", "[ui][combo][async]")
{
    luil::combo_box_config config {};
    config.allow_custom = false;
    const auto first { luil::begin_combo_query(config.suggestions, u8"a") };
    REQUIRE(luil::accept_combo_suggestions(config.suggestions, first,
        std::make_shared<const std::vector<luil::combo_suggestion>>(std::vector<luil::combo_suggestion> { { u8"ab", u8"ab" } })));
    static_cast<void>(luil::begin_combo_query(config.suggestions, u8"ab"));
    config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { key } }; };
    bool invalid { false };
    config.invalid = [&invalid](const std::u8string&) { invalid = true; return luil::input_action {}; };
    luil::text_input_view value {};
    value.text = u8"ab";
    luil::combo_box_element exact { { luil::application_element_kind(0), u8"exact" }, value, config };
    const auto chosen { exact.on_key({ luil::key_code::enter }) };
    REQUIRE(invalid == false);
    REQUIRE(chosen->size() == 1u);
    REQUIRE(*std::get<luil::app_message>(chosen->front()).get<std::u8string>() == u8"ab");
    // 앞 후보에 없는 글도 받는 중에는 틀렸다고 보내지 않는다.
    value.text = u8"zz";
    luil::combo_box_element pending { { luil::application_element_kind(0), u8"pending" }, value, config };
    const auto swallowed { pending.on_key({ luil::key_code::enter }) };
    REQUIRE(swallowed.has_value());
    REQUIRE(swallowed->empty());
    REQUIRE(invalid == false);
}
