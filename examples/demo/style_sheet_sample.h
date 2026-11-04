#pragma once

// 데모의 "스타일 파일" 묶음이다 (docs/style-sheet-design.md의 확인).
//  - 앱이 실행 중에 스타일 JSON 글을 읽어(`parse_ui_style`) 받으면 frame(`ui_frame::style`)과 주 tree 배치에 같은
//    포인터로 주고, 막히면 쥔 스타일을 그대로 둔다. 데스크톱 demo와 모바일 demo, page test가 이 파일 하나를 함께 쓴다
//    (line_break_sample.h처럼).
//  - 상태 줄(`theme-style-status`)이 결과와 지금 스타일의 어두운 창 바탕을 말한다. UIA Name과 화면으로 되읽는다.
//  - 견본의 바탕은 데모 빌드가 실은 내장 파일 글(LUIL_STYLE)이다. 글을 고쳐 짓고 파일을 읽지 않는다. 파일을 읽는 길은 파일 감시 묶음이다
//    (hot_reload_sample.h).

#include "luil/theme/ui_style.h"
#include "luil/theme/ui_style_json.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace demo {
    // 스타일 파일 묶음의 단추가 싣는 조작이다.
    enum class style_sheet_action
    {
        // 칸의 글을 읽는다 (데스크톱).
        apply,
        // 내장 스타일로 돌아가고 칸을 내장 파일 글로 되돌린다.
        reset,
        // 어두운·밝은 창 바탕과 컨트롤 모서리를 바꾼 견본이다. 받는다.
        sample_background,
        // 어두운 테마에 낯선 키(`windowBackgrund`)를 더한 견본이다. 막힌다.
        sample_unknown_key,
        // 닫는 괄호가 없는 견본이다. 문법 오류로 막힌다.
        sample_broken,
        // 여러 줄 정책(`text.wrapPolicy`)을 `unicode`로 바꾼 견본이다. 받는다. 칸이 빈 여러 줄 label이 음절 사이에서도 접힌다
        // (docs/wrap-policy-key-design.md의 확인).
        sample_wrap,
    };

    struct style_sheet_intent
    {
        style_sheet_action action { style_sheet_action::apply };
    };

    // 받은 견본이 바꾸는 값이다. 상태 줄이 어두운 창 바탕을 말한다.
    inline constexpr std::u8string_view style_sample_dark_background { u8"#1a1b26" };
    inline constexpr std::u8string_view style_sample_light_background { u8"#e8eaf2" };
    inline constexpr std::u8string_view style_sample_corner_radius { u8"8" };

    namespace style_sheet_detail {
        // `"key"` 다음이 쌍점인 자리, 곧 키로 쓰인 자리를 찾는다. 같은 글이 값으로 쓰인 자리(`"barBackground": "windowBackground"`)는
        // 건너뛴다. 키 뒤의 값이 시작하는 자리를 돌려준다.
        [[nodiscard]] inline std::size_t find_value(const std::u8string& text, const std::u8string_view key, std::size_t from)
        {
            const std::u8string quoted { u8"\"" + std::u8string { key } + u8"\"" };
            while ((from = text.find(quoted, from)) != std::u8string::npos)
            {
                std::size_t at { from + quoted.size() };
                while (at < text.size() && (text[at] == u8' ' || text[at] == u8'\t' || text[at] == u8'\r' || text[at] == u8'\n'))
                    ++at;
                if (at < text.size() && text[at] == u8':')
                {
                    ++at;
                    while (at < text.size() && (text[at] == u8' ' || text[at] == u8'\t' || text[at] == u8'\r' || text[at] == u8'\n'))
                        ++at;
                    return at;
                }
                from = at;
            }
            return std::u8string::npos;
        }

        // `nth`째(0부터) 키의 값 하나(글이나 숫자)를 `value`로 바꾼다. 키가 없으면 그대로 둔다.
        inline void replace_value(std::u8string& text, const std::u8string_view key, const std::size_t nth, const std::u8string_view value)
        {
            std::size_t at { 0 };
            for (std::size_t index { 0 }; index <= nth; ++index)
            {
                at = find_value(text, key, at);
                if (at == std::u8string::npos)
                    return;
                if (index < nth)
                    ++at;
            }
            std::size_t end { at };
            if (end < text.size() && text[end] == u8'"')
            {
                const std::size_t close { text.find(u8'"', end + 1) };
                end = close == std::u8string::npos ? text.size() : close + 1;
            }
            else
            {
                const std::u8string_view stops { u8",} \t\r\n" };
                while (end < text.size() && stops.find(text[end]) == std::u8string_view::npos)
                    ++end;
            }
            text.replace(at, end - at, value);
        }
    }

    // 견본 글이다. `apply`·`reset`은 바탕 글 그대로다.
    [[nodiscard]] inline std::u8string style_sheet_sample(const std::u8string_view source, const style_sheet_action action)
    {
        std::u8string text { source };
        switch (action)
        {
        case style_sheet_action::sample_background:
            // dark가 light보다 먼저 적힌다 (assets/style.json).
            style_sheet_detail::replace_value(text, u8"windowBackground", 0, u8"\"" + std::u8string { style_sample_dark_background } + u8"\"");
            style_sheet_detail::replace_value(text, u8"windowBackground", 1, u8"\"" + std::u8string { style_sample_light_background } + u8"\"");
            style_sheet_detail::replace_value(text, u8"controlCornerRadius", 0, style_sample_corner_radius);
            break;
        case style_sheet_action::sample_unknown_key:
            if (const std::size_t dark { style_sheet_detail::find_value(text, u8"dark", 0) }; dark != std::u8string::npos && text[dark] == u8'{')
                text.insert(dark + 1, u8"\n        \"windowBackgrund\": \"" + std::u8string { style_sample_dark_background } + u8"\",");
            break;
        case style_sheet_action::sample_wrap:
            if (style_sheet_detail::find_value(text, u8"wrapPolicy", 0) != std::u8string::npos)
                style_sheet_detail::replace_value(text, u8"wrapPolicy", 0, u8"\"unicode\"");
            else if (const std::size_t section { style_sheet_detail::find_value(text, u8"text", 0) }; section != std::u8string::npos && text[section] == u8'{')
            {
                // 키를 적지 않은 LUIL_STYLE 파일이면 `text` 절의 머리에 넣는다.
                text.insert(section + 1, u8"\n        \"wrapPolicy\": \"unicode\",");
            }
            break;
        case style_sheet_action::sample_broken:
            if (const std::size_t close { text.find_last_of(u8'}') }; close != std::u8string::npos)
                text.erase(close);
            break;
        default:
            break;
        }
        return text;
    }

    // `#rrggbb`, 불투명이 아니면 `#rrggbbaa`다. 스타일 파일과 같은 꼴이라 상태 줄과 칸의 글을 견줘 읽는다.
    [[nodiscard]] inline std::u8string style_color_text(const luil::ui_color color)
    {
        constexpr std::u8string_view digits { u8"0123456789abcdef" };
        std::u8string text { u8"#" };
        const auto push = [&text, digits](const unsigned value) {
            text += digits[(value >> 4) & 0xFu];
            text += digits[value & 0xFu];
        };
        push((color >> 16) & 0xFFu);
        push((color >> 8) & 0xFFu);
        push(color & 0xFFu);
        if ((color >> 24) != 0xFFu)
            push(color >> 24);
        return text;
    }

    // 스타일 파일 묶음의 상태다. 쥔 스타일은 비어 있으면 내장 스타일이다.
    class style_sheet_state
    {
    public:
        explicit style_sheet_state(std::u8string source = {})
            : source_ { std::move(source) }
        {
            reset();
        }

        // 글을 읽는다. 받으면 새 스타일을 쥐고, 막히면 쥔 스타일을 그대로 둔다 — 화면은 이전 스타일로 남는다.
        void apply(const std::u8string_view text)
        {
            const luil::style_parse_result parsed { luil::parse_ui_style(text) };
            if (parsed.ok())
            {
                style_ = std::make_shared<const luil::ui_style>(*parsed.style);
                status_ = u8"적용됨 · 오류 0 · dark.windowBackground " + style_color_text(style_->dark.window_background);
                return;
            }
            status_ = u8"거부 · 오류 " + count_text(parsed.errors.size()) + u8" · " + luil::describe(parsed.errors.front()) + u8" · 이전 스타일 유지";
        }

        // 다시 읽은 스타일을 받는다 (`hot_reload_update`). 해석은 도우미가 마쳤다. 값이 없는 결과는 여기로 오지 않아 상태 줄도 그대로다.
        void accept(std::shared_ptr<const luil::ui_style> style)
        {
            style_ = std::move(style);
            status_ = u8"적용됨 · 오류 0 · dark.windowBackground " + style_color_text(style_->dark.window_background);
        }

        void reset()
        {
            style_.reset();
            status_ = u8"내장값 · dark.windowBackground " + style_color_text(luil::default_ui_style().dark.window_background);
        }

        // 셸이 frame과 주 tree 배치에 같은 포인터로 싣는다.
        [[nodiscard]] const std::shared_ptr<const luil::ui_style>& style() const noexcept
        {
            return style_;
        }

        [[nodiscard]] const std::u8string& status() const noexcept
        {
            return status_;
        }

        // 내장 파일 글이다. 견본의 바탕이다.
        [[nodiscard]] const std::u8string& source() const noexcept
        {
            return source_;
        }

    private:
        [[nodiscard]] static std::u8string count_text(const std::size_t count)
        {
            std::array<char, 24> buffer {};
            const std::to_chars_result result { std::to_chars(buffer.data(), buffer.data() + buffer.size(), count) };
            return { reinterpret_cast<const char8_t*>(buffer.data()), static_cast<std::size_t>(result.ptr - buffer.data()) };
        }

        std::u8string source_ {};
        std::shared_ptr<const luil::ui_style> style_ {};
        std::u8string status_ {};
    };
}
