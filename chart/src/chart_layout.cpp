#include "chart_layout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace luil::chart_layout {
    namespace {
        // 대비 3:1 이상과 색각 이상에서의 구별을 두 바탕에서 맞춘 두 벌이다. 대비는 chart_element_tests가 잠근다.
        //  - 같은 차례가 같은 색상이라 테마를 바꿔도 계열의 색상이 바뀌지 않는다.
        constexpr std::array<ui_color, series_table_size> dark_background_table {
            make_ui_color(0x39, 0x87, 0xe5),
            make_ui_color(0xd9, 0x59, 0x26),
            make_ui_color(0x19, 0x9e, 0x70),
            make_ui_color(0xc9, 0x85, 0x00),
            make_ui_color(0xd5, 0x51, 0x81),
            make_ui_color(0x2f, 0x9e, 0x2f),
            make_ui_color(0x90, 0x85, 0xe9),
            make_ui_color(0xe6, 0x67, 0x67),
        };
        constexpr std::array<ui_color, series_table_size> light_background_table {
            make_ui_color(0x2a, 0x78, 0xd6),
            make_ui_color(0xd9, 0x53, 0x1f),
            make_ui_color(0x0f, 0x8a, 0x6a),
            make_ui_color(0xa8, 0x6f, 0x00),
            make_ui_color(0xc9, 0x4a, 0x7c),
            make_ui_color(0x00, 0x83, 0x00),
            make_ui_color(0x62, 0x50, 0xd6),
            make_ui_color(0xd9, 0x3f, 0x3e),
        };

        // 칸 하나의 최소 폭이다 (논리 픽셀). 이보다 좁은 칸은 손으로 가를 수 없다.
        constexpr float cell_minimum_width { 4.0f };
    }

    ui_color series_table_color(const bool dark_background, const std::size_t series) noexcept
    {
        const std::array<ui_color, series_table_size>& table { dark_background ? dark_background_table : light_background_table };
        return table[series % series_table_size];
    }

    float category_offset(const std::size_t category, const std::size_t count, const bool bands, const float width) noexcept
    {
        if (count == 0u)
            return 0.0f;
        if (bands)
            return static_cast<float>((static_cast<double>(category) + 0.5) / static_cast<double>(count) * static_cast<double>(width));
        if (count == 1u)
            return width / 2.0f;
        return static_cast<float>(static_cast<double>(category) / static_cast<double>(count - 1u) * static_cast<double>(width));
    }

    std::size_t cell_count(const std::size_t categories, const float logical_width) noexcept
    {
        // NaN은 "0보다 크다"가 거짓이라 칸이 서지 않는다.
        if (categories == 0u
            || (logical_width > 0.0f) == false)
            return 0u;
        const auto by_width { static_cast<std::size_t>(logical_width / cell_minimum_width) };
        return std::clamp<std::size_t>(categories, 1u, std::max<std::size_t>(by_width, 1u));
    }

    std::size_t category_of_cell(const std::size_t cell, const std::size_t cells, const std::size_t categories, const bool bands) noexcept
    {
        if (categories <= 1u
            || cells == 0u)
            return 0u;
        const double center { (static_cast<double>(cell) + 0.5) / static_cast<double>(cells) };
        const double position { bands ? center * static_cast<double>(categories) - 0.5 : center * static_cast<double>(categories - 1u) };
        const double nearest { std::floor(position + 0.5) };
        if (nearest <= 0.0)
            return 0u;
        return std::min(static_cast<std::size_t>(nearest), categories - 1u);
    }

    std::size_t label_stride(const std::size_t count, const float spacing, const float gap, const std::function<float(std::size_t)>& width_of)
    {
        if (count <= 1u)
            return 1u;
        if ((spacing > 0.0f) == false)
            return count;

        // 첫 이름표는 어느 걸음에서나 그려져 그 절반과 간격은 걸음이 반드시 덮어야 한다. 그보다 작은 걸음은 재지 않는다.
        const auto needed_stride = [spacing](const float distance) {
            const float stride { std::ceil(distance / spacing) };
            return stride > 1.0f ? static_cast<std::size_t>(stride) : std::size_t { 1 };
        };
        std::size_t stride { needed_stride(width_of(0u) / 2.0f + gap) };
        while (stride < count)
        {
            std::optional<float> overlap {};
            float width { width_of(0u) };
            for (std::size_t index { 0 }; count - index > stride; index += stride)
            {
                const float next_width { width_of(index + stride) };
                const float required { (width + next_width) / 2.0f + gap };
                if (required > static_cast<float>(stride) * spacing)
                {
                    overlap = required;
                    break;
                }
                width = next_width;
            }
            if (overlap.has_value() == false)
                return stride;
            stride = count > exact_stride_limit ? std::max(stride + 1u, needed_stride(*overlap)) : stride + 1u;
        }
        return count;
    }

    std::vector<value_column> reduce_to_columns(const std::span<const double> values, const std::size_t categories, const bool bands, const std::size_t columns)
    {
        std::vector<value_column> result(columns);
        if (columns == 0u
            || categories == 0u)
            return result;

        const auto last_column { static_cast<double>(columns - 1u) };
        const std::size_t limit { std::min(values.size(), categories) };
        for (std::size_t index { 0 }; index < limit; ++index)
        {
            const double value { values[index] };
            if (std::isfinite(value) == false)
                continue;

            // 그리기의 `category_offset`과 같은 자리를 double로 센다. float는 10만 번째 범주에서 이미 어긋난다.
            const double position { bands
                ? (static_cast<double>(index) + 0.5) / static_cast<double>(categories) * static_cast<double>(columns)
                : (categories == 1u ? static_cast<double>(columns) / 2.0 : static_cast<double>(index) / static_cast<double>(categories - 1u) * static_cast<double>(columns))
            };
            value_column& column { result[static_cast<std::size_t>(std::min(std::floor(position), last_column))] };
            if (column.has_value == false)
            {
                column.low = value;
                column.high = value;
                column.strongest = value;
                column.has_value = true;
                continue;
            }

            if (value < column.low)
            {
                column.low = value;
                column.low_first = false;
            }
            if (value > column.high)
            {
                column.high = value;
                column.low_first = true;
            }
            if (std::abs(value) > std::abs(column.strongest))
                column.strongest = value;
        }
        return result;
    }
}
