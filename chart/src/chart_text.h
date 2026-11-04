#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace luil::chart_text {
    // 차트가 스스로 화면이나 접근 tree에 놓는 글의 기본값이다 (docs/chart-library-design.md 결정 L4).
    //
    // core의 `default_text`와 같은 꼴이다. 그 헤더는 core 비공개라 설치본에 없어 차트가 쓸 수 없으므로 자기 파일을 둔다.
    //  - 기본 문구는 늘 한국어다. 앱이 config로 글을 주면 그 글이고, 비어 있으면 이 함수들이 돌려준다. OS 언어는 읽지 않는다.
    //  - 낱말마다 함수 하나인 까닭은 문자열 표로 옮길 때 몸통만 바꾸고 호출부를 그대로 두기 위해서다. 함수 이름이 곧 키 이름이다.
    //  - 값의 글은 호출부가 지어 넘긴다. 소수 자리와 앱의 `format_value`가 호출부 몫이기 때문이다.
    //
    // 돌려주는 view는 정적 저장소를 가리킨다.

    [[nodiscard]] std::u8string_view chart_name() noexcept;  // 차트
    [[nodiscard]] std::u8string_view chart_plot_name() noexcept;  // 자료 점
    // 기본 접근 요약이다 ("계열 2개, 범주 12개, 최솟값 3, 최댓값 120").
    //  - 계열과 범주는 복수형 항목이 될 수라 수로 받고, 최솟값과 최댓값은 지어 놓은 글로 받는다.
    //  - 어느 값 글이든 비면(빈 점뿐인 자료) 값 부분을 뺀다 ("계열 2개, 범주 12개").
    [[nodiscard]] std::u8string chart_summary(std::size_t series, std::size_t categories, std::u8string_view minimum, std::u8string_view maximum);
    // 고른 점의 글이다 ("매출, 3월, 120"). 계열이 하나인 차트는 이름을 비워 넘긴다 ("3월, 120").
    [[nodiscard]] std::u8string chart_point_text(std::u8string_view series_name, std::u8string_view category, std::u8string_view value);
    [[nodiscard]] std::u8string_view chart_missing_value() noexcept;  // 값 없음
    [[nodiscard]] std::u8string_view chart_empty() noexcept;  // 자료 없음
    // 이름표가 없는 범주의 글이다. 1부터 센 번호를 받는다 ("1").
    [[nodiscard]] std::u8string chart_category_number(std::size_t number);
}
