#include "luil/text/grapheme.h"
#include "luil/text/text_edit.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

TEST_CASE("Grapheme movement keeps combining Hangul flags and emoji sequences together", "[domain][grapheme]")
{
    constexpr std::array<std::u8string_view, 9> samples {
        u8"e\u0301", u8"\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466",
        u8"\u1100\u1161\u11A8", u8"\U0001F1F0\U0001F1F7", u8"\u2764\uFE0F",
        u8"\U0001F44D\U0001F3FD", u8"1\uFE0F\u20E3", u8"\r\n", u8"a\u093E"
    };
    for (const auto sample : samples)
    {
        CAPTURE(sample.size());
        REQUIRE(luil::text::grapheme_previous(sample, sample.size()) == 0u);
        REQUIRE(luil::text::grapheme_next(sample, 0) == sample.size());
        luil::text::text_edit_state state {};
        luil::text::text_edit_set_text(state, sample, true);
        luil::text::text_edit_move(state, luil::text::text_edit_motion::left, false);
        REQUIRE(state.caret == 0u);
        luil::text::text_edit_delete_forward(state);
        REQUIRE(state.text.empty());
    }
}

TEST_CASE("Backspace removes a combining mark but preserves emoji and Hangul units", "[domain][grapheme]")
{
    luil::text::text_edit_state state {};
    luil::text::text_edit_set_text(state, u8"e\u0301", true);
    luil::text::text_edit_backspace(state);
    REQUIRE(state.text == u8"e");
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == u8"e\u0301");
    for (const auto sample : { u8"\u1100\u1161\u11A8", u8"\U0001F1F0\U0001F1F7",
        u8"\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466", u8"1\uFE0F\u20E3"
    })
    {
        luil::text::text_edit_set_text(state, sample, true);
        luil::text::text_edit_backspace(state);
        REQUIRE(state.text.empty());
    }
}

TEST_CASE("Caret placement and selections never split an extended grapheme", "[domain][grapheme]")
{
    const std::u8string sample { u8"x\U0001F468\u200D\U0001F469y" };
    luil::text::text_edit_state state {};
    luil::text::text_edit_set_text(state, sample, true);
    luil::text::text_edit_place_caret(state, 6, false);
    REQUIRE(state.caret == 1u);
    luil::text::text_edit_move(state, luil::text::text_edit_motion::right, true);
    REQUIRE(luil::text::text_edit_selected_text(state) == u8"\U0001F468\u200D\U0001F469");
    REQUIRE(luil::text::grapheme_next({}, 100) == 0u);
    REQUIRE(luil::text::grapheme_previous({}, 100) == 0u);
    REQUIRE(luil::text::grapheme_next(u8"abc", 100) == 3u);
}
