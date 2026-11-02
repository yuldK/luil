#include "demo/gallery_page.h"
#include "demo/popups_page.h"
#include "demo/tabs_page.h"
#include "demo/text_targets.h"
#include "demo/theme_page.h"
#include "demo/toasts_page.h"
#include "demo/zoom_page.h"

#include "luil/ui/menu_element.h"
#include "luil/ui/tab_bar_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {
    [[nodiscard]] luil::ui_tree arranged(std::unique_ptr<luil::ui_element> root, const float width = 800.0f, const float height = 600.0f)
    {
        return luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, width, height }, 1.0f);
    }

    template<typename Page, typename Intent>
    void send(Page& page, Intent intent)
    {
        REQUIRE(page.handle(luil::app_message { std::move(intent) }));
    }

    [[nodiscard]] std::vector<luil::ui_element_id> tabs(demo::tabs_page& page)
    {
        return arranged(page.build(800.0f, 600.0f, 1.0f)).ids_of_kind(luil::ui_element_kind::tab);
    }

    // 앵커 창의 client 사각형이다 (물리 픽셀).
    constexpr luil::rect_f window_surface { 0.0f, 0.0f, 800.0f, 600.0f };

    const luil::ui_element_id split_id { luil::ui_element_kind::split_button, u8"save" };
    const luil::ui_element_id split_arrow_id { luil::ui_element_kind::split_button_arrow, u8"save" };
    const luil::ui_element_id split_body_id { luil::ui_element_kind::split_button_body, u8"save" };
    const luil::ui_element_id send_arrow_id { luil::ui_element_kind::split_button_arrow, u8"send" };

    // label이 그린 글이다. 화면에 그려지고 보조 기술이 읽는 말이 같다.
    [[nodiscard]] std::u8string label_text(const luil::ui_tree& tree, const std::u8string& owner)
    {
        const luil::ui_element* const label { tree.find({ demo::kind_text, owner }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    }

    [[nodiscard]] luil::input_action first_action(const std::vector<luil::input_action>& actions)
    {
        REQUIRE(actions.size() == 1u);
        return actions.front();
    }

    [[nodiscard]] bool arrow_open(const luil::ui_tree& tree, const luil::ui_element_id& arrow)
    {
        const luil::ui_element* const element { tree.find(arrow) };
        REQUIRE(element != nullptr);
        REQUIRE(element->accessibility().expanded.has_value());
        return *element->accessibility().expanded;
    }

    [[nodiscard]] bool selected(demo::tabs_page& page, const std::u8string& key)
    {
        const auto tree { arranged(page.build(800.0f, 600.0f, 1.0f)) };
        const auto* const tab { tree.find({ luil::ui_element_kind::tab, key }) };
        return tab != nullptr && tab->accessibility().selected == true;
    }
}

TEST_CASE("Gallery toggles retain independent state and reset to usable defaults", "[demo][gallery]")
{
    demo::gallery_page page {};
    send(page, demo::gallery_toggle_intent { u8"bold" });
    send(page, demo::gallery_toggle_intent { u8"autosave" });
    auto tree { arranged(page.build_controls()) };
    REQUIRE(tree.find({ luil::ui_element_kind::check, u8"gallery/bold" })->accessibility().checked == true);
    REQUIRE(tree.find({ luil::ui_element_kind::check, u8"gallery/italic" })->accessibility().checked == false);
    REQUIRE(tree.find({ luil::ui_element_kind::check, u8"gallery/autosave" })->accessibility().checked == false);
    send(page, demo::gallery_reset_intent {});
    tree = arranged(page.build_controls());
    REQUIRE(tree.find({ luil::ui_element_kind::check, u8"gallery/bold" })->accessibility().checked == false);
    REQUIRE(tree.find({ luil::ui_element_kind::check, u8"gallery/autosave" })->accessibility().checked == true);
    REQUIRE(tree.find({ demo::kind_layout, u8"gallery/hover" })->tooltip_content() != nullptr);
}

TEST_CASE("Theme demo uses shared visual pickers and publishes the selected appearance", "[demo][gallery]")
{
    demo::theme_page page {};
    send(page, demo::theme_intent { luil::theme_preference::light });
    const auto tree { arranged(page.build(800.0f, 600.0f, 1.0f)) };
    REQUIRE(page.appearance().theme == luil::theme_preference::light);
    REQUIRE(tree.find({ luil::ui_element_kind::choice, u8"theme/light" })->accessibility().selected == true);
    REQUIRE(tree.find({ luil::ui_element_kind::choice_group, u8"accent" })->accessibility().role == luil::access_role::radio_group);
}

TEST_CASE("Tab moves insert before or after targets and stale selections cannot select a closed tab", "[demo][tabs]")
{
    demo::tabs_page page {};
    send(page, demo::tab_reorder_intent { u8"tab-1", u8"tab-3", luil::drop_side::after });
    auto order { tabs(page) };
    REQUIRE(order[0].owner == u8"tab-2");
    REQUIRE(order[1].owner == u8"tab-3");
    REQUIRE(order[2].owner == u8"tab-1");
    send(page, demo::tab_close_intent { u8"tab-1" });
    send(page, demo::tab_select_intent { u8"tab-1" });
    REQUIRE(selected(page, u8"tab-4"));
    send(page, demo::tab_menu_command_intent { u8"reopen" });
    REQUIRE(selected(page, u8"tab-1"));
}

TEST_CASE("Tab keyboard commands wrap selection and preserve usable empty and reopened states", "[demo][tabs]")
{
    demo::tabs_page page {};
    send(page, demo::tab_menu_command_intent { u8"previous" });
    REQUIRE(selected(page, u8"tab-8"));
    send(page, demo::tab_menu_command_intent { u8"next" });
    REQUIRE(selected(page, u8"tab-1"));
    send(page, demo::tab_menu_command_intent { u8"close-all" });
    REQUIRE(tabs(page).empty());
    send(page, demo::tab_menu_command_intent { u8"reopen" });
    REQUIRE(tabs(page).size() == 1u);
    send(page, demo::tab_menu_command_intent { u8"new" });
    REQUIRE(tabs(page).size() == 2u);
    REQUIRE(selected(page, u8"tab-9"));
}

TEST_CASE("Tab selection reveals an offscreen tab while wheel scrolling remains under user control", "[demo][tabs]")
{
    demo::tabs_page page {};
    static_cast<void>(tabs(page));
    send(page, demo::tab_select_intent { u8"tab-8" });
    auto tree { arranged(page.build(800.0f, 600.0f, 1.0f)) };
    const auto* bar { dynamic_cast<const luil::tab_bar_element*>(tree.find({ luil::ui_element_kind::tab_bar, u8"demo-tabs" })) };
    REQUIRE(bar != nullptr);
    REQUIRE(bar->scroll_offset() > 0.0f);
    send(page, demo::tab_scroll_intent { -10000.0f });
    tree = arranged(page.build(800.0f, 600.0f, 1.0f));
    bar = dynamic_cast<const luil::tab_bar_element*>(tree.find({ luil::ui_element_kind::tab_bar, u8"demo-tabs" }));
    REQUIRE(bar->scroll_offset() == 0.0f);
}

TEST_CASE("Detached demo tabs publish separate windows and rejoin at the requested gap", "[demo][tabs]")
{
    demo::tabs_page page {};
    send(page, demo::tab_detach_intent { u8"tab-2", luil::window_screen_anchor { { 600, 300 }, 40.0f, 20.0f } });
    REQUIRE(tabs(page).size() == 7u);
    const auto windows { page.make_windows() };
    REQUIRE(windows.size() == 1u);
    REQUIRE(windows.front().independent);
    REQUIRE(windows.front().screen_anchor.has_value());
    REQUIRE(windows.front().tree->find({ luil::ui_element_kind::tab, u8"tab-2" }) != nullptr);
    send(page, demo::tab_attach_intent { u8"tab-2", u8"tab-4", luil::drop_side::after });
    REQUIRE(page.make_windows().empty());
    const auto order { tabs(page) };
    REQUIRE(order[2].owner == u8"tab-4");
    REQUIRE(order[3].owner == u8"tab-2");
    REQUIRE(selected(page, u8"tab-2"));
}

TEST_CASE("Document context menu exposes check state and guards deletion until unlocked", "[demo][popups]")
{
    demo::popups_page page {};
    send(page, demo::card_menu_intent { 50.0f, 60.0f });
    auto popups { page.make_popups(1.5f, window_surface) };
    REQUIRE(popups.size() == 1u);
    const luil::ui_element_id remove { luil::ui_element_kind::menu_item, u8"지우기" };
    REQUIRE(popups.front().tree->find(remove)->enabled() == false);
    send(page, demo::card_menu_select_intent { u8"지우기" });
    auto tree { arranged(page.build(800.0f, 600.0f, 1.0f)) };
    REQUIRE(tree.find({ demo::kind_card }) != nullptr);
    send(page, demo::card_menu_select_intent { u8"편집 잠금" });
    send(page, demo::card_menu_select_intent { u8"즐겨찾기" });
    send(page, demo::card_menu_intent { 50.0f, 60.0f });
    popups = page.make_popups(1.5f, window_surface);
    REQUIRE(popups.front().tree->find(remove)->enabled());
    REQUIRE(popups.front().tree->find({ luil::ui_element_kind::menu_item, u8"즐겨찾기" })->accessibility().checked == true);
    send(page, demo::card_menu_select_intent { u8"지우기" });
    tree = arranged(page.build(800.0f, 600.0f, 1.0f));
    REQUIRE(tree.find({ demo::kind_card }) == nullptr);
    send(page, demo::card_menu_select_intent { u8"복원" });
    tree = arranged(page.build(800.0f, 600.0f, 1.0f));
    REQUIRE(tree.find({ demo::kind_card }) != nullptr);
}

TEST_CASE("The popups page lists the split button and the dropdown button with their state labels", "[demo][popups][split-button]")
{
    demo::popups_page page {};
    const auto tree { arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f) };
    REQUIRE(label_text(tree, u8"split-state-save") == u8"저장 메뉴 열림: 닫힘");
    REQUIRE(label_text(tree, u8"split-state-send") == u8"보내기 메뉴 열림: 닫힘");
    REQUIRE(label_text(tree, u8"split-last-action") == u8"마지막 동작: 없음");

    // 분할 버튼은 단추 둘로, 드롭다운 단추는 한 노드로 읽힌다.
    REQUIRE(tree.find(split_id) != nullptr);
    REQUIRE(tree.find(split_body_id)->accessibility().name == u8"저장");
    REQUIRE(tree.find(split_arrow_id)->accessibility().name == u8"저장 옵션");
    REQUIRE(arrow_open(tree, split_arrow_id) == false);
    REQUIRE(tree.find({ luil::ui_element_kind::split_button_body, u8"send" }) == nullptr);
    REQUIRE(tree.find(send_arrow_id)->accessibility().name == u8"보내기");
    REQUIRE(arrow_open(tree, send_arrow_id) == false);
}

