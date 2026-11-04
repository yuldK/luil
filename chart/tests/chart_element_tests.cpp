#include "luil/chart/chart_element.h"

#include "chart_layout.h"
#include "raster_probe.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// 차트 요소다 (docs/chart-design.md의 확인 절). 배치와 접근은 요소를 직접 재고, 누름·끌기·키는 controller에 실제 이벤트를 넣는다.
// 그림은 raster로 재되 글은 비워 글꼴에 매이지 않는다.
namespace {
    constexpr double empty_point { std::numeric_limits<double>::quiet_NaN() };
    constexpr luil::rect_f wide_slot { 0.0f, 0.0f, 600.0f, 300.0f };
    constexpr luil::ui_color first_color { luil::make_ui_color(250, 30, 30) };
    constexpr luil::ui_color second_color { luil::make_ui_color(30, 200, 90) };

    struct picked
    {
        luil::chart_point point {};
    };

    [[nodiscard]] std::shared_ptr<const std::vector<double>> shared_values(const std::initializer_list<double> values)
    {
        return std::make_shared<const std::vector<double>>(values);
    }

    // 글자마다 글자 크기의 절반인 고정 폭 측정기다. core test에는 글꼴이 없다.
    const luil::text_measurer half_em_measurer { [](const std::u8string_view text, const float pixel_size) { return static_cast<float>(text.size()) * pixel_size * 0.5f; } };

    // 범주 넷에 선 계열 "매출"과 막대 계열 "비용"이 있는 차트다. 축은 0~200이라 값과 픽셀이 곧바로 옮겨진다.
    [[nodiscard]] luil::chart_config make_config(const std::optional<luil::chart_point> focused = std::nullopt)
    {
        luil::chart_config config {};
        config.owner = u8"sales";
        config.categories = { u8"1월", u8"2월", u8"3월", u8"4월" };
        config.series.push_back({ .name = u8"매출", .kind = luil::chart_series_kind::line, .values = shared_values({ 80.0, 100.0, 120.0, 160.0 }) });
        config.series.push_back({ .name = u8"비용", .kind = luil::chart_series_kind::bar, .values = shared_values({ 40.0, 60.0, empty_point, 90.0 }) });
        config.y_range = luil::chart_range { 0.0, 200.0 };
        config.focused = focused;
        config.focus = [](const luil::chart_point point) { return luil::make_app_action(picked { point }); };
        return config;
    }

    [[nodiscard]] std::unique_ptr<luil::chart_element> make_chart(luil::chart_config config, const luil::rect_f& slot = wide_slot, const float scale = 1.0f)
    {
        auto chart { std::make_unique<luil::chart_element>(std::move(config)) };
        chart->arrange({ slot, scale, 0.0f, {}, &half_em_measurer });
        return chart;
    }

    [[nodiscard]] const luil::ui_element& plot_of(const luil::chart_element& chart)
    {
        REQUIRE(chart.children().size() == 1u);
        return *chart.children().front();
    }

    [[nodiscard]] std::vector<luil::chart_point> points_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<luil::chart_point> points {};
        for (const luil::input_action& action : actions)
        {
            const auto* const message { std::get_if<luil::app_message>(&action) };
            REQUIRE(message != nullptr);
            if (const picked* const intent { message->get<picked>() }; intent != nullptr)
                points.push_back(intent->point);
        }
        return points;
    }

    [[nodiscard]] std::vector<luil::chart_point> stepped(const luil::chart_element& chart, const luil::value_step step)
    {
        const luil::key_step_target* const target { plot_of(chart).key_step() };
        REQUIRE(target != nullptr);
        const std::optional<std::vector<luil::input_action>> actions { target->on_step(step) };
        REQUIRE(actions.has_value());
        return points_of(*actions);
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] luil::pointer_pressed_event press(const float x, const float y, const int time)
    {
        return luil::pointer_pressed_event { x, y, luil::pointer_button::left, at(time) };
    }

    [[nodiscard]] luil::pointer_moved_event move(const float x, const float y, const int time)
    {
        return luil::pointer_moved_event { x, y, at(time) };
    }

    [[nodiscard]] luil::pointer_released_event release(const float x, const float y, const int time)
    {
        return luil::pointer_released_event { x, y, luil::pointer_button::left, at(time) };
    }

    [[nodiscard]] luil::key_pressed_event key(const luil::key_code code, const bool control = false)
    {
        return luil::key_pressed_event { code, control, false, false, false, at(0) };
    }

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> shared_tree(luil::chart_config config, const luil::rect_f& slot = wide_slot, const float scale = 1.0f)
    {
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::make_unique<luil::chart_element>(std::move(config)), slot, scale));
    }

    [[nodiscard]] const luil::chart_element& chart_in(const luil::ui_tree& tree)
    {
        const auto* const chart { dynamic_cast<const luil::chart_element*>(tree.find({ luil::ui_element_kind::chart, u8"sales" })) };
        REQUIRE(chart != nullptr);
        return *chart;
    }

    // 값의 y다. 축이 0~200인 차트에서만 맞는다.
    [[nodiscard]] float y_of(const luil::rect_f& plot, const double value)
    {
        return plot.y + plot.height * (1.0f - static_cast<float>(value / 200.0));
    }

    [[nodiscard]] float contrast(const luil::ui_color first, const luil::ui_color second)
    {
        const float lighter { std::max(luil::relative_luminance(first), luil::relative_luminance(second)) };
        const float darker { std::min(luil::relative_luminance(first), luil::relative_luminance(second)) };
        return (lighter + 0.05f) / (darker + 0.05f);
    }

    // 글을 모두 비운 설정이다. 글꼴 없이 도형만 그려진다.
    [[nodiscard]] luil::chart_config silent_config(const std::size_t categories)
    {
        luil::chart_config config {};
        config.owner = u8"sales";
        config.categories.assign(categories, std::u8string {});
        config.y_range = luil::chart_range { 0.0, 200.0 };
        config.legend = luil::chart_legend::none;
        config.grid = false;
        config.format_value = [](const double) { return std::u8string {}; };
        return config;
    }

    [[nodiscard]] luil::ui_tree raster_tree(luil::chart_config config, const luil::rect_f& slot)
    {
        return luil::make_arranged_tree(std::make_unique<luil::chart_element>(std::move(config)), slot, 2.0f);
    }

    // 비활성 차트다. 부품이 배치에서 비활성을 물려받으므로 배치 전에 끈다.
    [[nodiscard]] luil::ui_tree disabled_raster_tree(luil::chart_config config, const luil::rect_f& slot)
    {
        auto chart { std::make_unique<luil::chart_element>(std::move(config)) };
        chart->set_enabled(false);
        return luil::make_arranged_tree(std::move(chart), slot, 2.0f);
    }

    // 밝은·어두운 팔레트의 비활성 색은 알파가 있어 바탕과 섞여 찍힌다. 반올림 오차(채널당 둘)까지 같은 색으로 본다.
    [[nodiscard]] bool is_disabled_color(const luil::ui_color pixel, const luil::ui_color_palette& palette) noexcept
    {
        const std::uint32_t alpha { palette.disabled_foreground >> 24U };
        for (const std::uint32_t shift : { 0U, 8U, 16U })
        {
            const std::uint32_t top { (palette.disabled_foreground >> shift) & 0xFFU };
            const std::uint32_t bottom { (palette.window_background >> shift) & 0xFFU };
            const int expected { static_cast<int>((top * alpha + bottom * (255U - alpha) + 127U) / 255U) };
            const int actual { static_cast<int>((pixel >> shift) & 0xFFU) };
            if (actual - expected > 2 || expected - actual > 2)
                return false;
        }
        return true;
    }

    [[nodiscard]] luil::chart_series silent_series(const luil::chart_series_kind kind, const std::initializer_list<double> values, const luil::ui_color color)
    {
        luil::chart_series series {};
        series.kind = kind;
        series.values = shared_values(values);
        series.color = color;
        return series;
    }

    // 세로 한 줄에서 그 색인 픽셀 수다.
    [[nodiscard]] int column_height(const luil::testing::raster_frame& frame, const float x, const luil::ui_color color, const int height)
    {
        return frame.count_color({ std::floor(x), 0.0f, 1.0f, static_cast<float>(height) }, color);
    }
}

