#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace luil::text {
    // 줄을 바꿀 수 있는 자리를 고르는 규칙이다 (UAX #14, docs/line-break-design.md).
    enum class line_break_policy
    {
        // 한글(H2·H3·JL·JV·JT)을 AL로 본다. 한국어는 어절에서 바꾸고, 한자·가나는 글자 사이에서 바꾼다 (기본).
        hangul_words,
        // UAX #14 기본이다. 한글 음절 사이에서도 바꾼다.
        unicode
    };

    // `offset` 앞에서 줄을 바꿀 수 있다. 코드포인트 경계다.
    struct line_break_opportunity
    {
        std::size_t offset { 0 };
        // LB3·LB4·LB5의 반드시 바꾸는 자리다. 글 끝은 늘 참이다.
        bool mandatory { false };

        [[nodiscard]] bool operator==(const line_break_opportunity&) const noexcept = default;
    };

    // 0은 넣지 않고 글 끝은 늘 넣는다 (LB3). 빈 글은 빈 목록이다. 글자소 경계와 맞추는 일은 부르는 쪽의 몫이다.
    //  - 규칙은 Unicode 16.0.0의 기본 규칙과 LB1의 기본 해석이다. 내장 표라 구성·플랫폼·OS 판과 상관없이 같은 자리다.
    //  - 깨진 UTF-8은 byte 하나를 U+FFFD(CB) 하나로 본다. 자리는 글의 범위를 벗어나지 않는다.
    [[nodiscard]] std::vector<line_break_opportunity> line_break_opportunities(
        std::u8string_view text, line_break_policy policy = line_break_policy::hangul_words);
    // 접은 줄 끝에 매달려 줄 폭에서 빠지는 공백인가. 일반 분류 Zs 중 줄 나눔 분류가 GL이 아닌 것이다.
    //  - U+0020·U+3000은 참, NBSP(U+00A0)·U+2007·U+202F는 붙이는 공백이라 거짓이다. 폭이 없는 ZW(U+200B)도 거짓이다.
    [[nodiscard]] bool line_break_hangs(char32_t codepoint) noexcept;
}
