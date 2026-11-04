#include "mobile_demo/chart_page.h"

#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"

#include <algorithm>
#include <utility>

namespace mobile_demo {
    namespace {
        // 차트의 높이다. 차트는 원하는 높이를 말하지 않아 담는 쪽이 정한다.
        constexpr float chart_height { 300.0f };
        constexpr float section_gap { 8.0f };
    }

    bool chart_page::handle(const luil::app_message& message)
    {
        return sample_.handle(message);
    }

    page_content chart_page::build(const float width)
    {
        const float content_width { std::max(0.0f, width - 2.0f * page_padding) };
        page_column column { u8"chart" };
        column.note(u8"chart-hint", u8"점을 탭하면 값 상자가 뜬다.");
        column.note(u8"chart-hint-2", u8"오래 눌렀다 옮기면 값을 훑는다.");
        column.gap(section_gap);

        luil::choice_group_config kinds { sample_.kind_choice(u8"kind") };
        kinds.width = luil::choice_width::fill;
        const float kinds_height { luil::choice_group_element::height_for(kinds) };
        column.add(std::make_unique<luil::choice_group_element>(std::move(kinds)), kinds_height, content_width);
        column.gap(section_gap);

        luil::check_config large { sample_.large_switch(u8"large") };
        const float large_height { luil::check_element::height_for(large) };
        column.add(std::make_unique<luil::check_element>(std::move(large)), large_height, content_width);
        column.gap(section_gap);

        demo::chart_sample::built chart { sample_.build_chart(u8"sales") };
        column.add(std::move(chart.chart), chart_height, content_width);
        column.gap(section_gap);
        column.note(u8"state", std::move(chart.state));
        column.note(u8"summary", std::move(chart.summary));
        return column.finish();
    }
}
