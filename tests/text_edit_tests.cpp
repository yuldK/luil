#include "luil/text/text_edit.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace {
    luil::text::text_edit_state at_end(const std::u8string_view text)
    {
        luil::text::text_edit_state state {};
        luil::text::text_edit_set_text(state, text, true);
        return state;
    }
} // namespace

TEST_CASE("Text edit inserts and deletes at the caret", "[domain][text-edit]")
{
    luil::text::text_edit_state state { at_end(u8"main") };
    REQUIRE(state.caret == 4u);

    luil::text::text_edit_move(state, luil::text::text_edit_motion::line_start, false);
    luil::text::text_edit_insert(state, u8"origin/");
    REQUIRE(state.text == u8"origin/main");
    REQUIRE(state.caret == 7u);
    REQUIRE_FALSE(luil::text::text_edit_has_selection(state));

    luil::text::text_edit_backspace(state);
    REQUIRE(state.text == u8"originmain");
    REQUIRE(state.caret == 6u);

    luil::text::text_edit_delete_forward(state);
    REQUIRE(state.text == u8"originain");
    REQUIRE(state.caret == 6u);

    // 끝에서의 delete와 처음에서의 backspace는 아무 일도 하지 않는다.
    luil::text::text_edit_move(state, luil::text::text_edit_motion::line_end, false);
    luil::text::text_edit_delete_forward(state);
    luil::text::text_edit_move(state, luil::text::text_edit_motion::line_start, false);
    luil::text::text_edit_backspace(state);
    REQUIRE(state.text == u8"originain");
}

TEST_CASE("Text edit moves over whole characters, not bytes", "[domain][text-edit]")
{
    // 한글 한 글자는 UTF-8 3바이트다.
    // caret이 그 가운데 서면 안 된다.
    luil::text::text_edit_state state { at_end(u8"가나") };
    REQUIRE(state.caret == 6u);

    luil::text::text_edit_move(state, luil::text::text_edit_motion::left, false);
    REQUIRE(state.caret == 3u);
    luil::text::text_edit_backspace(state);
    REQUIRE(state.text == u8"나");
    REQUIRE(state.caret == 0u);

    // 바이트 한가운데를 가리켜도 문자 시작으로 되돌아간다.
    luil::text::text_edit_place_caret(state, 2u, false);
    REQUIRE(state.caret == 0u);
    luil::text::text_edit_place_caret(state, 99u, false);
    REQUIRE(state.caret == 3u);
}

TEST_CASE("Text edit moves by word over branch separators", "[domain][text-edit]")
{
    luil::text::text_edit_state state { at_end(u8"origin/feature-a") };

    luil::text::text_edit_move(state, luil::text::text_edit_motion::word_left, false);
    REQUIRE(state.caret == 15u);
    luil::text::text_edit_move(state, luil::text::text_edit_motion::word_left, false);
    REQUIRE(state.caret == 7u);
    luil::text::text_edit_move(state, luil::text::text_edit_motion::word_left, false);
    REQUIRE(state.caret == 0u);

    luil::text::text_edit_move(state, luil::text::text_edit_motion::word_right, false);
    REQUIRE(state.caret == 7u);
    luil::text::text_edit_move(state, luil::text::text_edit_motion::word_right, false);
    REQUIRE(state.caret == 15u);
}

TEST_CASE("Text edit selection replaces, copies and collapses", "[domain][text-edit]")
{
    luil::text::text_edit_state state { at_end(u8"origin/main") };

    // Shift+←는 anchor를 두고 caret만 옮겨 범위를 늘린다.
    luil::text::text_edit_move(state, luil::text::text_edit_motion::word_left, true);
    REQUIRE(luil::text::text_edit_has_selection(state));
    REQUIRE(luil::text::text_edit_selected_text(state) == u8"main");
    REQUIRE(luil::text::text_edit_selection_begin(state) == 7u);
    REQUIRE(luil::text::text_edit_selection_end(state) == 11u);

    // 늘리지 않는 이동은 선택의 끝으로 접힌다.
    luil::text::text_edit_move(state, luil::text::text_edit_motion::right, false);
    REQUIRE_FALSE(luil::text::text_edit_has_selection(state));
    REQUIRE(state.caret == 11u);

    luil::text::text_edit_select_all(state);
    luil::text::text_edit_insert(state, u8"develop");
    REQUIRE(state.text == u8"develop");
    REQUIRE(state.caret == 7u);

    // 선택이 있으면 backspace는 그 범위를 지운다.
    luil::text::text_edit_select_all(state);
    luil::text::text_edit_backspace(state);
    REQUIRE(state.text.empty());
    REQUIRE(state.caret == 0u);
}