TEST_CASE("The plot leaves the widest tick label on the left at every scale", "[chart]")
{
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        CAPTURE(scale);
        const luil::rect_f slot { 0.0f, 0.0f, 600.0f * scale, 300.0f * scale };
        luil::chart_config config { make_config() };
        const auto chart { make_chart(std::move(config), slot, scale) };
        const luil::rect_f plot { chart->plot_bounds() };

        // 눈금 글 "200"은 글자 셋에 글자 크기 11의 절반이고 그 뒤로 간격 6이다. 배율이 바뀌어도 논리 자리가 같다 (반올림 1 픽셀 안).
        REQUIRE(std::abs(plot.x / scale - (3.0f * 5.5f + 6.0f)) <= 1.0f / scale);
        REQUIRE(std::abs((plot.x + plot.width) / scale - (600.0f - 12.0f)) <= 1.0f / scale);
        // 범례 줄과 x 이름표 한 줄이 아래에 선다. 플롯은 바닥까지 닿지 않는다.
        REQUIRE(plot.y > 0.0f);
        REQUIRE(plot.y + plot.height < slot.height);
        REQUIRE(plot_of(*chart).bounds().x == plot.x);
        REQUIRE(plot_of(*chart).bounds().width == plot.width);
    }

    // 앱 함수의 글이 넓으면 왼쪽이 그만큼 비는 쪽으로 플롯이 줄어든다.
    luil::chart_config long_text { make_config() };
    long_text.format_value = [](const double) { return std::u8string { u8"1234567890" }; };
    REQUIRE(make_chart(std::move(long_text))->plot_bounds().x > make_chart(make_config())->plot_bounds().x);

    // 범례를 끄면 플롯이 그 줄만큼 높고, 위로 옮기면 아래가 그만큼 빈다.
    const float legend_bottom_height { make_chart(make_config())->plot_bounds().height };
    luil::chart_config no_legend { make_config() };
    no_legend.legend = luil::chart_legend::none;
    REQUIRE(make_chart(std::move(no_legend))->plot_bounds().height > legend_bottom_height);
    luil::chart_config legend_top { make_config() };
    legend_top.legend = luil::chart_legend::top;
    const auto top_chart { make_chart(std::move(legend_top)) };
    REQUIRE(top_chart->plot_bounds().height == legend_bottom_height);
    REQUIRE(top_chart->plot_bounds().y > make_chart(make_config())->plot_bounds().y);
}

TEST_CASE("The tick spacing stays at two and a half lines of text", "[chart]")
{
    // 칸 상한이 플롯 높이에서 나와 낮은 차트의 눈금이 듬성하다. 눈금 수는 눈금 글의 소수 자리로 함께 잰다.
    luil::chart_config config { make_config() };
    config.y_range = luil::chart_range { 0.0, 1.0 };
    const auto tall { make_chart(config, { 0.0f, 0.0f, 600.0f, 800.0f }) };
    const auto short_chart { make_chart(config, { 0.0f, 0.0f, 600.0f, 100.0f }) };
    // 높은 차트는 걸음 0.1이라 값 글이 두 자리이고 낮은 차트는 걸음 0.5라 값 글이 두 자리다. 그 사이를 재는 것은 nice_ticks의 test다.
    REQUIRE(tall->plot_bounds().height > short_chart->plot_bounds().height);
    // 플롯 높이가 0이어도 눈금은 서고 그림이 넘어지지 않는다.
    const auto flat { make_chart(config, { 0.0f, 0.0f, 600.0f, 10.0f }) };
    REQUIRE(flat->plot_bounds().height == 0.0f);
}

TEST_CASE("The x labels take the smallest stride that does not overlap", "[chart]")
{
    const auto twenty = [](const std::size_t) { return 20.0f; };
    // 폭 20에 간격 8이면 걸음이 28을 덮어야 한다.
    REQUIRE(luil::chart_layout::label_stride(12u, 30.0f, 8.0f, twenty) == 1u);
    REQUIRE(luil::chart_layout::label_stride(12u, 28.0f, 8.0f, twenty) == 1u);
    REQUIRE(luil::chart_layout::label_stride(12u, 14.0f, 8.0f, twenty) == 2u);
    REQUIRE(luil::chart_layout::label_stride(12u, 10.0f, 8.0f, twenty) == 3u);
    // 그려지지 않는 이름표는 걸음을 늘리지 않는다. 짝수 차례만 그리면 넓은 하나를 건너뛴다.
    const auto one_wide = [](const std::size_t category) { return category == 3u ? 200.0f : 20.0f; };
    REQUIRE(luil::chart_layout::label_stride(12u, 14.0f, 8.0f, one_wide) == 2u);
    // 범주가 하나거나 자리가 없으면 첫 이름표만 남는다.
    REQUIRE(luil::chart_layout::label_stride(1u, 0.0f, 8.0f, twenty) == 1u);
    REQUIRE(luil::chart_layout::label_stride(12u, 0.0f, 8.0f, twenty) == 12u);
    REQUIRE(luil::chart_layout::label_stride(3u, 1.0f, 8.0f, twenty) == 3u);

    // 아주 많은 범주는 재는 수가 범주 수를 따라 늘지 않는다. 그린 이름표는 겹치지 않는다.
    std::size_t measured { 0 };
    const auto count_width = [&measured](const std::size_t) {
        ++measured;
        return 40.0f;
    };
    const float spacing { 800.0f / 100000.0f };
    const std::size_t stride { luil::chart_layout::label_stride(100000u, spacing, 8.0f, count_width) };
    REQUIRE(static_cast<float>(stride) * spacing >= 48.0f);
    REQUIRE(measured < 2000u);
}

TEST_CASE("There is at most one cell per four logical pixels and each takes the nearest category", "[chart]")
{
    SECTION("Few categories get a cell each and the cells are contiguous")
    {
        const auto chart { make_chart(make_config()) };
        const luil::ui_element& plot { plot_of(*chart) };
        REQUIRE(plot.children().size() == 4u);
        float x { plot.bounds().x };
        for (const std::unique_ptr<luil::ui_element>& cell : plot.children())
        {
            REQUIRE(cell->id().kind == luil::ui_element_kind::chart_cell);
            REQUIRE(std::abs(cell->bounds().x - x) < 0.01f);
            REQUIRE(cell->bounds().height == plot.bounds().height);
            x += cell->bounds().width;
        }
        REQUIRE(std::abs(x - (plot.bounds().x + plot.bounds().width)) < 0.01f);
        for (std::size_t cell { 0 }; cell < 4u; ++cell)
        {
            REQUIRE(luil::chart_layout::category_of_cell(cell, 4u, 4u, true) == cell);
            REQUIRE(luil::chart_layout::category_of_cell(cell, 4u, 4u, false) == cell);
        }
    }

    SECTION("Many categories stop at the plot width")
    {
        luil::chart_config config { make_config() };
        config.series.front().values = std::make_shared<const std::vector<double>>(10000u, 50.0);
        config.series.pop_back();
        const auto chart { make_chart(std::move(config)) };
        const float width { chart->plot_bounds().width };
        const std::size_t cells { plot_of(*chart).children().size() };
        REQUIRE(cells == static_cast<std::size_t>(width / 4.0f));
        // 양 끝 칸이 양 끝 범주이고 뒤로 갈수록 범주가 줄지 않는다.
        REQUIRE(luil::chart_layout::category_of_cell(0u, cells, 10000u, false) < 10000u / cells);
        REQUIRE(luil::chart_layout::category_of_cell(cells - 1u, cells, 10000u, false) >= 10000u - 10000u / cells);
        std::size_t previous { 0 };
        for (std::size_t cell { 0 }; cell < cells; ++cell)
        {
            const std::size_t category { luil::chart_layout::category_of_cell(cell, cells, 10000u, false) };
            REQUIRE(category >= previous);
            previous = category;
        }
    }

    REQUIRE(luil::chart_layout::cell_count(0u, 400.0f) == 0u);
    REQUIRE(luil::chart_layout::cell_count(3u, 0.0f) == 0u);
    REQUIRE(luil::chart_layout::cell_count(3u, 2.0f) == 1u);
    REQUIRE(luil::chart_layout::cell_count(3u, std::numeric_limits<float>::quiet_NaN()) == 0u);
}

