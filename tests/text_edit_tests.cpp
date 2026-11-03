#include "luil/text/text_edit.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    luil::text::text_edit_state at_end(const std::u8string_view text)
    {
        luil::text::text_edit_state state {};
        luil::text::text_edit_set_text(state, text, true);
        return state;
    }

    std::size_t history_bytes(const luil::text::text_edit_state& state)
    {
        std::size_t bytes { 0 };
        for (const auto* revisions : { &state.undo, &state.redo })
            for (const luil::text::text_edit_revision& revision : *revisions)
                bytes += revision.text.size();
        return bytes;
    }

    // 줄마다 16바이트인 1 MiB 글이다.
    // 줄을 나눠 글자소 경계를 묻는 비용이 줄 하나에 머문다.
    std::u8string mebibyte_text()
    {
        std::u8string text {};
        text.reserve(std::size_t { 1024 } * 1024);
        while (text.size() < std::size_t { 1024 } * 1024)
            text += u8"0123456789abcde\n";
        return text;
    }
}

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

TEST_CASE("Text edit history keeps a large document under the byte limit", "[domain][text-edit][undo]")
{
    // 시점마다 1 MiB를 복사한다.
    // 개수 상한만 있으면 100걸음에 100 MiB를 쥔다.
    luil::text::text_edit_state state { at_end(mebibyte_text()) };
    std::u8string before_last {};
    for (std::size_t step { 0 }; step < luil::text::text_edit_history_limit; ++step)
    {
        before_last = state.text;
        // 두 코드포인트 조각은 묶이지 않아 걸음마다 새 덩어리다.
        luil::text::text_edit_insert(state, u8"xy");
    }
    REQUIRE(history_bytes(state) <= luil::text::text_edit_history_byte_limit);
    REQUIRE(state.undo.size() > 1u);
    REQUIRE(state.undo.size() < luil::text::text_edit_history_limit);

    // 가장 오래된 시점부터 버렸으므로 바로 앞 걸음은 그대로 되돌아간다.
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == before_last);
}

TEST_CASE("Text edit history keeps one step of a document larger than the byte limit", "[domain][text-edit][undo]")
{
    const std::u8string large(luil::text::text_edit_history_byte_limit + 1u, u8'a');
    luil::text::text_edit_state state { at_end(large) };
    luil::text::text_edit_insert(state, u8"xy");
    luil::text::text_edit_insert(state, u8"zw");

    // 상한을 넘어도 바로 앞 한 걸음은 남는다.
    REQUIRE(state.undo.size() == 1u);
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text.ends_with(u8"axy"));
    REQUIRE_FALSE(luil::text::text_edit_undo(state));

    // 다시 실행할 한 걸음도 남는다.
    REQUIRE(luil::text::text_edit_redo(state));
    REQUIRE(state.text.ends_with(u8"xyzw"));
}

TEST_CASE("Text edit history keeps the count limit for small texts", "[domain][text-edit][undo]")
{
    luil::text::text_edit_state state {};
    for (std::size_t step { 0 }; step < luil::text::text_edit_history_limit + 50u; ++step)
        luil::text::text_edit_insert(state, u8"xy");
    REQUIRE(state.undo.size() == luil::text::text_edit_history_limit);

    for (std::size_t step { 0 }; step < luil::text::text_edit_history_limit; ++step)
        REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE_FALSE(luil::text::text_edit_undo(state));
    REQUIRE(state.text.size() == 100u);
    REQUIRE(state.redo.size() == luil::text::text_edit_history_limit);
}

TEST_CASE("Text edit redo replays undone steps under the byte limit", "[domain][text-edit][undo]")
{
    luil::text::text_edit_state state { at_end(mebibyte_text()) };
    for (std::size_t step { 0 }; step < 60u; ++step)
        luil::text::text_edit_insert(state, u8"xy");
    const std::u8string last { state.text };

    // 실행 취소는 redo로 옮길 뿐 기록 전체를 늘리지 않아야 한다.
    for (std::size_t step { 0 }; step < 10u; ++step)
    {
        REQUIRE(luil::text::text_edit_undo(state));
        REQUIRE(history_bytes(state) <= luil::text::text_edit_history_byte_limit);
    }
    REQUIRE(state.redo.size() == 10u);
    for (std::size_t step { 0 }; step < 10u; ++step)
    {
        REQUIRE(luil::text::text_edit_redo(state));
        REQUIRE(history_bytes(state) <= luil::text::text_edit_history_byte_limit);
    }
    REQUIRE(state.text == last);
    REQUIRE_FALSE(luil::text::text_edit_redo(state));
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

TEST_CASE("Typed four byte characters join one undo group like other typed characters", "[domain][text-edit][undo]")
{
    // 친 글자인지는 바이트가 아니라 코드포인트로 센다. 바이트로 세면 이모지 하나하나가 따로 되돌아간다.
    luil::text::text_edit_state state {};
    for (const char32_t value : { U'\U0001F600', U'\U00020000', U'\U0001F600' })
        luil::text::text_edit_insert(state, luil::text::text_edit_encode_utf8(value));
    REQUIRE(state.text.size() == 12u);
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text.empty());

    // 코드포인트 둘 이상인 조각(붙여넣기)은 앞의 입력과 묶이지 않는다.
    luil::text::text_edit_insert(state, u8"a");
    luil::text::text_edit_insert(state, u8"bc");
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == u8"a");
}