TEST_CASE("Pressing the body acts and pressing the arrow asks for the opposite open state", "[demo][popups][split-button]")
{
    demo::popups_page page {};
    auto tree { arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f) };

    // 본체는 동작을 내고 마지막 동작에 적힌다.
    const luil::input_action invoked { first_action((*tree.find(split_body_id)->action(luil::ui_trigger::left_click))({ split_body_id, 0.0f, 0.0f })) };
    REQUIRE(page.handle(std::get<luil::app_message>(invoked)));
    tree = arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f);
    REQUIRE(label_text(tree, u8"split-last-action") == u8"마지막 동작: 저장");

    // 화살표는 열림의 반대를 담은 절대 메시지를 낸다.
    const luil::input_action opened { first_action((*tree.find(split_arrow_id)->action(luil::ui_trigger::left_click))({ split_arrow_id, 0.0f, 0.0f })) };
    REQUIRE(std::get<luil::app_message>(opened).get<demo::field_menu_intent>()->open);
    REQUIRE(page.handle(std::get<luil::app_message>(opened)));
    tree = arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f);
    REQUIRE(arrow_open(tree, split_arrow_id));
    REQUIRE(label_text(tree, u8"split-state-save") == u8"저장 메뉴 열림: 열림");
    const luil::input_action closed { first_action((*tree.find(split_arrow_id)->action(luil::ui_trigger::left_click))({ split_arrow_id, 0.0f, 0.0f })) };
    REQUIRE(std::get<luil::app_message>(closed).get<demo::field_menu_intent>()->open == false);

    // 드롭다운 단추는 버튼 전체가 메뉴를 연다.
    const luil::input_action send_opened { first_action((*tree.find(send_arrow_id)->action(luil::ui_trigger::left_click))({ send_arrow_id, 0.0f, 0.0f })) };
    REQUIRE(std::get<luil::app_message>(send_opened).get<demo::field_menu_intent>()->owner == u8"send");
}

