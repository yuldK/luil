#include "luil/ui/syntax_highlighter.h"

#include "luil/text/grapheme.h"
#include "luil/text/text_change.h"
#include "luil/text/text_edit.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// 구문 강조 도우미(docs/syntax-highlight-design.md)의 test다.
// 렉서는 이 파일의 작은 C 모양 렉서다. 키워드·문자열·숫자·`//`·여러 줄 `/* */`만 안다.
namespace {
    using luil::text_color_role;
    using luil::text_span;

    constexpr std::chrono::microseconds no_time { 0 };

    [[nodiscard]] bool is_word_byte(const char8_t byte) noexcept
    {
        return (byte >= u8'a' && byte <= u8'z')
            || (byte >= u8'A' && byte <= u8'Z')
            || (byte >= u8'0' && byte <= u8'9')
            || byte == u8'_'
            || byte >= 0x80;
    }

    [[nodiscard]] bool is_digit(const char8_t byte) noexcept
    {
        return byte >= u8'0' && byte <= u8'9';
    }

    [[nodiscard]] bool is_keyword(const std::u8string_view word) noexcept
    {
        for (const std::u8string_view keyword : { std::u8string_view { u8"if" }, std::u8string_view { u8"else" }, std::u8string_view { u8"for" }, std::u8string_view { u8"while" }, std::u8string_view { u8"return" } })
            if (word == keyword)
                return true;
        return false;
    }

    // 상태 0은 본문, 1은 여러 줄 주석 안이다.
    int toy_lexer(const std::u8string_view line, int state, std::vector<text_span>& spans)
    {
        std::size_t at { 0 };
        while (at < line.size())
        {
            if (state == 1)
            {
                const std::size_t close { line.find(u8"*/", at) };
                const std::size_t end { close == std::u8string_view::npos ? line.size() : close + 2 };
                spans.push_back({ at, end, text_color_role::comment, false, luil::text_background_role::none, true });
                at = end;
                if (close != std::u8string_view::npos)
                    state = 0;
                continue;
            }
            if (line.substr(at, 2) == u8"/*")
            {
                const std::size_t close { line.find(u8"*/", at + 2) };
                const std::size_t end { close == std::u8string_view::npos ? line.size() : close + 2 };
                spans.push_back({ at, end, text_color_role::comment, false, luil::text_background_role::none, true });
                at = end;
                state = close == std::u8string_view::npos ? 1 : 0;
                continue;
            }
            if (line.substr(at, 2) == u8"//")
            {
                spans.push_back({ at, line.size(), text_color_role::comment, false, luil::text_background_role::none, true });
                at = line.size();
                continue;
            }
            if (line[at] == u8'"')
            {
                const std::size_t close { line.find(u8'"', at + 1) };
                const std::size_t end { close == std::u8string_view::npos ? line.size() : close + 1 };
                spans.push_back({ at, end, text_color_role::string });
                at = end;
                continue;
            }
            if (is_digit(line[at]))
            {
                std::size_t end { at };
                while (end < line.size() && is_digit(line[end]))
                    ++end;
                spans.push_back({ at, end, text_color_role::number });
                at = end;
                continue;
            }
            if (is_word_byte(line[at]))
            {
                std::size_t end { at };
                while (end < line.size() && is_word_byte(line[end]))
                    ++end;
                if (is_keyword(line.substr(at, end - at)))
                    spans.push_back({ at, end, text_color_role::keyword, true });
                else if (end < line.size() && line[end] == u8'(')
                    spans.push_back({ at, end, text_color_role::function });
                at = end;
                continue;
            }
            ++at;
        }
        return state;
    }

    [[nodiscard]] luil::syntax_highlighter_config toy_config()
    {
        luil::syntax_highlighter_config config {};
        config.lexer = toy_lexer;
        return config;
    }

    [[nodiscard]] bool same_span(const text_span& first, const text_span& second) noexcept
    {
        return first.begin == second.begin
            && first.end == second.end
            && first.color == second.color
            && first.bold == second.bold
            && first.background == second.background
            && first.italic == second.italic;
    }