TEST_CASE("Text edit selects the word under an offset", "[domain][text-edit]")
{
    luil::text::text_edit_state state { at_end(u8"/branches/2026-08") };

    luil::text::text_edit_select_word(state, 3u);
    REQUIRE(luil::text::text_edit_selected_text(state) == u8"branches");

    // 구분자 위에서는 그 구분자 덩어리를 고른다.
    luil::text::text_edit_select_word(state, 9u);
    REQUIRE(luil::text::text_edit_selected_text(state) == u8"/");

    luil::text::text_edit_select_word(state, 12u);
    REQUIRE(luil::text::text_edit_selected_text(state) == u8"2026");

    // 빈 글에서는 선택이 생기지 않는다.
    luil::text::text_edit_state empty {};
    luil::text::text_edit_select_word(empty, 4u);
    REQUIRE_FALSE(luil::text::text_edit_has_selection(empty));
}

TEST_CASE("Text edit drag selection keeps the anchor", "[domain][text-edit]")
{
    luil::text::text_edit_state state { at_end(u8"origin/main") };

    // 누른 자리가 anchor이고 끌면 caret만 따라간다.
    luil::text::text_edit_place_caret(state, 2u, false);
    luil::text::text_edit_place_caret(state, 6u, true);
    REQUIRE(state.anchor == 2u);
    REQUIRE(state.caret == 6u);
    REQUIRE(luil::text::text_edit_selected_text(state) == u8"igin");

    // 왼쪽으로 되돌아가면 범위가 뒤집힌다.
    luil::text::text_edit_place_caret(state, 0u, true);
    REQUIRE(luil::text::text_edit_selection_begin(state) == 0u);
    REQUIRE(luil::text::text_edit_selection_end(state) == 2u);
}

TEST_CASE("Text edit clamps offsets when the text changes underneath", "[domain][text-edit]")
{
    luil::text::text_edit_state state { at_end(u8"origin/feature") };
    luil::text::text_edit_select_all(state);

    luil::text::text_edit_set_text(state, u8"dev", false);
    REQUIRE(state.caret == 3u);
    REQUIRE(state.anchor == 0u);

    luil::text::text_edit_set_text(state, u8"/trunk", true);
    REQUIRE(state.caret == 6u);
    REQUIRE(state.anchor == 6u);
}

TEST_CASE("Text edit encodes BMP characters as UTF-8", "[domain][text-edit]")
{
    REQUIRE(luil::text::text_edit_encode_utf8(U'a') == u8"a");
    REQUIRE(luil::text::text_edit_encode_utf8(U'é') == u8"é");
    REQUIRE(luil::text::text_edit_encode_utf8(U'가') == u8"가");
}

TEST_CASE("Text edit undo groups a typing run and redo replays it", "[domain][text-edit][undo]")
{
    luil::text::text_edit_state state {};
    for (const char8_t value : std::u8string_view { u8"main" })
        luil::text::text_edit_insert(state, std::u8string_view { &value, 1u });
    REQUIRE(state.text == u8"main");

    // 이어 친 글자는 한 덩어리다.
    // 한 번의 실행 취소로 모두 사라진다.
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text.empty());
    REQUIRE_FALSE(luil::text::text_edit_undo(state));

    REQUIRE(luil::text::text_edit_redo(state));
    REQUIRE(state.text == u8"main");
    REQUIRE(state.caret == 4u);
    REQUIRE_FALSE(luil::text::text_edit_redo(state));
}

TEST_CASE("Text edit undo breaks the group when the caret moves", "[domain][text-edit][undo]")
{
    luil::text::text_edit_state state {};
    luil::text::text_edit_insert(state, u8"ab");
    luil::text::text_edit_move(state, luil::text::text_edit_motion::line_start, false);
    luil::text::text_edit_insert(state, u8"cd");
    REQUIRE(state.text == u8"cdab");

    // caret을 옮긴 뒤의 입력은 새 덩어리다.
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == u8"ab");
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text.empty());
}

