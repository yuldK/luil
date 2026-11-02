#include "luil/text/text_change.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>

namespace {
    using luil::text::text_change;
    using luil::text::text_change_between;

    // 두 글 사이 변경을 `before`에 적용한 글이다. 맞게 찾았다면 `after`와 같다.
    [[nodiscard]] std::u8string applied(const std::u8string_view before, const std::u8string_view after, const text_change change)
    {
        std::u8string result { before.substr(0, change.offset) };
        result += after.substr(change.offset, change.inserted);
        result += before.substr(change.offset + change.removed);
        return result;
    }

    [[nodiscard]] bool is_boundary(const std::u8string_view text, const std::size_t offset) noexcept
    {
        return offset >= text.size() || (static_cast<unsigned char>(text[offset]) & 0xC0U) != 0x80U;
    }

    [[nodiscard]] bool in_range(const std::u8string_view before, const std::u8string_view after, const text_change change) noexcept
    {
        return change.offset <= before.size()
            && change.offset <= after.size()
            && change.removed <= before.size() - change.offset
            && change.inserted <= after.size() - change.offset;
    }
}

TEST_CASE("The same text is no change at all", "[text][text-change]")
{
    REQUIRE(text_change_between(u8"", u8"").empty());
    REQUIRE(text_change_between(u8"same", u8"same").empty());
    REQUIRE(text_change_between(u8"한글\nand more\r\n", u8"한글\nand more\r\n").empty());
}

TEST_CASE("An insertion, a deletion and a replacement are found at the start, the middle and the end", "[text][text-change]")
{
    SECTION("insertions")
    {
        REQUIRE(text_change_between(u8"world", u8"hello world") == text_change { 0, 0, 6 });
        REQUIRE(text_change_between(u8"hello world", u8"hello big world") == text_change { 6, 0, 4 });
        REQUIRE(text_change_between(u8"hello", u8"hello world") == text_change { 5, 0, 6 });
    }
    SECTION("deletions")
    {
        REQUIRE(text_change_between(u8"hello world", u8"world") == text_change { 0, 6, 0 });
        REQUIRE(text_change_between(u8"hello big world", u8"hello world") == text_change { 6, 4, 0 });
        REQUIRE(text_change_between(u8"hello world", u8"hello") == text_change { 5, 6, 0 });
    }
    SECTION("replacements")
    {
        REQUIRE(text_change_between(u8"cat sat", u8"dog sat") == text_change { 0, 3, 3 });
        REQUIRE(text_change_between(u8"a cat sat", u8"a doge sat") == text_change { 2, 3, 4 });
        REQUIRE(text_change_between(u8"sat cat", u8"sat dog") == text_change { 4, 3, 3 });
    }
    SECTION("an empty side")
    {
        REQUIRE(text_change_between(u8"", u8"abc") == text_change { 0, 0, 3 });
        REQUIRE(text_change_between(u8"abc", u8"") == text_change { 0, 3, 0 });
    }
}

TEST_CASE("The prefix is taken first when the prefix and the suffix would overlap", "[text][text-change]")
{
    // 같은 글자가 이어지면 접두가 먼찀 가져가고, 접미는 겹치지 않는 몫만 센다.
    REQUIRE(text_change_between(u8"aa", u8"aaa") == text_change { 2, 0, 1 });
    REQUIRE(text_change_between(u8"aaa", u8"aa") == text_change { 2, 1, 0 });
    REQUIRE(text_change_between(u8"abab", u8"ab") == text_change { 2, 2, 0 });
    REQUIRE(text_change_between(u8"ab", u8"abab") == text_change { 2, 0, 2 });
    REQUIRE(text_change_between(u8"a\n", u8"a\n\n") == text_change { 2, 0, 1 });
}

TEST_CASE("A change that splits a code point widens to the start of it", "[text][text-change]")
{
    // `가`는 EA B0 80이고 `각`은 EA B0 81이다. 접두 EA B0가 코드포인트 한가운데서 끝난다.
    const std::u8string before { u8"가" };
    const std::u8string after { u8"각" };
    REQUIRE(before.size() == 3u);
    REQUIRE(text_change_between(before, after) == text_change { 0, 3, 3 });
    REQUIRE(text_change_between(u8"a가b", u8"a각b") == text_change { 1, 3, 3 });
    // 갈린 뒤의 같은 글자는 접미로 남는다.
    REQUIRE(text_change_between(u8"가나", u8"각나") == text_change { 0, 3, 3 });
}