TEST_CASE("Word commands walk graphemes across CRLF line breaks", "[domain][text-edit]")
{
    // "ab 😀" CRLF CRLF "cd é" + "f" — 낱말 경계는 줄 바꿈을 넘어도 글자소 단위이고, CRLF는 한 덩어리다.
    const std::u8string text { u8"ab \U0001F600\r\n\r\ncd éf" };
    REQUIRE(text.size() == 18u);
    luil::text::text_edit_state state { at_end(text) };
    const auto word_left = [&state] {
        luil::text::text_edit_move(state, luil::text::text_edit_motion::word_left, false);
        return state.caret;
    };
    REQUIRE(word_left() == 14u);
    REQUIRE(word_left() == 11u);
    REQUIRE(word_left() == 3u);
    REQUIRE(word_left() == 0u);
    const auto word_right = [&state] {
        luil::text::text_edit_move(state, luil::text::text_edit_motion::word_right, false);
        return state.caret;
    };
    REQUIRE(word_right() == 3u);
    REQUIRE(word_right() == 11u);
    REQUIRE(word_right() == 14u);
    REQUIRE(word_right() == 18u);

    // 결합 문자 가운데를 가리켜도 낱말 전체를 고른다. 줄 바꿈 위면 이어진 줄 바꿈 덩어리다.
    luil::text::text_edit_select_word(state, 15);
    REQUIRE(luil::text::text_edit_selected_text(state) == u8"éf");
    luil::text::text_edit_select_word(state, 8);
    REQUIRE(luil::text::text_edit_selected_text(state) == u8"\r\n\r\n");
}

namespace {
    [[nodiscard]] std::shared_ptr<const luil::text::text_format> marked(const std::size_t begin, const std::size_t end, const luil::text::text_mark marks)
    {
        return std::make_shared<const luil::text::text_format>(luil::text::text_format { { { begin, end, marks } }, {} });
    }
}

TEST_CASE("Undo and redo bring back the text and the format together", "[domain][text-edit][rich-text]")
{
    luil::text::text_edit_state state { at_end(u8"hello") };
    const auto bold { marked(0, 5, luil::text::text_mark::bold) };
    luil::text::text_edit_set_format(state, bold);
    REQUIRE(state.format == bold);
    REQUIRE(state.undo.size() == 1u);
    luil::text::text_edit_insert(state, u8"!");
    REQUIRE(state.undo.size() == 2u);
    REQUIRE(state.format->runs == std::vector<luil::text::text_mark_run> { { 0, 6, luil::text::text_mark::bold } });

    // 한 걸음씩 글과 서식이 함께 되돌아간다. 시점은 그때의 서식 포인터를 그대로 쥔다.
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == u8"hello");
    REQUIRE(state.format == bold);
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == u8"hello");
    REQUIRE(state.format == nullptr);
    REQUIRE(luil::text::text_edit_redo(state));
    REQUIRE(state.format == bold);
    REQUIRE(luil::text::text_edit_redo(state));
    REQUIRE(state.text == u8"hello!");
    REQUIRE(state.format->runs == std::vector<luil::text::text_mark_run> { { 0, 6, luil::text::text_mark::bold } });
}

TEST_CASE("Setting a format is one step that never joins and clears redo", "[domain][text-edit][rich-text]")
{
    luil::text::text_edit_state state { at_end(u8"abc") };
    luil::text::text_edit_insert(state, u8"d");
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.redo.size() == 1u);

    // 서식만 바꾼 걸음 둘은 묶이지 않고, 다시 실행할 것을 비운다.
    luil::text::text_edit_set_format(state, marked(0, 1, luil::text::text_mark::bold));
    REQUIRE(state.redo.empty());
    luil::text::text_edit_set_format(state, marked(0, 2, luil::text::text_mark::bold));
    REQUIRE(state.undo.size() == 2u);
    // 같은 값은 걸음이 아니다.
    luil::text::text_edit_set_format(state, marked(0, 2, luil::text::text_mark::bold));
    REQUIRE(state.undo.size() == 2u);

    // 이어 친 글은 서식 걸음과 묶이지 않고, 저희끼리는 한 덩어리로 묶인다.
    luil::text::text_edit_insert(state, u8"x");
    luil::text::text_edit_insert(state, u8"y");
    luil::text::text_edit_insert(state, u8"z");
    REQUIRE(state.undo.size() == 3u);
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == u8"abc");
    REQUIRE(state.format->runs == std::vector<luil::text::text_mark_run> { { 0, 2, luil::text::text_mark::bold } });
}