TEST_CASE("Only one popup stays open and a late close does not close another field's popup", "[demo][popups][split-button]")
{
    demo::popups_page page {};
    send(page, demo::field_menu_intent { u8"save", true });
    auto tree { arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f) };
    REQUIRE(page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f }).size() == 1u);
    REQUIRE(page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f }).front().id == u8"save-menu");

    // 다른 필드를 열면 앞의 것이 닫힌다.
    send(page, demo::field_menu_intent { u8"send", true });
    tree = arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f);
    REQUIRE(label_text(tree, u8"split-state-save") == u8"저장 메뉴 열림: 닫힘");
    REQUIRE(label_text(tree, u8"split-state-send") == u8"보내기 메뉴 열림: 열림");
    auto popups { page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f }) };
    REQUIRE(popups.size() == 1u);
    REQUIRE(popups.front().id == u8"send-menu");

    // 이미 닫힌 필드의 늦은 닫기는 지금 떠 있는 popup을 건드리지 않는다.
    send(page, demo::field_menu_intent { u8"save", false });
    REQUIRE(page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f }).size() == 1u);

    // 드롭다운과 컨텍스트 메뉴도 같은 한 자리를 쓴다.
    send(page, demo::dropdown_toggle_intent { true });
    tree = arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f);
    REQUIRE(label_text(tree, u8"split-state-send") == u8"보내기 메뉴 열림: 닫힘");
    popups = page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f });
    REQUIRE(popups.size() == 1u);
    REQUIRE(popups.front().id == u8"dropdown");
    send(page, demo::field_menu_intent { u8"save", true });
    REQUIRE(page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f }).front().id == u8"save-menu");
    send(page, demo::card_menu_intent { 50.0f, 60.0f });
    popups = page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f });
    REQUIRE(popups.size() == 1u);
    REQUIRE(popups.front().id == u8"card-menu");

    page.close_popups();
    REQUIRE(page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f }).empty());
}