TEST_CASE("The cells are built once on the first arrange that has a width", "[chart]")
{
    auto chart { std::make_unique<luil::chart_element>(make_config()) };
    // 담는 쪽이 자리를 잡기 전에 0으로 배치해 볼 수 있다. 그때는 칸이 서지 않는다.
    chart->arrange({ { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f, {}, &half_em_measurer });
    REQUIRE(plot_of(*chart).children().empty());
    // 폭 있는 배치에서 칸이 한 벌 서고, 다시 배치해도 두 벌이 되지 않는다.
    chart->arrange({ wide_slot, 1.0f, 0.0f, {}, &half_em_measurer });
    REQUIRE(plot_of(*chart).children().size() == 4u);
    chart->arrange({ { 0.0f, 0.0f, 300.0f, 200.0f }, 1.0f, 0.0f, {}, &half_em_measurer });
    REQUIRE(plot_of(*chart).children().size() == 4u);
    // 칸은 새 플롯 폭을 따른다.
    const luil::rect_f plot { chart->plot_bounds() };
    const luil::ui_element& last { *plot_of(*chart).children().back() };
    REQUIRE(std::abs(last.bounds().x + last.bounds().width - (plot.x + plot.width)) < 0.01f);

    // 같은 id가 둘 서지 않아 tree가 받아 준다.
    const auto tree { shared_tree(make_config()) };
    REQUIRE(tree->find({ luil::ui_element_kind::chart_plot, u8"sales" }) != nullptr);
}

TEST_CASE("The keys walk the categories and the series", "[chart][focus]")
{
    const auto at_third { make_chart(make_config(luil::chart_point { 0u, 2u })) };
    REQUIRE(stepped(*at_third, luil::value_step::increase) == std::vector<luil::chart_point> { { 0u, 3u } });
    REQUIRE(stepped(*at_third, luil::value_step::decrease) == std::vector<luil::chart_point> { { 0u, 1u } });
    REQUIRE(stepped(*at_third, luil::value_step::minimum) == std::vector<luil::chart_point> { { 0u, 0u } });
    REQUIRE(stepped(*at_third, luil::value_step::maximum) == std::vector<luil::chart_point> { { 0u, 3u } });
    // 범주 넷의 10분의 1은 0이라 Page는 적어도 한 칸이다.
    REQUIRE(stepped(*at_third, luil::value_step::increase_page) == std::vector<luil::chart_point> { { 0u, 3u } });
    REQUIRE(stepped(*at_third, luil::value_step::decrease_page) == std::vector<luil::chart_point> { { 0u, 1u } });

    // 범주 100개면 Page가 열 칸이다. 범주를 옮겨도 계열은 그대로다.
    luil::chart_config many { make_config(luil::chart_point { 1u, 50u }) };
    many.series.back().values = std::make_shared<const std::vector<double>>(100u, 50.0);
    const auto hundred { make_chart(std::move(many)) };
    REQUIRE(stepped(*hundred, luil::value_step::increase_page) == std::vector<luil::chart_point> { { 1u, 60u } });
    REQUIRE(stepped(*hundred, luil::value_step::decrease_page) == std::vector<luil::chart_point> { { 1u, 40u } });

    // ↑·↓는 같은 범주에서 이전·다음 계열이다.
    const luil::key_handler* const keys { plot_of(*at_third).key_handler_of() };
    REQUIRE(keys != nullptr);
    REQUIRE(points_of(*(*keys)(key(luil::key_code::arrow_down))) == std::vector<luil::chart_point> { { 1u, 2u } });
    REQUIRE((*keys)(key(luil::key_code::arrow_up))->empty());
    // 화살표 밖의 키와 Ctrl을 든 키는 앱 단축키로 흐른다.
    REQUIRE((*keys)(key(luil::key_code::enter)).has_value() == false);
    REQUIRE((*keys)(key(luil::key_code::arrow_down, true)).has_value() == false);
}

TEST_CASE("A key at the end is swallowed and without a chosen point any key starts at the first point", "[chart][focus]")
{
    const auto first { make_chart(make_config(luil::chart_point { 0u, 0u })) };
    REQUIRE(stepped(*first, luil::value_step::decrease).empty());
    REQUIRE(stepped(*first, luil::value_step::decrease_page).empty());
    REQUIRE(stepped(*first, luil::value_step::minimum).empty());
    const auto last { make_chart(make_config(luil::chart_point { 1u, 3u })) };
    REQUIRE(stepped(*last, luil::value_step::increase).empty());
    REQUIRE(stepped(*last, luil::value_step::increase_page).empty());
    REQUIRE(stepped(*last, luil::value_step::maximum).empty());
    REQUIRE((*plot_of(*last).key_handler_of())(key(luil::key_code::arrow_down))->empty());

    const auto none { make_chart(make_config()) };
    for (const luil::value_step step : { luil::value_step::increase, luil::value_step::decrease, luil::value_step::minimum, luil::value_step::maximum, luil::value_step::increase_page })
        REQUIRE(stepped(*none, step) == std::vector<luil::chart_point> { { 0u, 0u } });
    REQUIRE(points_of(*(*plot_of(*none).key_handler_of())(key(luil::key_code::arrow_up))) == std::vector<luil::chart_point> { { 0u, 0u } });

    // 범위 밖의 고른 점은 고르지 않은 것과 같다. 자료가 줄어든 뒤에도 키가 멈추지 않는다.
    const auto stale { make_chart(make_config(luil::chart_point { 5u, 99u })) };
    REQUIRE(stepped(*stale, luil::value_step::increase) == std::vector<luil::chart_point> { { 0u, 0u } });
    REQUIRE(plot_of(*stale).accessibility().value.empty());

    // 자료가 없으면 키를 삼킨다.
    luil::chart_config empty {};
    empty.owner = u8"sales";
    empty.focus = [](const luil::chart_point point) { return luil::make_app_action(picked { point }); };
    REQUIRE(stepped(*make_chart(std::move(empty)), luil::value_step::increase).empty());
}

TEST_CASE("Pressing a cell picks the nearest series and sends the focus to the plot", "[chart][focus]")
{
    const auto tree { shared_tree(make_config()) };
    const luil::rect_f plot { chart_in(*tree).plot_bounds() };
    const luil::ui_element& plot_part { plot_of(chart_in(*tree)) };
    const luil::rect_f third { plot_part.children()[2]->bounds() };
    const luil::rect_f second { plot_part.children()[1]->bounds() };
    const float third_x { third.x + third.width / 2.0f };
    const float second_x { second.x + second.width / 2.0f };

    luil::interaction_controller controller {};
    controller.set_tree(tree);
    // 칸은 Tab 자리가 아니고 플롯이 하나의 자리다.
    REQUIRE(plot_part.children()[0]->tab_stop() == false);
    REQUIRE(plot_part.tab_stop());

    // 2월은 매출 100, 비용 60이다. 누른 y에 가까운 쪽이 고른 계열이다.
    REQUIRE(points_of(controller.process(press(second_x, y_of(plot, 100.0), 0))).empty());
    REQUIRE(points_of(controller.process(release(second_x, y_of(plot, 100.0), 10))) == std::vector<luil::chart_point> { { 0u, 1u } });
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::chart_plot, u8"sales" });
    static_cast<void>(controller.process(press(second_x, y_of(plot, 55.0), 500)));
    REQUIRE(points_of(controller.process(release(second_x, y_of(plot, 55.0), 510))) == std::vector<luil::chart_point> { { 1u, 1u } });
    // 3월의 비용은 빈 점이라 어느 y에서든 매출이다.
    static_cast<void>(controller.process(press(third_x, y_of(plot, 10.0), 1000)));
    REQUIRE(points_of(controller.process(release(third_x, y_of(plot, 10.0), 1010))) == std::vector<luil::chart_point> { { 0u, 2u } });

    // 누른 뒤 곧바로 화살표가 먹힌다. 고른 점은 앱 상태라 이 tree에서는 없으니 첫 점에서 시작한다.
    REQUIRE(points_of(controller.process(key(luil::key_code::arrow_right))) == std::vector<luil::chart_point> { { 0u, 0u } });
    REQUIRE(points_of(controller.process(key(luil::key_code::arrow_down))) == std::vector<luil::chart_point> { { 0u, 0u } });
    // Ctrl을 든 키는 앱 단축키로 흐른다.
    REQUIRE(points_of(controller.process(key(luil::key_code::arrow_right, true))).empty());
    REQUIRE(points_of(controller.process(key(luil::key_code::arrow_down, true))).empty());
}