TEST_CASE("The history byte limit counts the format of every revision", "[domain][text-edit][rich-text]")
{
    // 글은 작고 서식의 링크 주소가 크다. 글만 세면 네 시점이 모두 남는다.
    const std::u8string destination(std::size_t { 12 } * 1024 * 1024, u8'a');
    luil::text::text_edit_state state { at_end(u8"abc") };
    // 걸음마다 굵기 구간의 서식이 달라 같은 값으로 건너뛰지 않는다.
    for (const luil::text::text_mark marks : { luil::text::text_mark::bold, luil::text::text_mark::italic, luil::text::text_mark::underline, luil::text::text_mark::code })
    {
        auto format { std::make_shared<luil::text::text_format>() };
        format->runs.push_back({ 0, 1, marks });
        format->links.push_back({ 0, 3, destination });
        luil::text::text_edit_set_format(state, std::move(format));
    }
    std::size_t bytes { 0 };
    for (const luil::text::text_edit_revision& revision : state.undo)
        bytes += revision.text.size() + (revision.format != nullptr ? luil::text::text_format_bytes(*revision.format) : 0);
    // 서식 없는 시점(0)과 첫 서식(12 MiB)을 버려 두 시점(24 MiB)이 남는다.
    REQUIRE(state.undo.size() == 2u);
    REQUIRE(bytes <= luil::text::text_edit_history_byte_limit);
}

TEST_CASE("Setting text from outside drops the format and the typing marks", "[domain][text-edit][rich-text]")
{
    luil::text::text_edit_state state { at_end(u8"abc") };
    luil::text::text_edit_set_format(state, marked(0, 3, luil::text::text_mark::italic));
    state.typing_marks = luil::text::text_mark::bold;
    luil::text::text_edit_set_text(state, u8"other", true);
    REQUIRE(state.format == nullptr);
    REQUIRE(state.typing_marks.has_value() == false);
    REQUIRE(state.undo.empty());
}

TEST_CASE("An IME rewrite of repeated letters moves the format as if the letter at the caret changed", "[domain][text-edit][rich-text]")
{
    // "aa"의 첫 `a`만 굵다. caret 0에서 하나를 지운 글 "a"는 첫 `a`를 지운 것이라 남은 `a`가 보통 글이다.
    //  - 접두를 먼저 가져가는 견줌은 둘째 `a`를 지운 것으로 읽어 굵은 `a`를 남긴다.
    luil::text::text_edit_state removed {};
    luil::text::text_edit_set_formatted_text(removed, u8"aa", marked(0, 1, luil::text::text_mark::bold));
    luil::text::text_edit_replace_all(removed, u8"a", 0);
    REQUIRE(removed.text == u8"a");
    REQUIRE(removed.format->runs.empty());

    // "aa"의 둘째 `a`만 굵다. 처음에 `a`를 넣어 caret이 1이면 넣은 글은 글 처음의 것이라 뒤 글자(보통)를 따른다.
    luil::text::text_edit_state inserted {};
    luil::text::text_edit_set_formatted_text(inserted, u8"aa", marked(1, 2, luil::text::text_mark::bold));
    luil::text::text_edit_replace_all(inserted, u8"aaa", 1);
    REQUIRE(inserted.format->runs == std::vector<luil::text::text_mark_run> { { 2, 3, luil::text::text_mark::bold } });

    // 다음 글자 서식은 확정 글이 받는다.
    luil::text::text_edit_state typed {};
    luil::text::text_edit_set_text(typed, u8"가", true);
    typed.typing_marks = luil::text::text_mark::bold;
    // 같은 글을 다시 알린 것은 caret이 그대로면 다음 글자 서식을 지우지 않는다.
    luil::text::text_edit_replace_all(typed, u8"가", 3);
    REQUIRE(typed.typing_marks == luil::text::text_mark::bold);
    luil::text::text_edit_replace_all(typed, u8"가나", 6);
    REQUIRE(typed.format != nullptr);
    REQUIRE(typed.format->runs == std::vector<luil::text::text_mark_run> { { 3, 6, luil::text::text_mark::bold } });
    REQUIRE(typed.typing_marks.has_value() == false);
}

TEST_CASE("Moving the caret, selecting and undoing drop the typing marks", "[domain][text-edit][rich-text]")
{
    luil::text::text_edit_state state { at_end(u8"abc") };
    state.typing_marks = luil::text::text_mark::bold;
    luil::text::text_edit_move(state, luil::text::text_edit_motion::left, false);
    REQUIRE(state.typing_marks.has_value() == false);

    state.typing_marks = luil::text::text_mark::bold;
    luil::text::text_edit_select_all(state);
    REQUIRE(state.typing_marks.has_value() == false);

    state.typing_marks = luil::text::text_mark::bold;
    luil::text::text_edit_place_caret(state, 1, false);
    REQUIRE(state.typing_marks.has_value() == false);

    luil::text::text_edit_insert(state, u8"x");
    state.typing_marks = luil::text::text_mark::bold;
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.typing_marks.has_value() == false);

    // 지우기도 그 자리를 바꿔 비운다.
    state.typing_marks = luil::text::text_mark::bold;
    luil::text::text_edit_backspace(state);
    REQUIRE(state.typing_marks.has_value() == false);
}
