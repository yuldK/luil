#include "mobile_demo/tabs_page.h"

#include "luil/generated/codicons.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/tab_bar_element.h"

#include <utility>

namespace mobile_demo {
    namespace {
        constexpr float tab_bar_height { 36.0f };
        constexpr float overflow_menu_width { 200.0f };
        constexpr std::u8string_view bar_owner { u8"demo-tabs" };
    } // namespace

    tabs_page::tabs_page()
    {
        for (next_tab_ = 1; next_tab_ <= 8; ++next_tab_)
            tabs_.push_back({ u8"tab-" + to_u8(next_tab_), u8"문서 " + to_u8(next_tab_) });
        selected_ = tabs_.front().key;
    }

    bool tabs_page::handle(const luil::app_message& message)
    {
        if (const auto* const select { message.get<tab_select_intent>() }; select != nullptr)
        {
            selected_ = select->key;
            // 넘침 메뉴에서 골랐을 수도 있으니 함께 닫는다.
            overflow_open_ = false;
            return true;
        }
        if (const auto* const close { message.get<tab_close_intent>() }; close != nullptr)
        {
            for (auto position = tabs_.begin(); position != tabs_.end(); ++position)
            {
                if (position->key != close->key)
                    continue;
                const bool was_selected { selected_ == position->key };
                position = tabs_.erase(position);
                if (was_selected && tabs_.empty() == false)
                    selected_ = (position != tabs_.end() ? position : tabs_.end() - 1)->key;
                break;
            }
            return true;
        }
        if (const auto* const reorder { message.get<tab_reorder_intent>() }; reorder != nullptr)
        {
            if (reorder->moved == reorder->target)
                return true;
            auto find = [this](const std::u8string& key) {
                for (auto position = tabs_.begin(); position != tabs_.end(); ++position)
                    if (position->key == key)
                        return position;
                return tabs_.end();
            };
            const auto moved { find(reorder->moved) };
            if (moved == tabs_.end())
                return true;
            const tab_entry entry { *moved };
            tabs_.erase(moved);
            tabs_.insert(find(reorder->target), entry);
            return true;
        }
        if (const auto* const scroll { message.get<tab_scroll_intent>() }; scroll != nullptr)
        {
            scroll_ += scroll->delta;
            if (scroll_ < 0.0f)
                scroll_ = 0.0f;
            return true;
        }
        if (const auto* const overflow { message.get<tab_overflow_intent>() }; overflow != nullptr)
        {
            overflow_open_ = overflow->open;
            return true;
        }
        return false;
    }

    std::unique_ptr<luil::ui_element> tabs_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(height);
        static_cast<void>(scale);
        bar_ = nullptr;

        luil::tab_bar_config bar_config {};
        bar_config.owner = std::u8string { bar_owner };
        for (const tab_entry& tab : tabs_)
        {
            luil::tab_item item {};
            item.key = tab.key;
            item.label = tab.label;
            item.icon = luil::codicons::icon_file;
            item.closable = true;
            bar_config.items.push_back(std::move(item));
        }
        bar_config.selected = selected_;
        // 휴대폰 폭에 두세 개가 들어가 넘침을 바로 볼 수 있는 폭이다.
        bar_config.tab_width = 140.0f;
        bar_config.scroll_offset = scroll_;
        bar_config.select = [](const std::u8string& key) { return luil::make_app_action(tab_select_intent { key }); };
        bar_config.close = [](const std::u8string& key) { return luil::make_app_action(tab_close_intent { key }); };
        bar_config.reorder = [](const std::u8string& moved, const std::u8string& target) { return luil::make_app_action(tab_reorder_intent { moved, target }); };
        bar_config.overflow = luil::make_message_action(tab_overflow_intent { true });
        bar_config.overflow_tooltip = u8"넘친 탭 보기";
        // 넘친 레인을 손가락으로 옆으로 쓸어 넘긴다. 휠과 같은 메시지다.
        bar_config.scroll = [](const float value) { return luil::make_app_action(tab_scroll_intent { value }); };
        auto bar { std::make_unique<luil::tab_bar_element>(std::move(bar_config)) };
        bar_ = bar.get();

