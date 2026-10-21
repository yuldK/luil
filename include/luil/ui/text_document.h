#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>

namespace luil {
    enum class text_color_role { primary, secondary, accent, warning, error };
    enum class text_background_role { none, added, removed, changed };
    enum class line_background_role { none, added, removed, changed };
    enum class gutter_kind { line_number, custom };

    struct text_span
    {
        std::size_t begin { 0 };
        std::size_t end { 0 };
        text_color_role color { text_color_role::primary };
        bool bold { false };
        text_background_role background { text_background_role::none };
    };

    struct line_decoration
    {
        // 줄 바꿈으로 나뉜 논리 줄이다. 넘겨 접은 화면 줄과 구별한다.
        std::size_t line { 0 };
        line_background_role background { line_background_role::none };
        char32_t marker { 0 };
    };

    struct gutter_column
    {
        gutter_kind kind { gutter_kind::line_number };
        // 0부터 세는 논리 줄을 받는다. 줄 번호 표시는 1부터다.
        std::function<std::u8string(std::size_t line)> text {};
        float width { 0.0f };
    };

    struct text_highlight
    {
        std::size_t begin { 0 };
        std::size_t end { 0 };
        bool current { false };
    };

    // begin 순서이고 겹치지 않는 구간에서 표시 범위만 참조한다. 복사하지 않는다.
    [[nodiscard]] std::span<const text_span> text_spans_in_range(std::span<const text_span> spans, std::size_t begin, std::size_t end) noexcept;
    [[nodiscard]] std::span<const text_highlight> text_highlights_in_range(std::span<const text_highlight> spans, std::size_t begin, std::size_t end) noexcept;
} // namespace luil
