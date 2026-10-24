#include "luil/text/grapheme.h"
#include "luil/text/text_edit.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>
#include <vector>

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

TEST_CASE("Grapheme queries answer around line breaks without crossing them", "[domain][grapheme]")
{
    // Shaping 구성은 질의마다 offset을 감싼 줄 하나만 ICU에 건다. 줄 바꿈 둘레와 줄 처음·끝의 답이 글 전체로 물은
    // 답과 같아야 한다.
    const std::u8string_view crlf { u8"a\r\nb" };
    REQUIRE(luil::text::grapheme_next(crlf, 1) == 3u);
    REQUIRE(luil::text::grapheme_next(crlf, 2) == 3u);
    REQUIRE(luil::text::grapheme_previous(crlf, 3) == 1u);
    REQUIRE(luil::text::grapheme_previous(crlf, 2) == 1u);
    REQUIRE(luil::text::grapheme_previous(crlf, 1) == 0u);
    const std::u8string_view lone { u8"a\rb\n" };
    REQUIRE(luil::text::grapheme_next(lone, 1) == 2u);
    REQUIRE(luil::text::grapheme_previous(lone, 2) == 1u);
    REQUIRE(luil::text::grapheme_next(lone, 3) == 4u);
    REQUIRE(luil::text::grapheme_previous(lone, 4) == 3u);
    const std::u8string_view family { u8"x\n\U0001F468‍\U0001F469" };
    REQUIRE(luil::text::grapheme_next(family, 2) == family.size());
    REQUIRE(luil::text::grapheme_previous(family, family.size()) == 2u);

    // 한 걸음씩 묻는 답과 한 번에 모은 경계가 같다 (낱말 이동과 줄 넘김이 서로 다른 길을 쓴다).
    const std::u8string_view mixed { u8"ab\r\n\U0001F1F0\U0001F1F7\U0001F1F0\r\ré\n\n각x\U0001F468‍\U0001F469\r\n" };
    const std::vector<std::size_t> boundaries { luil::text::grapheme_boundaries(mixed) };
    std::vector<std::size_t> forward { 0 };
    for (std::size_t offset { 0 }; offset < mixed.size();)
        forward.push_back(offset = luil::text::grapheme_next(mixed, offset));
    REQUIRE(forward == boundaries);
    std::vector<std::size_t> backward { mixed.size() };
    for (std::size_t offset { mixed.size() }; offset > 0;)
        backward.insert(backward.begin(), offset = luil::text::grapheme_previous(mixed, offset));
    REQUIRE(backward == boundaries);
}