TEST_CASE("A press on the point already chosen sends nothing", "[chart][focus]")
{
    const auto tree { shared_tree(make_config(luil::chart_point { 0u, 1u })) };
    const luil::rect_f plot { chart_in(*tree).plot_bounds() };
    const luil::rect_f second { plot_of(chart_in(*tree)).children()[1]->bounds() };
    const float x { second.x + second.width / 2.0f };

    luil::interaction_controller controller {};
    controller.set_tree(tree);
    static_cast<void>(controller.process(press(x, y_of(plot, 100.0), 0)));
    REQUIRE(points_of(controller.process(release(x, y_of(plot, 100.0), 10))).empty());
}

TEST_CASE("A drag sends the pressed point and then every point the pointer enters", "[chart][pointer-drag]")
{
    const auto tree { shared_tree(make_config()) };
    const luil::rect_f plot { chart_in(*tree).plot_bounds() };
    const luil::ui_element& plot_part { plot_of(chart_in(*tree)) };
    const auto center = [&plot_part](const std::size_t cell) {
        const luil::rect_f box { plot_part.children()[cell]->bounds() };
        return box.x + box.width / 2.0f;
    };
    const float y { y_of(plot, 120.0) };

    luil::interaction_controller controller {};
    controller.set_tree(tree);
    // 시작 거리 안의 떨림은 클릭이다.
    static_cast<void>(controller.process(press(center(0u), y, 0)));
    REQUIRE(points_of(controller.process(move(center(0u) + 3.0f, y, 10))).empty());
    REQUIRE(points_of(controller.process(release(center(0u) + 3.0f, y, 20))) == std::vector<luil::chart_point> { { 0u, 0u } });

    // 거리를 넘는 순간 누른 자리의 점과 포인터의 점이 나간다.
    static_cast<void>(controller.process(press(center(0u), y, 1000)));
    REQUIRE(points_of(controller.process(move(center(1u), y, 1010))) == std::vector<luil::chart_point> { { 0u, 0u }, { 0u, 1u } });
    // 같은 칸 안의 이동은 말이 없다.
    REQUIRE(points_of(controller.process(move(center(1u) + 2.0f, y, 1020))).empty());
    REQUIRE(points_of(controller.process(move(center(2u), y, 1030))) == std::vector<luil::chart_point> { { 0u, 2u } });
    // 플롯 밖은 가까운 끝이다. 4월은 비용(90)이 매출(160)보다 이 y에 가깝다.
    REQUIRE(points_of(controller.process(move(plot.x + plot.width + 200.0f, y, 1040))) == std::vector<luil::chart_point> { { 1u, 3u } });
    REQUIRE(points_of(controller.process(release(plot.x + plot.width + 200.0f, y, 1050))).empty());
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});
}

TEST_CASE("A disabled chart sends nothing", "[chart][focus]")
{
    auto chart { std::make_unique<luil::chart_element>(make_config()) };
    chart->set_enabled(false);
    chart->arrange({ wide_slot, 1.0f, 0.0f, {}, &half_em_measurer });
    REQUIRE(plot_of(*chart).enabled() == false);
    const luil::ui_element& cell { *plot_of(*chart).children()[1] };
    REQUIRE(cell.enabled() == false);
    REQUIRE(points_of((*cell.action(luil::ui_trigger::left_click))({ cell.id(), 0.0f, 0.0f })).empty());
    REQUIRE(stepped(*chart, luil::value_step::increase).empty());
}