    [[nodiscard]] bool same_spans(const std::vector<text_span>& first, const std::vector<text_span>& second) noexcept
    {
        return first.size() == second.size() && std::equal(first.begin(), first.end(), second.begin(), same_span);
    }

    // 같은 글을 처음부터 칠한 구간이다. 증분 결과가 이것과 같아야 한다.
    [[nodiscard]] std::vector<text_span> painted_from_scratch(const std::u8string_view text, const luil::syntax_highlighter_config& config = toy_config())
    {
        luil::syntax_highlighter highlighter { config };
        highlighter.update(text);
        return *highlighter.spans();
    }

    // 구간 불변식이다. begin 순서, 겹침 없음, 비어 있지 않음, 글 안, 글자소 경계, 줄 바꿈 바이트를 덮지 않음.
    void require_well_formed(const std::u8string_view text, const std::vector<text_span>& spans)
    {
        const std::vector<std::size_t> boundaries { luil::text::grapheme_boundaries(text) };
        std::size_t previous_end { 0 };
        for (const text_span& span : spans)
        {
            REQUIRE(span.begin < span.end);
            REQUIRE(span.begin >= previous_end);
            REQUIRE(span.end <= text.size());
            REQUIRE(std::binary_search(boundaries.begin(), boundaries.end(), span.begin));
            REQUIRE(std::binary_search(boundaries.begin(), boundaries.end(), span.end));
            for (std::size_t at { span.begin }; at < span.end; ++at)
            {
                REQUIRE(text[at] != u8'\n');
                REQUIRE(text[at] != u8'\r');
            }
            previous_end = span.end;
        }
    }

    // 결정적인 난수다. 표준 분포는 구현마다 달라 쓰지 않는다.
    class sequence
    {
    public:
        explicit sequence(const std::uint32_t seed)
            : state_ { seed }
        {
        }

        [[nodiscard]] std::size_t next(const std::size_t bound)
        {
            state_ = state_ * 1664525U + 1013904223U;
            return static_cast<std::size_t>(state_ >> 8U) % bound;
        }

    private:
        std::uint32_t state_;
    };

    const std::u8string edit_pieces[] {
        u8"a", u8"if ", u8"x1", u8"12", u8" ", u8"(", u8")", u8"\"", u8"/*", u8"*/", u8"//", u8"\n", u8"\r\n", u8"\r", u8"\n\n",
        u8"가", u8"각", u8"é", u8"\U0001F600", u8"return ", u8"f(", u8"/", u8"*",
    };

    [[nodiscard]] std::u8string random_text(sequence& random, const std::size_t pieces)
    {
        std::u8string text {};
        for (std::size_t count { random.next(pieces + 1) }; count > 0; --count)
            text += edit_pieces[random.next(std::size(edit_pieces))];
        return text;
    }

    [[nodiscard]] std::u8string digits_of(std::size_t number)
    {
        std::u8string digits {};
        do
        {
            digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + number % 10));
            number /= 10;
        } while (number != 0);
        return digits;
    }

    // 줄 번호가 든 코드 모양 글이다. 줄은 `count`개이고 마지막에 줄 바꿈이 없다.
    [[nodiscard]] std::u8string numbered_lines(const std::size_t count)
    {
        std::u8string text {};
        for (std::size_t line { 0 }; line < count; ++line)
        {
            if (line > 0)
                text += u8'\n';
            text += u8"int value" + digits_of(line) + u8" = " + digits_of(line * 7) + u8";";
        }
        return text;
    }

    // 글의 `line`번째 줄(0부터) 처음 offset이다.
    [[nodiscard]] std::size_t line_begin(const std::u8string_view text, std::size_t line)
    {
        std::size_t at { 0 };
        while (line > 0)
        {
            at = text.find(u8'\n', at) + 1;
            --line;
        }
        return at;
    }

    [[nodiscard]] std::size_t line_end(const std::u8string_view text, const std::size_t line)
    {
        const std::size_t begin { line_begin(text, line) };
        const std::size_t end { text.find(u8'\n', begin) };
        return end == std::u8string_view::npos ? text.size() : end;
    }

    [[nodiscard]] std::u8string with_inserted(std::u8string text, const std::size_t at, const std::u8string_view piece)
    {
        text.insert(at, piece);
        return text;
    }
}

