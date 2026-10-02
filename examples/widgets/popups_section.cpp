// popup 섹션이다: 드롭다운과 컨텍스트 메뉴.
//
// 다루는 element: dropdown_element(닫힌 칸), menu_element(popup의 목록), panel_element(카드).
//
// popup은 앱 상태다. 열려 있으면 frame의 `popups`에 싣고, 닫으면 뺀다.
//   - 자리는 앵커 표면(여기서는 주 창) 기준 논리 픽셀이다. 칸 바로 아래에 붙이려면 배치가
//     끝난 칸의 자리가 필요해서, 섹션을 지을 때 칸을 남겨 두고 frame을 다 지은 뒤 잰다.
//   - 닫힘 계기(바깥 누름, 휠, Esc·뒤로 가기, 창 크기 변경)는 플랫폼이 감지해 `dismiss`가
//     돌려준 메시지를 낸다. 빼는 것은 앱이다.
//   - Windows는 popup마다 창을 띄우고, 휴대폰은 주 화면 위에 겹쳐 그린다. 앱 코드는 같다.

#include "widgets/app.h"

#include "luil/generated/codicons.h"

#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace widgets {
    namespace {
        constexpr float dropdown_width { 200.0f };
        constexpr float card_menu_width { 180.0f };
        constexpr std::array<std::u8string_view, 3> sort_values { u8"이름순", u8"날짜순", u8"크기순" };
    } // namespace

    section build_popups_section(const app_state& state, const luil::ui_element** const dropdown)
    {
        luil::stack_config column_config {};
        column_config.spacing = 8.0f;
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"popups" }, column_config) };

        luil::label_config title {};
        title.text = u8"팝업 — 드롭다운과 컨텍스트 메뉴";
        title.font_size = 13.0f;
        title.color = luil::label_color_role::primary;
        const float title_height { luil::label_element::height_for(title) };
        column->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"popups-title" }, title), title_height);

        luil::dropdown_config sort {};
        sort.owner = u8"sort";
        sort.text = state.sort;
        sort.open = state.sort_open;
        sort.set_open = [](const bool open) { return luil::make_app_action(sort_open_intent { open }); };
        auto field { std::make_unique<luil::dropdown_element>(std::move(sort)) };
        *dropdown = field.get();
        column->add(std::move(field), { .length = luil::dropdown_height, .cross_length = dropdown_width });

        // 우클릭(터치는 길게 누르기)으로 컨텍스트 메뉴를 여는 카드다.
        luil::panel_config surface {};
        surface.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        surface.corner_radius = 6.0f;
        auto card { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_card }, std::move(surface)) };
        card->set_action(luil::ui_trigger::right_click,
            [](const luil::ui_action_context& context) -> std::vector<luil::input_action> { return { luil::make_app_action(card_menu_intent { context.x, context.y }) }; });
        luil::label_config body {};
        body.text = u8"보고서.md — 우클릭하거나 길게 누르면 메뉴가 뜬다";
        body.font_size = 12.0f;
        body.color = luil::label_color_role::dim;
        luil::stack_config card_config {};
        card_config.padding = luil::edge_insets::all(12.0f);
        auto card_content { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"card" }, card_config) };
        card_content->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"card-body" }, body), luil::label_element::height_for(body));
        card->set_content(std::move(card_content));
        constexpr float card_height { 48.0f };
        column->add(std::move(card), card_height);

        return { std::move(column), title_height + 8.0f + luil::dropdown_height + 8.0f + card_height };
    }

    std::vector<luil::ui_popup> build_popups(const app_state& state, const luil::ui_element* const dropdown, const float scale)
    {
        std::vector<luil::ui_popup> popups {};
        // 어느 계기에서든 닫는다. 남기고 싶은 계기는 빈 액션을 돌려주면 된다.
        const auto close_on_any = [](luil::popup_dismiss_reason) { return luil::make_app_action(popup_close_intent {}); };

        if (state.sort_open && dropdown != nullptr)
        {
            luil::menu_config menu {};
            menu.owner = u8"sort";
            for (const std::u8string_view value : sort_values)
            {
                luil::menu_item_config item {};
                item.key = std::u8string { value };
                item.label = std::u8string { value };
                if (item.key == state.sort)
                    item.icon = luil::codicons::icon_check;
                menu.items.push_back(std::move(item));
            }
            menu.select = [](const std::u8string& value) { return luil::make_app_action(sort_select_intent { value }); };

            // 칸 바로 아래에 붙인다.
            const luil::rect_f field { dropdown->bounds() };
            luil::ui_popup popup {};
            popup.id = u8"sort-menu";
            popup.x = field.x / scale;
            popup.y = (field.y + field.height) / scale + 2.0f;
            popup.width = dropdown_width;
            popup.height = luil::menu_element::height_for(menu);
            auto root { std::make_unique<luil::menu_element>(std::move(menu)) };
            popup.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, popup.width * scale, popup.height * scale }, scale));
            popup.dismiss = close_on_any;
            popups.push_back(std::move(popup));
        }

        if (state.card_menu_open)
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
            menu.select = [](const std::u8string& key) { return luil::make_app_action(card_menu_select_intent { key }); };

            luil::ui_popup popup {};
            popup.id = u8"card-menu";
            popup.x = state.card_menu_x / scale;
            popup.y = state.card_menu_y / scale;
            popup.width = card_menu_width;
            popup.height = luil::menu_element::height_for(menu);
            auto root { std::make_unique<luil::menu_element>(std::move(menu)) };
            popup.tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, popup.width * scale, popup.height * scale }, scale));
            popup.dismiss = close_on_any;
            popups.push_back(std::move(popup));
        }
        return popups;
    }
} // namespace widgets
