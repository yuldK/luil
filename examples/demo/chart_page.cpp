#include "demo/chart_page.h"

#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"

#include <algorithm>
#include <utility>

namespace demo {
    namespace {
        // 차트의 높이다. 차트는 원하는 높이를 말하지 않아 담는 쪽이 정한다.
        constexpr float chart_height { 320.0f };
        constexpr float label_height { 18.0f };
    }

    bool chart_page::handle(const luil::app_message& message)
    {
        if (const auto* const scroll { message.get<chart_scroll_intent>() }; scroll != nullptr)
        {
            scroll_ = std::max(0.0f, scroll_ + scroll->delta);
            return true;
        }
        return sample_.handle(message);
    }

    std::unique_ptr<luil::ui_element> chart_page::build()
    {
        page_layout page { u8"chart" };
        page.heading(u8"차트", u8"선택 라이브러리 luil::luil_chart가 그린다. 선·막대·영역과 범주 x축, 왼쪽 y축, 범례를 한 요소로 짓는다.");
        page.begin_card(u8"매출과 비용", u8"칸을 누르거나 끌어 점을 고른다. 플롯에 초점을 두면 ← → Home End Page가 범주, ↑ ↓가 계열을 옮긴다. 마우스를 올리면 값 상자가 뜬다.");

        page.add(std::make_unique<luil::choice_group_element>(sample_.kind_choice(u8"chart/kind")), luil::choice_toggle_height);
        page.gap();
        luil::check_config large { sample_.large_switch(u8"chart/large") };
        const float large_height { luil::check_element::height_for(large) };
        page.add(std::make_unique<luil::check_element>(std::move(large)), large_height);
        page.gap();

        chart_sample::built chart { sample_.build_chart(u8"chart/sales") };
        page.add(std::move(chart.chart), chart_height);
        page.gap();
        page.add(make_label({ kind_text, u8"chart/state" }, std::move(chart.state), 12.0f, luil::label_color_role::primary), label_height);
        page.add(make_label({ kind_text, u8"chart/summary" }, std::move(chart.summary), 12.0f, luil::label_color_role::dim), label_height);
        page.end_card();
        return page.finish(scroll_, [](const float delta) { return luil::make_app_action(chart_scroll_intent { delta }); });
    }
}