TEST_CASE("A highlight paints keywords, numbers and comments at document offsets", "[ui][syntax]")
{
    luil::syntax_highlighter highlighter { toy_config() };
    const std::u8string text { u8"if (x) 12 // note\nreturn \"a\";" };
    highlighter.update(text);
    const auto spans { highlighter.spans() };
    REQUIRE(spans->size() == 5u);
    REQUIRE(same_span((*spans)[0], { 0, 2, text_color_role::keyword, true }));
    REQUIRE(same_span((*spans)[1], { 7, 9, text_color_role::number }));
    REQUIRE(same_span((*spans)[2], { 10, 17, text_color_role::comment, false, luil::text_background_role::none, true }));
    REQUIRE(same_span((*spans)[3], { 18, 24, text_color_role::keyword, true }));
    REQUIRE(same_span((*spans)[4], { 25, 28, text_color_role::string }));
    REQUIRE(highlighter.stats().lines == 2u);
    REQUIRE(highlighter.stats().spans == 5u);
    REQUIRE(highlighter.stats().relexed == 2u);
    REQUIRE(highlighter.pending() == false);
}

TEST_CASE("An empty lexer paints nothing and a long line is not sent to the lexer", "[ui][syntax]")
{
    luil::syntax_highlighter plain { luil::syntax_highlighter_config {} };
    plain.update(u8"if 1\n2");
    REQUIRE(plain.spans()->empty());
    REQUIRE(plain.stats().lines == 2u);

    // 8바이트를 넘는 줄은 렉서에 가지 않고, 입력 상태가 다음 줄로 그대로 흐른다.
    std::vector<std::u8string> seen {};
    luil::syntax_highlighter_config config {};
    config.max_line_size = 8;
    config.lexer = [&seen](const std::u8string_view line, const int state, std::vector<text_span>& spans) {
        seen.emplace_back(line);
        return toy_lexer(line, state, spans);
    };
    luil::syntax_highlighter highlighter { config };
    const std::u8string text { u8"/* a\n1234567890123\n*/ 5" };
    highlighter.update(text);
    REQUIRE(seen == std::vector<std::u8string> { u8"/* a", u8"*/ 5" });
    REQUIRE(highlighter.stats().relexed == 2u);
    const auto spans { highlighter.spans() };
    REQUIRE(spans->size() == 3u);
    // 긴 줄은 구간이 없고 둘째 줄의 주석은 그대로 이어져 셋째 줄에서 닫힌다.
    REQUIRE(same_span((*spans)[0], { 0, 4, text_color_role::comment, false, luil::text_background_role::none, true }));
    REQUIRE(same_span((*spans)[1], { 19, 21, text_color_role::comment, false, luil::text_background_role::none, true }));
    REQUIRE(same_span((*spans)[2], { 22, 23, text_color_role::number }));
}

TEST_CASE("Lines end at LF, CRLF and a lone CR and spans never cover the line break", "[ui][syntax]")
{
    luil::syntax_highlighter highlighter { toy_config() };
    const std::u8string text { u8"11\r\n22\r33\n" };
    highlighter.update(text);
    // "11\r\n", "22\r", "33\n", 그리고 마지막 빈 줄이다.
    REQUIRE(highlighter.stats().lines == 4u);
    const auto spans { highlighter.spans() };
    REQUIRE(spans->size() == 3u);
    REQUIRE(same_span((*spans)[0], { 0, 2, text_color_role::number }));
    REQUIRE(same_span((*spans)[1], { 4, 6, text_color_role::number }));
    REQUIRE(same_span((*spans)[2], { 7, 9, text_color_role::number }));

    // 줄 바꿈이 없는 글은 한 줄이고, 빈 글도 빈 줄 하나다.
    luil::syntax_highlighter single { toy_config() };
    single.update(u8"");
    REQUIRE(single.stats().lines == 1u);
    single.update(u8"7");
    REQUIRE(single.stats().lines == 1u);
    REQUIRE(single.spans()->size() == 1u);
}