TEST_CASE("A field menu sits under its button and flips above when the window has no room below", "[demo][popups][split-button]")
{
    demo::popups_page page {};
    send(page, demo::field_menu_intent { u8"save", true });
    const auto tree { arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f) };
    const luil::rect_f field { tree.find(split_id)->bounds() };

    // 아래에 간격 2를 두고 필드 왼쪽에 맞춘다.
    const auto below { page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f }) };
    REQUIRE(below.size() == 1u);
    REQUIRE(below.front().x == field.x);
    REQUIRE(below.front().y == field.y + field.height + 2.0f);

    // 창이 필드 바로 아래에서 끝나면 위로 뒤집힌다.
    const auto above { page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, field.y + field.height + 10.0f }) };
    REQUIRE(above.size() == 1u);
    REQUIRE(above.front().y + above.front().height + 2.0f == Catch::Approx(field.y));

    // 배율 1.5에서는 필드(물리 픽셀)를 논리 좌표로 나눠 놓는다.
    const auto scaled { page.make_popups(1.5f, { 0.0f, 0.0f, 800.0f, 2000.0f }) };
    REQUIRE(scaled.front().x == Catch::Approx(field.x / 1.5f));
    REQUIRE(scaled.front().y == Catch::Approx((field.y + field.height) / 1.5f + 2.0f));
}

