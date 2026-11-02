#include "mobile_demo/popups_page.h"

#include "luil/app/menu_popup.h"
#include "luil/generated/codicons.h"
#include "luil/ui/dropdown_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/split_button_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_tree.h"

#include <algorithm>
#include <utility>

namespace mobile_demo {
    namespace {
        constexpr float dropdown_width { 200.0f };
        constexpr float context_menu_width { 180.0f };
        // 분할 버튼과 드롭다운 단추의 폭이다 (논리 픽셀). 손가락으로 누르는 화살표라 기본(28)보다 넓다.
        constexpr float field_button_width { 220.0f };
        constexpr float field_arrow_width { 48.0f };
        inline constexpr std::u8string_view renderer_values[] { u8"자동", u8"CPU", u8"Vulkan" };

        // 필드 메뉴의 항목 키가 마지막 동작 label에 적히는 글이다.
        [[nodiscard]] std::u8string field_menu_action_text(const std::u8string_view key)
        {
            if (key == u8"save")
                return u8"저장";
            if (key == u8"save-as")
                return u8"다른 이름으로 저장";
            if (key == u8"mail")
                return u8"메일로 보내기";
            return u8"링크 복사";
        }

        // 필드 아래에 놓는 메뉴 popup이다. 자리는 `below`가 메뉴 크기를 잰 뒤에 정하고, 아래가 내용 영역 밖으로 넘치면 위로 뒤집힌다.
        [[nodiscard]] luil::ui_popup make_field_menu(std::u8string owner, std::vector<luil::menu_item_config> items, const luil::rect_f& field, const luil::rect_f& surface, const float scale)
        {
            luil::menu_config menu {};
            menu.owner = owner;
            menu.items = std::move(items);
            menu.select = [owner](const std::u8string& key) { return luil::make_app_action(field_menu_select_intent { owner, key }); };
            // 고르면 닫힌다. 닫기는 절대 메시지라 바깥 누름 닫기와 겹쳐 두 번 와도 같다.
            menu.after_select = luil::make_message_action(field_menu_intent { owner, false });

            luil::menu_popup_config popup {};
            popup.id = owner + u8"-menu";
            popup.scale = scale;
            popup.below = luil::field_popup_anchor { .field = field, .limit = surface };
            popup.dismiss = [owner](const luil::popup_dismiss_reason) { return luil::make_app_action(field_menu_intent { owner, false }); };
            return luil::make_menu_popup(std::move(menu), std::move(popup));
        }
    }

    void popups_page::show_popup(std::u8string owner)
    {
        if (open_popup_ != owner)
        {
            search_ = {};
            composition_.reset();
        }
        open_popup_ = std::move(owner);
    }

    void popups_page::close_popup(const std::u8string_view owner)
    {
        if (open_popup_ == owner)
            show_popup({});
    }

    bool popups_page::handle(const luil::app_message& message)
    {
        if (const auto* const toggle { message.get<dropdown_toggle_intent>() }; toggle != nullptr)
        {
            // 한 번에 하나만 띄운다. 다른 popup이 떠 있으면 이 열기가 그것을 대신한다.
            if (toggle->open)
                show_popup(u8"dropdown");
            else
                close_popup(u8"dropdown");
            return true;
        }
        if (const auto* const select { message.get<dropdown_select_intent>() }; select != nullptr)
        {
            renderer_ = select->value;
            close_popup(u8"dropdown");
            return true;
        }
        // 분할 버튼과 드롭다운 단추다. 열기·닫기는 절대 메시지 하나로 오고, 닫기는 자기 popup이 떠 있을 때만 듣는다.
        if (const auto* const menu { message.get<field_menu_intent>() }; menu != nullptr)
        {
            if (menu->open)
                show_popup(menu->owner);
            else
                close_popup(menu->owner);
            return true;
        }
        if (const auto* const picked { message.get<field_menu_select_intent>() }; picked != nullptr)
        {
            last_action_ = field_menu_action_text(picked->key);
            return true;
        }
        if (message.get<split_invoke_intent>() != nullptr)
        {
            last_action_ = u8"저장";
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
            show_popup(u8"card");
            menu_x_ = open->x;
            menu_y_ = open->y;
            return true;
        }
        if (message.get<card_menu_close_intent>() != nullptr || message.get<card_menu_select_intent>() != nullptr)
        {
            close_popup(u8"card");
            return true;
        }
        return false;
    }