TEST_CASE("The spans from a lexer are tidied: ordered, trimmed, inside the line and on grapheme boundaries", "[ui][syntax]")
{
    luil::syntax_highlighter_config config {};
    config.lexer = [](const std::u8string_view line, const int state, std::vector<text_span>& spans) {
        if (line.size() == 15)
        {
            // 순서가 뒤집히고, 겹치고, 비고, 줄 밖에 걸친 구간이다.
            spans.push_back({ 6, 9, text_color_role::keyword });
            spans.push_back({ 0, 4, text_color_role::string });
            spans.push_back({ 3, 8, text_color_role::number });
            spans.push_back({ 5, 5, text_color_role::type });
            spans.push_back({ 20, 30, text_color_role::function });
            spans.push_back({ 12, 40, text_color_role::comment });
        }
        return state;
    };
    luil::syntax_highlighter highlighter { config };
    highlighter.update(u8"abcdefghijklmno");
    const auto spans { highlighter.spans() };
    REQUIRE(spans->size() == 4u);
    // 겹친 몫은 앞 구간이 갖는다.
    REQUIRE(same_span((*spans)[0], { 0, 4, text_color_role::string }));
    REQUIRE(same_span((*spans)[1], { 4, 8, text_color_role::number }));
    REQUIRE(same_span((*spans)[2], { 8, 9, text_color_role::keyword }));
    REQUIRE(same_span((*spans)[3], { 12, 15, text_color_role::comment }));
}

TEST_CASE("A span that splits a grapheme grows to its boundaries and the earlier span keeps the overlap", "[ui][syntax]")
{
    // 둘째 줄의 "a", "e"와 결합 부호(2바이트), "b"다. 줄 처음은 offset 2이다.
    const std::u8string text { u8"1\n" u8"aé" u8"b" };
    REQUIRE(text.size() == 7u);
    luil::syntax_highlighter_config config {};
    config.lexer = [](const std::u8string_view line, const int state, std::vector<text_span>& spans) {
        if (line.size() == 5)
        {
            // [0, 2)는 "a"와 "e"의 앞 반쪽이고 [2, 3)은 결합 부호의 앞 바이트다.
            spans.push_back({ 0, 2, text_color_role::string });
            spans.push_back({ 2, 3, text_color_role::number });
        }
        return state;
    };
    luil::syntax_highlighter highlighter { config };
    highlighter.update(text);
    const auto spans { highlighter.spans() };
    // 앞 구간이 글자소 끝(결합 부호 뒤)까지 늘고, 뒤 구간은 그 안에 묻혀 사라진다.
    REQUIRE(spans->size() == 1u);
    REQUIRE(same_span((*spans)[0], { 2, 6, text_color_role::string }));
    require_well_formed(text, *spans);
}

TEST_CASE("An incremental highlight equals a fresh one after random edits", "[ui][syntax]")
{
    for (const std::uint32_t seed : { 1U, 2U, 3U, 4U, 5U, 6U })
    {
        sequence random { seed };
        luil::text::text_edit_state state {};
        luil::text::text_edit_set_text(state, random_text(random, 24), true);
        luil::syntax_highlighter highlighter { toy_config() };
        highlighter.update(state.text);

        for (int step { 0 }; step < 400; ++step)
        {
            const std::size_t size { state.text.size() };
            switch (random.next(11))
            {
            case 0:
            case 1:
            case 2:
            case 3:
                luil::text::text_edit_place_caret(state, random.next(size + 1), false);
                luil::text::text_edit_insert(state, edit_pieces[random.next(std::size(edit_pieces))]);
                break;
            case 4:
                luil::text::text_edit_place_caret(state, random.next(size + 1), random.next(2) == 0);
                luil::text::text_edit_insert(state, edit_pieces[random.next(std::size(edit_pieces))]);
                break;
            case 5:
                luil::text::text_edit_place_caret(state, random.next(size + 1), false);
                luil::text::text_edit_backspace(state);
                break;
            case 6:
                luil::text::text_edit_place_caret(state, random.next(size + 1), false);
                luil::text::text_edit_delete_forward(state);
                break;
            case 7:
                luil::text::text_edit_undo(state);
                break;
            case 8:
                luil::text::text_edit_redo(state);
                break;
            case 9:
                luil::text::text_edit_replace_all(state, random_text(random, 12), 0);
                break;
            default:
                luil::text::text_edit_set_text(state, random_text(random, 16), random.next(2) == 0);
                break;
            }
            // 편집이 여럿 쌓인 뒤에 한 번 부르는 turn도 있다.
            if (random.next(4) == 0)
                continue;
            highlighter.update(state.text);
            const auto spans { highlighter.spans() };
            INFO("seed " << seed << " step " << step);
            REQUIRE(same_spans(*spans, painted_from_scratch(state.text)));
            require_well_formed(state.text, *spans);
        }
    }
}

