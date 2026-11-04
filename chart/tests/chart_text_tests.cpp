#include "chart_text.h"

#include <catch2/catch_test_macros.hpp>

// 차트의 기본 문구다 (docs/chart-library-design.md 결정 L4). 앱이 글을 주지 않은 차트가 접근 tree와 화면에 놓는 글이다.
TEST_CASE("Chart default names are Korean", "[chart][text]")
{
    REQUIRE(luil::chart_text::chart_name() == u8"차트");
    REQUIRE(luil::chart_text::chart_plot_name() == u8"자료 점");
    REQUIRE(luil::chart_text::chart_missing_value() == u8"값 없음");
    REQUIRE(luil::chart_text::chart_empty() == u8"자료 없음");
}

TEST_CASE("The chart summary states series, categories and the value range", "[chart][text]")
{
    REQUIRE(luil::chart_text::chart_summary(2u, 12u, u8"3", u8"120") == u8"계열 2개, 범주 12개, 최솟값 3, 최댓값 120");
    REQUIRE(luil::chart_text::chart_summary(0u, 0u, {}, {}) == u8"계열 0개, 범주 0개");
    // 빈 점뿐이면 값이 없다. 한쪽만 있는 값 부분을 놓지 않는다.
    REQUIRE(luil::chart_text::chart_summary(1u, 3u, u8"3", {}) == u8"계열 1개, 범주 3개");
    REQUIRE(luil::chart_text::chart_summary(1u, 3u, {}, u8"120") == u8"계열 1개, 범주 3개");
}

TEST_CASE("The point text leaves out the series name for a single series", "[chart][text]")
{
    REQUIRE(luil::chart_text::chart_point_text(u8"매출", u8"3월", u8"120") == u8"매출, 3월, 120");
    REQUIRE(luil::chart_text::chart_point_text({}, u8"3월", u8"120") == u8"3월, 120");
    REQUIRE(luil::chart_text::chart_point_text(u8"매출", u8"3월", luil::chart_text::chart_missing_value()) == u8"매출, 3월, 값 없음");
}

TEST_CASE("Categories without a label are numbered", "[chart][text]")
{
    REQUIRE(luil::chart_text::chart_category_number(1u) == u8"1");
    REQUIRE(luil::chart_text::chart_category_number(12u) == u8"12");
    REQUIRE(luil::chart_text::chart_category_number(100000u) == u8"100000");
}
