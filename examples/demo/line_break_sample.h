#pragma once

// 데모의 "줄 나눔" 칸이다 (docs/line-break-design.md의 확인).
//  - 같은 글을 여러 줄 label과 여러 줄 칸에 같은 폭으로 놓고, 정책(`text::line_break_policy`)을 스위치로 바꾼다.
//    데스크톱 demo와 모바일 demo, page test가 이 파일 하나를 함께 쓴다 (code_lexer.h처럼).
//  - 상태 줄은 build에서 그 폭으로 잰 label의 줄 수와 배치한 칸의 줄 수, 둘의 첫 줄 끝 글자를 그린다. UIA Name과
//    화면으로 되읽어 정책이 접는 자리를 바꿨는지, label과 칸이 같은 규칙으로 접는지 본다.
//  - 테마 페이지의 스타일 견본도 같은 글과 폭을 쓴다. 그 label은 정책 칸을 비워 앱 스타일을 따른다 (`style_wrap_label_config`).

#include "luil/text/grapheme.h"
#include "luil/text/line_break.h"
#include "luil/text/utf8_text.h"
#include "luil/theme/ui_style.h"
#include "luil/ui/label_element.h"
#include "luil/ui/text_area_element.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demo {
    // 한국어 어절, 한자·가나와 닫는 부호, 하이픈, 주소가 한 문단에 섞인다. 칸이 좁아 여러 줄로 접힌다.
    inline constexpr std::u8string_view line_break_sample {
        u8"한국어 문장은 어절 단위로 바꾸고 낱말 한가운데서 가르지 않는다. 日本語の「東京」と「大阪」は文字の間で改行する。 alpha-beta https://example.com/luil/docs"
    };
    // label과 칸의 폭이다 (논리 픽셀). 휴대폰 세로 폭에도 든다. 상태 줄이 이 폭으로 센다.
    inline constexpr float line_break_cell_width { 220.0f };
    inline constexpr float line_break_area_height { 112.0f };
    // label과 칸의 글자 크기다. 둘이 다르면 같은 폭에서도 접는 자리가 달라 두 정책을 견줄 수 없다.
    inline constexpr float line_break_font_size { luil::text_area_font_size };

    // 정책을 바꾸자는 메시지다. 스위치가 다음 정책을 싣는다.
    struct line_break_policy_intent
    {
        luil::text::line_break_policy policy { luil::text::line_break_policy::hangul_words };
    };

    [[nodiscard]] inline std::u8string_view line_break_policy_name(const luil::text::line_break_policy policy) noexcept
    {
        return policy == luil::text::line_break_policy::unicode ? std::u8string_view { u8"unicode" } : std::u8string_view { u8"hangul_words" };
    }

    [[nodiscard]] inline luil::label_config line_break_label_config(std::u8string text, const luil::text::line_break_policy policy)
    {
        luil::label_config config {};
        config.text = std::move(text);
        config.font_size = line_break_font_size;
        config.color = luil::label_color_role::primary;
        config.wrap = true;
        config.line_break = policy;
        return config;
    }

    [[nodiscard]] inline luil::text_area_config line_break_area_config(const luil::text::line_break_policy policy, std::shared_ptr<luil::text_area_wrap_cache> cache)
    {
        luil::text_area_config config {};
        config.placeholder = u8"줄 나눔 칸";
        config.font_size = line_break_font_size;
        config.line_break = policy;
        config.wrap_cache = std::move(cache);
        return config;
    }

    // 줄 `[begin, end)`의 끝 글자소 셋이다. 접은 줄 끝에 매단 공백은 빼고, 셋의 앞머리에 걸린 공백도 뺀다.
    [[nodiscard]] inline std::u8string line_break_line_end(const std::u8string_view text, const std::size_t begin, std::size_t end)
    {
        if (begin >= end || end > text.size())
            return {};
        const std::u8string_view line { text.substr(begin, end - begin) };
        const std::vector<std::size_t> boundaries { luil::text::grapheme_boundaries(line) };
        std::size_t last { boundaries.size() - 1 };
        while (last > 0 && luil::text::line_break_hangs(luil::text::utf8_decode(line, boundaries[last - 1]).codepoint))
            --last;
        std::size_t first { last >= 3 ? last - 3 : 0 };
        while (first < last && luil::text::line_break_hangs(luil::text::utf8_decode(line, boundaries[first]).codepoint))
            ++first;
        return std::u8string { line.substr(boundaries[first], boundaries[last] - boundaries[first]) };
    }

    [[nodiscard]] inline std::u8string line_break_status_text(
        const luil::text::line_break_policy policy, const std::size_t label_lines, const std::size_t area_lines, const std::u8string_view label_end, const std::u8string_view area_end)
    {
        const auto number = [](const std::size_t value) {
            const std::string digits { std::to_string(value) };
            return std::u8string { digits.begin(), digits.end() };
        };
        return u8"정책 " + std::u8string { line_break_policy_name(policy) } + u8" · 라벨 " + number(label_lines) + u8"줄 · 칸 " + number(area_lines) + u8"줄 · 첫 줄 끝 '"
            + std::u8string { label_end } + u8"' / '" + std::u8string { area_end } + u8"'";
    }

    // label을 그 폭으로 재고 칸을 배치해 상태 줄을 짓는다. 칸은 배치한 채로 tree에 넣어도 된다 (부모가 다시 배치한다).
    //  - `measurer`가 비어 있으면 그리기와 같은 UI 글꼴로 잰다. page test는 고정 폭 측정기를 넘긴다.
    [[nodiscard]] inline std::u8string measure_line_break_status(const luil::text::line_break_policy policy, const luil::label_config& label, luil::text_area_element& area,
        const float scale, const luil::text_measurer* const measurer = nullptr)
    {
        const luil::label_wrap wrap { luil::label_element::measure_wrap(label, luil::measure_context { .scale = scale, .measurer = measurer }, line_break_cell_width) };
        area.arrange({ .slot = { 0.0f, 0.0f, line_break_cell_width * scale, line_break_area_height * scale }, .scale = scale, .measurer = measurer });
        const std::u8string label_end { wrap.lines.empty() ? std::u8string {} : line_break_line_end(label.text, wrap.lines.front().begin, wrap.lines.front().end) };
        // 칸의 줄은 화면에 보이는 글(IME 조합 중이면 조합 글까지)의 자리다. 그 글로 읽는다.
        const std::optional<luil::text_input_snapshot> snapshot { area.text_input() };
        const std::u8string_view area_text { snapshot.has_value() == false ? std::u8string_view {} : snapshot->composing ? snapshot->composition_text : snapshot->text };
        const std::u8string area_end { area.lines().empty() ? std::u8string {} : line_break_line_end(area_text, area.lines().front().begin, area.lines().front().end) };
        return line_break_status_text(policy, wrap.lines.size(), area.lines().size(), label_end, area_end);
    }

    // 테마 페이지의 "스타일 파일" 묶음이 쓰는 label이다 (docs/wrap-policy-key-design.md의 확인). 같은 견본 글과 글자 크기이고
    // 정책 칸을 비워 앱 스타일의 `text.wrapPolicy`를 따른다.
    [[nodiscard]] inline luil::label_config style_wrap_label_config()
    {
        luil::label_config config {};
        config.text = std::u8string { line_break_sample };
        config.font_size = line_break_font_size;
        config.color = luil::label_color_role::primary;
        config.wrap = true;
        return config;
    }

    // 그 label을 지금 스타일(`style`, 비면 내장 스타일)을 실은 측정 문맥으로 `line_break_cell_width`에서 잰 결과다.
    //  - 화면의 label은 주 tree의 배치 문맥이 같은 스타일을 실어 같은 정책으로 접는다.
    [[nodiscard]] inline luil::label_wrap measure_style_wrap(
        const luil::label_config& label, const luil::ui_style* const style, const float scale, const luil::text_measurer* const measurer = nullptr)
    {
        return luil::label_element::measure_wrap(label, luil::measure_context { .scale = scale, .measurer = measurer, .style = style }, line_break_cell_width);
    }

    // 상태 줄이다. "스타일 정책 unicode · 라벨 N줄 · 첫 줄 끝 'X'"이고 정책은 측정 문맥이 푼 값이다.
    [[nodiscard]] inline std::u8string measure_style_wrap_status(
        const luil::label_config& label, const luil::ui_style* const style, const float scale, const luil::text_measurer* const measurer = nullptr)
    {
        const luil::label_wrap wrap { measure_style_wrap(label, style, scale, measurer) };
        const luil::measure_context context { .scale = scale, .measurer = measurer, .style = style };
        const std::u8string end { wrap.lines.empty() ? std::u8string {} : line_break_line_end(label.text, wrap.lines.front().begin, wrap.lines.front().end) };
        const std::string lines { std::to_string(wrap.lines.size()) };
        return u8"스타일 정책 " + std::u8string { line_break_policy_name(context.line_break_for(label.line_break)) } + u8" · 라벨 " + std::u8string { lines.begin(), lines.end() }
            + u8"줄 · 첫 줄 끝 '" + end + u8"'";
    }
}
