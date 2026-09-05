#include "win32/utf8.h"
#include "win32/win32_clipboard.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("Pasted text keeps only the first line", "[win32][clipboard]")
{
    // 한 줄 칸에 넣을 값이라 첫 줄만 남는다.
    REQUIRE(luil::win32::first_clipboard_line(u8"hello") == u8"hello");
    REQUIRE(luil::win32::first_clipboard_line(u8"hello\r\nworld") == u8"hello");
    REQUIRE(luil::win32::first_clipboard_line(u8"hello\nworld") == u8"hello");
    // 탭도 줄 구분과 같이 본다 — 칸 사이를 옮기는 글자라 한 줄 안에 있을 것이 아니다.
    REQUIRE(luil::win32::first_clipboard_line(u8"hello\tworld") == u8"hello");
    // 첫 글자부터 구분이면 빈 값이다.
    REQUIRE(luil::win32::first_clipboard_line(u8"\r\nworld").empty());
    REQUIRE(luil::win32::first_clipboard_line(u8"").empty());
    // 파일 경로는 그대로 남는다 (구분자가 없다).
    REQUIRE(luil::win32::first_clipboard_line(u8"C:\\samples\\note.txt") == u8"C:\\samples\\note.txt");
}

TEST_CASE("Clipboard text block reads only the bytes it was given", "[win32][clipboard]")
{
    // NUL 종단이 없어도 준 크기까지만 읽는다 — 뒤의 X·Y는 범위 밖이다.
    const wchar_t block[] { L'h', L'i', L'!', L'X', L'Y' };
    const auto converted { luil::win32::text_from_utf16_block(block, 3 * sizeof(wchar_t)) };
    REQUIRE(converted.value.has_value());
    REQUIRE(*converted.value == u8"hi!");

    // 홀수 꼬리 바이트는 wchar_t 하나가 못 되니 버린다 — 7바이트면 세 글자다.
    const auto odd { luil::win32::text_from_utf16_block(block, 7) };
    REQUIRE(odd.value.has_value());
    REQUIRE(*odd.value == u8"hi!");
}

TEST_CASE("Clipboard text block stops at the first NUL", "[win32][clipboard]")
{
    // 종단이 크기보다 먼저 오면 거기까지가 글이다.
    const wchar_t block[] { L'a', L'b', L'\0', L'c', L'd' };
    const auto converted { luil::win32::text_from_utf16_block(block, sizeof(block)) };
    REQUIRE(converted.value.has_value());
    REQUIRE(*converted.value == u8"ab");
}

TEST_CASE("Empty clipboard block becomes empty text", "[win32][clipboard]")
{
    const wchar_t block[] { L'x' };
    const auto empty { luil::win32::text_from_utf16_block(block, 0) };
    REQUIRE(empty.value.has_value());
    REQUIRE(empty.value->empty());

    // 한 바이트는 wchar_t 하나가 못 된다.
    const auto sub_unit { luil::win32::text_from_utf16_block(block, 1) };
    REQUIRE(sub_unit.value.has_value());
    REQUIRE(sub_unit.value->empty());
}

TEST_CASE("Oversized clipboard block is truncated at the cap", "[win32][clipboard]")
{
    // 상한을 넘으면 앞에서부터 그만큼만 남는다.
    const std::wstring huge(luil::win32::max_clipboard_text_chars + 16, L'a');
    const auto converted { luil::win32::text_from_utf16_block(huge.data(), huge.size() * sizeof(wchar_t)) };
    REQUIRE(converted.value.has_value());
    REQUIRE(converted.value->size() == luil::win32::max_clipboard_text_chars);

    // 자른 자리가 surrogate 쌍의 가운데면 앞쪽 반쪽도 버린다 —
    // 반쪽이 남으면 변환이 블록 전체를 거절한다.
    std::wstring split(luil::win32::max_clipboard_text_chars - 1, L'a');
    split.push_back(static_cast<wchar_t>(0xD83Du));
    split.push_back(static_cast<wchar_t>(0xDE00u));
    const auto trimmed { luil::win32::text_from_utf16_block(split.data(), split.size() * sizeof(wchar_t)) };
    REQUIRE(trimmed.value.has_value());
    REQUIRE(trimmed.value->size() == luil::win32::max_clipboard_text_chars - 1);
}

TEST_CASE("Invalid UTF-16 in a clipboard block returns a structured error", "[win32][clipboard]")
{
    // 짝 없는 surrogate는 잘못된 입력이다 — 잘못된 UTF-8을 만들지 않는 것이 계약이다.
    const wchar_t block[] { static_cast<wchar_t>(0xD800u) };
    const auto converted { luil::win32::text_from_utf16_block(block, sizeof(block)) };
    REQUIRE_FALSE(converted.value.has_value());
    REQUIRE(converted.error.has_value());
    REQUIRE(converted.error->kind == luil::win32::utf_conversion_error_kind::invalid_input);
}