TEST_CASE("A one-line edit relexes one line and an opening comment relexes until the state returns", "[ui][syntax]")
{
    // 0번 줄에서 시작하는 100줄이다. 10번 줄이 주석을 열고 30번 줄이 닫는다.
    std::u8string text { numbered_lines(100) };
    text.insert(line_begin(text, 10), u8"/* open ");
    text.insert(line_end(text, 30), u8" close */");
    luil::syntax_highlighter highlighter { toy_config() };
    highlighter.update(text);
    REQUIRE(highlighter.stats().lines == 100u);
    REQUIRE(highlighter.stats().relexed == 100u);
    const std::vector<text_span> original { *highlighter.spans() };

    SECTION("a character inside a line")
    {
        const std::u8string edited { with_inserted(text, line_begin(text, 60) + 5, u8"7") };
        highlighter.update(edited);
        REQUIRE(highlighter.stats().relexed == 1u);
        REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(edited)));
    }
    SECTION("a space at the end of the text")
    {
        const std::u8string edited { text + u8" " };
        highlighter.update(edited);
        REQUIRE(highlighter.stats().relexed == 1u);
        REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(edited)));
    }
    SECTION("closing the comment early stops at the line where the state matches again")
    {
        // 20번 줄에서 닫으면 20~30번 줄이 다시 칠해지고, 31번 줄은 입력 상태가 같아 멈춘다.
        const std::u8string closed { with_inserted(text, line_end(text, 20), u8" */") };
        highlighter.update(closed);
        REQUIRE(highlighter.stats().relexed == 11u);
        REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(closed)));
        // 지우면 같은 11줄을 거쳐 처음 그림으로 돌아온다.
        highlighter.update(text);
        REQUIRE(highlighter.stats().relexed == 11u);
        REQUIRE(same_spans(*highlighter.spans(), original));
    }
    SECTION("opening a comment before a closed one stops at the old comment")
    {
        // 5번 줄이 연 주석은 10번 줄의 `/*`를 품은 채 이어진다. 11번 줄부터는 입력 상태가 지난번과 같아 5~10번 줄에서 멈춘다.
        const std::u8string opened { with_inserted(text, line_begin(text, 5), u8"/*") };
        highlighter.update(opened);
        REQUIRE(highlighter.stats().relexed == 6u);
        REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(opened)));
    }
    SECTION("an unclosed comment relexes through the last line")
    {
        const std::u8string opened { with_inserted(numbered_lines(100), line_begin(numbered_lines(100), 10), u8"/*") };
        luil::syntax_highlighter fresh { toy_config() };
        fresh.update(numbered_lines(100));
        fresh.update(opened);
        REQUIRE(fresh.stats().relexed == 90u);
        REQUIRE(same_spans(*fresh.spans(), painted_from_scratch(opened)));
    }
}