TEST_CASE("Without a focus message the chart is a picture whose cells only hover", "[chart][focus]")
{
    luil::chart_config config { make_config() };
    config.focus = {};
    const auto tree { shared_tree(std::move(config)) };
    const luil::ui_element& plot { plot_of(chart_in(*tree)) };
    // Tab 자리도 키도 없다.
    REQUIRE(plot.tab_stop() == false);
    REQUIRE(plot.key_step() == nullptr);
    REQUIRE(plot.key_handler_of() == nullptr);
    // 접근 노드는 root 하나다.
    REQUIRE(plot.accessibility().role == luil::access_role::none);
    // 칸은 누를 것이 없고 커서만 들어 hit 대상이 된다. 그래야 마우스 hover가 칸에 선다.
    REQUIRE(plot.children().size() == 4u);
    const luil::ui_element& cell { *plot.children()[1] };
    REQUIRE(cell.action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(cell.pointer_drag() == nullptr);
    REQUIRE(cell.tab_stop() == false);
    REQUIRE(cell.interactive());

    luil::interaction_controller controller {};
    controller.set_tree(tree);
    const luil::rect_f box { cell.bounds() };
    static_cast<void>(controller.process(move(box.x + box.width / 2.0f, box.y + 5.0f, 0)));
    REQUIRE(controller.snapshot().hovered == cell.id());
}

TEST_CASE("The chart reads as an image with a summary and the plot as a slider with the point text", "[chart][accessibility]")
{
    SECTION("The root is an image with the default name and summary")
    {
        // 배치 전에도 답한다. 축이 0~200이고 칸 상한이 5라 눈금 자리가 0이고 값 글은 한 자리다.
        luil::chart_config config { make_config() };
        const luil::chart_element chart { config };
        const luil::access_info info { chart.accessibility() };
        REQUIRE(info.role == luil::access_role::image);
        REQUIRE(info.name == u8"차트");
        REQUIRE(info.value == u8"계열 2개, 범주 4개, 최솟값 40.0, 최댓값 160.0");
    }

    SECTION("The app can name and summarize the chart")
    {
        luil::chart_config config { make_config() };
        config.name = u8"분기 매출";
        config.access_summary = [](const luil::chart_config& source) { return u8"계열 수 " + std::u8string(1u, static_cast<char8_t>(u8'0' + source.series.size())); };
        const luil::access_info info { make_chart(std::move(config))->accessibility() };
        REQUIRE(info.name == u8"분기 매출");
        REQUIRE(info.value == u8"계열 수 2");
    }

    SECTION("An app function that answers nothing falls back to the default text")
    {
        luil::chart_config config { make_config(luil::chart_point { 0u, 0u }) };
        config.access_summary = [](const luil::chart_config&) { return std::u8string {}; };
        config.access_point = [](const luil::chart_config&, const luil::chart_point) { return std::u8string {}; };
        const auto chart { make_chart(std::move(config)) };
        REQUIRE(chart->accessibility().value.starts_with(u8"계열 2개"));
        REQUIRE(plot_of(*chart).accessibility().value.starts_with(u8"매출, 1월"));
    }

    SECTION("A chart without values leaves the values out of the summary")
    {
        luil::chart_config config {};
        config.owner = u8"sales";
        config.categories = { u8"1", u8"2", u8"3" };
        config.series.push_back({ .name = u8"빈 계열", .values = shared_values({ empty_point }) });
        REQUIRE(make_chart(std::move(config))->accessibility().value == u8"계열 1개, 범주 3개");
        // 자료가 하나도 없어도 그림이 넘어지지 않는다.
        luil::chart_config nothing {};
        nothing.owner = u8"sales";
        REQUIRE(make_chart(std::move(nothing))->accessibility().value == u8"계열 0개, 범주 0개");
    }

    SECTION("The plot is a slider without a range whose value is the chosen point")
    {
        const auto chart { make_chart(make_config(luil::chart_point { 0u, 2u })) };
        const luil::access_info info { plot_of(*chart).accessibility() };
        REQUIRE(info.role == luil::access_role::slider);
        REQUIRE(info.name == u8"자료 점");
        REQUIRE(info.range.has_value() == false);
        REQUIRE(info.value == u8"매출, 3월, 120.0");
        // 값 글은 눈금보다 한 자리를 더 쓰고, 빈 점의 값은 "값 없음"이다.
        REQUIRE(plot_of(*make_chart(make_config(luil::chart_point { 1u, 2u }))).accessibility().value == u8"비용, 3월, 값 없음");
        // 고른 점이 없으면 값이 없다.
        REQUIRE(plot_of(*make_chart(make_config())).accessibility().value.empty());
        // 칸은 접근 tree에서 접힌다. 접근 노드는 점 수와 무관하게 둘이다.
        REQUIRE(plot_of(*chart).children().front()->accessibility().role == luil::access_role::none);
    }

    SECTION("A single series leaves its name out and the app can word the point")
    {
        luil::chart_config single { make_config(luil::chart_point { 0u, 2u }) };
        single.series.pop_back();
        REQUIRE(plot_of(*make_chart(single)).accessibility().value == u8"3월, 120.0");
        single.format_value = [](const double value) { return luil::format_chart_number(value, 0); };
        REQUIRE(plot_of(*make_chart(single)).accessibility().value == u8"3월, 120");
        single.access_point = [](const luil::chart_config&, const luil::chart_point point) { return u8"점 " + std::u8string(1u, static_cast<char8_t>(u8'0' + point.category)); };
        REQUIRE(plot_of(*make_chart(single)).accessibility().value == u8"점 2");
    }

    SECTION("Categories without a label read as their number")
    {
        luil::chart_config numbered { make_config(luil::chart_point { 0u, 3u }) };
        numbered.categories = { u8"1월" };
        REQUIRE(plot_of(*make_chart(std::move(numbered))).accessibility().value == u8"매출, 4, 160.0");
    }

    SECTION("The value text keeps the tick decimals when they pass the limit")
    {
        luil::chart_config tiny { make_config(luil::chart_point { 0u, 0u }) };
        tiny.series.pop_back();
        tiny.series.front().values = shared_values({ 2e-8, 9e-8 });
        tiny.y_range.reset();
        REQUIRE(plot_of(*make_chart(std::move(tiny))).accessibility().value == u8"1월, 0.00000002");
    }

    SECTION("The tree reaches the root and the plot")
    {
        const auto tree { shared_tree(make_config()) };
        REQUIRE(luil::access_reachable(*tree, { luil::ui_element_kind::chart, u8"sales" }));
        REQUIRE(luil::access_reachable(*tree, { luil::ui_element_kind::chart_plot, u8"sales" }));
    }
}

TEST_CASE("The focus ring goes around the chosen point or the plot", "[chart][focus]")
{
    REQUIRE(plot_of(*make_chart(make_config())).focus_ring_bounds().has_value() == false);
    const auto chart { make_chart(make_config(luil::chart_point { 0u, 2u })) };
    const std::optional<luil::rect_f> ring { plot_of(*chart).focus_ring_bounds() };
    REQUIRE(ring.has_value());
    const luil::rect_f plot { chart->plot_bounds() };
    REQUIRE(std::abs(ring->y + ring->height / 2.0f - y_of(plot, 120.0)) < 1.0f);
    REQUIRE(ring->x > plot.x);
    REQUIRE(ring->x + ring->width < plot.x + plot.width);
}

TEST_CASE("The series table clears three to one against both built in window backgrounds", "[chart]")
{
    const luil::ui_color dark_window { luil::color_palette_for(luil::color_theme::dark).window_background };
    const luil::ui_color light_window { luil::color_palette_for(luil::color_theme::light).window_background };
    REQUIRE(luil::is_dark_background(dark_window));
    REQUIRE(luil::is_dark_background(light_window) == false);
    for (std::size_t series { 0 }; series < luil::chart_layout::series_table_size; ++series)
    {
        CAPTURE(series);
        REQUIRE(contrast(luil::chart_layout::series_table_color(true, series), dark_window) >= 3.0f);
        REQUIRE(contrast(luil::chart_layout::series_table_color(false, series), light_window) >= 3.0f);
        for (std::size_t other { series + 1u }; other < luil::chart_layout::series_table_size; ++other)
        {
            REQUIRE(luil::chart_layout::series_table_color(true, series) != luil::chart_layout::series_table_color(true, other));
            REQUIRE(luil::chart_layout::series_table_color(false, series) != luil::chart_layout::series_table_color(false, other));
        }
    }
    // 9번째 계열부터 처음으로 돈다.
    REQUIRE(luil::chart_layout::series_table_color(true, 8u) == luil::chart_layout::series_table_color(true, 0u));
}

TEST_CASE("Bars are as tall as their values and side by side", "[chart][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    constexpr luil::rect_f slot { 0.0f, 0.0f, 800.0f, 600.0f };
    luil::chart_config config { silent_config(3u) };
    config.series.push_back(silent_series(luil::chart_series_kind::bar, { 25.0, 50.0, 100.0 }, first_color));
    config.series.push_back(silent_series(luil::chart_series_kind::bar, { 200.0, empty_point, 50.0 }, second_color));
    const luil::ui_tree tree { raster_tree(std::move(config), slot) };
    const luil::rect_f plot { chart_in(tree).plot_bounds() };

    luil::testing::raster_frame frame { 800, 600, palette };
    frame.draw(tree, {});

    // 범주 칸의 70%를 두 계열이 나눠 쓴다. 첫 계열은 가운데 왼쪽, 둘째는 오른쪽이다.
    const float band { plot.width / 3.0f };
    const float quarter { band * 0.7f / 4.0f };
    const auto bar_x = [&plot, band, quarter](const int category, const int series) {
        return plot.x + band * (static_cast<float>(category) + 0.5f) + (series == 0 ? -quarter : quarter);
    };
    const auto height_of = [&frame](const float x, const luil::ui_color color) { return column_height(frame, x, color, 600); };
    const float full { plot.height };
    REQUIRE(std::abs(static_cast<float>(height_of(bar_x(0, 0), first_color)) - full * 25.0f / 200.0f) <= 3.0f);
    REQUIRE(std::abs(static_cast<float>(height_of(bar_x(1, 0), first_color)) - full * 50.0f / 200.0f) <= 3.0f);
    REQUIRE(std::abs(static_cast<float>(height_of(bar_x(2, 0), first_color)) - full * 100.0f / 200.0f) <= 3.0f);
    REQUIRE(std::abs(static_cast<float>(height_of(bar_x(0, 1), second_color)) - full) <= 3.0f);
    // 빈 점에는 막대가 없다.
    REQUIRE(height_of(bar_x(1, 1), second_color) == 0);
    // 칸의 양옆 15%는 빈다.
    REQUIRE(height_of(plot.x + band * 0.05f, first_color) == 0);
}

TEST_CASE("A line passes through its points and breaks at an empty point", "[chart][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    constexpr luil::rect_f slot { 0.0f, 0.0f, 800.0f, 600.0f };

    SECTION("The first and the last category sit on the plot ends")
    {
        luil::chart_config config { silent_config(2u) };
        config.series.push_back(silent_series(luil::chart_series_kind::line, { 0.0, 200.0 }, first_color));
        const luil::ui_tree tree { raster_tree(std::move(config), slot) };
        const luil::rect_f plot { chart_in(tree).plot_bounds() };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.draw(tree, {});
        // 가운데는 선 위이고, 왼쪽 위와 오른쪽 아래는 선에서 멀다.
        const float center_x { plot.x + plot.width / 2.0f };
        const float center_y { plot.y + plot.height / 2.0f };
        REQUIRE(frame.contains_color({ center_x - 2.0f, center_y - 2.0f, 4.0f, 4.0f }, first_color));
        REQUIRE(frame.contains_color({ plot.x + plot.width / 4.0f, plot.y + plot.height / 4.0f, 8.0f, 8.0f }, first_color) == false);
        REQUIRE(frame.contains_color({ plot.x + plot.width * 0.75f, plot.y + plot.height * 0.75f, 8.0f, 8.0f }, first_color) == false);
    }

    SECTION("An empty point cuts the line and a lone value is still drawn")
    {
        luil::chart_config config { silent_config(7u) };
        config.series.push_back(silent_series(luil::chart_series_kind::line, { 100.0, 100.0, empty_point, 100.0, 100.0, empty_point, 100.0 }, first_color));
        const luil::ui_tree tree { raster_tree(std::move(config), slot) };
        const luil::rect_f plot { chart_in(tree).plot_bounds() };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.draw(tree, {});
        const float step { plot.width / 6.0f };
        const float y { plot.y + plot.height / 2.0f };
        const auto has_line_at = [&](const float category) {
            return frame.contains_color({ plot.x + step * category - 1.0f, y - 2.0f, 2.0f, 4.0f }, first_color);
        };
        REQUIRE(has_line_at(0.5f));
        REQUIRE(has_line_at(1.5f) == false);
        REQUIRE(has_line_at(2.0f) == false);
        REQUIRE(has_line_at(2.5f) == false);
        REQUIRE(has_line_at(3.5f));
        REQUIRE(has_line_at(5.0f) == false);
        // 앞뒤가 빈 끝 값은 긋는 선이 없어 점으로 찍힌다.
        REQUIRE(has_line_at(6.0f));
    }

    SECTION("An area fills down to zero under its line")
    {
        luil::chart_config config { silent_config(2u) };
        config.series.push_back(silent_series(luil::chart_series_kind::area, { 100.0, 100.0 }, first_color));
        const luil::ui_tree tree { raster_tree(std::move(config), slot) };
        const luil::rect_f plot { chart_in(tree).plot_bounds() };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.draw(tree, {});
        const int x { static_cast<int>(plot.x + plot.width / 2.0f) };
        const int line_y { static_cast<int>(plot.y + plot.height / 2.0f) };
        REQUIRE(frame.pixel_at(x, line_y) == first_color);
        // 선 아래는 계열색이 비쳐 바탕도 선 색도 아니다. 선 위는 바탕이다.
        const luil::ui_color below { frame.pixel_at(x, line_y + static_cast<int>(plot.height / 4.0f)) };
        REQUIRE(below != first_color);
        REQUIRE(below != palette.window_background);
        REQUIRE(frame.pixel_at(x, line_y - static_cast<int>(plot.height / 4.0f)) == palette.window_background);
    }

    SECTION("A negative bar hangs from the zero line")
    {
        luil::chart_config config { silent_config(1u) };
        config.y_range = luil::chart_range { -100.0, 100.0 };
        config.series.push_back(silent_series(luil::chart_series_kind::bar, { -100.0 }, first_color));
        const luil::ui_tree tree { raster_tree(std::move(config), slot) };
        const luil::rect_f plot { chart_in(tree).plot_bounds() };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.draw(tree, {});
        const int x { static_cast<int>(plot.x + plot.width / 2.0f) };
        REQUIRE(frame.pixel_at(x, static_cast<int>(plot.y + plot.height * 0.75f)) == first_color);
        REQUIRE(frame.pixel_at(x, static_cast<int>(plot.y + plot.height * 0.25f)) == palette.window_background);
    }
}

TEST_CASE("The grid and the axes take their palette roles", "[chart][raster]")
{
    // 두 역할은 알파가 있어 바탕과 섞여 그려진다. 불투명한 값으로 바꿔 어느 역할인지를 바이트로 잰다.
    luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    palette.control_border = luil::make_ui_color(10, 200, 240);
    palette.divider = luil::make_ui_color(240, 200, 10);
    constexpr luil::rect_f slot { 0.0f, 0.0f, 800.0f, 600.0f };
    luil::chart_config config { silent_config(2u) };
    config.series.push_back(silent_series(luil::chart_series_kind::line, { 0.0, 0.0 }, first_color));
    luil::chart_config with_grid { config };
    with_grid.grid = true;

    const luil::ui_tree bare { raster_tree(std::move(config), slot) };
    const luil::ui_tree gridded { raster_tree(std::move(with_grid), slot) };
    const luil::rect_f plot { chart_in(bare).plot_bounds() };
    luil::testing::raster_frame bare_frame { 800, 600, palette };
    bare_frame.draw(bare, {});
    luil::testing::raster_frame grid_frame { 800, 600, palette };
    grid_frame.draw(gridded, {});

    // 축 선은 왼쪽에 선다. 맨 위 눈금 줄은 격자를 켤 때만 칠해진다.
    REQUIRE(bare_frame.pixel_at(static_cast<int>(plot.x) + 1, static_cast<int>(plot.y + plot.height / 2.0f)) == palette.control_border);
    const int top { static_cast<int>(plot.y) };
    const int middle { static_cast<int>(plot.x + plot.width / 2.0f) };
    REQUIRE(bare_frame.pixel_at(middle, top) == palette.window_background);
    REQUIRE(grid_frame.pixel_at(middle, top) == palette.divider);
}

TEST_CASE("High contrast folds the series to one color and tells them apart by line and marker", "[chart][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::high_contrast) };
    constexpr luil::rect_f slot { 0.0f, 0.0f, 800.0f, 600.0f };

    SECTION("The second line is dashed and every color is the primary foreground")
    {
        luil::chart_config config { silent_config(2u) };
        config.series.push_back(silent_series(luil::chart_series_kind::line, { 50.0, 50.0 }, first_color));
        config.series.push_back(silent_series(luil::chart_series_kind::line, { 150.0, 150.0 }, second_color));
        config.series.back().marker = luil::chart_marker::none;
        config.series.front().marker = luil::chart_marker::none;
        const luil::ui_tree tree { raster_tree(std::move(config), slot) };
        const luil::rect_f plot { chart_in(tree).plot_bounds() };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.set_theme(luil::color_theme::high_contrast);
        frame.draw(tree, {});

        REQUIRE(frame.contains_color({ 0.0f, 0.0f, 800.0f, 600.0f }, first_color) == false);
        REQUIRE(frame.contains_color({ 0.0f, 0.0f, 800.0f, 600.0f }, second_color) == false);
        // 첫 계열은 실선이고 둘째는 긴 점선(8 긋고 4 쉼)이다.
        const auto row_count = [&](const double value) {
            const int y { static_cast<int>(y_of(plot, value)) };
            return frame.count_color({ plot.x + 8.0f, static_cast<float>(y), plot.width - 16.0f, 1.0f }, palette.primary_foreground);
        };
        const float width { plot.width - 16.0f };
        REQUIRE(static_cast<float>(row_count(50.0)) >= width * 0.95f);
        REQUIRE(static_cast<float>(row_count(150.0)) <= width * 0.75f);
        REQUIRE(static_cast<float>(row_count(150.0)) >= width * 0.55f);
    }

    SECTION("Automatic markers appear in high contrast and only there")
    {
        luil::chart_config config { silent_config(2u) };
        config.series.push_back(silent_series(luil::chart_series_kind::line, { 100.0, 100.0 }, first_color));
        const luil::ui_tree tree { raster_tree(config, slot) };
        const luil::rect_f plot { chart_in(tree).plot_bounds() };
        const int x { static_cast<int>(plot.x + plot.width) - 1 };
        const int y { static_cast<int>(plot.y + plot.height / 2.0f) - 5 };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.set_theme(luil::color_theme::high_contrast);
        frame.draw(tree, {});
        REQUIRE(frame.pixel_at(x, y) == palette.primary_foreground);

        const luil::ui_color_palette plain { luil::color_palette_for(luil::color_theme::dark) };
        luil::testing::raster_frame plain_frame { 800, 600, plain };
        plain_frame.draw(tree, {});
        REQUIRE(plain_frame.pixel_at(x, y) == plain.window_background);
    }

    SECTION("Only the first bar series is filled")
    {
        luil::chart_config config { silent_config(1u) };
        config.series.push_back(silent_series(luil::chart_series_kind::bar, { 200.0 }, first_color));
        config.series.push_back(silent_series(luil::chart_series_kind::bar, { 200.0 }, second_color));
        const luil::ui_tree tree { raster_tree(std::move(config), slot) };
        const luil::rect_f plot { chart_in(tree).plot_bounds() };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.set_theme(luil::color_theme::high_contrast);
        frame.draw(tree, {});
        const float quarter { plot.width * 0.7f / 4.0f };
        const float center { plot.x + plot.width / 2.0f };
        const int y { static_cast<int>(plot.y + plot.height / 2.0f) };
        REQUIRE(frame.pixel_at(static_cast<int>(center - quarter), y) == palette.primary_foreground);
        REQUIRE(frame.pixel_at(static_cast<int>(center + quarter), y) == palette.window_background);
    }
}

TEST_CASE("A disabled chart draws every series in the disabled color, in high contrast too", "[chart][raster]")
{
    constexpr luil::rect_f slot { 0.0f, 0.0f, 800.0f, 600.0f };
    const auto two_lines = [] {
        luil::chart_config config { silent_config(2u) };
        config.series.push_back(silent_series(luil::chart_series_kind::line, { 50.0, 50.0 }, first_color));
        config.series.push_back(silent_series(luil::chart_series_kind::line, { 150.0, 150.0 }, second_color));
        config.series.front().marker = luil::chart_marker::none;
        config.series.back().marker = luil::chart_marker::none;
        return config;
    };
    const auto two_bars = [] {
        luil::chart_config config { silent_config(1u) };
        config.series.push_back(silent_series(luil::chart_series_kind::bar, { 200.0 }, first_color));
        config.series.push_back(silent_series(luil::chart_series_kind::bar, { 200.0 }, second_color));
        return config;
    };
    const auto row_count = [](const luil::testing::raster_frame& frame, const luil::rect_f& plot, const double value, const luil::ui_color color) {
        const int y { static_cast<int>(y_of(plot, value)) };
        return static_cast<float>(frame.count_color({ plot.x + 8.0f, static_cast<float>(y), plot.width - 16.0f, 1.0f }, color));
    };

    SECTION("The plain theme gives up the series colors")
    {
        const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
        const luil::ui_tree tree { disabled_raster_tree(two_lines(), slot) };
        const luil::rect_f plot { chart_in(tree).plot_bounds() };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.draw(tree, {});

        REQUIRE(frame.contains_color({ 0.0f, 0.0f, 800.0f, 600.0f }, first_color) == false);
        REQUIRE(frame.contains_color({ 0.0f, 0.0f, 800.0f, 600.0f }, second_color) == false);
        const int middle { static_cast<int>(plot.x + plot.width / 2.0f) };
        REQUIRE(is_disabled_color(frame.pixel_at(middle, static_cast<int>(y_of(plot, 50.0))), palette));
        REQUIRE(is_disabled_color(frame.pixel_at(middle, static_cast<int>(y_of(plot, 150.0))), palette));

        const luil::ui_tree bar_tree { disabled_raster_tree(two_bars(), slot) };
        const luil::rect_f bar_plot { chart_in(bar_tree).plot_bounds() };
        luil::testing::raster_frame bar_frame { 800, 600, palette };
        bar_frame.draw(bar_tree, {});
        const float quarter { bar_plot.width * 0.7f / 4.0f };
        const float center { bar_plot.x + bar_plot.width / 2.0f };
        const int y { static_cast<int>(bar_plot.y + bar_plot.height / 2.0f) };
        REQUIRE(is_disabled_color(bar_frame.pixel_at(static_cast<int>(center - quarter), y), palette));
        REQUIRE(is_disabled_color(bar_frame.pixel_at(static_cast<int>(center + quarter), y), palette));
    }

    SECTION("High contrast keeps the line styles and the unfilled bars but not the primary color")
    {
        const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::high_contrast) };
        // 두 색이 같은 팔레트면 아래 단언이 활성과 비활성을 가르지 못한다.
        REQUIRE(palette.disabled_foreground != palette.primary_foreground);
        const luil::ui_tree tree { disabled_raster_tree(two_lines(), slot) };
        const luil::rect_f plot { chart_in(tree).plot_bounds() };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.set_theme(luil::color_theme::high_contrast);
        frame.draw(tree, {});

        // 첫 계열은 실선이고 둘째는 긴 점선(8 긋고 4 쉼)이다. 색만 비활성으로 바뀐다.
        const float width { plot.width - 16.0f };
        REQUIRE(row_count(frame, plot, 50.0, palette.disabled_foreground) >= width * 0.95f);
        REQUIRE(row_count(frame, plot, 150.0, palette.disabled_foreground) <= width * 0.75f);
        REQUIRE(row_count(frame, plot, 150.0, palette.disabled_foreground) >= width * 0.55f);
        REQUIRE(row_count(frame, plot, 50.0, palette.primary_foreground) == 0.0f);
        REQUIRE(row_count(frame, plot, 150.0, palette.primary_foreground) == 0.0f);

        const luil::ui_tree bar_tree { disabled_raster_tree(two_bars(), slot) };
        const luil::rect_f bar_plot { chart_in(bar_tree).plot_bounds() };
        luil::testing::raster_frame bar_frame { 800, 600, palette };
        bar_frame.set_theme(luil::color_theme::high_contrast);
        bar_frame.draw(bar_tree, {});
        const float quarter { bar_plot.width * 0.7f / 4.0f };
        const float center { bar_plot.x + bar_plot.width / 2.0f };
        const int y { static_cast<int>(bar_plot.y + bar_plot.height / 2.0f) };
        REQUIRE(bar_frame.pixel_at(static_cast<int>(center - quarter), y) == palette.disabled_foreground);
        REQUIRE(bar_frame.pixel_at(static_cast<int>(center + quarter), y) == palette.window_background);
    }
}

