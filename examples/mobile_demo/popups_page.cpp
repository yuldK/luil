#include "mobile_demo/popups_page.h"

#include "luil/generated/codicons.h"
#include "luil/ui/dropdown_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_tree.h"

#include <algorithm>
#include <utility>

namespace mobile_demo {
    namespace {
        constexpr float dropdown_width { 200.0f };
        constexpr float context_menu_width { 180.0f };
        inline constexpr std::u8string_view renderer_values[] { u8"자동", u8"CPU", u8"Vulkan" };
    } // namespace

    void popups_page::close_dropdown() noexcept
    {
        dropdown_open_ = false;
        search_ = {};
        composition_.reset();
    }

    bool popups_page::handle(const luil::app_message& message)
    {
        if (const auto* const toggle { message.get<dropdown_toggle_intent>() }; toggle != nullptr)
        {
            if (toggle->open == false)
            {
                close_dropdown();
                return true;
            }
            dropdown_open_ = true;
            // 한 번에 하나만 띄운다.
            menu_open_ = false;
            return true;
        }
        if (const auto* const select { message.get<dropdown_select_intent>() }; select != nullptr)
        {
            renderer_ = select->value;
            close_dropdown();
            return true;
        }
        // 검색 칸의 편집·조합이다.
        // popup 안이라고 다른 경로가 아니다 — target으로 자기 것만 갖는다.
        if (const auto* const edit { message.get<edit_intent>() }; edit != nullptr)
        {
            if (edit->request.target != target_dropdown_search)
                return false;
            luil::apply_text_edit(search_, edit->request);
            return true;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            if (composition->event.target != target_dropdown_search)
                return false;
            if (composition->event.composing)
                composition_ = composition->event;
            else
                composition_.reset();
            return true;
        }
        if (const auto* const open { message.get<card_menu_intent>() }; open != nullptr)
        {
            menu_open_ = true;
            menu_x_ = open->x;
            menu_y_ = open->y;
            close_dropdown();
            return true;
        }
        if (message.get<card_menu_close_intent>() != nullptr || message.get<card_menu_select_intent>() != nullptr)
        {
            menu_open_ = false;
            return true;
        }
        return false;
    }

