#include "demo/popups_page.h"

#include "luil/app/color_picker_popup.h"
#include "luil/app/menu_popup.h"
#include "luil/generated/codicons.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/color_picker_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/markdown_element.h"

#include "luil/ui/dropdown_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/split_button_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_tree.h"
#include <algorithm>

#include <utility>

namespace demo {
    namespace {
        constexpr float dropdown_width { 200.0f };
        // 분할 버튼과 드롭다운 단추의 폭이다 (논리 픽셀). 분할 버튼은 이 가운데 화살표 몫(28)이 오른쪽에 선다.
        constexpr float field_button_width { 160.0f };
        inline constexpr std::u8string_view presentation_values[] { u8"자세히", u8"간결하게" };
        // 색 칸 popup의 임자(`open_popup_`)와 popup id, 그 안 색 고르기의 owner다.
        inline constexpr std::u8string_view color_owner { u8"color-field" };
        inline constexpr std::u8string_view color_popup_id { u8"color-popup" };
        inline constexpr std::u8string_view color_picker_owner { u8"popups/color" };
        inline constexpr std::u8string_view color_field_owner { u8"popups/color-field" };

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

        // 필드 아래에 놓는 메뉴 popup이다. 자리는 `below`가 메뉴 크기를 잰 뒤에 정하고, 아래가 창 밖으로 넘치면 위로 뒤집힌다.
        [[nodiscard]] luil::win32::ui_popup make_field_menu(
            std::u8string owner, std::vector<luil::menu_item_config> items, const luil::rect_f& field, const luil::rect_f& surface, const float scale, const luil::popup_corner corner, const bool border)
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
            popup.corner = corner;
            popup.border = border;
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

    void popups_page::close_popups()
    {
        if (open_popup_ == color_owner)
            commit_color_hex();
        open_popup_.clear();
        search_ = {};
        composition_.reset();
    }

    void popups_page::commit_color_hex()
    {
        // 읽히면 지금 값을 keep으로 넘겨 회색·검정에서도 색상과 채도를 지킨다.
        const luil::hex_color_check check { luil::check_hex_color(field_hex_.text, false) };
        field_hex_invalid_ = check.valid() == false;
        if (check.valid())
        {
            field_color_ = luil::hsv_from_color(*check.color, field_color_);
            reset_color_hex();
        }
    }

    void popups_page::reset_color_hex()
    {
        luil::text_edit_request request {};
        request.target = target_popups_color_hex;
        request.command = luil::text::text_edit_command::replace_all;
        request.text = luil::format_hex_color(luil::color_from_hsv(field_color_), false);
        request.offset = request.text.size();
        luil::apply_text_edit(field_hex_, request);
        field_composition_.reset();
        field_hex_invalid_ = false;
    }

    void popups_page::request_focus(std::u8string surface, luil::ui_element_id target)
    {
        ++focus_revision_;
        focus_surface_ = std::move(surface);
        focus_target_ = std::move(target);
    }