        luil::stack_config config {};
        config.padding = luil::edge_insets::all(page_padding);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"tabs" }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"tabs-hint" }, u8"옆으로 쓸어 넘기고 끌어서 옮긴다.", note_text_size, luil::label_color_role::dim), note_line_height);
        column->add(make_label(luil::ui_element_id { kind_text, u8"tabs-hint-2" }, u8"넘치면 오른쪽 끝에 넘침 메뉴가 나온다.", note_text_size, luil::label_color_role::dim), note_line_height);
        column->add_gap(8.0f);
        column->add(std::move(bar), tab_bar_height);
        column->add_gap(16.0f);

        // 내용 영역은 앱 몫이다: 고른 탭을 보여 주는 판이다.
        std::u8string current { u8"열린 탭이 없다" };
        for (const tab_entry& tab : tabs_)
            if (tab.key == selected_)
                current = tab.label + u8" 내용";
        luil::panel_config surface_panel {};
        surface_panel.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        surface_panel.corner_radius = 6.0f;
        auto content_panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_page_panel, u8"tab-content" }, std::move(surface_panel)) };
        luil::stack_config content_config {};
        content_config.padding = luil::edge_insets::all(16.0f);
        auto content { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"tab-content" }, content_config) };
        content->add(make_label(luil::ui_element_id { kind_text, u8"tab-current" }, std::move(current), body_text_size, luil::label_color_role::primary), body_line_height);
        content_panel->set_content(std::move(content));
        column->add_flexible(std::move(content_panel));
        return column;
    }

    std::vector<luil::ui_popup> tabs_page::make_popups(const float scale) const
    {
        if (overflow_open_ == false || bar_ == nullptr || tabs_.empty())
            return {};

        // 열린 탭 목록이다. 고른 탭에 표식을 두고 선택하면 메뉴가 함께 닫힌다.
        luil::menu_config menu {};
        menu.owner = u8"tab-overflow";
        for (const tab_entry& tab : tabs_)
        {
            luil::menu_item_config item {};
            item.key = tab.key;
            item.label = tab.label;
            if (tab.key == selected_)
                item.icon = luil::codicons::icon_check;
            menu.items.push_back(std::move(item));
        }
        menu.select = [](const std::u8string& key) { return luil::make_app_action(tab_select_intent { key }); };

        // 탭 막대의 오른쪽 아래에 붙인다.
        // 자리는 이번 frame에서 배치된 막대의 bounds를 논리 픽셀로 되돌려 얻는다.
        const luil::rect_f bar_bounds { bar_->bounds() };
        luil::ui_popup popup {};
        popup.id = u8"tab-overflow";
        popup.width = overflow_menu_width;
        popup.height = luil::menu_element::height_for(menu);
        popup.x = (bar_bounds.x + bar_bounds.width) / scale - overflow_menu_width;
        popup.y = (bar_bounds.y + bar_bounds.height) / scale + 2.0f;
        auto root { std::make_unique<luil::menu_element>(std::move(menu)) };
        popup.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, popup.width * scale, popup.height * scale }, scale));
        popup.dismiss = [](luil::popup_dismiss_reason) { return luil::make_app_action(tab_overflow_intent { false }); };
        return { std::move(popup) };
    }

    std::vector<luil::input_action> tabs_page::route_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, const float delta)
    {
        static const luil::scroll_route routes[] {
            { { luil::ui_element_kind::tab_bar, std::u8string { bar_owner } }, [](const float value) { return luil::make_app_action(tab_scroll_intent { value }); } },
        };
        return luil::route_wheel(tree, event.x, event.y, delta, routes);
    }

    std::vector<luil::input_action> tabs_page::route_reveal(const luil::ui_tree& tree, const luil::ui_element_id& focused)
    {
        // 휠이 대는 바깥 막대가 아니라 **안쪽 레인**을 이름 댄다.
        // 넘침 버튼이 보일 때 막대의 bounds가 실제 레인보다 그 버튼 폭만큼 넓어,
        // 바깥을 대면 마지막 탭이 버튼 밑에 남는다 (focus-reveal-design.md).
        static const luil::scroll_route routes[] {
            { { luil::ui_element_kind::tab_strip, std::u8string { bar_owner } }, [](const float value) { return luil::make_app_action(tab_scroll_intent { value }); } },
        };
        return luil::route_reveal(tree, focused, routes);
    }
} // namespace mobile_demo
