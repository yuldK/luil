#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

    // 글 안의 하이퍼링크 구간이다 (UTF-8 byte offset).
    struct text_link
    {
        std::size_t begin { 0 };
        std::size_t end { 0 };
        // 누르면 앱에 넘기는 주소다. 보이는 글과 다를 수 있다 — 그래서 칸이 머무름 tooltip으로 이것을 보인다.
        std::u8string destination {};

        [[nodiscard]] bool operator==(const text_link&) const = default;
    };

    // begin 순서이고 겹치지 않는 비어 있지 않은 구간에서 표시 범위만 참조한다. 복사하지 않는다.
    // 문서에 적용할 byte 범위는 유효한 UTF-8 글자소 경계로 앱이 제공한다.
    // 빈 범위(`begin >= end`)는 아무것도 걸지 않는다. 빈 줄이나 줄 끝의 줄 바꿈 칸을 칠하려면 그 줄 바꿈 바이트까지
    // 넓혀 묻는다 (`text_area_element`는 줄 바꿈으로 끝나는 화면 줄을 [처음, 끝 + 1)로 묻는다).
    [[nodiscard]] std::span<const text_span> text_spans_in_range(std::span<const text_span> spans, std::size_t begin, std::size_t end) noexcept;
    [[nodiscard]] std::span<const text_highlight> text_highlights_in_range(std::span<const text_highlight> spans, std::size_t begin, std::size_t end) noexcept;
    [[nodiscard]] std::span<const text_link> text_links_in_range(std::span<const text_link> links, std::size_t begin, std::size_t end) noexcept;
    // offset의 글자를 품은 링크다. 링크 바로 뒤(끝 offset)는 품지 않는다. 없으면 nullptr다.
    [[nodiscard]] const text_link* text_link_at(std::span<const text_link> links, std::size_t offset) noexcept;

    // 글에서 주소를 찾아 링크 구간으로 낸다 (begin 순서, 겹치지 않음).
    //  - `http://`·`https://`·`mailto:`(대소문자 무시)와 낱말 처음의 `www.`, 그리고 `이름@도메인.최상위` 꼴의 메일 주소다.
    //    `www.`는 `https://`를, 메일 주소는 `mailto:`를 붙인 주소가 된다.
    //  - 주소는 공백·제어 문자·`<>"`·ASCII 밖 글자에서 끝난다. 한국어 조사가 주소에 붙어 오는 일이 흔해서다
    //    ("https://example.com에서"). 그래서 ASCII 밖 글자가 든 주소(IRI)는 그 앞에서 끊긴다 — 퍼센트 인코딩한 꼴은 끝까지 간다.
    //  - 끝의 문장 부호(`.,;:!?'*`)는 빼고, 닫는 괄호는 주소 안에 짝이 없을 때만 뺀다 ("(https://a.b/x_(y))").
    //  - ASCII 영숫자 바로 뒤에서는 시작하지 않는다 ("xhttp://"는 링크가 아니다).
    [[nodiscard]] std::vector<text_link> detect_text_links(std::u8string_view text);
}
