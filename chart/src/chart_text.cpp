#include "chart_text.h"

#include "chart_strings.h"

#include <array>
#include <cstdint>
#include <vector>

namespace luil::chart_text {
    namespace {
        using chart_strings::key;

        // 십진수 글자를 덧붙인다. 로케일이 자릿점을 끼우지 못하게 직접 짓는다.
        void append_number(std::u8string& text, std::size_t number)
        {
            if (number == 0u)
            {
                text += u8'0';
                return;
            }

            char8_t digits[20] {};
            int length { 0 };
            for (; number != 0u; number /= 10u)
                digits[length++] = static_cast<char8_t>(u8'0' + number % 10u);
            while (length > 0)
                text += digits[--length];
        }

        // 수 하나가 꼴을 고르는 문장이다.
        [[nodiscard]] std::u8string count_text(const string_lookup& strings, const key message, const std::size_t count)
        {
            const std::array<string_argument, 1> arguments { { { u8"count", static_cast<std::int64_t>(count) } } };
            return chart_strings::format(strings, message, arguments);
        }
    }

    string_lookup select_chart_strings()
    {
        return select_strings(chart_strings::table, text_languages().value_or(std::vector<std::u8string> {}));
    }

    std::u8string_view chart_name(const string_lookup& strings) noexcept
    {
        return chart_strings::text(strings, key::chart_name);
    }

    std::u8string_view chart_plot_name(const string_lookup& strings) noexcept
    {
        return chart_strings::text(strings, key::chart_plot_name);
    }

    std::u8string chart_summary(const string_lookup& strings, const std::size_t series, const std::size_t categories, const std::u8string_view minimum, const std::u8string_view maximum)
    {
        // 복수형은 `count` 하나로 고르므로 수가 둘인 문장을 한 키에 담지 못한다. 수마다 지어 자리 값으로 넣는다.
        const std::u8string series_text { count_text(strings, key::chart_summary_series, series) };
        const std::u8string categories_text { count_text(strings, key::chart_summary_categories, categories) };
        if (minimum.empty()
            || maximum.empty())
        {
            const std::array<string_argument, 2> arguments {
                {
                    { u8"series", std::u8string_view { series_text } }
                    , { u8"categories", std::u8string_view { categories_text } }
                }
            };
            return chart_strings::format(strings, key::chart_summary_without_range, arguments);
        }

        const std::array<string_argument, 4> arguments {
            {
                { u8"series", std::u8string_view { series_text } }
                , { u8"categories", std::u8string_view { categories_text } }
                , { u8"minimum", minimum }
                , { u8"maximum", maximum }
            }
        };
        return chart_strings::format(strings, key::chart_summary, arguments);
    }

    std::u8string chart_point_text(const string_lookup& strings, const std::u8string_view series_name, const std::u8string_view category, const std::u8string_view value)
    {
        if (series_name.empty())
        {
            const std::array<string_argument, 2> arguments {
                {
                    { u8"category", category }
                    , { u8"value", value }
                }
            };
            return chart_strings::format(strings, key::chart_point_text_single_series, arguments);
        }

        const std::array<string_argument, 3> arguments {
            {
                { u8"series", series_name }
                , { u8"category", category }
                , { u8"value", value }
            }
        };
        return chart_strings::format(strings, key::chart_point_text, arguments);
    }

    std::u8string_view chart_missing_value(const string_lookup& strings) noexcept
    {
        return chart_strings::text(strings, key::chart_missing_value);
    }

    std::u8string_view chart_empty(const string_lookup& strings) noexcept
    {
        return chart_strings::text(strings, key::chart_empty);
    }

    std::u8string chart_category_number(const std::size_t number)
    {
        std::u8string text {};
        append_number(text, number);
        return text;
    }
}