TEST_CASE("Joining and splitting lines keeps the highlight in step", "[ui][syntax]")
{
    const std::u8string text { u8"12 /* a\nb */ 34\nif 5\r\n6\r7" };
    luil::syntax_highlighter highlighter { toy_config() };
    highlighter.update(text);
    REQUIRE(highlighter.stats().lines == 5u);

    // 줄 병합: 첫 줄의 줄 바꿈을 지운다.
    std::u8string merged { text };
    merged.erase(merged.find(u8'\n'), 1);
    highlighter.update(merged);
    REQUIRE(highlighter.stats().lines == 4u);
    REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(merged)));

    // 줄 분할: 주석 한가운데에서 줄을 나눈다.
    const std::u8string split { with_inserted(merged, merged.find(u8"ab"), u8"\n") };
    highlighter.update(split);
    REQUIRE(highlighter.stats().lines == 5u);
    REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(split)));

    // CR 뒤에 LF를 넣으면 두 줄이 한 CRLF로 이어지고, CRLF의 CR을 지우면 LF 하나가 된다.
    const std::u8string joined { with_inserted(text, text.find(u8"6\r7") + 2, u8"\n") };
    highlighter.update(joined);
    REQUIRE(highlighter.stats().lines == 5u);
    REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(joined)));
    std::u8string unpaired { joined };
    unpaired.erase(unpaired.find(u8"\r\n"), 1);
    highlighter.update(unpaired);
    REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(unpaired)));
    require_well_formed(unpaired, *highlighter.spans());
}

TEST_CASE("The highlight is steady when nothing changed and publishes a new vector only for new spans", "[ui][syntax]")
{
    const std::u8string text { u8"if 1\nreturn 2\n" };
    luil::syntax_highlighter highlighter { toy_config() };
    highlighter.update(text);
    const auto first { highlighter.spans() };
    REQUIRE(highlighter.spans() == first);

    // 같은 글을 다시 줘도 아무 일이 없다.
    highlighter.update(text);
    REQUIRE(highlighter.stats().relexed == 0u);
    REQUIRE(highlighter.spans() == first);

    // 구간을 바꾸지 않는 편집이다 (끝 줄에 공백).
    highlighter.update(text + u8" ");
    REQUIRE(highlighter.stats().relexed == 1u);
    const auto after_space { highlighter.spans() };
    REQUIRE(after_space == first);

    // 구간이 바뀌면 새 vector이고, 게시한 vector는 그대로다.
    const std::vector<text_span> before { *first };
    highlighter.update(u8"12 " + text);
    const auto after_edit { highlighter.spans() };
    REQUIRE(after_edit != first);
    REQUIRE(same_spans(*first, before));
    REQUIRE(same_spans(*after_edit, painted_from_scratch(u8"12 " + text)));

    // 한 turn에 편집이 여럿이어도 게시는 한 번이다.
    highlighter.update(u8"12 12 " + text);
    highlighter.update(u8"12 12 12 " + text);
    const auto after_two { highlighter.spans() };
    REQUIRE(after_two != after_edit);
    REQUIRE(highlighter.spans() == after_two);
}

TEST_CASE("A new document starts from nothing after reset", "[ui][syntax]")
{
    luil::syntax_highlighter highlighter { toy_config() };
    highlighter.update(u8"if 1\n/* open\n2");
    REQUIRE(highlighter.spans()->empty() == false);
    highlighter.reset();
    REQUIRE(highlighter.pending() == false);
    REQUIRE(highlighter.spans()->empty());
    REQUIRE(highlighter.stats().lines == 0u);
    // 앞 문서의 열린 주석이 새 문서로 새지 않는다.
    const std::u8string other { u8"3\n4" };
    highlighter.update(other);
    REQUIRE(highlighter.stats().relexed == 2u);
    REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(other)));
}

TEST_CASE("A zero budget paints only the edited lines and advance finishes the rest", "[ui][syntax][budget]")
{
    const std::u8string text { numbered_lines(200) };
    luil::syntax_highlighter highlighter { toy_config() };
    highlighter.update(text, no_time);
    // 처음 불러오기는 예산 안에서만 칠한다. 예산이 없으면 아무 줄도 칠하지 않는다.
    REQUIRE(highlighter.pending());
    REQUIRE(highlighter.stats().relexed == 0u);
    REQUIRE(highlighter.spans()->empty());

    // advance는 예산이 작아도 한 줄은 칠한다. 호출이 제자리걸음하지 않는다.
    std::size_t calls { 0 };
    while (highlighter.pending())
    {
        highlighter.advance(std::chrono::microseconds { 1 });
        REQUIRE(highlighter.stats().relexed >= 1u);
        const auto spans { highlighter.spans() };
        // 중간에도 구간은 현재 글에 맞는다.
        require_well_formed(text, *spans);
        ++calls;
        REQUIRE(calls <= 200u);
    }
    REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(text)));

    // 남은 일이 없으면 advance는 아무 일도 하지 않는다.
    highlighter.advance(std::chrono::microseconds { 1 });
    REQUIRE(highlighter.stats().relexed == 0u);
}

