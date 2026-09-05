#include "demo/lists_page.h"

#include "luil/generated/codicons.h"
#include "luil/ui/grouped_list_element.h"
#include "luil/ui/list_element.h"
#include "luil/ui/scroll_view_element.h"
#include "luil/ui/split_handle_element.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace demo {
    namespace {
        // 목록 배치의 길이들이다 (논리 픽셀).
        constexpr float list_row_height { 30.0f };
        constexpr float tree_row_height { 24.0f };
        constexpr float panel_inset { 8.0f };
        constexpr float top_row_gap { 12.0f };
        constexpr float grouped_row_height { 22.0f };
        // 두 판이 반드시 받는 높이와, 나눈 자리를 잡는 손잡이의 두께다.
        // 손잡이는 두 판 사이의 한 칸을 차지하며, 그 칸의 총량은 배치 계산에 포함된다.
        constexpr float list_panel_minimum { 120.0f };
        constexpr float grouped_minimum { 88.0f };
        constexpr float split_handle_height { 16.0f };
        // 두 판 밖에서 늘 자리를 차지하는 것들의 합이다 (여백·힌트·간격·손잡이).
        constexpr float split_fixed { 2.0f * 24.0f + 18.0f + 8.0f + split_handle_height + 18.0f + 6.0f };

        // tree view가 보여 주는 고정 트리다 (pre-order).
        // 무엇이 펼쳐졌는지는 **앱 상태**이고, 목록에는 "지금 보이는 줄들"만 준다 —
        // 그래서 라이브러리는 계층을 모른 채 들여쓰기만 그린다.
        struct tree_node
        {
            std::u8string_view key {};
            std::u8string_view label {};
            int depth { 0 };
            bool branch { false };
        };

        constexpr tree_node tree_nodes[] {
            { u8"src", u8"src", 0, true },
            { u8"src/ui", u8"ui", 1, true },
            { u8"src/ui/list_element.cpp", u8"list_element.cpp", 2, false },
            { u8"src/ui/ui_tree.cpp", u8"ui_tree.cpp", 2, false },
            { u8"src/win32", u8"win32", 1, true },
            { u8"src/win32/win32_window.cpp", u8"win32_window.cpp", 2, false },
            { u8"docs", u8"docs", 0, true },
            { u8"docs/list-view-design.md", u8"list-view-design.md", 1, false },
            { u8"docs/components.md", u8"components.md", 1, false },
            { u8"README.md", u8"README.md", 0, false },
        };
    } // namespace

    lists_page::lists_page()
    {
        for (int index = 1; index <= 24; ++index)
            items_.push_back(u8"항목 " + to_u8(index));
        // 처음에는 뿌리 둘만 펼쳐 둔다.
        tree_expanded_.emplace_back(u8"src");
        tree_expanded_.emplace_back(u8"docs");
    }

    bool lists_page::is_expanded(const std::u8string& key) const
    {
        return std::find(tree_expanded_.begin(), tree_expanded_.end(), key) != tree_expanded_.end();
    }

    bool lists_page::handle(const luil::app_message& message)
    {
        if (const auto* const scroll { message.get<list_scroll_intent>() }; scroll != nullptr)
        {
            scroll_ += scroll->delta;
            return true;
        }
        if (const auto* const scroll_to { message.get<list_scroll_to_intent>() }; scroll_to != nullptr)
        {
            scroll_ = scroll_to->offset;
            return true;
        }
        if (const auto* const scroll { message.get<grouped_scroll_intent>() }; scroll != nullptr)
        {
            grouped_scroll_ += scroll->delta;
            return true;
        }
        if (const auto* const split { message.get<split_intent>() }; split != nullptr)
        {
            // 다듬지 않고 그대로 더한다. 한계가 담긴 자리에 달려 있어 build가 다듬고
            // 그때 상태에 되돌려 준다 (사이드바는 한계가 상수라 여기서 자른다).
            grouped_height_ += split->delta;
            return true;
        }
        if (const auto* const select { message.get<tree_select_intent>() }; select != nullptr)
        {
            tree_selected_ = select->key;
            return true;
        }
        if (const auto* const expand { message.get<tree_expand_intent>() }; expand != nullptr)
        {
            // 펼침은 키의 집합이다. 목표 상태 그대로 넣고 뺀다 — 같은 메시지가
            // 겹쳐 와도 결과가 같다.
            const auto found { std::find(tree_expanded_.begin(), tree_expanded_.end(), expand->key) };
            if (expand->expanded && found == tree_expanded_.end())
                tree_expanded_.push_back(expand->key);
            else if (expand->expanded == false && found != tree_expanded_.end())
                tree_expanded_.erase(found);
            return true;
        }
        if (const auto* const scroll { message.get<tree_scroll_intent>() }; scroll != nullptr)
        {
            tree_scroll_ += scroll->delta;
            return true;
        }
        if (const auto* const scroll_to { message.get<tree_scroll_to_intent>() }; scroll_to != nullptr)
        {
            tree_scroll_ = scroll_to->offset;
            return true;
        }
        if (const auto* const reorder { message.get<reorder_intent>() }; reorder != nullptr)
        {
            if (reorder->moved == reorder->target)
                return true;
            auto find = [this](const std::u8string& value) {
                for (auto position = items_.begin(); position != items_.end(); ++position)
                    if (*position == value)
                        return position;
                return items_.end();
            };
            const auto moved { find(reorder->moved) };
            if (moved == items_.end())
                return true;
            const std::u8string value { *moved };
            items_.erase(moved);
            items_.insert(find(reorder->target), value);
            return true;
        }
        return false;
    }

    std::unique_ptr<luil::ui_element> lists_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(scale);
        luil::stack_config config {};
        config.padding = luil::edge_insets::all(24.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"lists" }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"lists-hint" },
                        u8"왼쪽은 끌어서 순서를 바꾸는 목록, 오른쪽은 고르고 접는 tree다 (Tab으로 들어가 ↑↓·Home/End·글자로 옮긴다). 두 목록 사이를 끌면 자리를 나눈다.", 11.0f,
                        luil::label_color_role::dim),
            18.0f);
        column->add_gap(8.0f);

        // 두 목록이 나머지를 나눠 갖고, 나눈 자리는 손잡이가 옮긴다.
        // 아래 판의 높이가 앱 상태이고 위 판이 나머지를 갖는다 (창이 커지면 위가 먹는다).
        const float budget { height - split_fixed };
        float grouped_height { grouped_height_ };
        const float grouped_maximum { budget - list_panel_minimum };
        if (grouped_height > grouped_maximum)
            grouped_height = grouped_maximum;
        if (grouped_height < grouped_minimum)
            grouped_height = grouped_minimum;
        // 다듬은 값을 상태에 되돌려 다음 끌기가 범위 밖에서 시작하지 않게 한다.
        //  - 창이 너무 낮아 두 하한이 함께 설 수 없으면(범위가 빈다) 되쓰지 않는다.
        //    그때 되쓰면 창을 다시 키워도 사용자가 정한 자리가 돌아오지 않는다 —
        //    넘침은 화면에 보이지만 지워진 상태는 보이지 않는다.
        if (grouped_maximum >= grouped_minimum)
            grouped_height_ = grouped_height;
        // 위 판의 하한은 넘침을 만든다 (아래로 밀려난다). stack의 하한과 같은 규칙이다.
        float panel_height { budget - grouped_height };
        if (panel_height < list_panel_minimum)
            panel_height = list_panel_minimum;

        column->add(make_top_row(panel_height - 2.0f * panel_inset), panel_height);
        column->add(make_split_handle(), split_handle_height);
        column->add(
            make_label(luil::ui_element_id { kind_text, u8"grouped-hint" }, u8"그룹 머리행은 스크롤 중에도 위에 붙고 다음 머리행에 밀려난다.", 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(6.0f);
        column->add(make_grouped_section(grouped_height), grouped_height);
        return column;
    }

    std::unique_ptr<luil::ui_element> lists_page::make_split_handle()
    {
        // 세로로 나눈 자리라 위아래로 끈다. 임자는 아래 판이므로 위로 끌 때 넓어진다.
        luil::split_handle_config config {};
        config.axis = luil::split_axis::vertical;
        config.grows = luil::split_grows::toward_start;
        config.resize = [](const float delta) { return luil::make_app_action(split_intent { delta }); };
        return std::make_unique<luil::split_handle_element>(luil::ui_element_id { kind_list_split }, config);
    }

    std::unique_ptr<luil::ui_element> lists_page::make_top_row(const float viewport_height)
    {
        // 같은 element의 파생 둘을 나란히 세운다.
        // 왼쪽은 `reorder`만, 오른쪽은 `select`+`toggle`이다 — 그래서 왼쪽에는
        // Tab이 서지 않고 오른쪽에는 선다. 그 차이가 화면에서 그대로 보인다.
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = top_row_gap;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"top-lists" }, config) };
        row->add_flexible(make_list_panel(viewport_height));
        row->add_flexible(make_tree_panel(viewport_height));
        return row;
    }

    std::unique_ptr<luil::ui_element> lists_page::make_tree_panel(const float viewport_height)
    {
        // 펼친 가지만 따라 내려가며 **지금 보이는 줄들**을 편다.
        // 접힌 가지의 자식은 아예 담지 않으므로 라이브러리는 계층을 몰라도 된다.
        luil::list_config config {};
        config.owner = u8"tree";
        int hidden_below { -1 };
        for (const tree_node& node : tree_nodes)
        {
            if (hidden_below >= 0 && node.depth > hidden_below)
                continue;
            hidden_below = -1;
            const std::u8string key { node.key };
            const bool expanded { node.branch && is_expanded(key) };
            luil::list_item item {};
            item.key = key;
            item.label = std::u8string { node.label };
            item.icon = node.branch ? (expanded ? luil::codicons::icon_folder_opened : luil::codicons::icon_folder) : luil::codicons::icon_file;
            item.depth = node.depth;
            if (node.branch)
                item.expansion = expanded ? luil::list_expansion::expanded : luil::list_expansion::collapsed;
            config.items.push_back(std::move(item));
            if (node.branch && expanded == false)
                hidden_below = node.depth;
        }

        const float content_height { static_cast<float>(config.items.size()) * tree_row_height };
        tree_scroll_ = luil::clamp_scroll(content_height, viewport_height, tree_scroll_);
        config.row_height = tree_row_height;
        config.scroll_offset = tree_scroll_;
        config.selected = tree_selected_;
        config.select = [](const std::u8string& key) { return luil::make_app_action(tree_select_intent { key }); };
        // 절대 상태 factory 하나로 삼각형 클릭과 보조 기술의 펼치기가 함께 선다.
        config.set_expanded = [](const std::u8string& key, const bool expanded) { return luil::make_app_action(tree_expand_intent { key, expanded }); };
        config.scroll = [](const float delta) { return luil::make_app_action(tree_scroll_intent { delta }); };
        config.scroll_to = [](const float offset) { return luil::make_app_action(tree_scroll_to_intent { offset }); };

        luil::stack_config inset {};
        inset.padding = luil::edge_insets::all(panel_inset);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"tree" }, inset) };
        column->add_flexible(std::make_unique<luil::list_element>(std::move(config)));

        luil::panel_config surface_panel {};
        surface_panel.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        surface_panel.corner_radius = 6.0f;
        auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_list_panel, u8"tree" }, std::move(surface_panel)) };
        panel->set_content(std::move(column));
        return panel;
    }

    std::unique_ptr<luil::ui_element> lists_page::make_list_panel(const float viewport_height)
    {
        // 스크롤 값을 창 높이에 맞춰 다듬는다.
        // clamp_scroll은 목록의 arrange와 같은 식이라 앱 상태와 목록이 같은 값을 본다.
        const float content_height { static_cast<float>(items_.size()) * list_row_height };
        scroll_ = luil::clamp_scroll(content_height, viewport_height, scroll_);

        // 앱이 대는 것은 항목·스크롤 값과 메시지 factory뿐이다.
        // 행 그리기·끌기·막대 조립·창 다듬기는 전부 목록의 몫이 되었다.
        luil::list_config config {};
        config.owner = u8"items";
        for (const std::u8string& value : items_)
        {
            luil::list_item item {};
            item.key = value;
            item.label = value;
            config.items.push_back(std::move(item));
        }
        config.row_height = list_row_height;
        config.scroll_offset = scroll_;
        config.reorder = [](const std::u8string& moved, const std::u8string& target) { return luil::make_app_action(reorder_intent { moved, target }); };
        // 세로 목록이라 끄는 동안은 위아래 화살표다 (평소 모양 grab은 라이브러리가 고른다).
        config.row_active_cursor = cursor_reorder;
        config.scroll = [](const float delta) { return luil::make_app_action(list_scroll_intent { delta }); };
        config.scroll_to = [](const float offset) { return luil::make_app_action(list_scroll_to_intent { offset }); };

        // 목록 둘레의 여백은 담는 쪽의 몫이다.
        luil::stack_config inset {};
        inset.padding = luil::edge_insets::all(panel_inset);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"list" }, inset) };
        column->add_flexible(std::make_unique<luil::list_element>(std::move(config)));

        luil::panel_config surface_panel {};
        surface_panel.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        surface_panel.corner_radius = 6.0f;
        auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_list_panel }, std::move(surface_panel)) };
        panel->set_content(std::move(column));
        return panel;
    }

    std::unique_ptr<luil::ui_element> lists_page::make_grouped_section(const float viewport_height)
    {
        // 머리행을 누르면 어느 그룹인지 토스트로 보여 준다 (activate factory의 예).
        luil::grouped_list_config config {};
        config.owner = u8"history";
        config.activate = [](const std::u8string& key) { return luil::make_app_action(toast_request_intent { u8"그룹 머리행: " + key, luil::toast_severity::info }); };

        const auto make_rows = [](const std::u8string_view prefix, const int count) {
            luil::stack_config rows_config {};
            auto rows { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"rows-" + std::u8string { prefix } }, rows_config) };
            for (int index = 1; index <= count; ++index)
                rows->add(make_label(luil::ui_element_id { kind_text, std::u8string { prefix } + u8"-" + to_u8(index) }, u8"    " + std::u8string { prefix } + u8" 문서 " + to_u8(index), 11.0f,
                              luil::label_color_role::primary),
                    grouped_row_height);
            return rows;
        };

        std::vector<luil::list_group> groups {};
        const auto add_group = [&](std::u8string key, std::u8string title, const int count) {
            luil::list_group group {};
            group.key = std::move(key);
            group.title = std::move(title);
            group.content_height = static_cast<float>(count) * grouped_row_height;
            group.content = make_rows(group.key, count);
            groups.push_back(std::move(group));
        };
        add_group(u8"오늘", u8"오늘", 4);
        add_group(u8"이번 주", u8"이번 주", 6);
        add_group(u8"지난", u8"지난", 8);

        auto list { std::make_unique<luil::grouped_list_element>(std::move(config), std::move(groups)) };
        const float content_height { list->content_height() };
        grouped_scroll_ = luil::clamp_scroll(content_height, viewport_height, grouped_scroll_);

        auto view { std::make_unique<luil::scroll_view_element>(luil::ui_element_id { kind_grouped_list }, luil::scroll_view_config { content_height, grouped_scroll_ }) };
        view->set_content(std::move(list));

        luil::panel_config surface_panel {};
        surface_panel.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        surface_panel.corner_radius = 6.0f;
        auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_layout, u8"grouped-panel" }, std::move(surface_panel)) };
        panel->set_content(std::move(view));
        return panel;
    }

    std::vector<luil::input_action> lists_page::route_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, const float delta)
    {
        // 어느 목록을 어떤 메시지로 스크롤할지(표)만 적는다.
        // 좌표 판정은 라이브러리의 route_wheel이 한다.
        static const luil::scroll_route routes[] {
            { { luil::ui_element_kind::list, u8"items" }, [](const float value) { return luil::make_app_action(list_scroll_intent { value }); } },
            { { luil::ui_element_kind::list, u8"tree" }, [](const float value) { return luil::make_app_action(tree_scroll_intent { value }); } },
            { { kind_grouped_list }, [](const float value) { return luil::make_app_action(grouped_scroll_intent { value }); } },
        };
        return luil::route_wheel(tree, event.x, event.y, delta, routes);
    }

    std::vector<luil::input_action> lists_page::route_reveal(const luil::ui_tree& tree, const luil::ui_element_id& focused)
    {
        // **창의 id를 이름 댄다.** 목록 바깥(`kind_layout` 패널)은 스크롤 막대 칸까지
        // 품어 창이 아니고, 그것을 대면 마지막 행이 막대 밑에 남는다.
        //  - 휠의 표와 줄이 겹치지만 같은 표가 아니다. 휠은 포인터가 덮는 것을
        //    묻고 되살리기는 초점을 품는 것을 묻는다.
        static const luil::scroll_route routes[] {
            { { luil::ui_element_kind::list_scroll, u8"items" }, [](const float value) { return luil::make_app_action(list_scroll_intent { value }); } },
            { { luil::ui_element_kind::list_scroll, u8"tree" }, [](const float value) { return luil::make_app_action(tree_scroll_intent { value }); } },
            // 그룹 목록의 머리행이 묶음의 자리다 — 창은 그것을 감싼 흘리는 창이다.
            { { kind_grouped_list }, [](const float value) { return luil::make_app_action(grouped_scroll_intent { value }); } },
        };
        return luil::route_reveal(tree, focused, routes);
    }
} // namespace demo