    page_content popups_page::build(const float width, const float scale)
    {
        static_cast<void>(scale);
        dropdown_ = nullptr;

        page_column column { u8"popups" };
        column.note(u8"popups-hint", u8"popup은 화면 위에 겹쳐 뜬다.");
        column.note(u8"popups-hint-2", u8"바깥을 누르거나 뒤로 가기로 닫는다.");
        column.gap(16.0f);

        column.note(u8"dropdown-label", u8"렌더러 (드롭다운)");
        column.gap(4.0f);
        luil::dropdown_config dropdown {};
        dropdown.owner = u8"renderer";
        dropdown.text = renderer_;
        dropdown.open = dropdown_open_;
        // 절대 메시지 하나로 누름(지금 상태의 반대)과 보조 기술의 Expand·Collapse가 함께 선다.
        dropdown.set_open = [](const bool open) { return luil::make_app_action(dropdown_toggle_intent { open }); };
        auto field { std::make_unique<luil::dropdown_element>(std::move(dropdown)) };
        dropdown_ = field.get();
        column.add(std::move(field), luil::dropdown_height, std::min(width - 2.0f * page_padding, dropdown_width));
        column.gap(24.0f);

        // 길게 누르면(마우스는 오른쪽 클릭) 컨텍스트 메뉴를 여는 카드다. 길게 누르기는 라이브러리가
        // 오른쪽 클릭으로 옮겨 주므로 앱은 한 계기만 듣는다.
        column.note(u8"card-label", u8"길게 누르면 메뉴가 뜬다");
        column.gap(4.0f);
        luil::panel_config surface_panel {};
        surface_panel.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        surface_panel.corner_radius = 8.0f;
        auto card { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_card }, std::move(surface_panel)) };
        card->set_cursor(luil::ui_cursor::hand);
        card->set_action(luil::ui_trigger::right_click,
            [](const luil::ui_action_context& context) -> std::vector<luil::input_action> { return { luil::make_app_action(card_menu_intent { context.x, context.y }) }; });
        luil::stack_config card_config {};
        card_config.padding = luil::edge_insets::all(16.0f);
        auto card_content { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"card" }, card_config) };
        card_content->add(make_label(luil::ui_element_id { kind_text, u8"card-title" }, u8"보고서.md", body_text_size, luil::label_color_role::primary), body_line_height);
        card_content->add(make_label(luil::ui_element_id { kind_text, u8"card-body" }, u8"고르면 토스트로 알려 준다.", note_text_size, luil::label_color_role::dim), note_line_height);
        card_content->add(make_label(luil::ui_element_id { kind_text, u8"card-body-2" }, u8"항목 하나는 비활성이다.", note_text_size, luil::label_color_role::dim), note_line_height);
        card->set_content(std::move(card_content));
        column.add(std::move(card), 32.0f + body_line_height + 2.0f * note_line_height);
        return column.finish();
    }

    std::vector<luil::ui_popup> popups_page::make_popups(const float scale) const
    {
        std::vector<luil::ui_popup> popups {};

        if (dropdown_open_ && dropdown_ != nullptr)
        {
            const luil::text_input_view search { luil::make_text_input_view(search_, composition_, target_dropdown_search) };

            std::vector<luil::menu_item_config> values {};
            for (const std::u8string_view value : renderer_values)
            {
                luil::menu_item_config item {};
                item.key = std::u8string { value };
                item.label = std::u8string { value };
                if (item.key == renderer_)
                    item.icon = luil::codicons::icon_check;
                values.push_back(std::move(item));
            }

            luil::menu_config menu {};
            menu.owner = u8"renderer";
            menu.items = narrow_menu_items(std::move(values), search);
            menu.select = [](const std::u8string& value) { return luil::make_app_action(dropdown_select_intent { value }); };

            // 칸 바로 아래에 붙인다.
            const luil::rect_f field { dropdown_->bounds() };
            luil::ui_popup popup {};
            popup.id = u8"dropdown";
            popup.width = dropdown_width;
            popup.height = search_menu_height(menu);
            popup.x = field.x / scale;
            popup.y = (field.y + field.height) / scale + 2.0f;
            popup.tree = make_search_menu_tree(std::move(menu), kind_dropdown_search_input, search, u8"검색", popup.width, scale);
            popup.dismiss = [](const luil::popup_dismiss_reason reason) {
                // 검색 중이던 목록이다 — 계기를 가려서 남는다.
                if (search_popup_stays(reason))
                    return luil::input_action {};
                return luil::make_app_action(dropdown_toggle_intent { false });
            };
            popups.push_back(std::move(popup));
        }

        if (menu_open_)
        {
            luil::menu_config menu {};
            menu.owner = u8"card";
            menu.items.push_back({ u8"열기", u8"열기", luil::codicons::icon_go_to_file });
            menu.items.push_back({ u8"복사", u8"경로 복사", luil::codicons::icon_copy });
            luil::menu_item_config remove {};
            remove.key = u8"지우기";
            remove.label = u8"지우기";
            remove.icon = luil::codicons::icon_trash;
            remove.enabled = false;
            remove.separator_above = true;
            menu.items.push_back(std::move(remove));
            // 선택이 메뉴를 닫는 것도 앱 몫이다.
            // 셸이 이 메시지를 받아 토스트 알림으로 잇는다.
            menu.select = [](const std::u8string& key) { return luil::make_app_action(card_menu_select_intent { key }); };

            luil::ui_popup popup {};
            popup.id = u8"card-menu";
            popup.width = context_menu_width;
            popup.height = luil::menu_element::height_for(menu);
            popup.x = menu_x_ / scale;
            popup.y = menu_y_ / scale;
            auto root { std::make_unique<luil::menu_element>(std::move(menu)) };
            popup.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, popup.width * scale, popup.height * scale }, scale));
            popup.dismiss = [](luil::popup_dismiss_reason) { return luil::make_app_action(card_menu_close_intent {}); };
            popups.push_back(std::move(popup));
        }
        return popups;
    }
} // namespace mobile_demo
