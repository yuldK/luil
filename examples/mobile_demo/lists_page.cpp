#include "mobile_demo/lists_page.h"

#include "luil/generated/codicons.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/grouped_list_element.h"
#include "luil/ui/list_element.h"
#include "luil/ui/scroll_view_element.h"
#include "luil/ui/split_handle_element.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace mobile_demo {
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
        // 목록 셋을 나란히 둘 수 있는 폭이다 (논리 픽셀). 그보다 좁으면 하나씩 고른다.
        constexpr float wide_lists_width { 720.0f };
        constexpr float view_choice_height { 36.0f };

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

        // --- 가상 목록 (세 번째 판) ---
        // 치수와 모델 크기다.
        constexpr float log_row_height { 26.0f };
        // 커서와 고른 항목을 함께 적는 아래 줄이다.
        // 둘이 갈라져 있다는 것이 화면에 보여야 `move`와 `select`를 왜 나눴는지가
        // 예제에서 읽힌다 — 상태 이름만으로는 아무도 그 차이를 보지 못한다.
        constexpr float log_footer_height { 16.0f };
        constexpr float log_footer_gap { 4.0f };
        // 가상화가 요점이 되는 크기다. 이만큼을 `list_element`에 담으면 element가
        // 만 개 넘게 서고, 그 tree는 frame마다 통째로 다시 지어진다.
        constexpr std::size_t log_item_count { 12000 };
        // 회차 이름 앞에 붙는 갈래다.
        // 글자 탐색이 이 낱말로 후보를 돈다 — 모든 줄이 같은 낱말로 시작하면
        // 글자를 쳐도 좁혀지는 것이 없어 탐색이 있는지조차 보이지 않는다.
        constexpr std::array<std::u8string_view, 5> log_levels { u8"trace", u8"debug", u8"info", u8"warn", u8"error" };

        // 다섯 자리로 맞춘 회차 번호다.
        // 자리를 맞추지 않으면 badge 안의 글 폭이 줄마다 달라져 눈금이 흔들린다.
        [[nodiscard]] std::u8string log_number(const std::size_t index)
        {
            std::u8string digits { to_u8(static_cast<int>(index)) };
            while (digits.size() < 5u)
                digits.insert(digits.begin(), u8'0');
            return digits;
        }

        [[nodiscard]] std::u8string log_key(const std::size_t index)
        {
            return u8"log-" + log_number(index);
        }

        // 오른쪽 칸에 서는 보조 값이다 (그 회차가 걸린 시간).
        // 색인에서 바로 뽑으므로 모델을 두 벌 들지 않는다.
        [[nodiscard]] std::u8string log_elapsed_text(const std::size_t index)
        {
            return to_u8(static_cast<int>(index * 37u % 900u + 12u)) + u8" ms";
        }

        // 그 키의 이름이다. 모델에 없으면 "없음"이라 아래 줄이 거짓말을 하지 않는다.
        [[nodiscard]] std::u8string log_label_of(const std::vector<luil::virtual_list_item>& items, const std::u8string& key)
        {
            for (const luil::virtual_list_item& item : items)
                if (item.key == key)
                    return item.label;
            return u8"(없음)";
        }

        // 행 하나의 **내용**이다.
        // 자리표·선택 표시·누름 액션·글자 탐색 이름은 그래도 목록이 쥔다 — 앱이
        // 행을 통째로 만들면 앱마다 선택과 키보드를 다시 짜고 그중 하나를 반드시 틀린다.
        //  - **아무것도 붙잡지 않는다.** tree를 짓는 동안 불리는 순수한 함수라야
        //    하고, 필요한 것이 항목과 색인뿐이라 붙잡을 것도 없다. 페이지를 붙잡으면
        //    게시된 tree가 앱 상태를 가리키는 셈이 된다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_log_row(const luil::virtual_list_item& item, const std::size_t index, const bool selected)
        {
            luil::stack_config config {};
            config.direction = luil::stack_direction::row;
            config.spacing = 8.0f;
            config.padding = luil::edge_insets::symmetric(8.0f, 3.0f);
            auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"log-row-" + item.key }, config) };

            // 색인 badge다. 바탕이 있어야 번호가 이름과 한 덩어리로 읽히지 않는다.
            luil::label_config badge {};
            badge.text = log_number(index);
            badge.font_size = 10.0f;
            badge.color = luil::label_color_role::dim;
            badge.background = luil::label_background_role::notice;
            badge.padding = 6.0f;
            row->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"log-index-" + item.key }, std::move(badge)), 52.0f);

            // 고름의 판정은 목록이 한다 — 앱은 그 답을 받아 글의 색만 고른다.
            const luil::label_color_role color { selected ? luil::label_color_role::primary : luil::label_color_role::dim };
            row->add_flexible(make_label(luil::ui_element_id { kind_text, u8"log-label-" + item.key }, item.label, 11.0f, color));
            // 오른쪽 끝의 보조 칸이다. 고정 길이를 마지막에 담으면 남는 자리를 앞의
            // 유연 항목이 다 먹으므로 이 칸이 오른쪽 끝에 붙는다.
            row->add(make_label(luil::ui_element_id { kind_text, u8"log-elapsed-" + item.key }, log_elapsed_text(index), 10.0f, luil::label_color_role::dim), 56.0f);
            return row;
        }
    } // namespace

    lists_page::lists_page()
    {
        for (int index = 1; index <= 24; ++index)
            items_.push_back(u8"항목 " + to_u8(index));
        // 처음에는 뿌리 둘만 펼쳐 둔다.
        tree_expanded_.emplace_back(u8"src");
        tree_expanded_.emplace_back(u8"docs");

        // 만 줄이 넘는 모델을 **값으로** 한 번 짓는다.
        // 여기까지는 복사 한 번이고, tree에 서는 것은 창에 걸치는 몇 줄뿐이다.
        log_items_.reserve(log_item_count);
        for (std::size_t index = 0; index < log_item_count; ++index)
        {
            luil::virtual_list_item item {};
            item.key = log_key(index);
            item.label = std::u8string { log_levels[index % log_levels.size()] } + u8" " + log_number(index);
            log_items_.push_back(std::move(item));
        }
        // 커서의 첫 자리를 앱이 적어 둔다. 비워 두면 목록은 첫 항목을 커서로 보는데
        // 앱 상태는 비어 있어, 같은 자리를 둘이 다르게 알고 아래 줄이 없는 것을 가리킨다.
        log_cursor_ = log_items_.front().key;
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
        if (const auto* const view { message.get<list_view_intent>() }; view != nullptr)
        {
            view_ = view->value;
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
        if (const auto* const select { message.get<log_select_intent>() }; select != nullptr)
        {
            // 고르는 것이 커서도 함께 옮긴다. 목록은 누름에 `select`만 내므로,
            // 여기서 커서를 두고 오면 다음 화살표가 아까 서 있던 자리에서 이어져
            // 방금 누른 줄에서 달아난다.
            log_selected_ = select->key;
            log_cursor_ = select->key;
            return true;
        }
        if (const auto* const cursor { message.get<log_cursor_intent>() }; cursor != nullptr)
        {
            // 화살표·Page·Home/End·글자는 **커서만** 옮긴다.
            // 훑는 것과 고르는 것을 가른 쪽을 이 페이지가 고른 것이고, 그 선택이
            // 앱의 것이라 element는 옮길 키만 실어 보낸다.
            log_cursor_ = cursor->key;
            return true;
        }
        if (const auto* const scroll { message.get<log_scroll_intent>() }; scroll != nullptr)
        {
            log_scroll_ += scroll->delta;
            return true;
        }
        if (const auto* const scroll_to { message.get<log_scroll_to_intent>() }; scroll_to != nullptr)
        {
            log_scroll_ = scroll_to->offset;
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
        static_cast<void>(scale);
        const bool narrow { width < wide_lists_width };
        luil::stack_config config {};
        config.padding = luil::edge_insets::all(page_padding);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"lists" }, config) };
        // 좁은 화면은 목록 셋 가운데 하나만 보인다. 넓으면 셋이 나란히 선다.
        float header_height { 0.0f };
        if (narrow)
        {
            column->add(make_view_choice(), view_choice_height);
            column->add_gap(8.0f);
            header_height = view_choice_height + 8.0f;
        }
        else
        {
            column->add(make_label(luil::ui_element_id { kind_text, u8"lists-hint" }, u8"순서를 바꾸는 목록, 접는 tree, 만 줄의 가상 목록이다.", note_text_size, luil::label_color_role::dim),
                note_line_height);
            column->add_gap(8.0f);
            header_height = note_line_height + 8.0f;
        }

        // 두 판이 나머지를 나눠 갖고, 나눈 자리는 손잡이가 옮긴다.
        // 아래 판의 높이가 앱 상태이고 위 판이 나머지를 갖는다 (화면이 커지면 위가 먹는다).
        const float budget { height - (2.0f * page_padding + header_height + split_handle_height + note_line_height + 6.0f) };
        float grouped_height { grouped_height_ };
        const float grouped_maximum { budget - list_panel_minimum };
        if (grouped_height > grouped_maximum)
            grouped_height = grouped_maximum;
        if (grouped_height < grouped_minimum)
            grouped_height = grouped_minimum;
        // 다듬은 값을 상태에 되돌려 다음 끌기가 범위 밖에서 시작하지 않게 한다. 두 하한이 함께 설 수
        // 없으면(범위가 빈다) 되쓰지 않는다 — 화면을 돌려 다시 키웠을 때 정한 자리가 돌아온다.
        if (grouped_maximum >= grouped_minimum)
            grouped_height_ = grouped_height;
        float panel_height { budget - grouped_height };
        if (panel_height < list_panel_minimum)
            panel_height = list_panel_minimum;

        column->add(make_top_row(width, panel_height - 2.0f * panel_inset), panel_height);
        column->add(make_split_handle(), split_handle_height);
        column->add(make_label(luil::ui_element_id { kind_text, u8"grouped-hint" }, u8"머리행은 흘리는 동안 위에 붙는다.", note_text_size, luil::label_color_role::dim), note_line_height);
        column->add_gap(6.0f);
        column->add(make_grouped_section(grouped_height), grouped_height);
        return column;
    }

    std::unique_ptr<luil::ui_element> lists_page::make_view_choice() const
    {
        luil::choice_group_config view {};
        view.owner = u8"list-view";
        view.style = luil::choice_style::toggle;
        view.items = { { u8"list", u8"순서" }, { u8"tree", u8"tree" }, { u8"log", u8"가상 목록" } };
        view.selected = view_;
        view.select = [](const std::u8string& value) { return luil::make_app_action(list_view_intent { value }); };
        view.name = u8"보일 목록";
        return std::make_unique<luil::choice_group_element>(std::move(view));
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

    std::unique_ptr<luil::ui_element> lists_page::make_top_row(const float width, const float viewport_height)
    {
        if (width < wide_lists_width)
        {
            if (view_ == u8"tree")
                return make_tree_panel(viewport_height);
            if (view_ == u8"log")
                return make_log_panel(viewport_height);
            return make_list_panel(viewport_height);
        }
        // 목록 셋을 나란히 세운다.
        // 왼쪽 둘은 같은 `list_element`의 파생이다 — 왼쪽은 `reorder`만, 가운데는
        // `select`+`toggle`이라 왼쪽에는 Tab이 서지 않고 가운데에는 선다.
        // 오른쪽은 **계약이 다른** 목록이다: 행이 아니라 목록 자신이 Tab의 자리이고,
        // 창에 걸치는 줄만 tree에 선다 (virtual-list-design.md의 표 다섯 줄).
        // 그 차이가 한 화면에서 그대로 보인다.
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = top_row_gap;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"top-lists" }, config) };
        row->add_flexible(make_list_panel(viewport_height));
        row->add_flexible(make_tree_panel(viewport_height));
        row->add_flexible(make_log_panel(viewport_height));
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

    std::unique_ptr<luil::ui_element> lists_page::make_log_panel(const float viewport_height)
    {
        // 목록이 받는 높이는 아래 줄과 그 사이 간격을 뺀 나머지다.
        // 다듬기의 기준이 실제 창 높이여야 하므로 여기서 미리 뺀다 — `add_flexible`이
        // 나눠 줄 값을 눈대중으로 적으면 앱과 목록이 다른 창을 재게 된다.
        const float list_height { viewport_height - log_footer_height - log_footer_gap };
        const float content_height { luil::virtual_list_content_height(log_items_, log_row_height) };
        // 다른 두 목록과 같은 자리에서 같은 식으로 다듬는다.
        // 앱이 든 값과 목록이 재는 값이 어긋나면 커서를 따라가는 스크롤이 한 번에
        // 닿지 못한다 — 목록은 다듬은 값에서, 앱은 원값에서 델타를 세기 때문이다.
        log_scroll_ = luil::clamp_scroll(content_height, list_height, log_scroll_);

        luil::virtual_list_config config {};
        config.owner = u8"log";
        // 모델 **전체**를 넘긴다. 창에 걸치지 않는 항목도 담아야 Home/End와 글자
        // 탐색이 화면이 아니라 모델의 끝까지 간다.
        config.items = log_items_;
        config.selected = log_selected_;
        config.cursor = log_cursor_;
        config.row_height = log_row_height;
        config.scroll_offset = log_scroll_;
        // 기본 행은 이름 한 줄이다. 색인 badge와 오른쪽 보조 칸은 앱의 것이라
        // 여기서 짓는다 (그 둘 말고는 전부 목록이 쥔다).
        config.build_row = make_log_row;
        config.select = [](const std::u8string& key) { return luil::make_app_action(log_select_intent { key }); };
        // `move`가 없으면 목록은 Tab의 자리조차 아니다 — 커서가 앱 상태라
        // 라이브러리가 고칠 수 없고, 옮길 길이 없는 화살표는 아무 일도 하지 않는다.
        config.move = [](const std::u8string& key) { return luil::make_app_action(log_cursor_intent { key }); };
        config.scroll = [](const float delta) { return luil::make_app_action(log_scroll_intent { delta }); };
        config.scroll_to = [](const float offset) { return luil::make_app_action(log_scroll_to_intent { offset }); };

        luil::stack_config inset {};
        inset.padding = luil::edge_insets::all(panel_inset);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"log" }, inset) };
        column->add_flexible(std::make_unique<luil::virtual_list_element>(std::move(config)));
        column->add_gap(log_footer_gap);
        // 커서와 고른 항목을 함께 적는다. 화살표로 훑기만 하면 앞의 값만 움직이고,
        // 눌러야 뒤의 값이 따라온다 — 둘을 가른 이유가 이 한 줄에서 보인다.
        const std::u8string footer { u8"커서 " + log_label_of(log_items_, log_cursor_) + u8"  ·  고른 것 " + log_label_of(log_items_, log_selected_) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"log-footer" }, footer, 10.0f, luil::label_color_role::dim), log_footer_height);

        luil::panel_config surface_panel {};
        surface_panel.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        surface_panel.corner_radius = 6.0f;
        auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_list_panel, u8"log" }, std::move(surface_panel)) };
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
        //  - **가상 목록은 이 표에 없다.** 그쪽은 자기 메시지를 든 흘리는 영역을
        //    안에 품고 있어(`scroll_source`) 셸의 표 없는 짝이 찾는다. 여기 한 줄을
        //    더 적으면 같은 factory가 두 곳에 살고 언젠가 한쪽만 고쳐진다.
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
        //  - 가상 목록은 여기에도 없다. 커서는 초점이 아니라 앱 상태라 되살리기의
        //    계기 자체가 오지 않고, 얼마나 흘릴지는 목록이 커서 메시지와 함께 낸다.
        static const luil::scroll_route routes[] {
            { { luil::ui_element_kind::list_scroll, u8"items" }, [](const float value) { return luil::make_app_action(list_scroll_intent { value }); } },
            { { luil::ui_element_kind::list_scroll, u8"tree" }, [](const float value) { return luil::make_app_action(tree_scroll_intent { value }); } },
            // 그룹 목록의 머리행이 묶음의 자리다 — 창은 그것을 감싼 흘리는 창이다.
            { { kind_grouped_list }, [](const float value) { return luil::make_app_action(grouped_scroll_intent { value }); } },
        };
        return luil::route_reveal(tree, focused, routes);
    }
} // namespace mobile_demo
