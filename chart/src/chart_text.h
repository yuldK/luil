#pragma once

#include "luil/text/string_table.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace luil::chart_text {
    // 차트가 스스로 화면이나 접근 tree에 놓는 글의 기본값이다 (docs/chart-library-design.md 결정 L4).
    //
    // 원본은 `chart/text/`의 언어 파일이고 `luil_add_string_table`이 지은 표에서 찾는다.
    // core의 `default_text`는 core 비공개라 설치본에 없어 차트가 자기 표를 둔다.
    //  - 기본 언어는 한국어다. 앱이 config로 글을 주면 그 글이고, 비어 있으면 이 함수들이 돌려준다. OS 언어는 읽지 않는다.
    //  - 함수 이름이 곧 키 이름이다. 한 문장에 수가 둘인 요약과 갈래가 있는 점 글만 키가 여럿이다.
    //  - 값의 글은 호출부가 지어 넘긴다. 소수 자리와 앱의 `format_value`가 호출부 몫이기 때문이다.
    //
    // 돌려주는 view는 정적 저장소를 가리킨다.

    // 라이브러리와 같은 선호 목록으로 언어를 고른다. 정한 적이 없거나 표에 없는 언어만 든 목록이면 한국어다.
    //  - `text_languages`가 잠금 아래에서 목록을 복사하므로 문구 함수마다 부르지 않는다.
    //    요소를 지을 때 한 번 골라 그 요소의 문구에 넘긴다 (docs/i18n-design.md의 chart 약속).
    [[nodiscard]] string_lookup select_chart_strings();

    [[nodiscard]] std::u8string_view chart_name(const string_lookup& strings) noexcept;  // 차트
    [[nodiscard]] std::u8string_view chart_plot_name(const string_lookup& strings) noexcept;  // 자료 점
    // 기본 접근 요약이다 ("계열 2개, 범주 12개, 최솟값 3, 최댓값 120").
    //  - 계열과 범주는 복수형 항목이 될 수라 수로 받고, 최솟값과 최댓값은 지어 놓은 글로 받는다.
    //  - 어느 값 글이든 비면(빈 점뿐인 자료) 값 부분을 뺀다 ("계열 2개, 범주 12개").
    [[nodiscard]] std::u8string chart_summary(const string_lookup& strings, std::size_t series, std::size_t categories, std::u8string_view minimum, std::u8string_view maximum);
    // 고른 점의 글이다 ("매출, 3월, 120"). 계열이 하나인 차트는 이름을 비워 넘긴다 ("3월, 120").
    [[nodiscard]] std::u8string chart_point_text(const string_lookup& strings, std::u8string_view series_name, std::u8string_view category, std::u8string_view value);
    [[nodiscard]] std::u8string_view chart_missing_value(const string_lookup& strings) noexcept;  // 값 없음
    [[nodiscard]] std::u8string_view chart_empty(const string_lookup& strings) noexcept;  // 자료 없음
    // 이름표가 없는 범주의 글이다. 1부터 센 번호를 받는다 ("1").
    //  - 표에 두지 않는다. 숫자 글자뿐이라 번역할 글이 없고, 칸의 자동화 id(owner 꼬리)에도 쓰여 언어에 따라 바뀌면 안 된다.
    [[nodiscard]] std::u8string chart_category_number(std::size_t number);
}
