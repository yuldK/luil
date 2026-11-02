#pragma once

#include "luil/text/line_break.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace luil {
    // 두 접기(`build_text_layout`·여러 줄 칸의 `wrap_paragraph`)가 함께 쓰는 자리 고르기다 (docs/line-break-design.md).
    //  - 폭을 모른다. 폭에 드는 가장 먼 글자소 경계(`fit`)는 부르는 쪽이 자기 측정으로 찾아 넘긴다. 그래서 문단마다
    //    다른 폭을 넘겨도 된다.
    //  - 모든 offset은 문단 처음 기준 byte다. `boundaries`는 그 문단의 글자소 경계(0과 끝 포함)이고, 후보는
    //    `boundaries`에 없으면 버린다. 표와 플랫폼 글자소의 판이 달라도 자르는 자리는 늘 글자소 경계다.
    struct line_fold
    {
        // 자른 자리다. 윗줄의 끝이자 아랫줄의 처음이다.
        std::size_t cut { 0 };
        // 보이는 끝이다. 자른 자리에서 매단 공백을 걷어낸 자리이고, 줄 폭은 여기까지다.
        //  - 글의 끝에서 자른 마지막 줄은 공백도 입력 칸의 caret 자리라 걷어내지 않는다 (`cut`과 같다).
        std::size_t visible { 0 };
        // 자른 자리 앞 글자소가 매달리는 공백이다. 넘친 폭이 그 공백 몫이라 다시 줄이지 않는다.
        bool hanging { false };
    };

    // 줄 처음 `start`와 `fit`(start보다 뒤의 글자소 경계)에서 자를 자리를 고른다.
    //  1. `fit`에서 매달리는 공백을 건넌 자리가 후보면 거기서 자른다 (공백은 윗줄에 매달린다).
    //  2. 아니면 `start` 뒤, `fit` 이하의 마지막 후보다.
    //  3. 그런 후보가 없으면 `fit`이다 (비상 분할).
    [[nodiscard]] line_fold choose_line_fold(
        std::u8string_view content, std::span<const std::size_t> boundaries, std::span<const text::line_break_opportunity> opportunities, std::size_t start, std::size_t fit) noexcept;
    // `cut`보다 앞, `start`보다 뒤의 마지막 후보다. 없으면 `cut` 앞 글자소 경계다. native 길이 줄 경계 문맥으로 폭이
    // 늘어 다시 줄일 때 물러서는 자리다.
    [[nodiscard]] line_fold previous_line_fold(
        std::u8string_view content, std::span<const std::size_t> boundaries, std::span<const text::line_break_opportunity> opportunities, std::size_t start, std::size_t cut) noexcept;
}