TEST_CASE("The value box stands inside the element", "[chart][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    constexpr luil::rect_f slot { 40.0f, 40.0f, 600.0f, 400.0f };
    const auto outside = [&](const luil::testing::raster_frame& frame) {
        return frame.count_color({ 0.0f, 0.0f, 800.0f, 40.0f }, palette.tooltip_background)
            + frame.count_color({ 0.0f, 440.0f, 800.0f, 160.0f }, palette.tooltip_background)
            + frame.count_color({ 0.0f, 40.0f, 40.0f, 400.0f }, palette.tooltip_background)
            + frame.count_color({ 640.0f, 40.0f, 160.0f, 400.0f }, palette.tooltip_background)
        ;
    };
    const auto base_config = [] {
        luil::chart_config config { silent_config(3u) };
        // 축 꼭대기와 오른쪽 끝의 점이다. 위에 놓을 자리가 없어 값 상자가 다른 쪽으로 돈다.
        config.series.push_back(silent_series(luil::chart_series_kind::line, { 200.0, 100.0, 200.0 }, first_color));
        config.series.back().name = {};
        config.series.push_back(silent_series(luil::chart_series_kind::line, { 20.0, 30.0, 40.0 }, second_color));
        config.series.back().name = {};
        return config;
    };

    SECTION("A chosen point opens it")
    {
        luil::chart_config config { base_config() };
        config.focused = luil::chart_point { 0u, 2u };
        const luil::ui_tree tree { raster_tree(std::move(config), slot) };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.draw(tree, {});
        REQUIRE(frame.contains_color({ 40.0f, 40.0f, 600.0f, 400.0f }, palette.tooltip_background));
        REQUIRE(outside(frame) == 0);
    }

    SECTION("A hovered cell opens it and leaving closes it")
    {
        const luil::ui_tree tree { raster_tree(base_config(), slot) };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.draw(tree, {});
        REQUIRE(frame.contains_color({ 40.0f, 40.0f, 600.0f, 400.0f }, palette.tooltip_background) == false);

        luil::interaction_snapshot interaction {};
        interaction.hovered = plot_of(chart_in(tree)).children()[1]->id();
        frame.draw(tree, interaction);
        REQUIRE(frame.contains_color({ 40.0f, 40.0f, 600.0f, 400.0f }, palette.tooltip_background));
        REQUIRE(outside(frame) == 0);
    }

    SECTION("A chosen point wins over the hovered cell")
    {
        luil::chart_config config { base_config() };
        config.focused = luil::chart_point { 1u, 0u };
        const luil::ui_tree tree { raster_tree(std::move(config), slot) };
        const luil::rect_f plot { chart_in(tree).plot_bounds() };
        luil::interaction_snapshot interaction {};
        interaction.hovered = plot_of(chart_in(tree)).children()[2]->id();
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.draw(tree, interaction);
        // 왼쪽 끝 범주의 점 곁에 서서 플롯 오른쪽 절반에는 상자가 없다.
        REQUIRE(frame.contains_color({ plot.x, plot.y, plot.width / 2.0f, plot.height }, palette.tooltip_background));
        REQUIRE(frame.contains_color({ plot.x + plot.width / 2.0f + 40.0f, plot.y, plot.width / 2.0f - 40.0f, plot.height }, palette.tooltip_background) == false);
    }

    SECTION("A chart too short for the rows still keeps the box inside")
    {
        luil::chart_config config { base_config() };
        config.focused = luil::chart_point { 0u, 1u };
        const luil::ui_tree tree { raster_tree(std::move(config), { 40.0f, 40.0f, 600.0f, 60.0f }) };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.draw(tree, {});
        REQUIRE(frame.count_color({ 0.0f, 100.0f, 800.0f, 500.0f }, palette.tooltip_background) == 0);
    }
}

