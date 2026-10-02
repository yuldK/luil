#include "mobile_demo/groups_page.h"

#include "luil/ui/choice_group_element.h"
#include "luil/ui/group_element.h"
#include "luil/ui/stack_element.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace mobile_demo {
    bool groups_page::handle(const luil::app_message& message)
    {
        if (const auto* const collapse { message.get<group_collapse_intent>() }; collapse != nullptr)
        {
            advanced_collapsed_ = collapse->collapsed;
            return true;
        }
        if (const auto* const coffee { message.get<coffee_intent>() }; coffee != nullptr)
        {
            coffee_ = coffee->value;
            return true;
        }
        if (const auto* const view { message.get<view_intent>() }; view != nullptr)
        {
            view_ = view->value;
            return true;
        }
        return false;
    }

    page_content groups_page::build(const float width, const float scale)
    {
        static_cast<void>(scale);

        // 라디오 묶음이다: 접이식 섹션의 내용으로 넣는다.
        luil::choice_group_config coffee {};
        coffee.owner = u8"coffee";
        coffee.style = luil::choice_style::radio;
        coffee.items = { { u8"latte", u8"라떼" }, { u8"mocha", u8"모카" }, { u8"drip", u8"드립" } };
        coffee.selected = coffee_;
        coffee.select = [](const std::u8string& value) { return luil::make_app_action(coffee_intent { value }); };
        // 묶음의 이름이 「무엇을 고르는 중인가」다. 화면의 머리글과 같은 말을 적는다.
        coffee.name = u8"음료";
        const float coffee_height { luil::choice_group_element::height_for(coffee) };

        luil::stack_config inner_config {};
        inner_config.padding = luil::edge_insets::all(8.0f);
        auto inner { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"group-inner" }, inner_config) };
        inner->add(std::make_unique<luil::choice_group_element>(std::move(coffee)), coffee_height);

        luil::group_config advanced {};
        advanced.owner = u8"advanced";
        advanced.title = u8"음료 선택";
        advanced.collapsed = advanced_collapsed_;
        advanced.content_height = coffee_height + 16.0f;
        // 절대 메시지 하나로 제목 줄 누름(지금 상태의 반대)과 보조 기술의 Expand·Collapse가 함께 선다.
        advanced.set_collapsed = [](const bool collapsed) { return luil::make_app_action(group_collapse_intent { collapsed }); };
        const float group_height { luil::group_element::height_for(advanced) };
        auto group { std::make_unique<luil::group_element>(std::move(advanced), std::move(inner)) };

        // 토글 묶음이다: slot 폭을 같은 크기로 나눠 갖는다.
        luil::choice_group_config view {};
        view.owner = u8"view";
        view.style = luil::choice_style::toggle;
        view.items = { { u8"grid", u8"바둑판" }, { u8"list", u8"목록" }, { u8"detail", u8"자세히" } };
        view.selected = view_;
        view.select = [](const std::u8string& value) { return luil::make_app_action(view_intent { value }); };
        view.name = u8"보기 방식";
        const float view_height { luil::choice_group_element::height_for(view) };

        page_column column { u8"groups" };
        column.note(u8"groups-hint", u8"제목 줄을 누르면 접힌다.");
        column.note(u8"groups-hint-2", u8"접힌 내용은 보이지도 눌리지도 않는다.");
        column.gap(10.0f);
        column.add(std::move(group), group_height);
        column.gap(24.0f);
        column.note(u8"view-hint", u8"보기 방식 (토글 묶음)");
        column.gap(6.0f);
        column.add(std::make_unique<luil::choice_group_element>(std::move(view)), view_height, std::min(width - 2.0f * page_padding, 360.0f));
        return column.finish();
    }
} // namespace mobile_demo
