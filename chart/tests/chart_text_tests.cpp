#include "chart_text.h"

#include "luil/chart.h"
#include "luil/text/string_table.h"
#include "luil/ui/accessibility.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// 차트의 기본 문구다 (docs/chart-library-design.md 결정 L4). 앱이 글을 주지 않은 차트가 접근 tree와 화면에 놓는 글이다.
namespace {
    // 언어는 프로세스에 하나라 바꾼 test가 끝에서 되돌린다.
    //  - core의 되돌림 도우미는 core 비공개 생성 헤더를 들여 차트 test에서 쓰지 못한다. 빈 목록은 정한 적이 없는 상태처럼 한국어다.
    class language_scope
    {
    public:
        explicit language_scope(std::u8string language)
            : saved_ { luil::text_languages() }
        {
            use(std::move(language));
        }

        ~language_scope()
        {
            luil::set_text_languages(saved_.value_or(std::vector<std::u8string> {}));
        }

        language_scope(const language_scope&) = delete;
        language_scope& operator=(const language_scope&) = delete;

        void use(std::u8string language) const
        {
            const std::vector<std::u8string> preferences { std::move(language) };
            luil::set_text_languages(preferences);
        }

    private:
        std::optional<std::vector<std::u8string>> saved_ {};
    };

    [[nodiscard]] luil::chart_config sample_config()
    {
        luil::chart_config config {};
        config.owner = u8"sample";
        config.series.push_back({ .name = u8"sales", .values = std::make_shared<const std::vector<double>>(std::vector<double> { 1.0, 2.0 }) });
        return config;
    }
}

TEST_CASE("Chart default names are Korean", "[chart][text]")
{
    const luil::string_lookup strings { luil::chart_text::select_chart_strings() };
    REQUIRE(luil::chart_text::chart_name(strings) == u8"차트");
    REQUIRE(luil::chart_text::chart_plot_name(strings) == u8"자료 점");
    REQUIRE(luil::chart_text::chart_missing_value(strings) == u8"값 없음");
    REQUIRE(luil::chart_text::chart_empty(strings) == u8"자료 없음");
}

TEST_CASE("The chart summary states series, categories and the value range", "[chart][text]")
{
    const luil::string_lookup strings { luil::chart_text::select_chart_strings() };
    REQUIRE(luil::chart_text::chart_summary(strings, 2u, 12u, u8"3", u8"120") == u8"계열 2개, 범주 12개, 최솟값 3, 최댓값 120");
    REQUIRE(luil::chart_text::chart_summary(strings, 0u, 0u, {}, {}) == u8"계열 0개, 범주 0개");
    // 빈 점뿐이면 값이 없다. 한쪽만 있는 값 부분을 놓지 않는다.
    REQUIRE(luil::chart_text::chart_summary(strings, 1u, 3u, u8"3", {}) == u8"계열 1개, 범주 3개");
    REQUIRE(luil::chart_text::chart_summary(strings, 1u, 3u, {}, u8"120") == u8"계열 1개, 범주 3개");
}

TEST_CASE("The point text leaves out the series name for a single series", "[chart][text]")
{
    const luil::string_lookup strings { luil::chart_text::select_chart_strings() };
    REQUIRE(luil::chart_text::chart_point_text(strings, u8"매출", u8"3월", u8"120") == u8"매출, 3월, 120");
    REQUIRE(luil::chart_text::chart_point_text(strings, {}, u8"3월", u8"120") == u8"3월, 120");
    REQUIRE(luil::chart_text::chart_point_text(strings, u8"매출", u8"3월", luil::chart_text::chart_missing_value(strings)) == u8"매출, 3월, 값 없음");
}

TEST_CASE("Categories without a label are numbered", "[chart][text]")
{
    REQUIRE(luil::chart_text::chart_category_number(1u) == u8"1");
    REQUIRE(luil::chart_text::chart_category_number(12u) == u8"12");
    REQUIRE(luil::chart_text::chart_category_number(100000u) == u8"100000");
}

TEST_CASE("Choosing English gives the English chart words", "[chart][text]")
{
    const language_scope english { u8"en" };
    const luil::string_lookup strings { luil::chart_text::select_chart_strings() };
    REQUIRE(strings.tag() == u8"en");
    REQUIRE(luil::chart_text::chart_name(strings) == u8"Chart");
    REQUIRE(luil::chart_text::chart_plot_name(strings) == u8"Data points");
    REQUIRE(luil::chart_text::chart_missing_value(strings) == u8"No value");
    REQUIRE(luil::chart_text::chart_empty(strings) == u8"No data");
    REQUIRE(luil::chart_text::chart_point_text(strings, u8"Sales", u8"March", u8"120") == u8"Sales, March, 120");
    REQUIRE(luil::chart_text::chart_point_text(strings, {}, u8"March", u8"120") == u8"March, 120");
    REQUIRE(luil::chart_text::chart_summary(strings, 2u, 12u, u8"3", u8"120") == u8"2 series, 12 categories, minimum 3, maximum 120");
    // 범주 수가 꼴을 고른다. 계열과 범주의 꼴을 따로 고르는지도 여기서 본다.
    REQUIRE(luil::chart_text::chart_summary(strings, 2u, 1u, {}, {}) == u8"2 series, 1 category");
    REQUIRE(luil::chart_text::chart_summary(strings, 1u, 0u, {}, {}) == u8"1 series, 0 categories");
}

TEST_CASE("A language the chart table lacks falls back to Korean", "[chart][text]")
{
    const language_scope french { u8"fr" };
    REQUIRE(luil::chart_text::chart_name(luil::chart_text::select_chart_strings()) == u8"차트");
}

TEST_CASE("A chart keeps the language it was built in", "[chart][text]")
{
    const language_scope language { u8"en" };
    const luil::chart_element built_in_english { sample_config() };
    language.use(u8"ko");
    const luil::chart_element built_in_korean { sample_config() };

    REQUIRE(built_in_english.accessibility().name == u8"Chart");
    REQUIRE(built_in_english.accessibility().value.starts_with(u8"1 series, 2 categories, minimum "));
    REQUIRE(built_in_korean.accessibility().name == u8"차트");
    REQUIRE(built_in_korean.accessibility().value.starts_with(u8"계열 1개, 범주 2개, 최솟값 "));
}