TEST_CASE("A large series is reduced to the lowest and highest value of each pixel column", "[chart]")
{
    SECTION("The columns keep the spike and the order of the extremes")
    {
        std::vector<double> values(1000u, 10.0);
        values[500] = 90.0;
        values[501] = -90.0;
        values[700] = empty_point;
        const std::vector<luil::chart_layout::value_column> columns { luil::chart_layout::reduce_to_columns(values, values.size(), false, 100u) };
        REQUIRE(columns.size() == 100u);
        const luil::chart_layout::value_column& spike { columns[50] };
        REQUIRE(spike.has_value);
        REQUIRE(spike.high == 90.0);
        REQUIRE(spike.low == -90.0);
        REQUIRE(spike.low_first == false);
        REQUIRE(std::abs(spike.strongest) == 90.0);
        REQUIRE(columns[0].low == 10.0);
        REQUIRE(columns[0].high == 10.0);
        // 모든 값이 빈 열은 끊긴다. 빈 값 하나만 든 열은 그대로다.
        REQUIRE(columns[70].has_value);
        REQUIRE(luil::chart_layout::reduce_to_columns(std::vector<double> { empty_point, empty_point }, 2u, false, 2u)[1].has_value == false);
    }

    SECTION("Short series and degenerate sizes do not read out of range")
    {
        const std::vector<double> values { 1.0, 2.0 };
        const std::vector<luil::chart_layout::value_column> columns { luil::chart_layout::reduce_to_columns(values, 1000u, true, 10u) };
        REQUIRE(columns.size() == 10u);
        REQUIRE(columns[0].has_value);
        REQUIRE(columns[9].has_value == false);
        REQUIRE(luil::chart_layout::reduce_to_columns(values, 0u, false, 10u)[0].has_value == false);
        REQUIRE(luil::chart_layout::reduce_to_columns(values, 2u, false, 0u).empty());
        REQUIRE(luil::chart_layout::reduce_to_columns(values, 1u, false, 4u)[2].has_value);
    }

    SECTION("A spike among a hundred thousand values reaches the plot top")
    {
        const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
        auto values { std::make_shared<std::vector<double>>(100000u, 0.0) };
        (*values)[50000] = 200.0;
        luil::chart_config config { silent_config(0u) };
        luil::chart_series line {};
        line.values = values;
        line.color = first_color;
        config.series.push_back(std::move(line));
        const luil::ui_tree tree { raster_tree(std::move(config), { 0.0f, 0.0f, 800.0f, 600.0f }) };
        const luil::rect_f plot { chart_in(tree).plot_bounds() };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.draw(tree, {});
        REQUIRE(frame.contains_color({ plot.x + plot.width / 2.0f - 3.0f, plot.y + 4.0f, 6.0f, 4.0f }, first_color));
        REQUIRE(frame.contains_color({ plot.x + plot.width / 4.0f, plot.y + 4.0f, 6.0f, 4.0f }, first_color) == false);
    }

    SECTION("Bars take the strongest value of each column")
    {
        const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
        auto values { std::make_shared<std::vector<double>>(100000u, 10.0) };
        (*values)[50000] = 200.0;
        luil::chart_config config { silent_config(0u) };
        luil::chart_series bars {};
        bars.kind = luil::chart_series_kind::bar;
        bars.values = values;
        bars.color = first_color;
        config.series.push_back(std::move(bars));
        const luil::ui_tree tree { raster_tree(std::move(config), { 0.0f, 0.0f, 800.0f, 600.0f }) };
        const luil::rect_f plot { chart_in(tree).plot_bounds() };
        luil::testing::raster_frame frame { 800, 600, palette };
        frame.draw(tree, {});
        REQUIRE(frame.contains_color({ plot.x + plot.width / 2.0f - 3.0f, plot.y + 4.0f, 6.0f, 4.0f }, first_color));
        const int low_bar { column_height(frame, plot.x + plot.width / 4.0f, first_color, 600) };
        REQUIRE(std::abs(static_cast<float>(low_bar) - plot.height * 10.0f / 200.0f) <= 3.0f);
    }
}