TEST_CASE("Choosing a field menu item records it, runs the select message and closes the menu", "[demo][popups][split-button]")
{
    demo::popups_page page {};
    send(page, demo::field_menu_intent { u8"save", true });
    auto tree { arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f) };
    const auto popups { page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f }) };
    REQUIRE(popups.size() == 1u);
    const auto& menu { static_cast<const luil::menu_element*>(popups.front().tree->root())->config() };
    REQUIRE(menu.items.size() == 2u);
    REQUIRE(menu.items[1].key == u8"save-as");

    // 항목을 고르면 select 메시지가 나가고, 이어서 닫기가 나간다. 바깥 누름 닫기와 같은 절대 메시지다.
    const luil::input_action selected_action { menu.select(u8"save-as") };
    REQUIRE(page.handle(std::get<luil::app_message>(selected_action)));
    const luil::input_action after { first_action(menu.after_select(luil::ui_action_context { {}, 0.0f, 0.0f })) };
    REQUIRE(std::get<luil::app_message>(after).get<demo::field_menu_intent>()->open == false);
    REQUIRE(page.handle(std::get<luil::app_message>(after)));
    const luil::input_action dismissed { popups.front().dismiss(luil::popup_dismiss_reason::pointer_press_outside) };
    REQUIRE(std::get<luil::app_message>(dismissed).get<demo::field_menu_intent>()->open == false);

    tree = arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f);
    REQUIRE(label_text(tree, u8"split-last-action") == u8"마지막 동작: 다른 이름으로 저장");
    REQUIRE(label_text(tree, u8"split-state-save") == u8"저장 메뉴 열림: 닫힘");
    REQUIRE(page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f }).empty());
}

namespace {
    struct bar_names
    {
        std::u8string_view owner {};
        std::u8string_view word {};
    };

    constexpr bar_names bar_list[] { { u8"info", u8"정보" }, { u8"success", u8"성공" }, { u8"warning", u8"경고" }, { u8"error", u8"오류" } };

    [[nodiscard]] luil::ui_tree toasts_tree(demo::toasts_page& page, const float width = 800.0f)
    {
        return arranged(page.build(width, 2000.0f, 1.0f), width, 2000.0f);
    }

    [[nodiscard]] luil::ui_element_id bar_part(const luil::ui_element_kind kind, const std::u8string_view owner)
    {
        return { kind, std::u8string { owner } };
    }

