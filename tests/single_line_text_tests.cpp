#include "ui/single_line_text.h"

#include "luil/theme/ui_style.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkFont.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // 줄바꿈 처리는 프로세스에 하나라 한 실행 파일의 다른 test로 새지 않게 기본값으로 되돌린다.
    class line_break_scope
    {
    public:
        explicit line_break_scope(const luil::single_line_break mode) noexcept { luil::set_single_line_break(mode); }
        line_break_scope(const line_break_scope&) = delete;
        line_break_scope& operator=(const line_break_scope&) = delete;
        ~line_break_scope() { luil::set_single_line_break(luil::single_line_break::space); }
    };

    [[nodiscard]] std::u8string transformed(const std::u8string_view text, const luil::single_line_break mode)
    {
        std::u8string storage {};
        return std::u8string { luil::single_line_text(text, mode, storage) };
    }
} // namespace

TEST_CASE("Single-line text draws each line break as one space by default", "[ui][text]")
{
    REQUIRE(luil::single_line_break_mode() == luil::single_line_break::space);
    REQUIRE(transformed(u8"a\nb", luil::single_line_break::space) == u8"a b");
    REQUIRE(transformed(u8"a\rb", luil::single_line_break::space) == u8"a b");
    // CRLF 짝은 줄바꿈 하나다 — Windows에서 온 글만 공백이 두 칸이 되지 않는다.
    REQUIRE(transformed(u8"a\r\nb", luil::single_line_break::space) == u8"a b");
    // 빈 줄은 줄바꿈 둘이다.
    REQUIRE(transformed(u8"a\n\nb", luil::single_line_break::space) == u8"a  b");
    REQUIRE(transformed(u8"a\n\r\nb\r", luil::single_line_break::space) == u8"a  b ");
}

TEST_CASE("Single-line text leaves text without breaks untouched", "[ui][text]")
{
    const std::u8string_view text { u8"한 줄 글" };
    for (const luil::single_line_break mode : { luil::single_line_break::space, luil::single_line_break::symbol, luil::single_line_break::paragraphs })
    {
        std::u8string storage { u8"그대로" };
        const std::u8string_view result { luil::single_line_text(text, mode, storage) };
        // 할당도 복사도 없이 원문을 가리킨다.
        REQUIRE(result.data() == text.data());
        REQUIRE(result.size() == text.size());
        REQUIRE(storage == u8"그대로");
    }
}

TEST_CASE("Single-line text shows a return mark under symbol", "[ui][text]")
{
    REQUIRE(transformed(u8"a\nb", luil::single_line_break::symbol) == u8"a↵b");
    REQUIRE(transformed(u8"a\r\nb", luil::single_line_break::symbol) == u8"a↵b");
    REQUIRE(transformed(u8"\r\n", luil::single_line_break::symbol) == u8"↵");
}

TEST_CASE("Single-line text keeps invalid UTF-8 bytes around a break", "[ui][text]")
{
    // CR·LF는 이어지는 byte가 될 수 없어 byte로 훑는다. 잘못된 byte는 그리기의 관문이 고치도록 남는다.
    const std::u8string broken { char8_t { 0xE3 }, u8'\n', u8'b' };
    const std::u8string expected { char8_t { 0xE3 }, u8' ', u8'b' };
    REQUIRE(transformed(broken, luil::single_line_break::space) == expected);
}

TEST_CASE("Single-line paragraphs split at each break and count CRLF once", "[ui][text]")
{
    // paragraphs는 글을 바꾸지 않는다. 줄을 나누는 것은 따로다.
    REQUIRE(transformed(u8"a\nb", luil::single_line_break::paragraphs) == u8"a\nb");
    REQUIRE(luil::single_line_paragraphs(u8"abc") == std::vector<std::u8string_view> { u8"abc" });
    REQUIRE(luil::single_line_paragraphs(u8"a\r\nb\nc\rd") == std::vector<std::u8string_view> { u8"a", u8"b", u8"c", u8"d" });
    REQUIRE(luil::single_line_paragraphs(u8"a\n\nb") == std::vector<std::u8string_view> { u8"a", u8"", u8"b" });
    // 끝의 줄바꿈 뒤에는 빈 줄 하나가 선다.
    REQUIRE(luil::single_line_paragraphs(u8"a\r\n") == std::vector<std::u8string_view> { u8"a", u8"" });
}

TEST_CASE("measure_text follows the installed line break the same way drawing does", "[ui][text]")
{
    // 글꼴이 없는 test 환경에서는 폭이 0일 수 있다. 같은 규칙을 지난 두 글이 같은 폭이라는 것은 그래도 성립한다.
    const SkFont font {};

    SECTION("space")
    {
        const line_break_scope scope { luil::single_line_break::space };
        REQUIRE(luil::measure_text(u8"a\nb", font) == luil::measure_text(u8"a b", font));
        REQUIRE(luil::measure_text(u8"a\r\nb", font) == luil::measure_text(u8"a b", font));
    }

    SECTION("symbol")
    {
        const line_break_scope scope { luil::single_line_break::symbol };
        REQUIRE(luil::single_line_break_mode() == luil::single_line_break::symbol);
        REQUIRE(luil::measure_text(u8"a\r\nb", font) == luil::measure_text(u8"a↵b", font));
    }

    SECTION("paragraphs")
    {
        const line_break_scope scope { luil::single_line_break::paragraphs };
        const float widest { std::max(luil::measure_text(u8"ab", font), luil::measure_text(u8"cdef", font)) };
        REQUIRE(luil::measure_text(u8"ab\r\ncdef", font) == widest);
        REQUIRE(luil::measure_text(u8"cdef\nab", font) == widest);
    }

    REQUIRE(luil::single_line_break_mode() == luil::single_line_break::space);
}
