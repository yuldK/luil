#include "demo/popups_page.h"

#include "luil/app/menu_popup.h"
#include "luil/generated/codicons.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/markdown_element.h"

#include "luil/ui/dropdown_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_tree.h"
#include <algorithm>

#include <utility>

namespace demo {
    namespace {
        constexpr float dropdown_width { 200.0f };
        inline constexpr std::u8string_view presentation_values[] { u8"자세히", u8"간결하게" };
    } // namespace

    void popups_page::close_dropdown() noexcept
    {
        dropdown_open_ = false;
        search_ = {};
        composition_.reset();
    }

    bool popups_page::handle(const luil::app_message& message)
    {
        if (const auto* const scroll { message.get<popups_scroll_intent>() }; scroll != nullptr)
        {
            scroll_ = std::max(0.0f, scroll_ + scroll->delta);
            return true;
        }
        if (const auto* const shape { message.get<popup_shape_intent>() }; shape != nullptr)
        {
            if (shape->setting == u8"corner")
                corner_ = shape->value == u8"round" ? luil::popup_corner::round
                    : shape->value == u8"small"     ? luil::popup_corner::round_small
                    : shape->value == u8"square"    ? luil::popup_corner::square
                                                    : luil::popup_corner::system;
            else if (shape->setting == u8"border")
                popup_border_ = shape->value == u8"on";
            return true;
        }
        if (const auto* const select { message.get<card_menu_select_intent>() }; select != nullptr)
        {
            menu_open_ = false;
            if (select->key == u8"열기")
                preview_ = !preview_;
            if (select->key == u8"즐겨찾기")
                pinned_ = !pinned_;
            if (select->key == u8"편집 잠금")
                locked_ = !locked_;
            if (select->key == u8"지우기" && locked_ == false)
                deleted_ = true;
            if (select->key == u8"복원")
                deleted_ = false;
            return true;
        }
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
            presentation_ = select->value;
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
        if (message.get<card_menu_close_intent>() != nullptr)
        {
            menu_open_ = false;
            return true;
        }
        return false;
    }

    std::unique_ptr<luil::ui_element> popups_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(height);
        static_cast<void>(scale);
        dropdown_ = nullptr;
        page_layout page { u8"popups" };
        page.heading(u8"메뉴와 팝업", u8"우클릭 또는 Shift+F10으로 문서 작업을 열고, 선택 결과를 확인하세요.");

        luil::dropdown_config dropdown {};
        dropdown.owner = u8"presentation";
        dropdown.text = presentation_;
        dropdown.open = dropdown_open_;
        dropdown.set_open = [](const bool open) { return luil::make_app_action(dropdown_toggle_intent { open }); };
        auto field { std::make_unique<luil::dropdown_element>(std::move(dropdown)) };
        field->set_access_name(u8"문서 표시 방식");
        dropdown_ = field.get();
        page.add(make_label({ kind_text, u8"presentation-label" }, u8"문서 표시 방식 (검색 가능)", 15.0f, luil::label_color_role::primary), 28.0f);
        page.add(std::move(field), luil::dropdown_height);
        page.gap(24.0f);

