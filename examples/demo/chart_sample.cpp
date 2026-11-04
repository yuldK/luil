#include "demo/chart_sample.h"

#include "luil/chart.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace demo {
    namespace {
        constexpr std::size_t large_size { 100000 };

        [[nodiscard]] luil::chart_series_kind series_kind(const chart_sample_kind kind) noexcept
        {
            switch (kind)
            {
            case chart_sample_kind::bar:
                return luil::chart_series_kind::bar;
            case chart_sample_kind::area:
                return luil::chart_series_kind::area;
            case chart_sample_kind::line:
                break;
            }
            return luil::chart_series_kind::line;
        }

        // 큰 파도에 잔물결을 얹은 값이다. 픽셀 열마다 값이 흔들려 최소·최대로 모으는 길이 눈에 보인다.
        [[nodiscard]] std::shared_ptr<const std::vector<double>> make_wave(const double center, const double height, const double ripple)
        {
            auto values { std::make_shared<std::vector<double>>() };
            values->reserve(large_size);
            for (std::size_t index { 0 }; index < large_size; ++index)
            {
                const double position { static_cast<double>(index) * std::numbers::pi / 5000.0 };
                values->push_back(center + height * std::sin(position) + ripple * std::sin(position * 40.0));
            }
            return values;
        }
    }

    chart_sample::chart_sample()
        : sales_ { std::make_shared<const std::vector<double>>(std::vector<double> { 80.0, 95.0, 120.0, 110.0, 140.0, 150.0, 135.0, 160.0, 170.0, 155.0, 180.0, 200.0 }) }
        , costs_ { std::make_shared<const std::vector<double>>(std::vector<double> { 60.0, 70.0, 85.0, 80.0, 90.0, 100.0, 95.0, 105.0, 110.0, 100.0, 115.0, 125.0 }) }
    {
    }

    bool chart_sample::handle(const luil::app_message& message)
    {
        if (const auto* const point { message.get<chart_sample_point_intent>() }; point != nullptr)
        {
            chosen_ = point->point;
            return true;
        }
        if (const auto* const kind { message.get<chart_sample_kind_intent>() }; kind != nullptr)
        {
            kind_ = kind->kind;
            return true;
        }
        if (const auto* const large { message.get<chart_sample_large_intent>() }; large != nullptr)
        {
            large_ = large->on;
            // 범주가 12개와 10만 개를 오가 고른 점의 뜻이 달라진다.
            chosen_.reset();
            if (large_
                && large_sales_ == nullptr)
            {
                large_sales_ = make_wave(100.0, 60.0, 20.0);
                large_costs_ = make_wave(60.0, 30.0, 10.0);
            }
            return true;
        }
        return false;
    }

    chart_sample::built chart_sample::build_chart(std::u8string owner) const
    {
        luil::chart_config config {};
        config.owner = std::move(owner);
        config.name = u8"매출과 비용";
        if (large_ == false)
            config.categories = { u8"1월", u8"2월", u8"3월", u8"4월", u8"5월", u8"6월", u8"7월", u8"8월", u8"9월", u8"10월", u8"11월", u8"12월" };
        config.series.push_back({ .name = u8"매출", .kind = series_kind(kind_), .values = large_ ? large_sales_ : sales_ });
        config.series.push_back({ .name = u8"비용", .kind = luil::chart_series_kind::bar, .values = large_ ? large_costs_ : costs_ });
        if (chosen_.has_value())
            config.focused = luil::chart_point { chosen_->series, chosen_->category };
        config.focus = [](const luil::chart_point point) { return luil::make_app_action(chart_sample_point_intent { { point.series, point.category } }); };
        // 견본의 값이 정수라 소수 자리를 달지 않는다. 숫자 서식이 앱 몫이라는 것을 보이는 자리이기도 하다.
        config.format_value = [](const double value) { return luil::format_chart_number(value, 0); };

        auto chart { std::make_unique<luil::chart_element>(std::move(config)) };
        built result {};
        // 두 label은 차트가 보조 기술에 내는 글을 그대로 옮겨 적는다. 그래서 화면과 UIA가 같은 말을 한다.
        const std::u8string point_text { chart->children().front()->accessibility().value };
        result.state = point_text.empty()
            ? std::u8string { u8"고른 점: 없음" }
            : u8"고른 점: " + point_text + u8" · 계열 2 · 범주 " + (large_ ? u8"100000" : u8"12")
        ;
        result.summary = chart->accessibility().value;
        result.chart = std::move(chart);
        return result;
    }

    luil::choice_group_config chart_sample::kind_choice(std::u8string owner) const
    {
        // 값이 곧 항목의 id라 다른 묶음과 겹치지 않게 묶음 owner를 앞에 붙인다.
        const std::u8string line_value { owner + u8"-line" };
        const std::u8string bar_value { owner + u8"-bar" };
        const std::u8string area_value { owner + u8"-area" };
        luil::choice_group_config choice {};
        choice.owner = std::move(owner);
        choice.style = luil::choice_style::toggle;
        choice.name = u8"매출 계열의 꼴";
        choice.items = { { line_value, u8"선" }, { bar_value, u8"막대" }, { area_value, u8"영역" } };
        choice.selected = kind_ == chart_sample_kind::bar ? bar_value : (kind_ == chart_sample_kind::area ? area_value : line_value);
        choice.select = [bar_value, area_value](const std::u8string& value) {
            if (value == bar_value)
                return luil::make_app_action(chart_sample_kind_intent { chart_sample_kind::bar });
            if (value == area_value)
                return luil::make_app_action(chart_sample_kind_intent { chart_sample_kind::area });
            return luil::make_app_action(chart_sample_kind_intent { chart_sample_kind::line });
        };
        return choice;
    }

    luil::check_config chart_sample::large_switch(std::u8string owner) const
    {
        luil::check_config config {};
        config.owner = std::move(owner);
        config.style = luil::check_style::toggle_switch;
        config.label = u8"큰 자료 (계열마다 10만 점)";
        config.checked = large_;
        config.toggle = luil::make_message_action(chart_sample_large_intent { large_ == false });
        return config;
    }
}
