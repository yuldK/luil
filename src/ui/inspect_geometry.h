#pragma once

#include "luil/ui/ui_element.h"

#include <cstddef>
#include <optional>

namespace luil {
    // 요소의 안쪽 여백을 재는 바탕이 되는 자리다. 배치된 보이는 자식들의 bounds 합집합을 요소 자신의 bounds로 자른 것이다.
    //  - 자식이 없거나 모두 숨었거나 배치되지 않았으면 비어 있다. 넓이 없는 자식(일부러 0인 자리)은 합집합을 끌어당기지 않는다.
    //  - 정보 상자의 `inset`과 overlay의 안쪽 여백 띠가 같은 답을 쓰도록 한 곳에 둔다. 공개 형식이 아니다.
    [[nodiscard]] std::optional<rect_f> inspect_content_bounds(const ui_element& element);

    // 견본 판의 열 수와 열마다 쓰는 줄 수다. 쓰는 견본은 `count`와 열 × 줄 가운데 작은 쪽이다.
    struct inspect_palette_grid
    {
        std::size_t columns { 1 };
        std::size_t rows { 0 };

        [[nodiscard]] bool operator==(const inspect_palette_grid&) const noexcept = default;
    };

    // 판을 두 열로 세운다. 역할이 쉰 개를 넘어 한 열이면 기본 창 높이에서 잘린다.
    //  - 두 열이 `available_width`(판의 안쪽 폭)에 들지 않으면 한 열이다. 폭에서 잘린 열은 통째로 보이지 않는다 (세로로 선 폰).
    //  - 줄은 `rows_that_fit`을 넘지 않는다. 표면의 높이가 모자라면 들어가는 줄까지만 쓰고, 둘째 열은 첫 열이 멈춘 다음 역할부터다.
    //  - 글 폭을 재는 그리기와 떼어 둔다. core 시험의 글꼴 관리자는 비어 있어 글 폭으로 한 열을 만들 수 없다.
    [[nodiscard]] inspect_palette_grid inspect_palette_grid_for(std::size_t count, float column_width, float column_gap, float available_width, std::size_t rows_that_fit) noexcept;
}