TEST_CASE("An edit under a zero budget paints its own lines and leaves the old colors on the tail", "[ui][syntax][budget]")
{
    const std::u8string text { numbered_lines(50) };
    luil::syntax_highlighter highlighter { toy_config() };
    highlighter.update(text);
    const std::vector<text_span> original { *highlighter.spans() };

    // 0번 줄이 주석을 열면 나머지 49줄의 상태가 바뀐다. 예산이 없으면 0번 줄만 칠하고 나머지는 대기다.
    const std::u8string opened { u8"/*" + text };
    highlighter.update(opened, no_time);
    REQUIRE(highlighter.stats().relexed == 1u);
    REQUIRE(highlighter.pending());
    const auto stale { highlighter.spans() };
    require_well_formed(opened, *stale);
    // 대기 중인 줄의 지난 구간이 글이 밀린 만큼 옮겨져 남아 있다. 줄마다 숫자 하나가 칠해져 있었다.
    REQUIRE(stale->size() == original.size());
    REQUIRE((*stale)[0].color == text_color_role::comment);
    REQUIRE((*stale)[0].end == line_end(opened, 0));
    REQUIRE((*stale)[1].begin == original[1].begin + 2u);

    highlighter.advance(luil::syntax_unlimited_budget);
    REQUIRE(highlighter.pending() == false);
    REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(opened)));
    // 줄 49개가 모두 주석이다.
    REQUIRE(highlighter.spans()->size() == 50u);
}

TEST_CASE("An edit while work is pending paints the pending lines before it", "[ui][syntax][budget]")
{
    const std::u8string text { numbered_lines(120) };
    luil::syntax_highlighter highlighter { toy_config() };
    highlighter.update(text, no_time);
    REQUIRE(highlighter.pending());

    // 80번 줄을 고친다. 그 줄의 상태는 앞 줄들에 기대므로 0번 줄부터 80번 줄까지를 칠한다.
    const std::u8string edited { with_inserted(text, line_begin(text, 80) + 3, u8"9") };
    highlighter.update(edited, no_time);
    REQUIRE(highlighter.stats().relexed == 81u);
    REQUIRE(highlighter.pending());
    require_well_formed(edited, *highlighter.spans());
    highlighter.advance(luil::syntax_unlimited_budget);
    REQUIRE(highlighter.pending() == false);
    REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(edited)));
}

TEST_CASE("Random edits under random budgets end at the fresh highlight", "[ui][syntax][budget]")
{
    const std::chrono::microseconds budgets[] { no_time, std::chrono::microseconds { 1 }, std::chrono::microseconds { 20 }, luil::syntax_unlimited_budget };
    for (const std::uint32_t seed : { 11U, 12U, 13U, 14U })
    {
        sequence random { seed };
        luil::text::text_edit_state state {};
        luil::text::text_edit_set_text(state, random_text(random, 40), true);
        luil::syntax_highlighter highlighter { toy_config() };
        highlighter.update(state.text, budgets[random.next(std::size(budgets))]);

        for (int step { 0 }; step < 300; ++step)
        {
            const std::size_t size { state.text.size() };
            switch (random.next(8))
            {
            case 0:
            case 1:
            case 2:
            case 3:
                luil::text::text_edit_place_caret(state, random.next(size + 1), false);
                luil::text::text_edit_insert(state, edit_pieces[random.next(std::size(edit_pieces))]);
                break;
            case 4:
                luil::text::text_edit_place_caret(state, random.next(size + 1), false);
                luil::text::text_edit_backspace(state);
                break;
            case 5:
                luil::text::text_edit_undo(state);
                break;
            case 6:
                luil::text::text_edit_replace_all(state, random_text(random, 20), 0);
                break;
            default:
                highlighter.advance(budgets[random.next(std::size(budgets))]);
                break;
            }
            highlighter.update(state.text, budgets[random.next(std::size(budgets))]);
            INFO("seed " << seed << " step " << step);
            // 대기 중인 줄이 있어도 구간은 늘 현재 글에 맞는다.
            require_well_formed(state.text, *highlighter.spans());
        }
        highlighter.advance(luil::syntax_unlimited_budget);
        REQUIRE(highlighter.pending() == false);
        REQUIRE(same_spans(*highlighter.spans(), painted_from_scratch(state.text)));
    }
}

