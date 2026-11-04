#pragma once

#include "luil/theme/ui_theme.h"

#include <cstddef>
#include <functional>
#include <span>
#include <vector>

namespace luil::chart_layout {
    // 차트 요소가 쓰는 비공개 순수 계산이다. 글꼴과 그리기 문맥 없이 test가 그대로 잰다 (docs/chart-design.md).

    // 계열 표의 색 수다. 9번째 계열부터는 처음으로 돌고 표식이 가른다.
    inline constexpr std::size_t series_table_size { 8 };

    // 앱이 색을 주지 않은 계열의 색이다 (결정 E2). 바탕은 이름이 아니라 색으로 고른다 (`is_dark_background`).
    [[nodiscard]] ui_color series_table_color(bool dark_background, std::size_t series) noexcept;

    // 범주의 가로 자리다 (플롯 왼쪽에서 잰 거리).
    //  - `bands`면(막대 계열이 있다) 범주마다 칸 가운데다.
    //  - 아니면 첫·끝 범주가 플롯 양 끝이다. 범주가 하나면 가운데다.
    [[nodiscard]] float category_offset(std::size_t category, std::size_t count, bool bands, float width) noexcept;

    // hover 칸 수다. 범주 수와 "플롯 폭(논리 픽셀) / 4" 가운데 작은 값이고, 범주와 폭이 있으면 적어도 하나다.
    [[nodiscard]] std::size_t cell_count(std::size_t categories, float logical_width) noexcept;
    // 칸이 맡는 범주다. 칸 가운데에 가장 가까운 범주다.
    [[nodiscard]] std::size_t category_of_cell(std::size_t cell, std::size_t cells, std::size_t categories, bool bands) noexcept;

    // x 이름표를 몇 개마다 하나씩 그리는가다. 그린 이름표끼리 `gap` 이상 떨어지는 가장 작은 걸음이고, 첫 이름표 하나만 남아도
    // 겹치면 `count`다.
    //  - `spacing`은 이웃한 범주의 거리, `width_of`는 그 범주 이름표의 폭이다 (같은 단위).
    //  - 범주가 `exact_stride_limit`보다 많으면 재는 수가 범주 수를 따라 늘지 않게 겹친 쌍이 요구하는 걸음으로 건너뛴다. 폭이 크게
    //    갈리는 이름표에서는 가장 작은 걸음이 아닐 수 있지만 겹치지는 않는다.
    inline constexpr std::size_t exact_stride_limit { 4096 };
    [[nodiscard]] std::size_t label_stride(std::size_t count, float spacing, float gap, const std::function<float(std::size_t)>& width_of);

    // 픽셀 열 하나에 든 값이다 (큰 자료).
    struct value_column
    {
        double low { 0.0 };
        double high { 0.0 };
        // 절댓값이 가장 큰 값이다. 막대가 쓴다.
        double strongest { 0.0 };
        // 열 안에서 최솟값이 최댓값보다 먼저 나왔다. 선이 이웃한 열과 꼬이지 않게 그 차례로 잇는다.
        bool low_first { true };
        // 유한한 값이 하나라도 있다. 없으면 선과 영역이 끊긴다.
        bool has_value { false };
    };

    // 값을 범주 자리의 픽셀 열로 모은다. 결과는 `columns`개이고, 그래서 긋는 수는 열의 두 배를 넘지 않는다.
    [[nodiscard]] std::vector<value_column> reduce_to_columns(std::span<const double> values, std::size_t categories, bool bands, std::size_t columns);
}