    // 정보 막대의 단추를 눌러 나온 메시지를 페이지에 넘긴다. 누름은 tree에 있던 그 액션이다.
    void press(demo::toasts_page& page, const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const button { tree.find(id) };
        REQUIRE(button != nullptr);
        const luil::ui_action* const action { button->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        const luil::input_action pressed { first_action((*action)(luil::ui_action_context { id, 0.0f, 0.0f })) };
        REQUIRE(page.handle(std::get<luil::app_message>(pressed)));
    }

    [[nodiscard]] float center_y(const luil::rect_f& box)
    {
        return box.y + box.height / 2.0f;
    }
}

TEST_CASE("The toasts page lists four info bars with their state labels", "[demo][toasts][info-bar]")
{
    demo::toasts_page page {};
    const auto tree { toasts_tree(page) };
    for (const bar_names& bar : bar_list)
    {
        CAPTURE(bar.owner);
        const luil::ui_element* const element { tree.find(bar_part(luil::ui_element_kind::info_bar, bar.owner)) };
        REQUIRE(element != nullptr);
        // 보조 기술은 막대를 alert로 읽고, 이름은 심각도 낱말로 시작한다.
        const luil::access_info info { element->accessibility() };
        REQUIRE(info.role == luil::access_role::alert);
        REQUIRE(info.name.starts_with(std::u8string { bar.word } + u8" "));
        REQUIRE(tree.find(bar_part(luil::ui_element_kind::info_bar_action, bar.owner)) != nullptr);
        REQUIRE(tree.find(bar_part(luil::ui_element_kind::info_bar_close, bar.owner)) != nullptr);
        REQUIRE(label_text(tree, u8"info-bar-state-" + std::u8string { bar.owner }) == std::u8string { bar.word } + u8" 막대: 열림");
    }
    REQUIRE(label_text(tree, u8"info-bar-last-action") == u8"마지막 동작: 없음");
    // 닫은 막대가 없으면 다시 여는 단추도 없다.
    REQUIRE(tree.find({ demo::kind_toast_trigger, u8"info-bar-reopen" }) == nullptr);
}

TEST_CASE("Pressing a bar action records it and closing a bar removes it until it is reopened", "[demo][toasts][info-bar]")
{
    demo::toasts_page page {};
    auto tree { toasts_tree(page) };

    press(page, tree, bar_part(luil::ui_element_kind::info_bar_action, u8"error"));
    tree = toasts_tree(page);
    REQUIRE(label_text(tree, u8"info-bar-last-action") == u8"마지막 동작: 오류 막대 다시 시도");
    // 동작은 막대를 닫지 않는다.
    REQUIRE(tree.find(bar_part(luil::ui_element_kind::info_bar, u8"error")) != nullptr);

    // 닫는 것은 앱이다. 다음 frame에 그 막대를 짓지 않고, 다른 막대는 그대로다. 다시 여는 단추가 선다.
    press(page, tree, bar_part(luil::ui_element_kind::info_bar_close, u8"warning"));
    tree = toasts_tree(page);
    REQUIRE(tree.find(bar_part(luil::ui_element_kind::info_bar, u8"warning")) == nullptr);
    REQUIRE(tree.find(bar_part(luil::ui_element_kind::info_bar_close, u8"warning")) == nullptr);
    REQUIRE(tree.find(bar_part(luil::ui_element_kind::info_bar, u8"info")) != nullptr);
    REQUIRE(label_text(tree, u8"info-bar-state-warning") == u8"경고 막대: 닫힘");
    REQUIRE(label_text(tree, u8"info-bar-state-error") == u8"오류 막대: 열림");
    REQUIRE(label_text(tree, u8"info-bar-last-action") == u8"마지막 동작: 경고 막대 닫기");

    press(page, tree, bar_part(luil::ui_element_kind::info_bar_close, u8"info"));
    tree = toasts_tree(page);
    REQUIRE(label_text(tree, u8"info-bar-state-info") == u8"정보 막대: 닫힘");

    // 다시 열면 닫았던 막대가 모두 돌아오고 단추는 사라진다.
    press(page, tree, { demo::kind_toast_trigger, u8"info-bar-reopen" });
    tree = toasts_tree(page);
    for (const bar_names& bar : bar_list)
        REQUIRE(tree.find(bar_part(luil::ui_element_kind::info_bar, bar.owner)) != nullptr);
    REQUIRE(label_text(tree, u8"info-bar-state-warning") == u8"경고 막대: 열림");
    REQUIRE(label_text(tree, u8"info-bar-last-action") == u8"마지막 동작: 막대 다시 열기");
    REQUIRE(tree.find({ demo::kind_toast_trigger, u8"info-bar-reopen" }) == nullptr);
}

TEST_CASE("The action button of an info bar moves under the text when the page narrows", "[demo][toasts][info-bar]")
{
    demo::toasts_page page {};
    const luil::ui_element_id bar_id { bar_part(luil::ui_element_kind::info_bar, u8"error") };
    const luil::ui_element_id action_id { bar_part(luil::ui_element_kind::info_bar_action, u8"error") };
    const luil::ui_element_id close_id { bar_part(luil::ui_element_kind::info_bar_close, u8"error") };

    // 넓은 페이지에서는 동작 단추가 닫기 옆 첫 줄에 서서 막대가 낮다.
    const auto wide { toasts_tree(page, 800.0f) };
    const luil::rect_f wide_bar { wide.find(bar_id)->bounds() };
    const luil::rect_f wide_action { wide.find(action_id)->bounds() };
    REQUIRE(center_y(wide_action) == Catch::Approx(center_y(wide.find(close_id)->bounds())));

    // 좁은 페이지에서는 글 폭이 모자라 글 아래 줄로 내려가고, 단추는 막대 안에 머문다.
    const auto narrow { toasts_tree(page, 400.0f) };
    const luil::rect_f narrow_bar { narrow.find(bar_id)->bounds() };
    const luil::rect_f narrow_action { narrow.find(action_id)->bounds() };
    const luil::rect_f narrow_close { narrow.find(close_id)->bounds() };
    REQUIRE(narrow_action.y >= narrow_close.y + narrow_close.height);
    REQUIRE(narrow_action.y + narrow_action.height <= narrow_bar.y + narrow_bar.height + 0.5f);
    REQUIRE(narrow_bar.height > wide_bar.height);
}

TEST_CASE("Every editable field of the demo has a text target", "[demo][text-input]")
{
    // 편집 대상이 없는 칸은 키 입력이 닿지 않고 UIA Value가 읽기 전용으로 답한다. 칸을 더하면 이 표에도 더한다.
    struct field
    {
        luil::ui_element_kind kind {};
        luil::text_input_target target {};
    };
    const field fields[] {
        { demo::kind_number_input, demo::target_number },
        { demo::kind_note_input, demo::target_note },
        { demo::kind_memo_input, demo::target_memo },
        { demo::kind_link_view_input, demo::target_link_view },
        { demo::kind_code_input, demo::target_code },
        { demo::kind_line_break_input, demo::target_line_break },
        { demo::kind_tool_note_input, demo::target_tool_note },
        { demo::kind_dropdown_search_input, demo::target_dropdown_search },
        { demo::kind_tool_menu_search_input, demo::target_tool_menu_search },
        { demo::kind_network_url_input, demo::target_network_url },
        { demo::kind_workbench_summary_input, demo::target_workbench_summary },
        { demo::kind_workbench_author_input, demo::target_workbench_author },
        { demo::kind_workbench_diff, demo::target_workbench_diff },
        { demo::kind_workbench_find_input, demo::target_workbench_find },
        { luil::ui_element_kind::command_palette_search, demo::target_palette },
    };
    for (const field& each : fields)
    {
        const std::optional<luil::text_input_target> target { demo::text_target_of_kind(each.kind) };
        REQUIRE(target.has_value());
        REQUIRE(*target == each.target);
    }
    // 글이 아닌 element는 편집 대상이 없다.
    REQUIRE(demo::text_target_of_kind(demo::kind_text).has_value() == false);
}

namespace {
    [[nodiscard]] luil::ui_tree zoom_tree(demo::zoom_page& page)
    {
        return arranged(page.build(800.0f, 600.0f, 1.0f));
    }

