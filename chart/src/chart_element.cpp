#include "luil/chart/chart_element.h"

#include "chart_layout.h"
#include "chart_text.h"

#include "luil/ui/draw_primitives.h"
#include "luil/ui/field_popup.h"
#include "luil/ui/text_block.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/effects/SkDashPathEffect.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace luil {
    namespace {
        // 치수는 논리 픽셀이다. 배치와 그리기가 배율을 곱한다.
        constexpr float tick_label_gap { 6.0f };
        // 플롯 오른쪽의 빈자리다. 끝 범주의 이름표가 플롯 끝에 가운데로 서서 그 절반이 플롯 밖으로 나간다.
        constexpr float plot_right_inset { 12.0f };
        constexpr float x_label_gap { 4.0f };
        constexpr float x_label_spacing { 8.0f };
        constexpr float legend_gap { 6.0f };
        constexpr float legend_swatch_width { 18.0f };
        constexpr float legend_swatch_gap { 6.0f };
        constexpr float legend_item_gap { 14.0f };
        constexpr float line_width { 2.0f };
        constexpr float rule_width { 1.0f };
        constexpr float marker_radius { 3.5f };
        constexpr float selected_marker_radius { 5.0f };
        constexpr float selected_marker_ring { 1.5f };
        // 범주 간격이 이보다 좁으면 점마다의 표식이 선을 덮어 계열을 가르지 못하므로 고른 점에만 그린다.
        constexpr float marker_minimum_spacing { 8.0f };
        constexpr float value_box_padding { 8.0f };
        constexpr float value_box_gap { 6.0f };
        constexpr float value_box_marker_slot { 14.0f };
        constexpr float value_box_column_gap { 12.0f };
        // 글 폭이 상자 폭과 꼭 같으면 부동소수 오차로 말줄임이 설 수 있어 상자를 조금 넓게 준다.
        constexpr float text_box_slack { 2.0f };
        // 눈금 글 간격이 글 높이의 이 배수보다 좁아지지 않는다.
        constexpr float tick_spacing_lines { 2.5f };
        // 눈금 글 폭이 이 몫을 넘으면 플롯이 사라지므로 글을 줄인다.
        constexpr float tick_label_width_limit { 0.5f };
        constexpr float bar_band_ratio { 0.7f };
        constexpr float area_fill_alpha { 0.25f };
        // 배치 전의 눈금 칸 상한이다. 접근 글의 소수 자리가 배치 전에도 서야 한다.
        constexpr std::size_t default_tick_target { 5 };
        constexpr int value_decimals_limit { 6 };
        constexpr int fixed_decimals_limit { 20 };
        // 누른 채 이 거리를 넘으면 끌기다 (물리 픽셀, `pointer_drag_target::start_distance`와 같은 단위).
        constexpr float cell_drag_start_distance { 6.0f };
        // 범위 밖의 값이 이만큼을 넘어도 플롯 밖이라 잘린다. 유한한 큰 값이 float로 넘쳐 경로가 통째로 버려지는 것을 막는다.
        constexpr float far_pixels { 16384.0f };

        // 고대비의 선 모양이다 (실선, 긴 점선, 점선, 쇄선 차례, 논리 픽셀).
        constexpr std::array<float, 2> long_dash { 8.0f, 4.0f };
        constexpr std::array<float, 2> dot_dash { 2.0f, 3.0f };
        constexpr std::array<float, 4> dash_dot { 8.0f, 3.0f, 2.0f, 3.0f };
        constexpr std::size_t line_style_count { 4 };
        // 큰 자료의 선을 이 수만큼씩 나눠 긋는다. 경로 하나로 그으면 한 줄을 지나는 변이 폭만큼 쌓여 칠하는 비용이 그 곱으로 는다.
        constexpr std::size_t large_line_chunk_points { 32 };

        // 공개 고정 소수 서식의 상한을 넘는 눈금은 지수로 옮긴다. 눈금 걸음이 요구하는 유효 자리만 남긴다.
        [[nodiscard]] std::u8string default_value_text(const double value, const int decimals)
        {
            if (decimals <= fixed_decimals_limit)
                return format_chart_number(value, decimals);
            if (std::isfinite(value) == false)
                return {};
            if (value == 0.0)
                return u8"0";
            const int exponent { static_cast<int>(std::floor(std::log10(std::abs(value)))) };
            const int precision { std::clamp(decimals + exponent, 0, std::numeric_limits<double>::max_digits10 - 1) };
            std::array<char, 32> buffer {};
            const std::to_chars_result result { std::to_chars(buffer.data(), buffer.data() + buffer.size()
                , value, std::chars_format::scientific, precision)
            };
            if (result.ec != std::errc {})
                return {};
            return { reinterpret_cast<const char8_t*>(buffer.data()), static_cast<std::size_t>(result.ptr - buffer.data()) };
        }

        // hover 칸이다. 그리지 않고 hit와 hover와 끌기만 맡는다. 고르는 일은 전부 차트가 한다.
        class chart_cell final : public ui_element
        {
        public:
            using ui_element::ui_element;

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context&, const interaction_snapshot&) const override
            {
            }
        };

        [[nodiscard]] std::u8string cell_owner(const std::u8string& owner, const std::size_t number)
        {
            std::u8string result { owner };
            result += u8'\u001f';
            result += chart_text::chart_category_number(number);
            return result;
        }

        [[nodiscard]] float valid_scale(const float scale) noexcept
        {
            return std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
        }

        [[nodiscard]] SkRect sk_rect(const rect_f& rect) noexcept
        {
            return SkRect::MakeXYWH(rect.x, rect.y, rect.width, rect.height);
        }

        [[nodiscard]] text_block_style block_style(const float font_size, const text_align align = text_align::start)
        {
            text_block_style style {};
            style.font.size = font_size;
            style.align = align;
            return style;
        }

        // 선 모양 하나로 긋는 붓이다. 차례 0은 실선이다.
        [[nodiscard]] SkPaint stroke_paint(const ui_color color, const float width, const std::size_t line_style, const float scale)
        {
            SkPaint paint { solid_paint(color) };
            paint.setStyle(SkPaint::kStroke_Style);
            paint.setStrokeWidth(width * scale);
            paint.setStrokeJoin(SkPaint::kRound_Join);
            const auto dashed = [&paint, scale](const std::span<const float> pattern) {
                std::array<SkScalar, 4> intervals {};
                for (std::size_t index { 0 }; index < pattern.size(); ++index)
                    intervals[index] = pattern[index] * scale;
                paint.setPathEffect(SkDashPathEffect::Make({ intervals.data(), pattern.size() }, 0.0f));
            };
            switch (line_style % line_style_count)
            {
            case 1u:
                dashed(long_dash);
                break;
            case 2u:
                dashed(dot_dash);
                break;
            case 3u:
                dashed(dash_dot);
                break;
            default:
                break;
            }
            return paint;
        }

        // 색 하나로 또렷이 칠하는 붓이다. 격자와 막대의 가장자리가 픽셀에 맞아 번지지 않는다.
        [[nodiscard]] SkPaint crisp_paint(const ui_color color)
        {
            SkPaint paint { solid_paint(color) };
            paint.setAntiAlias(false);
            return paint;
        }

        void draw_marker_shape(SkCanvas& canvas, const chart_marker marker, const float x, const float y, const float radius, const SkPaint& paint)
        {
            switch (marker)
            {
            case chart_marker::circle:
                canvas.drawCircle(x, y, radius, paint);
                break;
            case chart_marker::square:
                canvas.drawRect(SkRect::MakeLTRB(x - radius, y - radius, x + radius, y + radius), paint);
                break;
            case chart_marker::triangle:
            case chart_marker::diamond:
            {
                SkPathBuilder path {};
                path.moveTo(x, y - radius);
                path.lineTo(x + radius, marker == chart_marker::triangle ? y + radius : y);
                if (marker == chart_marker::diamond)
                    path.lineTo(x, y + radius);
                path.lineTo(x - radius, marker == chart_marker::triangle ? y + radius : y);
                path.close();
                canvas.drawPath(path.detach(), paint);
                break;
            }
            case chart_marker::automatic:
            case chart_marker::none:
                break;
            }
        }
    }

    // 자료 요약과 배치 결과다. 생성자가 자료를, `arrange`가 자리를 정하고 그 뒤로는 읽기만 한다 (게시된 tree는 불변이다).
    struct chart_element::state
    {
        // 계열 하나의 그리는 모양이다.
        struct series_look
        {
            ui_color color { 0 };
            // 고대비만 계열을 선 모양으로 가른다. 그 밖에는 실선이다.
            std::size_t line_style { 0 };
            // 모든 점과 범례에 그리는 표식이다.
            chart_marker point_marker { chart_marker::none };
            // 고른 점에 그리는 표식이다.
            chart_marker selected_marker { chart_marker::none };
            // 막대를 채운다. 고대비는 첫 막대 계열만 채우고 나머지는 테로 그린다.
            bool filled_bar { true };
        };

        // 경로로 옮긴 선 계열이다. 끊긴 구간마다 부분 경로가 하나다.
        struct traced_series
        {
            // 큰 자료는 조각 여럿이다 (`large_line_chunk_points`).
            std::vector<SkPath> lines {};
            // 영역의 면이다. 큰 자료는 열마다 칠해 쓰지 않는다.
            SkPath fill {};
            // 앞뒤가 빈 점인 값이다. 긋는 선이 없어 점으로 찍는다.
            std::vector<point_f> dots {};
        };

        state(const chart_element& element, chart_config source);

        [[nodiscard]] bool bands() const noexcept
        {
            return bar_series > 0u;
        }

        [[nodiscard]] bool has_points() const noexcept
        {
            return categories > 0u && config.series.empty() == false;
        }

        [[nodiscard]] std::span<const double> values_of(const std::size_t series) const noexcept
        {
            const std::shared_ptr<const std::vector<double>>& values { config.series[series].values };
            return values != nullptr ? std::span<const double> { *values } : std::span<const double> {};
        }

        [[nodiscard]] double value_at(const std::size_t series, const std::size_t category) const noexcept
        {
            const std::span<const double> values { values_of(series) };
            return category < values.size() ? values[category] : std::numeric_limits<double>::quiet_NaN();
        }

        [[nodiscard]] std::u8string category_text(const std::size_t category) const
        {
            return category < config.categories.size() ? config.categories[category] : chart_text::chart_category_number(category + 1u);
        }

        [[nodiscard]] std::u8string tick_text(const double value) const
        {
            return config.format_value != nullptr ? config.format_value(value) : default_value_text(value, ticks.decimals);
        }

        // 값 상자와 접근 글이다. 눈금보다 한 자리 더 쓴다. 눈금이 이미 상한보다 길면 눈금 자리를 그대로 둬 값이 0으로 뭉개지지 않는다.
        [[nodiscard]] std::u8string value_text(const double value) const
        {
            if (config.format_value != nullptr)
                return config.format_value(value);
            return default_value_text(value, std::max(ticks.decimals, std::min(ticks.decimals + 1, value_decimals_limit)));
        }

        [[nodiscard]] std::optional<chart_point> current_point() const noexcept
        {
            if (config.focused.has_value() == false
                || config.focused->series >= config.series.size()
                || config.focused->category >= categories)
                return std::nullopt;
            return config.focused;
        }

        [[nodiscard]] float category_x(const std::size_t category) const noexcept
        {
            return plot.x + chart_layout::category_offset(category, categories, bands(), plot.width);
        }

        // 그 계열의 점이 서는 가로 자리다. 막대면 그 막대의 가운데다.
        [[nodiscard]] float point_x(const std::size_t series, const std::size_t category) const noexcept
        {
            if (config.series[series].kind != chart_series_kind::bar
                || large)
                return category_x(category);
            const float each { bar_width() };
            return category_x(category) + (static_cast<float>(bar_order[series]) - static_cast<float>(bar_series - 1u) / 2.0f) * each;
        }

        [[nodiscard]] float bar_width() const noexcept
        {
            return plot.width / static_cast<float>(categories) * bar_band_ratio / static_cast<float>(bar_series);
        }

        [[nodiscard]] float value_y(const double value) const noexcept
        {
            return std::clamp(chart_value_to_pixel(value, axis, plot.y + plot.height, plot.y), plot.y - far_pixels, plot.y + plot.height + far_pixels);
        }

        // 막대와 영역이 뻗는 기준선이다. 축이 0을 품지 않으면 0에 가까운 끝이다.
        [[nodiscard]] float baseline_y() const noexcept
        {
            return value_y(std::clamp(0.0, axis.minimum, axis.maximum));
        }

        [[nodiscard]] double tick_value(const std::size_t index) const noexcept
        {
            return ticks.first + static_cast<double>(index) * ticks.step;
        }

        [[nodiscard]] series_look look_of(const draw_context& context, std::size_t series) const noexcept;
        [[nodiscard]] traced_series trace(std::size_t series) const;

        void arrange(const arrange_context& context);
        void draw(draw_context& context, const interaction_snapshot& interaction) const;
        void draw_grid_and_labels(draw_context& context) const;
        void draw_axes(const draw_context& context) const;
        void draw_series(const draw_context& context) const;
        void draw_bars(const draw_context& context, std::size_t series) const;
        void draw_line(const draw_context& context, std::size_t series) const;
        void draw_area_fill(const draw_context& context, std::size_t series, const traced_series& traced, ui_color color) const;
        void draw_markers(const draw_context& context) const;
        void draw_legend(draw_context& context) const;
        void draw_legend_swatch(const draw_context& context, std::size_t series, const rect_f& swatch) const;
        void draw_value_box(draw_context& context, const interaction_snapshot& interaction) const;

        [[nodiscard]] std::u8string summary_text() const;
        [[nodiscard]] std::u8string point_text(chart_point point) const;
        // 값 상자를 띄울 범주다. 고른 점이 있으면 그 범주, 없으면 마우스가 머문 칸의 범주다.
        [[nodiscard]] std::optional<std::size_t> shown_category(const interaction_snapshot& interaction) const;
        // 그 점의 표식 자리다. 빈 점이면 플롯 세로 가운데다.
        [[nodiscard]] rect_f marker_bounds(chart_point point) const noexcept;
        // 그 범주에서 `y`에 가장 가까운 값을 가진 계열이다. 값이 모두 비면 첫 계열이다.
        [[nodiscard]] std::size_t nearest_series(std::size_t category, float y) const noexcept;
        // 포인터 자리의 칸이 맡은 범주에서 고르는 점이다.
        [[nodiscard]] chart_point point_at(float x, float y) const noexcept;

        // 이 점을 고르라는 메시지다. 비활성이거나 `focus`가 없거나 이미 고른 점이면 비어 있다.
        [[nodiscard]] std::vector<input_action> emit(chart_point point) const;
        [[nodiscard]] std::vector<input_action> step_category(value_step step) const;
        [[nodiscard]] std::vector<input_action> step_series(bool next) const;

        const chart_element& owner;
        chart_config config;
        // 기본 문구의 언어다. 요소를 지을 때 한 번 고른다. 언어가 바뀌면 앱이 다시 지은 요소부터 새 언어다.
        string_lookup strings { chart_text::select_chart_strings() };

        // --- 자료 (생성자) ---
        std::size_t categories { 0 };
        std::size_t bar_series { 0 };
        // 계열마다 막대 계열 가운데의 차례다.
        std::vector<std::size_t> bar_order {};
        std::optional<std::size_t> first_bar {};
        // 빈 점을 뺀 자료의 최소·최대다. 값이 없으면 비어 있다.
        std::optional<chart_range> data {};
        // 눈금이 덮을 범위다 (`y_range`, 비면 자료).
        chart_range value_range {};

        // --- 배치 ---
        float scale { 1.0f };
        rect_f plot {};
        float line_height { 0.0f };
        chart_ticks ticks {};
        // 축의 양 끝이다. 눈금이 덮은 범위라 맨 아래 눈금이 플롯 바닥, 맨 위 눈금이 플롯 꼭대기다.
        chart_range axis {};
        std::vector<std::u8string> tick_labels {};
        float tick_label_width { 0.0f };
        std::size_t x_label_stride { 1 };
        float x_label_top { 0.0f };
        bool legend_shown { false };
        float legend_top { 0.0f };
        std::vector<float> legend_widths {};
        // 범주 수가 플롯 폭의 픽셀 수를 넘어 계열을 픽셀 열로 모아 그린다.
        bool large { false };
        std::vector<std::vector<chart_layout::value_column>> columns {};
        std::size_t cells { 0 };
    };

    chart_element::state::state(const chart_element& element, chart_config source)
        : owner { element }
        , config { std::move(source) }
    {
        categories = config.categories.size();
        bar_order.resize(config.series.size());
        bool zero_based { false };
        double low { std::numeric_limits<double>::infinity() };
        double high { -std::numeric_limits<double>::infinity() };
        for (std::size_t index { 0 }; index < config.series.size(); ++index)
        {
            const chart_series& series { config.series[index] };
            if (series.kind == chart_series_kind::bar)
            {
                if (first_bar.has_value() == false)
                    first_bar = index;
                bar_order[index] = bar_series++;
            }
            if (series.kind != chart_series_kind::line)
                zero_based = true;

            const std::span<const double> values { values_of(index) };
            categories = std::max(categories, values.size());
            for (const double value : values)
            {
                if (std::isfinite(value))
                {
                    low = std::min(low, value);
                    high = std::max(high, value);
                }
            }
        }
        if (low <= high)
            data = chart_range { low, high };

        if (config.y_range.has_value())
            value_range = *config.y_range;
        else
        {
            value_range = data.value_or(chart_range {});
            if (zero_based)
            {
                value_range.minimum = std::min(value_range.minimum, 0.0);
                value_range.maximum = std::max(value_range.maximum, 0.0);
            }
        }
        ticks = nice_ticks(value_range, default_tick_target);
        axis = { ticks.first, tick_value(ticks.count - 1u) };
    }

    void chart_element::state::arrange(const arrange_context& context)
    {
        scale = valid_scale(context.scale);
        const rect_f box { context.slot };
        const measure_context measure { context.measure() };
        const float font_size { measure.style_metrics().small_font_size };
        line_height = text_block_line_height(block_style(font_size)) * scale;

        legend_shown = config.legend != chart_legend::none && config.series.empty() == false;
        const float legend_height { legend_shown ? line_height + legend_gap * scale : 0.0f };
        const float x_label_height { line_height + x_label_gap * scale };
        const bool legend_above { legend_shown && config.legend == chart_legend::top };
        const bool legend_below { legend_shown && config.legend == chart_legend::bottom };
        // 맨 위 눈금 글은 눈금 선에 세로 가운데라 글 높이의 절반이 플롯 위로 나간다.
        const float top { std::round(box.y + line_height / 2.0f + (legend_above ? legend_height : 0.0f)) };
        const float bottom { std::round(box.y + box.height - x_label_height - (legend_below ? legend_height : 0.0f)) };
        const float plot_height { std::max(0.0f, bottom - top) };
        legend_top = legend_above ? box.y : box.y + box.height - line_height;

        // 높이를 먼저 정해 눈금을 고르고, 그 눈금 글의 폭으로 왼쪽을 뺀다. 눈금 수는 폭에 달리지 않아 순환이 없다.
        const float tick_spacing { line_height * tick_spacing_lines };
        ticks = nice_ticks(value_range, tick_spacing > 0.0f ? static_cast<std::size_t>(plot_height / tick_spacing) : default_tick_target);
        axis = { ticks.first, tick_value(ticks.count - 1u) };
        tick_labels.clear();
        tick_labels.reserve(ticks.count);
        tick_label_width = 0.0f;
        for (std::size_t index { 0 }; index < ticks.count; ++index)
        {
            tick_labels.push_back(tick_text(tick_value(index)));
            tick_label_width = std::max(tick_label_width, measure.text_width(tick_labels.back(), font_size) * scale);
        }
        tick_label_width = std::min(tick_label_width, box.width * tick_label_width_limit);

        const float left { std::round(box.x + tick_label_width + tick_label_gap * scale) };
        const float right { std::round(box.x + box.width - plot_right_inset * scale) };
        plot = { left, top, std::max(0.0f, right - left), plot_height };
        x_label_top = plot.y + plot.height + x_label_gap * scale;

        legend_widths.clear();
        if (legend_shown)
        {
            legend_widths.reserve(config.series.size());
            for (const chart_series& series : config.series)
                legend_widths.push_back(measure.text_width(series.name, font_size) * scale);
        }

        const float spacing { categories > 1u ? plot.width / static_cast<float>(bands() ? categories : categories - 1u) : plot.width };
        x_label_stride = chart_layout::label_stride(categories, spacing, x_label_spacing * scale
            , [this, &measure, font_size](const std::size_t category) { return measure.text_width(category_text(category), font_size) * scale; }
        );

        const auto pixel_columns { static_cast<std::size_t>(plot.width) };
        const bool now_large { pixel_columns > 0u && categories > pixel_columns };
        // 큰 자료도 값을 훑는 일은 폭이 바뀔 때 한 번이다. 그리기와 hover는 열 표만 읽는다.
        if (now_large
            && (large == false || columns.empty() || columns.front().size() != pixel_columns))
        {
            columns.clear();
            columns.reserve(config.series.size());
            for (std::size_t series { 0 }; series < config.series.size(); ++series)
                columns.push_back(chart_layout::reduce_to_columns(values_of(series), categories, bands(), pixel_columns));
        }
        large = now_large;
    }

    chart_element::state::series_look chart_element::state::look_of(const draw_context& context, const std::size_t series) const noexcept
    {
        constexpr std::array<chart_marker, line_style_count> marker_order { chart_marker::circle, chart_marker::square, chart_marker::triangle, chart_marker::diamond };
        const chart_series& source { config.series[series] };
        const bool high_contrast { context.theme == color_theme::high_contrast };
        const chart_marker automatic { marker_order[series % marker_order.size()] };

        series_look look {};
        // 비활성이 고대비보다 먼저다. 고대비의 비활성을 활성과 같은 색으로 그리면 눌리지 않는 차트가 눌리는 것처럼 보인다.
        //  - 고대비 팔레트에도 비활성 색(COLOR_GRAYTEXT)이 있다. 계열은 색이 아니라 선 모양과 표식으로 가르므로 그대로 갈린다.
        if (owner.enabled() == false)
            look.color = context.palette.disabled_foreground;
        else if (high_contrast)
            look.color = context.palette.primary_foreground;
        else
            look.color = source.color.value_or(chart_layout::series_table_color(is_dark_background(context.palette.window_background), series));
        look.line_style = high_contrast ? series : 0u;
        look.point_marker = source.marker == chart_marker::automatic ? (high_contrast ? automatic : chart_marker::none) : source.marker;
        look.selected_marker = source.marker == chart_marker::automatic ? automatic : source.marker;
        look.filled_bar = high_contrast == false || first_bar == series;
        return look;
    }

    chart_element::state::traced_series chart_element::state::trace(const std::size_t series) const
    {
        SkPathBuilder line {};
        SkPathBuilder fill {};
        traced_series traced {};
        const float baseline { baseline_y() };
        const bool filled { config.series[series].kind == chart_series_kind::area && large == false };
        bool open { false };
        std::size_t run_points { 0 };
        std::size_t chunk_points { 0 };
        point_f last {};
        const auto add = [&](const float x, const float y) {
            if (open == false)
            {
                line.moveTo(x, y);
                if (filled)
                    fill.moveTo(x, baseline);
                open = true;
                run_points = 0u;
            }
            else
            {
                line.lineTo(x, y);
                // 앞 조각의 끝에서 이어 긋는다. 불투명한 계열은 겹친 자리가 보이지 않는다.
                if (large
                    && ++chunk_points >= large_line_chunk_points)
                {
                    traced.lines.push_back(line.detach());
                    line.moveTo(x, y);
                    chunk_points = 0u;
                }
            }
            if (filled)
                fill.lineTo(x, y);
            ++run_points;
            last = { x, y };
        };
        const auto cut = [&]() {
            if (open == false)
                return;
            if (run_points == 1u)
                traced.dots.push_back(last);
            if (filled)
            {
                fill.lineTo(last.x, baseline);
                fill.close();
            }
            open = false;
        };

        if (large)
        {
            const std::vector<chart_layout::value_column>& source { columns[series] };
            for (std::size_t index { 0 }; index < source.size(); ++index)
            {
                const chart_layout::value_column& column { source[index] };
                if (column.has_value == false)
                {
                    cut();
                    continue;
                }

                const float x { plot.x + static_cast<float>(index) + 0.5f };
                add(x, value_y(column.low_first ? column.low : column.high));
                if (column.low != column.high)
                    add(x, value_y(column.low_first ? column.high : column.low));
            }
        }
        else
        {
            const std::span<const double> values { values_of(series) };
            for (std::size_t category { 0 }; category < values.size(); ++category)
            {
                if (std::isfinite(values[category]) == false)
                {
                    cut();
                    continue;
                }

                add(category_x(category), value_y(values[category]));
            }
        }
        cut();
        traced.lines.push_back(line.detach());
        traced.fill = fill.detach();
        return traced;
    }

    void chart_element::state::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { owner.bounds() };
        if (box.width <= 0.0f
            || box.height <= 0.0f)
            return;

        const SkAutoCanvasRestore restore { &context.canvas, true };
        context.canvas.clipRect(sk_rect(box));
        draw_grid_and_labels(context);
        if (plot.width > 0.0f
            && plot.height > 0.0f)
        {
            if (has_points())
            {
                const SkAutoCanvasRestore plot_clip { &context.canvas, true };
                // 끝 범주의 선 굵기와 표식이 플롯 가장자리에서 잘리지 않게 그만큼 넓혀 자른다.
                const float margin { selected_marker_radius * scale };
                context.canvas.clipRect(SkRect::MakeLTRB(plot.x - margin, plot.y - margin, plot.x + plot.width + margin, plot.y + plot.height + margin));
                draw_series(context);
                draw_axes(context);
                draw_markers(context);
            }
            else
            {
                draw_axes(context);
                draw_text_block(context, chart_text::chart_empty(strings), plot, block_style(context.metrics.small_font_size, text_align::center), solid_paint(context.palette.secondary_foreground));
            }
        }
        draw_legend(context);
        draw_value_box(context, interaction);
    }

    void chart_element::state::draw_grid_and_labels(draw_context& context) const
    {
        const rect_f box { owner.bounds() };
        const float thickness { std::max(1.0f, std::round(rule_width * scale)) };
        const SkPaint grid_paint { crisp_paint(context.palette.divider) };
        const SkPaint text_paint { solid_paint(context.palette.secondary_foreground) };
        const float font_size { context.metrics.small_font_size };
        const float slack { text_box_slack * scale };

        const text_block_style tick_style { block_style(font_size, text_align::end) };
        for (std::size_t index { 0 }; index < tick_labels.size(); ++index)
        {
            const float y { std::round(value_y(tick_value(index))) };
            if (config.grid
                && plot.height > 0.0f)
                context.canvas.drawRect(SkRect::MakeXYWH(plot.x, y, plot.width, thickness), grid_paint);
            draw_text_block(context, tick_labels[index], { box.x - slack, y - line_height / 2.0f, tick_label_width + slack, line_height }, tick_style, text_paint);
        }

        if (categories == 0u
            || plot.width <= 0.0f)
            return;
        const text_block_style label_style { block_style(font_size, text_align::center) };
        const SkFont font { make_text_font(context, label_style.font) };
        for (std::size_t category { 0 }; category < categories; category += x_label_stride)
        {
            const std::u8string text { category_text(category) };
            const float width { measure_text(text, font) + slack };
            // 끝 범주의 이름표가 요소 밖으로 잘리지 않게 안으로 민다.
            const float left { std::clamp(category_x(category) - width / 2.0f, box.x, std::max(box.x, box.x + box.width - width)) };
            draw_text_block(context, text, { left, x_label_top, width, line_height }, label_style, text_paint);
        }
    }

    void chart_element::state::draw_axes(const draw_context& context) const
    {
        const float thickness { std::max(1.0f, std::round(rule_width * scale)) };
        const SkPaint paint { crisp_paint(context.palette.control_border) };
        context.canvas.drawRect(SkRect::MakeXYWH(plot.x, plot.y, thickness, plot.height), paint);
        context.canvas.drawRect(SkRect::MakeXYWH(plot.x, std::round(baseline_y()), plot.width, thickness), paint);
    }

    void chart_element::state::draw_series(const draw_context& context) const
    {
        // 막대를 맨 아래에 둔다. 넓은 면이 선을 가리면 선 계열을 읽을 수 없다.
        for (std::size_t series { 0 }; series < config.series.size(); ++series)
        {
            if (config.series[series].kind == chart_series_kind::bar)
                draw_bars(context, series);
        }
        for (const chart_series_kind kind : { chart_series_kind::area, chart_series_kind::line })
        {
            for (std::size_t series { 0 }; series < config.series.size(); ++series)
            {
                if (config.series[series].kind == kind)
                    draw_line(context, series);
            }
        }
    }

    void chart_element::state::draw_bars(const draw_context& context, const std::size_t series) const
    {
        const series_look look { look_of(context, series) };
        const SkPaint fill { crisp_paint(look.color) };
        const SkPaint outline { stroke_paint(look.color, rule_width, look.line_style, scale) };
        const SkPaint& paint { look.filled_bar ? fill : outline };
        const float baseline { baseline_y() };

        if (large)
        {
            // 열 하나에 막대 하나다. 나란히 세울 폭이 없어 계열은 같은 열에 겹친다.
            const std::vector<chart_layout::value_column>& source { columns[series] };
            for (std::size_t index { 0 }; index < source.size(); ++index)
            {
                if (source[index].has_value == false)
                    continue;
                const float y { value_y(source[index].strongest) };
                context.canvas.drawRect(SkRect::MakeLTRB(plot.x + static_cast<float>(index), std::min(y, baseline), plot.x + static_cast<float>(index) + 1.0f, std::max(y, baseline)), paint);
            }
            return;
        }

        const float width { std::max(1.0f, bar_width()) };
        const std::span<const double> values { values_of(series) };
        for (std::size_t category { 0 }; category < values.size(); ++category)
        {
            if (std::isfinite(values[category]) == false)
                continue;
            const float x { point_x(series, category) };
            const float y { value_y(values[category]) };
            context.canvas.drawRect(SkRect::MakeLTRB(x - width / 2.0f, std::min(y, baseline), x + width / 2.0f, std::max(y, baseline)), paint);
        }
    }

    void chart_element::state::draw_line(const draw_context& context, const std::size_t series) const
    {
        const series_look look { look_of(context, series) };
        const traced_series traced { trace(series) };
        if (config.series[series].kind == chart_series_kind::area)
            draw_area_fill(context, series, traced, with_alpha(look.color, area_fill_alpha));
        const SkPaint stroke { stroke_paint(look.color, line_width, look.line_style, scale) };
        for (const SkPath& line : traced.lines)
            context.canvas.drawPath(line, stroke);
        const SkPaint dot { solid_paint(look.color) };
        for (const point_f& point : traced.dots)
            context.canvas.drawCircle(point.x, point.y, line_width * scale, dot);
    }

    void chart_element::state::draw_area_fill(const draw_context& context, const std::size_t series, const traced_series& traced, const ui_color color) const
    {
        if (large == false)
        {
            context.canvas.drawPath(traced.fill, solid_paint(color));
            return;
        }

        // 큰 자료의 면은 경로 대신 열마다 한 픽셀 폭으로 칠한다. 열이 픽셀에 맞아 이음이 없고, 값이 흔들려도 비용이 폭에만 달린다.
        const SkPaint paint { crisp_paint(color) };
        const float baseline { baseline_y() };
        const std::vector<chart_layout::value_column>& source { columns[series] };
        for (std::size_t index { 0 }; index < source.size(); ++index)
        {
            if (source[index].has_value == false)
                continue;
            const float left { plot.x + static_cast<float>(index) };
            context.canvas.drawRect(SkRect::MakeLTRB(left, std::min(value_y(source[index].high), baseline), left + 1.0f, std::max(value_y(source[index].low), baseline)), paint);
        }
    }

    void chart_element::state::draw_markers(const draw_context& context) const
    {
        const float spacing { categories > 1u ? plot.width / static_cast<float>(bands() ? categories : categories - 1u) : plot.width };
        if (large == false
            && spacing >= marker_minimum_spacing * scale)
        {
            for (std::size_t series { 0 }; series < config.series.size(); ++series)
            {
                const series_look look { look_of(context, series) };
                if (look.point_marker == chart_marker::none)
                    continue;
                const SkPaint paint { solid_paint(look.color) };
                const std::span<const double> values { values_of(series) };
                for (std::size_t category { 0 }; category < values.size(); ++category)
                {
                    if (std::isfinite(values[category]))
                        draw_marker_shape(context.canvas, look.point_marker, point_x(series, category), value_y(values[category]), marker_radius * scale, paint);
                }
            }
        }

        const std::optional<chart_point> selected { current_point() };
        if (selected.has_value() == false
            || std::isfinite(value_at(selected->series, selected->category)) == false)
            return;
        const series_look look { look_of(context, selected->series) };
        if (look.selected_marker == chart_marker::none)
            return;
        // 창 바탕색 고리로 선과 막대에서 띄워 어느 계열 위에서든 표식이 보인다.
        const float x { point_x(selected->series, selected->category) };
        const float y { value_y(value_at(selected->series, selected->category)) };
        draw_marker_shape(context.canvas, look.selected_marker, x, y, (selected_marker_radius + selected_marker_ring) * scale, solid_paint(context.palette.window_background));
        draw_marker_shape(context.canvas, look.selected_marker, x, y, selected_marker_radius * scale, solid_paint(look.color));
    }

    void chart_element::state::draw_legend(draw_context& context) const
    {
        if (legend_shown == false)
            return;

        const rect_f box { owner.bounds() };
        const SkAutoCanvasRestore restore { &context.canvas, true };
        // 범례는 한 줄이다. 넘치는 계열은 잘린다. 줄이 넘칠 만큼 많은 계열은 앱이 범례를 끄고 이름을 따로 보인다.
        context.canvas.clipRect(SkRect::MakeXYWH(box.x, legend_top, box.width, line_height));
        const text_block_style style { block_style(context.metrics.small_font_size) };
        const SkPaint paint { solid_paint(context.palette.secondary_foreground) };
        float x { plot.x };
        for (std::size_t series { 0 }; series < config.series.size(); ++series)
        {
            draw_legend_swatch(context, series, { x, legend_top, legend_swatch_width * scale, line_height });
            x += (legend_swatch_width + legend_swatch_gap) * scale;
            draw_text_block(context, config.series[series].name, { x, legend_top, legend_widths[series] + text_box_slack * scale, line_height }, style, paint);
            x += legend_widths[series] + legend_item_gap * scale;
        }
    }

    void chart_element::state::draw_legend_swatch(const draw_context& context, const std::size_t series, const rect_f& swatch) const
    {
        const series_look look { look_of(context, series) };
        const chart_series_kind kind { config.series[series].kind };
        const float middle { swatch.y + swatch.height / 2.0f };
        if (kind == chart_series_kind::bar)
        {
            const float half { 5.0f * scale };
            const SkRect square { SkRect::MakeLTRB(swatch.x + swatch.width / 2.0f - half, middle - half, swatch.x + swatch.width / 2.0f + half, middle + half) };
            context.canvas.drawRect(square, look.filled_bar ? crisp_paint(look.color) : stroke_paint(look.color, rule_width, look.line_style, scale));
            return;
        }

        if (kind == chart_series_kind::area)
            context.canvas.drawRect(SkRect::MakeLTRB(swatch.x, middle, swatch.x + swatch.width, middle + 5.0f * scale), crisp_paint(with_alpha(look.color, area_fill_alpha)));
        SkPathBuilder path {};
        path.moveTo(swatch.x, middle);
        path.lineTo(swatch.x + swatch.width, middle);
        context.canvas.drawPath(path.detach(), stroke_paint(look.color, line_width, look.line_style, scale));
        draw_marker_shape(context.canvas, look.point_marker, swatch.x + swatch.width / 2.0f, middle, marker_radius * scale, solid_paint(look.color));
    }

    void chart_element::state::draw_value_box(draw_context& context, const interaction_snapshot& interaction) const
    {
        const std::optional<std::size_t> category { shown_category(interaction) };
        if (category.has_value() == false)
            return;

        const rect_f box { owner.bounds() };
        const text_block_style start_style { block_style(context.metrics.small_font_size) };
        const text_block_style end_style { block_style(context.metrics.small_font_size, text_align::end) };
        const SkFont font { make_text_font(context, start_style.font) };
        const float row_height { text_block_line_height(start_style) * scale };
        const float padding { value_box_padding * scale };
        const float marker_slot { value_box_marker_slot * scale };
        const float column_gap { value_box_column_gap * scale };
        const float slack { text_box_slack * scale };

        // 요소에 들어가는 줄까지만 싣는다. 값 상자는 요소 밖으로 나가지 않는다.
        const float room { row_height > 0.0f ? (box.height - 2.0f * padding) / row_height - 1.0f : 0.0f };
        const std::size_t row_count { std::min(config.series.size(), room > 0.0f ? static_cast<std::size_t>(room) : std::size_t { 0 }) };

        struct row
        {
            std::u8string value {};
            float name_width { 0.0f };
            float value_width { 0.0f };
        };
        std::vector<row> rows {};
        rows.reserve(row_count);
        const std::u8string title { category_text(*category) };
        float content_width { measure_text(title, font) };
        for (std::size_t series { 0 }; series < row_count; ++series)
        {
            const double value { value_at(series, *category) };
            row line {};
            line.value = std::isfinite(value) ? value_text(value) : std::u8string { chart_text::chart_missing_value(strings) };
            line.name_width = measure_text(config.series[series].name, font);
            line.value_width = measure_text(line.value, font);
            content_width = std::max(content_width, marker_slot + line.name_width + column_gap + line.value_width);
            rows.push_back(std::move(line));
        }

        const float width { std::min(content_width + 2.0f * padding + slack, box.width) };
        const float height { std::min(static_cast<float>(row_count + 1u) * row_height + 2.0f * padding, box.height) };
        // 고른 점이면 그 점, 마우스가 머문 범주면 가장 높은 점 위에 띄워 상자가 그 범주의 점을 가리지 않는다.
        const std::optional<chart_point> selected { current_point() };
        chart_point anchor_point { selected.value_or(chart_point { nearest_series(*category, plot.y), *category }) };
        const field_popup_anchor anchor { marker_bounds(anchor_point), box, value_box_gap };
        const field_popup_side_position position { place_field_popup_at_side(anchor, { width / scale, height / scale }, scale, field_popup_side::above) };
        const rect_f body { position.x * scale, position.y * scale, width, height };

        const float radius { context.metrics.control_corner_radius * scale };
        const SkRRect shape { SkRRect::MakeRectXY(sk_rect(body), radius, radius) };
        context.canvas.drawRRect(shape, solid_paint(context.palette.tooltip_background));
        context.canvas.drawRRect(shape, stroke_paint(context.palette.tooltip_border, rule_width, 0u, scale));

        const SkAutoCanvasRestore restore { &context.canvas, true };
        context.canvas.clipRect(sk_rect(body));
        const SkPaint text_paint { solid_paint(context.palette.primary_foreground) };
        const float left { body.x + padding };
        const float inner_width { std::max(0.0f, body.width - 2.0f * padding) };
        float y { body.y + padding };
        draw_text_block(context, title, { left, y, inner_width, row_height }, start_style, text_paint);
        for (std::size_t series { 0 }; series < rows.size(); ++series)
        {
            y += row_height;
            const series_look look { look_of(context, series) };
            const chart_marker marker { look.selected_marker == chart_marker::none ? chart_marker::square : look.selected_marker };
            draw_marker_shape(context.canvas, marker, left + marker_radius * scale, y + row_height / 2.0f, marker_radius * scale, solid_paint(look.color));
            draw_text_block(context, config.series[series].name, { left + marker_slot, y, rows[series].name_width + slack, row_height }, start_style, text_paint);
            draw_text_block(context, rows[series].value, { left, y, inner_width, row_height }, end_style, text_paint);
        }
    }

    std::u8string chart_element::state::summary_text() const
    {
        if (config.access_summary != nullptr)
        {
            if (std::u8string text { config.access_summary(config) }; text.empty() == false)
                return text;
        }
        return chart_text::chart_summary(strings, config.series.size(), categories
            , data.has_value() ? value_text(data->minimum) : std::u8string {}
            , data.has_value() ? value_text(data->maximum) : std::u8string {}
        );
    }

    std::u8string chart_element::state::point_text(const chart_point point) const
    {
        if (config.access_point != nullptr)
        {
            if (std::u8string text { config.access_point(config, point) }; text.empty() == false)
                return text;
        }
        const double value { value_at(point.series, point.category) };
        const std::u8string value_glyphs { std::isfinite(value) ? value_text(value) : std::u8string { chart_text::chart_missing_value(strings) } };
        // 계열이 하나면 이름이 군말이다.
        const std::u8string_view name { config.series.size() > 1u ? std::u8string_view { config.series[point.series].name } : std::u8string_view {} };
        return chart_text::chart_point_text(strings, name, category_text(point.category), value_glyphs);
    }

    std::optional<std::size_t> chart_element::state::shown_category(const interaction_snapshot& interaction) const
    {
        if (has_points() == false)
            return std::nullopt;
        if (const std::optional<chart_point> selected { current_point() }; selected.has_value())
            return selected->category;
        if (interaction.hovered.kind != ui_element_kind::chart_cell
            || owner.children().empty())
            return std::nullopt;

        const std::span<const std::unique_ptr<ui_element>> parts { owner.children().front()->children() };
        for (std::size_t index { 0 }; index < parts.size(); ++index)
        {
            if (parts[index]->id() == interaction.hovered)
                return chart_layout::category_of_cell(index, parts.size(), categories, bands());
        }
        return std::nullopt;
    }

    rect_f chart_element::state::marker_bounds(const chart_point point) const noexcept
    {
        const double value { value_at(point.series, point.category) };
        const float x { point_x(point.series, point.category) };
        const float y { std::isfinite(value) ? std::clamp(value_y(value), plot.y, plot.y + plot.height) : plot.y + plot.height / 2.0f };
        const float radius { selected_marker_radius * scale };
        return { x - radius, y - radius, 2.0f * radius, 2.0f * radius };
    }

    std::size_t chart_element::state::nearest_series(const std::size_t category, const float y) const noexcept
    {
        std::size_t nearest { 0 };
        float distance { std::numeric_limits<float>::infinity() };
        for (std::size_t series { 0 }; series < config.series.size(); ++series)
        {
            const double value { value_at(series, category) };
            if (std::isfinite(value) == false)
                continue;
            const float gap { std::abs(value_y(value) - y) };
            if (gap < distance)
            {
                distance = gap;
                nearest = series;
            }
        }
        return nearest;
    }

    chart_point chart_element::state::point_at(const float x, const float y) const noexcept
    {
        std::size_t cell { 0 };
        if (cells > 0u
            && plot.width > 0.0f)
        {
            const float position { std::floor((x - plot.x) / plot.width * static_cast<float>(cells)) };
            // 플롯 밖의 끌기와 NaN은 가까운 끝이다. 부동소수를 정수로 바꾸기 전에 거른다.
            if (position >= static_cast<float>(cells))
                cell = cells - 1u;
            else if (position > 0.0f)
                cell = static_cast<std::size_t>(position);
        }
        const std::size_t category { chart_layout::category_of_cell(cell, cells, categories, bands()) };
        return { nearest_series(category, y), category };
    }

    std::vector<input_action> chart_element::state::emit(const chart_point point) const
    {
        if (config.focus == nullptr
            || owner.enabled() == false
            || current_point() == point)
            return {};
        return { config.focus(point) };
    }

    std::vector<input_action> chart_element::state::step_category(const value_step step) const
    {
        if (has_points() == false)
            return {};
        const std::optional<chart_point> selected { current_point() };
        // 고른 점이 없으면 어느 키든 첫 점에서 시작한다.
        if (selected.has_value() == false)
            return emit({});

        const std::size_t page { std::max<std::size_t>(categories / 10u, 1u) };
        const std::size_t last { categories - 1u };
        const std::size_t current { selected->category };
        std::size_t target { current };
        switch (step)
        {
        case value_step::decrease:
            target = current > 0u ? current - 1u : 0u;
            break;
        case value_step::increase:
            target = std::min(current + 1u, last);
            break;
        case value_step::decrease_page:
            target = current > page ? current - page : 0u;
            break;
        case value_step::increase_page:
            target = std::min(current + page, last);
            break;
        case value_step::minimum:
            target = 0u;
            break;
        case value_step::maximum:
            target = last;
            break;
        }
        return emit({ selected->series, target });
    }

    std::vector<input_action> chart_element::state::step_series(const bool next) const
    {
        if (has_points() == false)
            return {};
        const std::optional<chart_point> selected { current_point() };
        if (selected.has_value() == false)
            return emit({});
        if (next)
            return emit({ std::min(selected->series + 1u, config.series.size() - 1u), selected->category });
        return emit({ selected->series > 0u ? selected->series - 1u : 0u, selected->category });
    }

    // 플롯이다. Tab 자리와 키를 맡고 칸을 품는다. 그림은 차트가 함께 그린다.
    class chart_element::plot_part final : public ui_element
    {
    public:
        explicit plot_part(state& shared)
            : ui_element { ui_element_id { ui_element_kind::chart_plot, shared.config.owner } }
            , state_ { shared }
        {
            // 그림만 내는 차트에는 Tab 자리도 키 역할도 없다. 없는 것은 두지 않는다.
            if (state_.config.focus == nullptr)
                return;

            set_tab_stop(true);
            // 가로 걸음이 범주이고 ↑↓는 계열이다. 색 고르기의 두 축과 같은 조합이다. Ctrl·Alt를 든 키는 앱 단축키로 흐른다.
            //  - 끝에 닿은 키는 빈 목록으로 삼킨다. 흘려보내면 끝의 Home이 앱 단축키로 새어 화면이 함께 맨 위로 뛴다.
            key_step_target steps {};
            steps.axis = focus_axis::horizontal;
            steps.on_step = [this](const value_step step) -> std::optional<std::vector<input_action>> { return state_.step_category(step); };
            set_key_step_target(std::move(steps));
            set_key_handler([this](const key_pressed_event& event) -> std::optional<std::vector<input_action>> {
                if (event.shortcut_modifier_down()
                    || (event.key != key_code::arrow_up && event.key != key_code::arrow_down))
                    return std::nullopt;
                return state_.step_series(event.key == key_code::arrow_down);
            });
        }

        void arrange(const arrange_context& context) override
        {
            set_bounds(context.slot);
            const rect_f slot { context.slot };
            // 칸은 **폭이 있는 첫 배치**에서 한 번만 짓는다.
            //  - 자식은 tree 하나에 한 벌이라 두 번 지으면 같은 자리표가 둘 선다.
            //  - 폭이 0인 배치를 계기로 삼지 않는 이유는, 담는 쪽이 자리를 잡기 전에 한 번 0으로 배치해 볼 수 있어서다.
            //  - 창 크기가 바뀌면 앱이 tree를 다시 지으므로 칸 수도 새로 정해진다.
            if (cells_built_ == false
                && slot.width > 0.0f)
            {
                cells_built_ = true;
                build_cells(slot.width / valid_scale(context.scale));
            }

            const std::span<const std::unique_ptr<ui_element>> parts { children() };
            const float cell_width { parts.empty() ? 0.0f : slot.width / static_cast<float>(parts.size()) };
            for (std::size_t index { 0 }; index < parts.size(); ++index)
            {
                ui_element& cell { *parts[index] };
                cell.set_enabled(enabled());
                cell.arrange(context.for_child({ slot.x + static_cast<float>(index) * cell_width, slot.y, cell_width, slot.height }));
            }
        }

        void draw(draw_context&, const interaction_snapshot&) const override
        {
        }

        [[nodiscard]] access_info accessibility() const override
        {
            if (state_.config.focus == nullptr)
                return {};
            access_info info { .role = access_role::slider };
            info.name = chart_text::chart_plot_name(state_.strings);
            // 값이 비지 않아야 UIA Value 패턴이 읽기 전용으로 선다. 아직 고른 점이 없으면 읽을 값도 없다.
            if (const std::optional<chart_point> selected { state_.current_point() }; selected.has_value())
                info.value = state_.point_text(*selected);
            return info;
        }

        [[nodiscard]] std::optional<rect_f> focus_ring_bounds() const override
        {
            const std::optional<chart_point> selected { state_.current_point() };
            if (selected.has_value() == false)
                return std::nullopt;
            return state_.marker_bounds(*selected);
        }

    private:
        void build_cells(const float logical_width)
        {
            const std::size_t count { chart_layout::cell_count(state_.categories, logical_width) };
            state_.cells = count;
            const bool pickable { state_.config.focus != nullptr };
            for (std::size_t index { 0 }; index < count; ++index)
            {
                auto cell { std::make_unique<chart_cell>(ui_element_id { ui_element_kind::chart_cell, cell_owner(state_.config.owner, index + 1u) }) };
                // 칸은 Tab 자리가 아니다. 자리는 플롯 하나이고, 눌러서 얻는 초점도 플롯으로 보낸다 (칸을 누른 뒤 곧바로 화살표가 먹힌다).
                cell->set_tab_stop(false);
                if (pickable)
                {
                    const std::size_t category { chart_layout::category_of_cell(index, count, state_.categories, state_.bands()) };
                    cell->set_action(ui_trigger::left_click, [this, category](const ui_action_context& context) -> std::vector<input_action> {
                        return state_.emit({ state_.nearest_series(category, context.y), category });
                    });

                    // 시작 거리를 둔 손잡이다. 누르고 떼면 클릭이고, 거리를 넘기면 끌기다. 터치는 탭이 클릭이고 축에 맞는 빠른 쓸기는
                    // 감싼 창의 스크롤이라 세로로 흘리는 페이지 안의 차트가 스크롤을 막지 않는다.
                    //  - 절대 메시지라 이동이 겹쳐도 쌓이지 않는다. 앞뒤 포인터의 점이 같으면 메시지가 없다.
                    pointer_drag_target drag {};
                    drag.start_distance = cell_drag_start_distance;
                    drag.on_press = [this](const ui_action_context& context) -> std::vector<input_action> { return state_.emit(state_.point_at(context.x, context.y)); };
                    drag.on_move = [this](const ui_action_context& previous, const ui_action_context& current) -> std::vector<input_action> {
                        const chart_point target { state_.point_at(current.x, current.y) };
                        if (target == state_.point_at(previous.x, previous.y))
                            return {};
                        return state_.emit(target);
                    };
                    cell->set_pointer_drag_target(std::move(drag));
                    cell->set_cursor(ui_cursor::hand);
                    cell->set_pointer_focus_target(id());
                }
                else
                {
                    // 커서가 있어야 hit 대상이 되고 그래야 마우스 hover가 칸에 서서 값 상자가 열린다. 그림만 내는 차트의 칸은 이것만 든다.
                    cell->set_cursor(ui_cursor::arrow);
                }
                add_child(std::move(cell));
            }
        }

        state& state_;
        bool cells_built_ { false };
    };

    chart_element::chart_element(chart_config config)
        : ui_element { ui_element_id { ui_element_kind::chart, config.owner } }
        , state_ { std::make_unique<state>(*this, std::move(config)) }
    {
        auto plot { std::make_unique<plot_part>(*state_) };
        plot_ = plot.get();
        add_child(std::move(plot));
    }

    chart_element::~chart_element() = default;

    void chart_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        state_->arrange(context);
        plot_->set_enabled(enabled());
        plot_->arrange(context.for_child(state_->plot));
    }

    void chart_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 플롯과 칸은 그리지 않아 자식을 돌지 않는다. 칸이 플롯 폭만큼 있어도 그리기 비용이 늘지 않는다.
        state_->draw(context, interaction);
    }

    access_info chart_element::accessibility() const
    {
        return {
            .role = access_role::image,
            .name = state_->config.name.empty() ? std::u8string { chart_text::chart_name(state_->strings) } : state_->config.name,
            .value = state_->summary_text(),
        };
    }

    rect_f chart_element::plot_bounds() const noexcept
    {
        return state_->plot;
    }
}
