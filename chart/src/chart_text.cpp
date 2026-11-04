#include "chart_text.h"

namespace luil::chart_text {
    namespace {
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
    }

    std::u8string_view chart_name() noexcept
    {
        return u8"차트";
    }

    std::u8string_view chart_plot_name() noexcept
    {
        return u8"자료 점";
    }

    std::u8string chart_summary(const std::size_t series, const std::size_t categories, const std::u8string_view minimum, const std::u8string_view maximum)
    {
        std::u8string text { u8"계열 " };
        append_number(text, series);
        text += u8"개, 범주 ";
        append_number(text, categories);
        text += u8"개";
        if (minimum.empty()
            || maximum.empty())
            return text;

        text += u8", 최솟값 ";
        text += minimum;
        text += u8", 최댓값 ";
        text += maximum;
        return text;
    }

    std::u8string chart_point_text(const std::u8string_view series_name, const std::u8string_view category, const std::u8string_view value)
    {
        std::u8string text {};
        if (series_name.empty() == false)
        {
            text += series_name;
            text += u8", ";
        }
        text += category;
        text += u8", ";
        text += value;
        return text;
    }

    std::u8string_view chart_missing_value() noexcept
    {
        return u8"값 없음";
    }

    std::u8string_view chart_empty() noexcept
    {
        return u8"자료 없음";
    }

    std::u8string chart_category_number(const std::size_t number)
    {
        std::u8string text {};
        append_number(text, number);
        return text;
    }
}