    // UIA가 되읽는 줌 단추의 이름이다. 단추의 글이 곧 접근 이름이다.
    [[nodiscard]] std::u8string zoom_button_name(const luil::ui_tree& tree, const luil::ui_element_kind kind)
    {
        const luil::ui_element* const button { tree.find({ kind, u8"diagram" }) };
        REQUIRE(button != nullptr);
        return button->accessibility().name;
    }

    // 줌 단추 글을 바꾸는 단추를 눌러 나온 메시지를 페이지에 넘긴다. 누름은 tree에 있던 그 액션이다.
    void press_zoom_labels(demo::zoom_page& page, const luil::ui_tree& tree)
    {
        const luil::ui_element_id id { demo::kind_layout, u8"zoom-label-toggle" };
        const luil::ui_element* const button { tree.find(id) };
        REQUIRE(button != nullptr);
        const luil::ui_action* const action { button->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        const luil::input_action pressed { first_action((*action)(luil::ui_action_context { id, 0.0f, 0.0f })) };
        REQUIRE(page.handle(std::get<luil::app_message>(pressed)));
    }
}

TEST_CASE("The desktop zoom page switches its button names between the default language and English", "[demo][zoom][default-text]")
{
    demo::zoom_page page {};
    auto tree { zoom_tree(page) };
    REQUIRE(zoom_button_name(tree, luil::ui_element_kind::zoom_decrease) == u8"축소");
    REQUIRE(zoom_button_name(tree, luil::ui_element_kind::zoom_increase) == u8"확대");
    REQUIRE(zoom_button_name(tree, luil::ui_element_kind::zoom_fit) == u8"맞춤");
    REQUIRE(label_text(tree, u8"zoom-label-language") == u8"줌 단추 글: 기본");

    // 바꾸는 단추는 반대 언어를 절대 값으로 보낸다.
    press_zoom_labels(page, tree);
    tree = zoom_tree(page);
    REQUIRE(zoom_button_name(tree, luil::ui_element_kind::zoom_decrease) == u8"Zoom out");
    REQUIRE(zoom_button_name(tree, luil::ui_element_kind::zoom_increase) == u8"Zoom in");
    REQUIRE(zoom_button_name(tree, luil::ui_element_kind::zoom_fit) == u8"Fit");
    REQUIRE(label_text(tree, u8"zoom-label-language") == u8"줌 단추 글: English");

    // 같은 메시지가 거듭 와도 결과가 같다.
    send(page, demo::zoom_labels_intent { true });
    REQUIRE(zoom_button_name(zoom_tree(page), luil::ui_element_kind::zoom_fit) == u8"Fit");

    press_zoom_labels(page, tree);
    tree = zoom_tree(page);
    REQUIRE(zoom_button_name(tree, luil::ui_element_kind::zoom_decrease) == u8"축소");
    REQUIRE(label_text(tree, u8"zoom-label-language") == u8"줌 단추 글: 기본");
}

TEST_CASE("The desktop zoom page puts the language switch after the fit buttons in the same row", "[demo][zoom][default-text]")
{
    demo::zoom_page page {};
    const auto tree { zoom_tree(page) };
    const luil::ui_element* const controls { tree.find({ luil::ui_element_kind::zoom_controls, u8"diagram" }) };
    const luil::ui_element* const fit_height { tree.find({ demo::kind_layout, u8"zoom-fit-height" }) };
    const luil::ui_element* const toggle { tree.find({ demo::kind_layout, u8"zoom-label-toggle" }) };
    const luil::ui_element* const language { tree.find({ demo::kind_text, u8"zoom-label-language" }) };
    REQUIRE(controls != nullptr);
    REQUIRE(fit_height != nullptr);
    REQUIRE(toggle != nullptr);
    REQUIRE(language != nullptr);
    // 한 줄에 놓이고 800 폭 창 안에 든다.
    REQUIRE(toggle->bounds().x >= fit_height->bounds().x + fit_height->bounds().width);
    REQUIRE(language->bounds().x >= toggle->bounds().x + toggle->bounds().width);
    REQUIRE(language->bounds().x + language->bounds().width <= 800.0f);
    REQUIRE(toggle->bounds().y == Catch::Approx(controls->bounds().y));
}
