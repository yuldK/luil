#include "luil/chart/chart_scale.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>
#include <system_error>

namespace luil {
    namespace {
        // 몫을 눈금 자리로 접을 때 둔 오차다. 0.3 / 0.1이 3.0000000000000004로 나와 눈금이 하나 더 서는 것을 막는다.
        constexpr double quotient_tolerance { 1e-9 };
        // 이보다 큰 끝은 {0, 1}로 본다. 앞뒤로 한 걸음 넓히거나 폭을 잴 때 넘치지 않는 크기다.
        constexpr double largest_bound { 1e300 };
        // 이보다 작은 값은 자릿수를 재지 않고 0으로 본다. 10의 거듭제곱이 정규 수로 남는 크기다.
        constexpr double smallest_magnitude { 1e-300 };
        // 폭이 끝의 크기에 견줘 이보다 작으면 같은 값으로 본다. 더 좁으면 눈금 글끼리 구별되지 않는다.
        constexpr double same_value_ratio { 1e-12 };
        // `format_chart_number`의 소수 자리 상한이다. 뒤로는 double이 담지 못하는 자리다.
        constexpr int largest_decimals { 20 };

        [[nodiscard]] bool usable_bound(const double bound) noexcept
        {
            return std::isfinite(bound)
                && std::abs(bound) <= largest_bound
            ;
        }

        // `multiple` x 10^exponent 걸음이다. 음수 지수는 나누어 지어 0.1·0.2·0.5가 가장 가까운 double로 선다.
        [[nodiscard]] double step_of(const double multiple, const int exponent) noexcept
        {
            return exponent >= 0
                ? multiple * std::pow(10.0, exponent)
                : multiple / std::pow(10.0, -exponent)
            ;
        }

        [[nodiscard]] chart_ticks ticks_covering(const double low, const double high, const double step, const int exponent) noexcept
        {
            const double first_index { std::floor(low / step + quotient_tolerance) };
            const double last_index { std::ceil(high / step - quotient_tolerance) };
            chart_ticks ticks {};
            ticks.first = first_index * step;
            // -0.0 글이 서지 않게 한다.
            if (ticks.first == 0.0)
                ticks.first = 0.0;
            ticks.step = step;
            ticks.count = static_cast<std::size_t>(last_index - first_index) + 1u;
            ticks.decimals = std::max(0, -exponent);
            return ticks;
        }

        // 폭이 없는 범위다. 그 값의 자릿수 단위로 앞뒤 한 걸음씩 덮는다.
        [[nodiscard]] chart_ticks ticks_around(const double value) noexcept
        {
            const double magnitude { std::abs(value) };
            const int exponent { magnitude < smallest_magnitude ? 0 : static_cast<int>(std::floor(std::log10(magnitude))) };
            const double step { step_of(1.0, exponent) };
            return ticks_covering(value - step, value + step, step, exponent);
        }
    }

    chart_ticks nice_ticks(const chart_range range, const std::size_t target_count) noexcept
    {
        const bool usable { usable_bound(range.minimum) && usable_bound(range.maximum) };
        const double low { usable ? std::min(range.minimum, range.maximum) : 0.0 };
        const double high { usable ? std::max(range.minimum, range.maximum) : 1.0 };
        const double width { high - low };
        if (width <= std::max(std::abs(low), std::abs(high)) * same_value_ratio
            || width < std::numeric_limits<double>::min())
            return ticks_around(low + width / 2.0);

        const auto target { static_cast<double>(std::clamp<std::size_t>(target_count, 2u, 10u)) };
        // 칸이 상한 안에 드는 걸음은 폭 / 상한 이상이다. 그 자릿수의 1부터 위로 올라가며 첫 걸음을 고른다.
        //  - 폭 자신 이상인 걸음이면 칸은 많아야 둘이라 네 자릿수 안에서 반드시 끝난다.
        const int first_exponent { static_cast<int>(std::floor(std::log10(width / target))) };
        constexpr std::array<double, 3> multiples { 1.0, 2.0, 5.0 };
        chart_ticks ticks {};
        for (int exponent = first_exponent; exponent < first_exponent + 4; ++exponent)
        {
            for (const double multiple : multiples)
            {
                ticks = ticks_covering(low, high, step_of(multiple, exponent), exponent);
                if (static_cast<double>(ticks.count) - 1.0 <= target)
                    return ticks;
            }
        }
        return ticks;
    }

    std::u8string format_chart_number(const double value, const int decimals)
    {
        if (std::isfinite(value) == false)
            return {};

        // 가장 큰 double의 정수부가 309자리이고 부호, 소수점, 소수 자리 상한이 더 든다.
        std::array<char, 340> buffer {};
        const std::to_chars_result result { std::to_chars(buffer.data(), buffer.data() + buffer.size()
            , value, std::chars_format::fixed, std::clamp(decimals, 0, largest_decimals))
        };
        if (result.ec != std::errc {})
            return {};

        std::string_view text { buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()) };
        if (text.starts_with('-')
            && text.find_first_not_of("-0.") == std::string_view::npos)
            text.remove_prefix(1);
        return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
    }

    float chart_value_to_pixel(const double value, const chart_range range, const float start, const float end) noexcept
    {
        const double width { range.maximum - range.minimum };
        const double pixel_start { start };
        const double pixel_width { static_cast<double>(end) - pixel_start };
        if (width == 0.0
            || std::isfinite(width) == false)
            return static_cast<float>(pixel_start + pixel_width / 2.0);
        return static_cast<float>(pixel_start + pixel_width * ((value - range.minimum) / width));
    }
}
