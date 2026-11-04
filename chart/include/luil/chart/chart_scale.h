#pragma once

#include <cstddef>
#include <string>

namespace luil {
    // 차트 축의 순수 계산이다 (docs/chart-design.md). 요소 없이 부를 수 있고 글꼴과 OS 지역을 읽지 않는다.
    //  - 앱이 자기 그림에 같은 눈금을 쓰고 test가 계산만 재는 자리다 (달력의 `calendar_date.h`와 같은 꼴).

    // 값의 범위다. 두 끝을 포함한다.
    struct chart_range
    {
        double minimum { 0.0 };
        double maximum { 1.0 };
    };

    // 범위를 덮는 눈금이다. i번째 눈금의 값은 `first + i * step`이다.
    struct chart_ticks
    {
        double first { 0.0 };    // 첫 눈금. 범위의 최소 이하다
        double step { 1.0 };     // 1·2·5 x 10^k다
        std::size_t count { 0 }; // 마지막 눈금이 범위의 최대 이상이 되는 수다
        int decimals { 0 };      // 눈금 글의 소수 자리다. step에서 나온다
    };

    // 범위를 덮는 둥근 눈금이다. `target_count`는 눈금 사이 칸 수의 상한이고 2~10으로 접는다.
    //  - 칸이 상한을 넘지 않는 가장 작은 걸음을 고른다. 상한을 글 자리로 나눠 주면 눈금 글이 그 간격보다 빽빽해지지 않는다.
    //  - 최소와 최대가 뒤바뀌어 오면 바꿔 읽는다.
    //  - 최소와 최대가 같거나 끝의 크기에 견줘 폭이 없는 것과 같으면, 그 값의 자릿수 단위(5면 1, 120이면 100, 0이면 1)로 앞뒤 한 걸음씩 덮는다.
    //  - 끝이 유한하지 않거나 절댓값이 1e300을 넘으면 {0, 1}로 본다. 값이 없는 자료도 그림이 서야 한다.
    [[nodiscard]] chart_ticks nice_ticks(chart_range range, std::size_t target_count) noexcept;
    // 지역에 매이지 않는 숫자 글이다 ("-1250.5"). 소수 자리가 고정이고 천 단위 구분이 없다.
    //  - 0으로 반올림된 음수는 부호를 뗀다 ("-0"은 "0", -0.001의 두 자리는 "0.00"). 유한하지 않으면 빈 글이다.
    //  - `decimals`는 0~20으로 접는다.
    [[nodiscard]] std::u8string format_chart_number(double value, int decimals);
    // 값을 [start, end] 픽셀로 옮긴다. end가 start보다 작으면 축이 뒤집힌다 (y축).
    //  - 범위 밖의 값은 자르지 않는다. 범위 폭이 0이거나 유한하지 않으면 모든 값이 가운데다. 값이 NaN이면 NaN이다.
    [[nodiscard]] float chart_value_to_pixel(double value, chart_range range, float start, float end) noexcept;
}