    std::optional<luil::focus_request> popups_page::make_focus_request() const
    {
        if (focus_revision_ == 0)
            return std::nullopt;
        // popup은 활성화를 받지 않는 표면이라 초점만 옮긴다. popup이 서는 frame을 기다린다 (timeout).
        return luil::focus_request { .id = focus_revision_, .surface = focus_surface_, .target = focus_target_ };
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
            close_popup(u8"card");
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
            // 한 번에 하나만 띄운다. 다른 popup이 떠 있으면 이 열기가 그것을 대신한다.
            if (toggle->open)
                show_popup(u8"presentation");
            else
                close_popup(u8"presentation");
            return true;
        }
        if (const auto* const select { message.get<dropdown_select_intent>() }; select != nullptr)
        {
            presentation_ = select->value;
            close_popup(u8"presentation");
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
        // 색 칸이다. 열면 초안을 지금 색의 글로 다시 쓰고 판으로 초점을 옮긴다. 닫는 모든 길은 초안을 먼저 확정한다.
        if (const auto* const field { message.get<color_field_intent>() }; field != nullptr)
        {
            if (field->open)
            {
                if (open_popup_ != color_owner)
                {
                    show_popup(std::u8string { color_owner });
                    reset_color_hex();
                    request_focus(std::u8string { color_popup_id }, luil::color_picker_element::plane_id(color_picker_owner));
                }
            }
            else if (open_popup_ == color_owner)
            {
                commit_color_hex();
                close_popup(color_owner);
            }
            return true;
        }
        if (const auto* const dismiss { message.get<color_field_dismiss_intent>() }; dismiss != nullptr)
        {
            if (open_popup_ != color_owner)
                return true;
            commit_color_hex();
            close_popup(color_owner);
            // popup이 사라지면 그 안의 초점은 지워질 뿐이다. Esc로 닫았을 때만 칸으로 돌린다. 다른 계기는 누른 자리가 초점을 가진다.
            if (dismiss->reason == luil::popup_dismiss_reason::escape_key)
                request_focus({}, luil::ui_element_id { luil::ui_element_kind::color_field, std::u8string { color_field_owner } });
            return true;
        }
        if (const auto* const color { message.get<color_field_value_intent>() }; color != nullptr)
        {
            field_color_ = luil::clamp_hsv(color->value);
            reset_color_hex();
            return true;
        }
        if (message.get<color_field_commit_intent>() != nullptr)
        {
            commit_color_hex();
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
            // 색 칸 popup의 16진 칸은 16진 글자만 받는다.
            if (edit->request.target == target_popups_color_hex)
            {
                luil::apply_text_edit(field_hex_, edit->request, luil::hex_color_filter());
                return true;
            }
            if (edit->request.target != target_dropdown_search)
                return false;
            luil::apply_text_edit(search_, edit->request);
            return true;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            if (composition->event.target == target_popups_color_hex)
            {
                if (composition->event.composing)
                    field_composition_ = composition->event;
                else
                    field_composition_.reset();
                return true;
            }
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
        if (message.get<card_menu_close_intent>() != nullptr)
        {
            close_popup(u8"card");
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
        split_ = nullptr;
        send_ = nullptr;
        color_field_ = nullptr;
        page_layout page { u8"popups" };
        page.heading(u8"메뉴와 팝업", u8"우클릭 또는 Shift+F10으로 문서 작업을 열고, 선택 결과를 확인하세요.");

        luil::dropdown_config dropdown {};
        dropdown.owner = u8"presentation";
        dropdown.text = presentation_;
        dropdown.open = open_popup_ == u8"presentation";
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

        // 분할 버튼과 드롭다운 단추다. 본체를 누르면 동작이 나가고, 화살표를 누르면(Alt+↓·F4도) 필드 아래에 메뉴가 뜬다.
        // 열림과 마지막 동작은 label로도 그려 보조 기술과 자동화가 되읽는다.
        page.add(make_label({ kind_text, u8"split-title" }, u8"분할 버튼과 드롭다운 단추", 15.0f, luil::label_color_role::primary), 28.0f);
        luil::split_button_config save {};
        save.owner = u8"save";
        save.look.text = u8"저장";
        save.look.glyph = luil::codicons::icon_save;
        save.invoke = luil::make_message_action(split_invoke_intent {});
        save.open = open_popup_ == u8"save";
        save.set_open = [](const bool open) { return luil::make_app_action(field_menu_intent { u8"save", open }); };
        save.tooltip = u8"문서를 저장합니다";
        save.arrow_tooltip = u8"저장 옵션";
        auto split { std::make_unique<luil::split_button_element>(std::move(save)) };
        split_ = split.get();
        page.add(std::move(split), luil::stack_item { .length = luil::text_button_height, .cross_length = field_button_width });
        page.gap(8.0f);

        // 본체가 없는 분할 버튼이 드롭다운 단추다. 버튼 전체가 메뉴를 연다.
        luil::split_button_config send {};
        send.owner = u8"send";
        send.look.text = u8"보내기";
        send.look.glyph = luil::codicons::icon_send;
        send.open = open_popup_ == u8"send";
        send.set_open = [](const bool open) { return luil::make_app_action(field_menu_intent { u8"send", open }); };
        send.tooltip = u8"보내는 방법을 고릅니다";
        auto dropdown_button { std::make_unique<luil::split_button_element>(std::move(send)) };
        send_ = dropdown_button.get();
        page.add(std::move(dropdown_button), luil::stack_item { .length = luil::text_button_height, .cross_length = field_button_width });
        page.gap(8.0f);

        const auto state_text = [this](const std::u8string_view owner, const std::u8string_view name) { return std::u8string { name } + u8" 메뉴 열림: " + (open_popup_ == owner ? u8"열림" : u8"닫힘"); };
        page.add(make_label({ kind_text, u8"split-state-save" }, state_text(u8"save", u8"저장"), 12.0f, luil::label_color_role::dim), 22.0f);
        page.add(make_label({ kind_text, u8"split-state-send" }, state_text(u8"send", u8"보내기"), 12.0f, luil::label_color_role::dim), 22.0f);
        page.add(make_label({ kind_text, u8"split-last-action" }, std::u8string { u8"마지막 동작: " } + (last_action_.empty() ? std::u8string { u8"없음" } : last_action_), 12.0f, luil::label_color_role::dim), 22.0f);
        page.gap(16.0f);

        // 색 칸이다. 누르면(Alt+↓·F4·↓도) 칸 아래에 색 고르기 popup이 뜨고 초점이 판으로 간다. Esc로 닫으면 칸으로 돌아온다.
        page.add(make_label({ kind_text, u8"color-field-title" }, u8"색 칸 (popup에서 판·막대·16진 칸으로 고르기)", 15.0f, luil::label_color_role::primary), 28.0f);
        luil::color_field_config color {};
        color.owner = std::u8string { color_field_owner };
        color.value = field_color_;
        color.open = open_popup_ == color_owner;
        color.set_open = [](const bool open) { return luil::make_app_action(color_field_intent { open }); };
        color.name = u8"글자 색";
        auto color_field { std::make_unique<luil::color_field_element>(std::move(color)) };
        color_field_ = color_field.get();
        page.add(std::move(color_field), luil::stack_item { .length = luil::dropdown_height, .cross_length = field_button_width });
        page.gap(8.0f);
        const std::u8string color_state { std::u8string { u8"색 칸: " } + (open_popup_ == color_owner ? u8"열림" : u8"닫힘") + u8" · " + luil::format_hex_color(luil::color_from_hsv(field_color_), false) };
        page.add(make_label({ kind_text, u8"popups/color-state" }, color_state, 12.0f, luil::label_color_role::dim), 22.0f);
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
    std::vector<luil::win32::ui_popup> popups_page::make_popups(const float scale, const luil::rect_f& surface) const
    {
        std::vector<luil::win32::ui_popup> popups {};

        if (open_popup_ == u8"presentation" && dropdown_ != nullptr)
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

        if (open_popup_ == u8"save" && split_ != nullptr)
        {
            std::vector<luil::menu_item_config> items {};
            items.push_back({ u8"save", u8"저장", luil::codicons::icon_save });
            items.push_back({ u8"save-as", u8"다른 이름으로 저장…", luil::codicons::icon_save_as });
            popups.push_back(make_field_menu(u8"save", std::move(items), split_->bounds(), surface, scale, corner_, popup_border_));
        }
        if (open_popup_ == u8"send" && send_ != nullptr)
        {
            std::vector<luil::menu_item_config> items {};
            items.push_back({ u8"mail", u8"메일로 보내기", luil::codicons::icon_mail });
            items.push_back({ u8"link", u8"링크 복사", luil::codicons::icon_link });
            popups.push_back(make_field_menu(u8"send", std::move(items), send_->bounds(), surface, scale, corner_, popup_border_));
        }

        if (open_popup_ == color_owner && color_field_ != nullptr)
        {
            luil::color_picker_config picker {};
            picker.owner = std::u8string { color_picker_owner };
            picker.value = field_color_;
            picker.change_to = [](const luil::hsv_color value) { return luil::make_app_action(color_field_value_intent { value }); };
            picker.hex = luil::color_picker_hex {
                .id = { kind_popups_color_hex_input, u8"popups/color-hex" },
                .value = luil::make_text_input_view(field_hex_, field_composition_, target_popups_color_hex),
                .invalid = field_hex_invalid_,
                .commit = luil::make_message_action(color_field_commit_intent {}),
            };
            picker.presets = { { luil::make_ui_color(0x33, 0x66, 0xFF), u8"파랑" }, { luil::make_ui_color(0xE5, 0x39, 0x35), u8"빨강" }, { luil::make_ui_color(0x80, 0x80, 0x80), u8"회색" } };

            luil::color_picker_popup_config popup {};
            popup.id = std::u8string { color_popup_id };
            popup.scale = scale;
            popup.below = luil::field_popup_anchor { .field = color_field_->bounds(), .limit = surface };
            // 16진 칸을 실은 popup은 글을 치는 popup이다. 창 이동·활성 전환에는 남는다 (검색 칸이 있는 드롭다운과 같다).
            popup.dismiss = [](const luil::popup_dismiss_reason reason) {
                if (search_popup_stays(reason))
                    return luil::input_action {};
                return luil::make_app_action(color_field_dismiss_intent { reason });
            };
            popup.corner = corner_;
            popups.push_back(luil::make_color_picker_popup(std::move(picker), std::move(popup)));
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
}