TEST_CASE("Drawing a hundred thousand points stays under the budget", "[chart][raster]")
{
    // 그릴 때 값을 훑지 않고 선을 조각으로 나눠 그어 frame 비용이 점 수를 따라 늘지 않는다는 뜻의 상한이다.
    // 잰 값과 상한의 까닭은 docs/chart-design.md의 확인 절에 있다.
#if defined(__ANDROID__) && !defined(NDEBUG)
    // 실기기 Debug는 최적화하지 않은 Skia를 링크해 영역이 300ms를 넘는다. 영역 잰 값의 두 배다.
    constexpr auto draw_budget { std::chrono::milliseconds { 650 } };
#else
    constexpr auto draw_budget { std::chrono::milliseconds { 200 } };
#endif
    auto values { std::make_shared<std::vector<double>>() };
    values->reserve(100000u);
    for (std::size_t index { 0 }; index < 100000u; ++index)
        values->push_back(100.0 + 90.0 * std::sin(static_cast<double>(index) * std::numbers::pi / 500.0));

    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    for (const luil::chart_series_kind kind : { luil::chart_series_kind::line, luil::chart_series_kind::area, luil::chart_series_kind::bar })
    {
        CAPTURE(static_cast<int>(kind));
        luil::chart_config config { silent_config(0u) };
        luil::chart_series series {};
        series.kind = kind;
        series.values = values;
        series.color = first_color;
        config.series.push_back(std::move(series));
        const luil::ui_tree tree { raster_tree(std::move(config), { 0.0f, 0.0f, 800.0f, 600.0f }) };

        luil::testing::raster_frame frame { 800, 600, palette };
        // 동시에 도는 다른 test가 끼어든 한 번을 재지 않게 세 번 가운데 가장 빠른 값을 본다.
        auto best { std::chrono::steady_clock::duration::max() };
        for (int attempt { 0 }; attempt < 3; ++attempt)
        {
            const auto start { std::chrono::steady_clock::now() };
            frame.draw(tree, {});
            best = std::min(best, std::chrono::steady_clock::now() - start);
        }
        CAPTURE(std::chrono::duration_cast<std::chrono::microseconds>(best).count());
        // 한 종류가 넘어도 멈추지 않아 세 종류의 값이 모두 나온다.
        CHECK(best <= draw_budget);
    }
}