TEST_CASE("A suffix that begins inside a code point shrinks to the next boundary", "[text][text-change]")
{
    // EA B0 80과 EC B0 80은 이어짐 바이트 B0 80이 같다. 접미는 코드포인트 처음에서만 시작한다.
    const std::u8string before { u8"가" };
    const std::u8string after { u8"찀" };
    REQUIRE(before != after);
    const text_change change { text_change_between(before, after) };
    REQUIRE(change == text_change { 0, 3, 3 });
    // 줄임은 뒤에서도 코드포인트 단위다.
    REQUIRE(text_change_between(u8"x가y", u8"x찀y") == text_change { 1, 3, 3 });
}

TEST_CASE("Both ends of a change are code point boundaries in both texts", "[text][text-change]")
{
    // 이모지와 결합 부호를 섞은 글 둘을 모든 자리에서 견준다.
    const std::u8string pieces[] { u8"a", u8"가", u8"각", u8"\U0001F600", u8"é", u8"\n", u8"\r\n", u8"é" };
    std::uint32_t seed { 12345u };
    const auto next = [&seed](const std::size_t bound) {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<std::size_t>(seed >> 8U) % bound;
    };
    for (int round { 0 }; round < 2000; ++round)
    {
        std::u8string before {};
        std::u8string after {};
        for (std::size_t count { next(8) }; count > 0; --count)
            before += pieces[next(std::size(pieces))];
        for (std::size_t count { next(8) }; count > 0; --count)
            after += pieces[next(std::size(pieces))];
        if (next(2) == 0)
            after = before.substr(0, next(before.size() + 1));
        const text_change change { text_change_between(before, after) };
        INFO("round " << round);
        REQUIRE(in_range(before, after, change));
        REQUIRE(applied(before, after, change) == after);
        REQUIRE(is_boundary(before, change.offset));
        REQUIRE(is_boundary(after, change.offset));
        REQUIRE(is_boundary(before, change.offset + change.removed));
        REQUIRE(is_boundary(after, change.offset + change.inserted));
        REQUIRE((before == after) == change.empty());
    }
}

TEST_CASE("Broken UTF-8 never leaves the texts", "[text][text-change]")
{
    // 이어짐 바이트만 있는 글, 잘린 글, 앞이 없는 이어짐 바이트도 범위 안에서 답한다.
    const std::u8string cases[] {
        std::u8string { char8_t { 0x80 } },
        std::u8string { char8_t { 0x80 }, char8_t { 0x80 } },
        std::u8string { char8_t { 0xEA }, char8_t { 0xB0 } },
        std::u8string { u8"a" } + char8_t { 0xEA },
        std::u8string { char8_t { 0xB0 }, char8_t { 0x80 }, u8'x' },
        std::u8string { char8_t { 0xFF }, char8_t { 0xFE } },
        std::u8string {},
        std::u8string { u8"가나" },
    };
    for (const std::u8string& before : cases)
    {
        for (const std::u8string& after : cases)
        {
            const text_change change { text_change_between(before, after) };
            REQUIRE(in_range(before, after, change));
            REQUIRE(applied(before, after, change) == after);
        }
    }
}

TEST_CASE("A long text with one edit is compared across the block boundaries", "[text][text-change]")
{
    // 블록 단위 비교(64바이트)의 경계 앞뒤에서 바뀐 자리를 놓치지 않는다.
    std::u8string before(1000, u8'x');
    for (const std::size_t at : { std::size_t { 0 }, std::size_t { 63 }, std::size_t { 64 }, std::size_t { 65 }, std::size_t { 500 }, std::size_t { 935 }, std::size_t { 936 }, std::size_t { 999 } })
    {
        std::u8string after { before };
        after[at] = u8'y';
        REQUIRE(text_change_between(before, after) == text_change { at, 1, 1 });
        after.insert(at, u8"zz");
        const text_change change { text_change_between(before, after) };
        REQUIRE(applied(before, after, change) == after);
    }
}