        luil::text_button_config menu_button_config { .text = deleted_ ? u8"문서 복원" : u8"문서 작업…", .glyph = luil::codicons::icon_ellipsis };
        auto menu_button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"card-menu-button" }, std::move(menu_button_config)) };
        menu_button->set_cursor(luil::ui_cursor::hand);
        if (deleted_)
            menu_button->set_action(luil::ui_trigger::left_click, luil::make_message_action(card_menu_select_intent { u8"복원" }));
        else
            menu_button->set_action(luil::ui_trigger::left_click,
                [](const luil::ui_action_context& context) { return std::vector<luil::input_action> { luil::make_app_action(card_menu_intent { context.x, context.y }) }; });
        page.add(std::move(menu_button), luil::text_button_height);
        page.gap();
        if (deleted_)
            page.add(make_label({ kind_text, u8"card-deleted" }, u8"보고서.md를 지웠습니다. 위 버튼으로 복원할 수 있습니다.", 13.0f, luil::label_color_role::dim), 72.0f);
        else
        {
            luil::panel_config panel {};
            panel.corner_radius = 8.0f;
            panel.border = [](const luil::ui_color_palette& palette) { return palette.control_border; };
            auto card { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_card }, std::move(panel)) };
            card->set_access_name(u8"보고서.md");
            card->set_cursor(luil::ui_cursor::hand);
            card->set_tooltip(u8"클릭해서 미리 보기 · 우클릭 또는 Shift+F10으로 문서 작업");
            card->set_action(luil::ui_trigger::left_click, luil::make_message_action(card_menu_select_intent { u8"열기" }));
            card->set_action(luil::ui_trigger::right_click,
                [](const luil::ui_action_context& context) { return std::vector<luil::input_action> { luil::make_app_action(card_menu_intent { context.x, context.y }) }; });
            luil::stack_config body_config {};
            body_config.padding = luil::edge_insets::all(16.0f);
            auto body { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"card-body" }, body_config) };
            body->add(make_label({ kind_text, u8"card-title" }, (pinned_ ? u8"★  " : u8"") + std::u8string { u8"보고서.md" } + (locked_ ? u8"  · 잠김" : u8""), 16.0f, luil::label_color_role::primary),
                28.0f);
            if (presentation_ == u8"자세히")
                body->add(make_label({ kind_text, u8"card-description" }, u8"문서 / 보고서.md · 즐겨찾기와 편집 잠금을 메뉴에서 변경할 수 있습니다.", 12.0f, luil::label_color_role::dim), 24.0f);
            card->set_content(std::move(body));
            page.add(std::move(card), presentation_ == u8"자세히" ? 88.0f : 60.0f);
        }
        page.gap(16.0f);
        if (preview_ && deleted_ == false)
        {
            luil::markdown_config markdown {};
            markdown.text = u8"## 보고서 미리 보기\n공용 메뉴에서 **열기**를 실행했습니다.\n\n- 즐겨찾기: 체크 메뉴 상태로 표시\n- 편집 잠금: 지우기 명령의 활성 상태와 연결\n\n표시 방식과 메뉴 "
                            u8"선택은 페이지를 이동해도 유지됩니다.";
            page.add(std::make_unique<luil::markdown_element>(luil::ui_element_id { kind_text, u8"document-preview" }, std::move(markdown)), 180.0f);
        }
        page.add(make_label({ kind_text, u8"popups-help" }, u8"검색 중에는 IME 조합을 지원합니다. Esc 또는 바깥 클릭으로 팝업을 닫을 수 있습니다.", 12.0f, luil::label_color_role::dim), 28.0f);
        page.gap(16.0f);

        // popup 창의 모양이다. 앱마다 고르는 렌더링 선택지이고, 고르지 않으면 OS 기본이다. 둥근 모서리를 못 쓰는 OS·원격
        // 세션에서는 무엇을 골라도 직각과 처음부터의 경계다.
        const auto shape_choice = [](std::u8string setting, std::u8string name, std::vector<luil::choice_item> items, std::u8string selected) {
            luil::choice_group_config choices {};
            choices.owner = u8"popup-shape/" + setting;
            choices.name = std::move(name);
            choices.style = luil::choice_style::toggle;
            choices.items = std::move(items);
            choices.selected = std::move(selected);
            choices.select = [setting](const std::u8string& value) { return luil::make_app_action(popup_shape_intent { setting, value }); };
            return std::make_unique<luil::choice_group_element>(std::move(choices));
        };
        std::u8string corner_value { u8"system" };
        switch (corner_)
        {
        case luil::popup_corner::round:
            corner_value = u8"round";
            break;
        case luil::popup_corner::round_small:
            corner_value = u8"small";
            break;
        case luil::popup_corner::square:
            corner_value = u8"square";
            break;
        case luil::popup_corner::system:
            break;
        }
        page.add(make_label({ kind_text, u8"popup-shape-title" }, u8"popup 모양 (드롭다운·문서 작업 메뉴)", 15.0f, luil::label_color_role::primary), 28.0f);
        page.add(shape_choice(u8"corner", u8"popup 모서리", { { u8"system", u8"OS 기본" }, { u8"round", u8"둥글게" }, { u8"small", u8"작게 둥글게" }, { u8"square", u8"직각" } }, corner_value),
            luil::choice_toggle_height);
        page.gap(4.0f);
        page.add(shape_choice(u8"border", u8"popup 경계", { { u8"on", u8"경계 있음" }, { u8"off", u8"경계 없음" } }, popup_border_ ? u8"on" : u8"off"), luil::choice_toggle_height);
        const std::u8string radius { to_u8(static_cast<int>(luil::popup_corner_radius(corner_))) };
        page.add(make_label({ kind_text, u8"popup-shape-radius" }, u8"이 OS에서 고른 모서리의 반지름: " + radius + u8" 논리 픽셀", 12.0f, luil::label_color_role::dim), 24.0f);
        return page.finish(scroll_, [](const float delta) { return luil::make_app_action(popups_scroll_intent { delta }); });
    }
    std::vector<luil::win32::ui_popup> popups_page::make_popups(const float scale) const
    {
        std::vector<luil::win32::ui_popup> popups {};

        if (dropdown_open_ && dropdown_ != nullptr)
        {
            const luil::text_input_view search { luil::make_text_input_view(search_, composition_, target_dropdown_search) };

            std::vector<luil::menu_item_config> values {};
            for (const std::u8string_view value : presentation_values)
            {
                luil::menu_item_config item {};
                item.key = std::u8string { value };
                item.label = std::u8string { value };
                if (item.key == presentation_)
                    item.icon = luil::codicons::icon_check;
                values.push_back(std::move(item));
            }

            luil::menu_config menu {};
            menu.owner = u8"presentation";
            menu.items = narrow_menu_items(std::move(values), search);
            menu.select = [](const std::u8string& value) { return luil::make_app_action(dropdown_select_intent { value }); };
            // 둥근 창에서는 메뉴의 사각 선이 모서리에서 튀어나온다. 경계는 OS 테두리가 긋는다.
            menu.border = popup_border_ && luil::popup_corner_radius(corner_) <= 0.0f;

            // 칸 바로 아래에 붙인다.
            const luil::rect_f field { dropdown_->bounds() };
            luil::win32::ui_popup popup {};
            popup.id = u8"dropdown";
            popup.width = dropdown_width;
            popup.height = search_menu_height(menu);
            popup.x = field.x / scale;
            popup.y = (field.y + field.height) / scale + 2.0f;
            popup.tree = make_search_menu_tree(std::move(menu), kind_dropdown_search_input, search, u8"검색", popup.width, scale);
            popup.corner = corner_;
            popup.border = popup_border_;
            popup.dismiss = [](const luil::win32::popup_dismiss_reason reason) {
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
            remove.enabled = locked_ == false;
            remove.separator_above = true;
            menu.items.push_back({ .key = u8"즐겨찾기", .label = u8"즐겨찾기", .kind = luil::menu_item_kind::check, .checked = pinned_ });
            menu.items.push_back({ .key = u8"편집 잠금", .label = u8"편집 잠금", .kind = luil::menu_item_kind::check, .checked = locked_ });
            menu.items.push_back(std::move(remove));
            // 선택이 메뉴를 닫는 것도 앱 몫이다.
            // 셸이 이 메시지를 받아 토스트 알림으로 잇는다.
            menu.select = [](const std::u8string& key) { return luil::make_app_action(card_menu_select_intent { key }); };

            luil::menu_popup_config popup {};
            popup.id = u8"card-menu";
            popup.x = menu_x_ / scale;
            popup.y = menu_y_ / scale;
            popup.scale = scale;
            popup.dismiss = [](const luil::popup_dismiss_reason) { return luil::make_app_action(card_menu_close_intent {}); };
            popup.corner = corner_;
            popup.border = popup_border_;
            popups.push_back(luil::make_menu_popup(std::move(menu), std::move(popup)));
        }
        return popups;
    }
} // namespace demo
