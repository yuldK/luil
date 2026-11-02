#pragma once

// 데모의 작은 코드 렉서와 "코드 칸"의 상태 줄이다.
//  - 구문 강조 도우미(luil/ui/syntax_highlighter.h)에 줄 하나를 칠하는 렉서를 주는 것은 앱의 몫이라 라이브러리가
//    아니라 예제에 둔다. 데스크톱 demo와 모바일 demo가 이 파일 하나를 함께 쓴다 (그림 자산처럼).
//  - C 모양 글만 안다. 키워드, 자료형, 함수 호출, 문자열, 숫자, `//`, `/* */`다.
//  - 고대비에서는 색이 창 글자색 하나로 접히므로 의미를 색에만 싣지 않는다. 키워드는 굵게, 주석은 기울여 낸다.

#include "luil/ui/syntax_highlighter.h"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace demo {
    // 렉서 상태다. 줄 밖의 문맥(여러 줄 주석)은 이 정수 하나에 담긴다.
    inline constexpr int code_state_normal { 0 };
    inline constexpr int code_state_block_comment { 1 };

    inline constexpr std::array<std::u8string_view, 14> code_keywords {
        u8"if", u8"else", u8"for", u8"while", u8"do", u8"switch", u8"case", u8"break", u8"continue", u8"return", u8"const", u8"static", u8"struct", u8"namespace",
    };

    inline constexpr std::array<std::u8string_view, 9> code_types {
        u8"int", u8"char", u8"bool", u8"float", u8"double", u8"void", u8"auto", u8"size_t", u8"string",
    };

    namespace code_lexer_detail {
        [[nodiscard]] inline bool is_digit(const char8_t byte) noexcept
        {
            return byte >= u8'0' && byte <= u8'9';
        }

        // 영문자, 밑줄, ASCII 밖 바이트(한글 식별자)다.
        [[nodiscard]] inline bool is_word_start(const char8_t byte) noexcept
        {
            return (byte >= u8'a' && byte <= u8'z')
                || (byte >= u8'A' && byte <= u8'Z')
                || byte == u8'_'
                || byte >= 0x80;
        }

        [[nodiscard]] inline bool is_word_part(const char8_t byte) noexcept
        {
            return is_word_start(byte) || is_digit(byte);
        }

        template<std::size_t count>
        [[nodiscard]] inline bool contains(const std::array<std::u8string_view, count>& words, const std::u8string_view word) noexcept
        {
            for (const std::u8string_view candidate : words)
                if (candidate == word)
                    return true;
            return false;
        }

        inline void add_span(std::vector<luil::text_span>& spans, const std::size_t begin, const std::size_t end, const luil::text_color_role color, const bool bold = false, const bool italic = false)
        {
            spans.push_back({ begin, end, color, bold, luil::text_background_role::none, italic });
        }
    }

    // 줄 하나를 칠한다 (`luil::syntax_line_lexer`). (줄 글, 입력 상태)의 순수 함수다.
    inline int lex_code_line(const std::u8string_view line, int state, std::vector<luil::text_span>& spans)
    {
        using luil::text_color_role;
        std::size_t at { 0 };
        while (at < line.size())
        {
            if (state == code_state_block_comment)
            {
                const std::size_t close { line.find(u8"*/", at) };
                const std::size_t end { close == std::u8string_view::npos ? line.size() : close + 2 };
                code_lexer_detail::add_span(spans, at, end, text_color_role::comment, false, true);
                at = end;
                if (close != std::u8string_view::npos)
                    state = code_state_normal;
                continue;
            }

            const char8_t byte { line[at] };
            if (line.substr(at, 2) == u8"//")
            {
                code_lexer_detail::add_span(spans, at, line.size(), text_color_role::comment, false, true);
                break;
            }
            if (line.substr(at, 2) == u8"/*")
            {
                const std::size_t close { line.find(u8"*/", at + 2) };
                const std::size_t end { close == std::u8string_view::npos ? line.size() : close + 2 };
                code_lexer_detail::add_span(spans, at, end, text_color_role::comment, false, true);
                at = end;
                if (close == std::u8string_view::npos)
                    state = code_state_block_comment;
                continue;
            }
            if (byte == u8'"' || byte == u8'\'')
            {
                // 닫는 따옴표까지다. 역슬래시는 다음 글자를 건너뛰고, 닫히지 않으면 줄 끝까지다.
                std::size_t end { at + 1 };
                while (end < line.size() && line[end] != byte)
                    end += line[end] == u8'\\' ? 2 : 1;
                end = end < line.size() ? end + 1 : line.size();
                code_lexer_detail::add_span(spans, at, end, text_color_role::string);
                at = end;
                continue;
            }
            if (code_lexer_detail::is_digit(byte))
            {
                std::size_t end { at };
                while (end < line.size() && (code_lexer_detail::is_word_part(line[end]) || line[end] == u8'.'))
                    ++end;
                code_lexer_detail::add_span(spans, at, end, text_color_role::number);
                at = end;
                continue;
            }
            if (code_lexer_detail::is_word_start(byte))
            {
                std::size_t end { at };
                while (end < line.size() && code_lexer_detail::is_word_part(line[end]))
                    ++end;
                const std::u8string_view word { line.substr(at, end - at) };
                if (code_lexer_detail::contains(code_keywords, word))
                    code_lexer_detail::add_span(spans, at, end, text_color_role::keyword, true);
                else if (code_lexer_detail::contains(code_types, word))
                    code_lexer_detail::add_span(spans, at, end, text_color_role::type);
                else if (end < line.size() && line[end] == u8'(')
                    code_lexer_detail::add_span(spans, at, end, text_color_role::function);
                at = end;
                continue;
            }
            ++at;
        }
        return state;
    }

    [[nodiscard]] inline luil::syntax_highlighter_config code_highlighter_config()
    {
        luil::syntax_highlighter_config config {};
        config.lexer = lex_code_line;
        return config;
    }

    // 코드 칸에 처음 들어 있는 글이다. 여섯 색과 굵게·기울임이 모두 나온다.
    inline constexpr std::u8string_view code_sample {
        u8"// 코드 칸: 키워드는 굵게, 주석은 기울여 칠한다.\n"
        u8"int sum(const int* values, int count)\n"
        u8"{\n"
        u8"    /* 여러 줄 주석은\n"
        u8"       상태로 이어진다. */\n"
        u8"    int total = 0;\n"
        u8"    for (int index = 0; index < count; ++index)\n"
        u8"        total += values[index] * 2;\n"
        u8"    return total;\n"
        u8"}\n"
        u8"\n"
        u8"const char* name = \"luil\"; // 문자열\n"
        u8"int answer = 0x2A + 7;\n"
    };

    // 구간의 종류별 수다. "코드 칸"의 상태 줄이 그린다.
    struct code_span_counts
    {
        std::size_t total { 0 };
        std::size_t keywords { 0 };
        std::size_t strings { 0 };
        std::size_t comments { 0 };
    };

    [[nodiscard]] inline code_span_counts count_code_spans(const std::span<const luil::text_span> spans) noexcept
    {
        code_span_counts counts {};
        counts.total = spans.size();
        for (const luil::text_span& span : spans)
        {
            if (span.color == luil::text_color_role::keyword)
                ++counts.keywords;
            else if (span.color == luil::text_color_role::string)
                ++counts.strings;
            else if (span.color == luil::text_color_role::comment)
                ++counts.comments;
        }
        return counts;
    }

    // 상태 줄의 글이다. UIA로 글을 넣고 이 글을 되읽어 증분이 맞게 도는지 본다.
    //  - `relexed`는 마지막 편집이 렉서에 보낸 줄 수다. 한 줄 편집이면 1이다.
    [[nodiscard]] inline std::u8string code_status_text(const code_span_counts& counts, const std::size_t relexed)
    {
        const auto digits = [](std::size_t number) {
            std::u8string text {};
            do
            {
                text.insert(text.begin(), static_cast<char8_t>(u8'0' + number % 10));
                number /= 10;
            } while (number != 0);
            return text;
        };
        return u8"구간 " + digits(counts.total) + u8" · 키워드 " + digits(counts.keywords) + u8" · 문자열 " + digits(counts.strings) + u8" · 주석 " + digits(counts.comments)
            + u8" · 마지막에 다시 칠한 줄 " + digits(relexed);
    }
}