    page_content popups_page::build(const float width, const float scale)
    {
        static_cast<void>(scale);
        dropdown_ = nullptr;
        split_ = nullptr;
        send_ = nullptr;

        page_column column { u8"popups" };
        column.note(u8"popups-hint", u8"popup은 화면 위에 겹쳐 뜬다.");
        column.note(u8"popups-hint-2", u8"바깥을 누르거나 뒤로 가기로 닫는다.");
        column.gap(16.0f);

        column.note(u8"dropdown-label", u8"렌더러 (드롭다운)");
        column.gap(4.0f);
        luil::dropdown_config dropdown {};
        dropdown.owner = u8"renderer";
        dropdown.text = renderer_;
        dropdown.open = open_popup_ == u8"dropdown";
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
        column.gap(24.0f);

        // 분할 버튼과 드롭다운 단추다. 본체를 누르면 동작이 나가고, 화살표를 누르면 필드 아래에 메뉴가 뜬다.
        // 열림과 마지막 동작은 label로도 그려 접근성과 자동화가 되읽는다. 손가락으로 누르므로 높이와 화살표 폭을 키웠다.
        column.note(u8"split-label", u8"분할 버튼과 드롭다운 단추");
        column.gap(4.0f);
        luil::split_button_config save {};
        save.owner = u8"save";
        save.look.text = u8"저장";
        save.look.glyph = luil::codicons::icon_save;
        save.invoke = luil::make_message_action(split_invoke_intent {});
        save.open = open_popup_ == u8"save";
        save.set_open = [](const bool open) { return luil::make_app_action(field_menu_intent { u8"save", open }); };
        save.arrow_tooltip = u8"저장 옵션";
        save.arrow_width = field_arrow_width;
        auto split { std::make_unique<luil::split_button_element>(std::move(save)) };
        split_ = split.get();
        column.add(std::move(split), touch_height, std::min(width - 2.0f * page_padding, field_button_width));
        column.gap(8.0f);

        // 본체가 없는 분할 버튼이 드롭다운 단추다. 버튼 전체가 메뉴를 연다.
        luil::split_button_config send {};
        send.owner = u8"send";
        send.look.text = u8"보내기";
        send.look.glyph = luil::codicons::icon_send;
        send.open = open_popup_ == u8"send";
        send.set_open = [](const bool open) { return luil::make_app_action(field_menu_intent { u8"send", open }); };
        send.arrow_width = field_arrow_width;
        auto dropdown_button { std::make_unique<luil::split_button_element>(std::move(send)) };
        send_ = dropdown_button.get();
        column.add(std::move(dropdown_button), touch_height, std::min(width - 2.0f * page_padding, field_button_width));
        column.gap(8.0f);

        const auto state_text = [this](const std::u8string_view owner, const std::u8string_view name) { return std::u8string { name } + u8" 메뉴 열림: " + (open_popup_ == owner ? u8"열림" : u8"닫힘"); };
        column.note(u8"split-state-save", state_text(u8"save", u8"저장"));
        column.note(u8"split-state-send", state_text(u8"send", u8"보내기"));
        column.note(u8"split-last-action", std::u8string { u8"마지막 동작: " } + (last_action_.empty() ? std::u8string { u8"없음" } : last_action_));
        return column.finish();
    }

    std::vector<luil::ui_popup> popups_page::make_popups(const float scale, const luil::rect_f& surface) const
    {
        std::vector<luil::ui_popup> popups {};

        if (open_popup_ == u8"dropdown" && dropdown_ != nullptr)
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

        if (open_popup_ == u8"save" && split_ != nullptr)
        {
            std::vector<luil::menu_item_config> items {};
            items.push_back({ u8"save", u8"저장", luil::codicons::icon_save });
            items.push_back({ u8"save-as", u8"다른 이름으로 저장…", luil::codicons::icon_save_as });
            popups.push_back(make_field_menu(u8"save", std::move(items), split_->bounds(), surface, scale));
        }
        if (open_popup_ == u8"send" && send_ != nullptr)
        {
            std::vector<luil::menu_item_config> items {};
            items.push_back({ u8"mail", u8"메일로 보내기", luil::codicons::icon_mail });
            items.push_back({ u8"link", u8"링크 복사", luil::codicons::icon_link });
            popups.push_back(make_field_menu(u8"send", std::move(items), send_->bounds(), surface, scale));
        }

        if (open_popup_ == u8"card")
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
}