TEST_CASE("Text edit undo restores the selection that a replacement consumed", "[domain][text-edit][undo]")
{
    luil::text::text_edit_state state {};
    luil::text::text_edit_insert(state, u8"origin/main");
    luil::text::text_edit_select_all(state);
    luil::text::text_edit_insert(state, u8"dev");
    REQUIRE(state.text == u8"dev");

    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == u8"origin/main");
    REQUIRE(luil::text::text_edit_selected_text(state) == u8"origin/main");

    // 지우기도 되돌아간다.
    REQUIRE(luil::text::text_edit_redo(state));
    REQUIRE(state.text == u8"dev");
    luil::text::text_edit_move(state, luil::text::text_edit_motion::line_end, false);
    luil::text::text_edit_backspace(state);
    luil::text::text_edit_backspace(state);
    REQUIRE(state.text == u8"d");
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == u8"dev");
}

TEST_CASE("Text edit deletes whole words with Ctrl", "[domain][text-edit]")
{
    luil::text::text_edit_state state {};
    luil::text::text_edit_insert(state, u8"origin/feature-a");

    // Ctrl+Backspace는 낱말과 그 앞의 구분자까지 지운다 (Windows 관례).
    luil::text::text_edit_delete_word_left(state);
    REQUIRE(state.text == u8"origin/feature-");
    luil::text::text_edit_delete_word_left(state);
    REQUIRE(state.text == u8"origin/");

    // Ctrl+Delete는 다음 낱말의 시작까지 지운다.
    luil::text::text_edit_move(state, luil::text::text_edit_motion::line_start, false);
    luil::text::text_edit_delete_word_right(state);
    REQUIRE(state.text.empty());

    // 되돌리면 낱말이 통째로 돌아온다.
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == u8"origin/");
}

TEST_CASE("Text edit history is dropped when the text comes from outside", "[domain][text-edit][undo]")
{
    luil::text::text_edit_state state {};
    luil::text::text_edit_insert(state, u8"typed");
    luil::text::text_edit_set_text(state, u8"/trunk", true);
    // 사용자의 편집이 아니므로 되돌릴 것이 없다.
    REQUIRE_FALSE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == u8"/trunk");
}

TEST_CASE("Text edit accepts characters above the BMP", "[domain][text-edit]")
{
    // 이모지는 UTF-8 4바이트다.
    // 3바이트까지만 만들던 시절에는 여기서 깨졌다.
    const std::u8string emoji { luil::text::text_edit_encode_utf8(U'\U0001F600') };
    REQUIRE(emoji.size() == 4u);
    REQUIRE(emoji == u8"😀");

    // 잘못된 UTF-8을 만들지 않는 것이 계약이다.
    REQUIRE(luil::text::text_edit_encode_utf8(static_cast<char32_t>(0xD83Du)).empty());
    REQUIRE(luil::text::text_edit_encode_utf8(static_cast<char32_t>(0x110000u)).empty());
}

TEST_CASE("Caret motion treats an emoji as one character", "[domain][text-edit]")
{
    luil::text::text_edit_state state { at_end(u8"a😀b") };
    REQUIRE(state.text.size() == 6u);
    REQUIRE(state.caret == 6u);

    // ←는 이모지를 통째로 넘는다.
    // 4바이트 가운데로 들어가면 글이 깨진다.
    luil::text::text_edit_move(state, luil::text::text_edit_motion::left, false);
    REQUIRE(state.caret == 5u);
    luil::text::text_edit_move(state, luil::text::text_edit_motion::left, false);
    REQUIRE(state.caret == 1u);

    // backspace도 통째로 지운다.
    luil::text::text_edit_move(state, luil::text::text_edit_motion::line_end, false);
    luil::text::text_edit_backspace(state);
    luil::text::text_edit_backspace(state);
    REQUIRE(state.text == u8"a");
}

TEST_CASE("Typing an emoji inserts all four bytes at the caret", "[domain][text-edit]")
{
    luil::text::text_edit_state state { at_end(u8"ab") };
    luil::text::text_edit_move(state, luil::text::text_edit_motion::left, false);
    luil::text::text_edit_insert(state, luil::text::text_edit_encode_utf8(U'\U0001F600'));

    REQUIRE(state.text == u8"a😀b");
    REQUIRE(state.caret == 5u);
}