TEST_CASE("Highlighting twenty megabytes", "[.][benchmark][syntax]")
{
    // 20MB, 50만 줄이다. Release 구성에서 이렇게 돌려 시간을 문서에 옮긴다.
    //   luil_core_tests "[benchmark][syntax]"
    // 시간은 단언하지 않고 출력만 한다.
    using time_source = std::chrono::steady_clock;
    const auto milliseconds = [](const time_source::time_point from) {
        return std::chrono::duration<double, std::milli>(time_source::now() - from).count();
    };
    std::u8string text {};
    text.reserve(std::size_t { 21 } * 1024 * 1024);
    for (std::size_t line { 0 }; line < 500000; ++line)
        text += u8"int value = 12345; // trailing comment here\n";
    std::printf("text: %zu bytes, 500000 lines\n", text.size());

    luil::syntax_highlighter highlighter { toy_config() };
    auto started { time_source::now() };
    highlighter.update(text);
    std::printf("first paint: %.1f ms (spans %zu)\n", milliseconds(started), highlighter.stats().spans);
    started = time_source::now();
    const auto published { highlighter.spans() };
    std::printf("first publish: %.1f ms\n", milliseconds(started));

    // 한 글자 편집 (수렴)이다. 한가운데에 숫자 하나를 더한다.
    std::u8string edited { text };
    const std::size_t middle { line_begin(text, 250000) + 4 };
    started = time_source::now();
    edited.insert(middle, u8"7");
    std::printf("string insert (the app's own copy): %.1f ms\n", milliseconds(started));
    started = time_source::now();
    const luil::text::text_change change { luil::text::text_change_between(text, edited) };
    std::printf("text_change_between over the whole text: %.1f ms (offset %zu, removed %zu, inserted %zu)\n", milliseconds(started), change.offset, change.removed, change.inserted);
    started = time_source::now();
    highlighter.update(edited);
    const double update_ms { milliseconds(started) };
    started = time_source::now();
    const auto republished { highlighter.spans() };
    std::printf("one character edit: update %.1f ms, publish %.1f ms, relexed %zu\n", update_ms, milliseconds(started), highlighter.stats().relexed);

    // 상태를 뒤집는 편집이다. 맨 위에 `/*`를 치면 문서 전체가 주석이 된다.
    edited.insert(0, u8"/*");
    started = time_source::now();
    highlighter.update(edited);
    const double flip_ms { milliseconds(started) };
    started = time_source::now();
    const auto flipped { highlighter.spans() };
    std::printf("state flip at the top: update %.1f ms, publish %.1f ms, relexed %zu\n", flip_ms, milliseconds(started), highlighter.stats().relexed);

    // 같은 편집을 예산 0으로 받는다. 편집이 닿은 줄만 칠하고 나머지는 대기다.
    edited.erase(0, 2);
    started = time_source::now();
    highlighter.update(edited, no_time);
    std::printf("state flip back under a zero budget: update %.1f ms, relexed %zu, pending %d\n", milliseconds(started), highlighter.stats().relexed, highlighter.pending() ? 1 : 0);
    started = time_source::now();
    while (highlighter.pending())
        highlighter.advance(std::chrono::milliseconds { 8 });
    std::printf("the rest in 8 ms slices: %.1f ms\n", milliseconds(started));
    REQUIRE(published != nullptr);
    REQUIRE(republished != nullptr);
    REQUIRE(flipped != nullptr);
}
