#include "demo/gallery_page.h"
#include "demo/inspector_page.h"
#include "demo/line_break_sample.h"
#include "demo/popups_page.h"
#include "demo/readonly_undo_keys.h"
#include "demo/rich_text_page.h"
#include "demo/shell_menu.h"
#include "demo/tabs_page.h"
#include "demo/text_targets.h"
#include "demo/theme_page.h"
#include "demo/toasts_page.h"
#include "demo/zoom_page.h"

#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/color_picker_element.h"
#include "luil/ui/menu_bar_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/root_element.h"
#include "luil/ui/tab_bar_element.h"
#include "luil/ui/text_area_element.h"
#include "luil/ui/ui_inspect.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

// 시험 실행 파일에 실은 assets/style.json이다 (tests/CMakeLists.txt).
extern const std::uint8_t luil_test_base_style[];
extern const std::size_t luil_test_base_style_size;
#include <vector>

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
        { demo::kind_style_json_input, demo::target_style_json },
        { demo::kind_rich_editor_input, demo::target_rich_editor },
        { demo::kind_tool_note_input, demo::target_tool_note },
        { demo::kind_dropdown_search_input, demo::target_dropdown_search },
        { demo::kind_tool_menu_search_input, demo::target_tool_menu_search },
        { demo::kind_network_url_input, demo::target_network_url },
        { demo::kind_workbench_summary_input, demo::target_workbench_summary },
        { demo::kind_workbench_author_input, demo::target_workbench_author },
        { demo::kind_workbench_diff, demo::target_workbench_diff },
        { demo::kind_workbench_find_input, demo::target_workbench_find },
        { luil::ui_element_kind::command_palette_search, demo::target_palette },
        { demo::kind_gallery_color_hex_input, demo::target_gallery_color_hex },
        { demo::kind_popups_color_hex_input, demo::target_popups_color_hex },
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
    REQUIRE(zoom_button_name(tree, luil::ui_element_kind::zoom_decrease) == u8"Out");
    REQUIRE(zoom_button_name(tree, luil::ui_element_kind::zoom_increase) == u8"In");
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

namespace {
    // 글꼴이 넓은 쪽을 본뜬 측정기다: 로마자는 글자 크기의 0.6배, 그 밖(한글)은 1배. 이 폭이면 56dp 단추에 글꼴 배율 1.1에서
    // 일곱 글자쯤 들어, 실기기에서 "Zoom out"이 "Zoom o…"로 잘린 것과 맞는다. 설치 글꼴에 기대지 않아 어느 기기에서나 같다.
    [[nodiscard]] float wide_sans_width(const std::u8string_view text, const float pixel_size)
    {
        float em { 0.0f };
        for (const char8_t byte : text)
        {
            if ((byte & 0xC0u) == 0x80u)
                continue;
            em += byte < 0x80u ? 0.6f : 1.0f;
        }
        return em * pixel_size;
    }

    const luil::text_measurer wide_sans_measurer { wide_sans_width };
}

TEST_CASE("The English zoom button names of the desktop page fit their buttons at a 1.1 font scale", "[demo][zoom][default-text]")
{
    // 줌 단추의 폭은 라이브러리가 정한다(56dp). 영어 글이 길면 "Zoom o…"로 잘리므로 데모는 짧은 글을 쓴다.
    demo::zoom_page page {};
    send(page, demo::zoom_labels_intent { true });
    const auto tree { zoom_tree(page) };
    const luil::measure_context measure { .scale = 1.0f, .measurer = &wide_sans_measurer };
    for (const luil::ui_element_kind kind : { luil::ui_element_kind::zoom_decrease, luil::ui_element_kind::zoom_increase, luil::ui_element_kind::zoom_fit })
    {
        const luil::ui_element* const button { tree.find({ kind, u8"diagram" }) };
        REQUIRE(button != nullptr);
        const float text_width { measure.text_width(button->accessibility().name, measure.style_metrics().body_font_size) };
        REQUIRE(text_width * 1.1f <= button->bounds().width);
    }
}

namespace {
    // 별점 줄의 label이 그린 글이다. 줄마다 "이름: 3 / 5" 꼴이고 화면과 보조 기술이 같은 말을 한다.
    [[nodiscard]] std::u8string rating_label(const luil::ui_tree& tree, const std::u8string& line)
    {
        return label_text(tree, u8"gallery/rating-" + line + u8"-value");
    }

    [[nodiscard]] const luil::ui_element& rating_of(const luil::ui_tree& tree, const std::u8string& line)
    {
        const luil::ui_element* const rating { tree.find({ luil::ui_element_kind::rating, u8"gallery/rating-" + line }) };
        REQUIRE(rating != nullptr);
        return *rating;
    }

    // 낸 메시지에서 별점 intent를 꺼낸다.
    [[nodiscard]] demo::gallery_rating_intent rating_intent_of(const luil::input_action& action)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        REQUIRE(message != nullptr);
        const auto* const intent { message->get<demo::gallery_rating_intent>() };
        REQUIRE(intent != nullptr);
        return *intent;
    }
}

TEST_CASE("The gallery rating card draws whole, half and average lines and reads each value back", "[demo][gallery][rating]")
{
    demo::gallery_page page {};
    const auto tree { arranged(page.build_controls()) };

    // 줄마다 label이 값을 그린다. UIA가 이름으로 되읽는 말이다.
    REQUIRE(rating_label(tree, u8"whole") == u8"별점: 3 / 5");
    REQUIRE(rating_label(tree, u8"half") == u8"반 별: 3.5 / 5");
    REQUIRE(rating_label(tree, u8"average") == u8"평균: 4.3 / 5");

    // 별점 요소의 접근 값도 같은 글이다. 이름은 줄마다 다르다.
    const luil::access_info whole { rating_of(tree, u8"whole").accessibility() };
    REQUIRE(whole.role == luil::access_role::slider);
    REQUIRE(whole.name == u8"별점");
    REQUIRE(whole.value == u8"3 / 5");
    REQUIRE(whole.range == luil::access_range { 0.0f, 5.0f, 3.0f, 1.0f, 1.0f });
    const luil::access_info half { rating_of(tree, u8"half").accessibility() };
    REQUIRE(half.name == u8"반 별 점수");
    REQUIRE(half.value == u8"3.5 / 5");
    REQUIRE(half.range == luil::access_range { 0.0f, 5.0f, 3.5f, 0.5f, 1.0f });
    const luil::access_info average { rating_of(tree, u8"average").accessibility() };
    REQUIRE(average.name == u8"평균 점수");
    REQUIRE(average.value == u8"4.3 / 5");

    // 정수 별은 칸이 다섯, 반 별은 열이고, 평균은 표시 전용이라 칸이 없다.
    REQUIRE(rating_of(tree, u8"whole").children().size() == 5u);
    REQUIRE(rating_of(tree, u8"half").children().size() == 10u);
    REQUIRE(rating_of(tree, u8"average").children().empty());
    REQUIRE(luil::plan_access_request(rating_of(tree, u8"average"), { luil::access_command::set_value, 4.0f }).has_value() == false);
}

TEST_CASE("A star click, a value set from assistive technology and the reset button change the rating lines", "[demo][gallery][rating]")
{
    demo::gallery_page page {};
    auto tree { arranged(page.build_controls()) };

    // 별 3은 지금 값이라 다시 누르면 지운다. 별 4는 4를 매긴다.
    const luil::ui_element& whole { rating_of(tree, u8"whole") };
    const luil::ui_action* const clear { whole.children()[2]->action(luil::ui_trigger::left_click) };
    REQUIRE(clear != nullptr);
    const demo::gallery_rating_intent cleared { rating_intent_of(first_action((*clear)({ whole.children()[2]->id() }))) };
    REQUIRE(cleared.key == u8"whole");
    REQUIRE(cleared.value == 0.0f);
    const luil::ui_action* const fourth { whole.children()[3]->action(luil::ui_trigger::left_click) };
    REQUIRE(fourth != nullptr);
    const demo::gallery_rating_intent chosen { rating_intent_of(first_action((*fourth)({ whole.children()[3]->id() }))) };
    REQUIRE(chosen.value == 4.0f);
    send(page, chosen);
    tree = arranged(page.build_controls());
    REQUIRE(rating_label(tree, u8"whole") == u8"별점: 4 / 5");
    REQUIRE(rating_label(tree, u8"half") == u8"반 별: 3.5 / 5");

    // 보조 기술의 값 정하기는 가장 가까운 별로 반올림해 같은 메시지를 낸다. 반 별 줄은 반 별이 걸음이다.
    const luil::ui_element& half { rating_of(tree, u8"half") };
    const auto plan { luil::plan_access_request(half, { luil::access_command::set_value, 1.3f }) };
    REQUIRE(plan.has_value());
    const demo::gallery_rating_intent set { rating_intent_of(first_action(*plan)) };
    REQUIRE(set.key == u8"half");
    REQUIRE(set.value == 1.5f);
    send(page, set);
    tree = arranged(page.build_controls());
    REQUIRE(rating_label(tree, u8"half") == u8"반 별: 1.5 / 5");

    // 범위 밖 값은 앱이 자른다.
    send(page, demo::gallery_rating_intent { u8"whole", 9.0f });
    tree = arranged(page.build_controls());
    REQUIRE(rating_label(tree, u8"whole") == u8"별점: 5 / 5");

    // 초기화 단추는 두 줄을 아직 매기지 않음으로 되돌리고 평균은 그대로다.
    const luil::ui_element* const reset { tree.find({ demo::kind_layout, u8"gallery/rating-reset" }) };
    REQUIRE(reset != nullptr);
    const luil::ui_action* const pressed { reset->action(luil::ui_trigger::left_click) };
    REQUIRE(pressed != nullptr);
    send(page, rating_intent_of(first_action((*pressed)({ reset->id() }))));
    tree = arranged(page.build_controls());
    REQUIRE(rating_label(tree, u8"whole") == u8"별점: 0 / 5");
    REQUIRE(rating_label(tree, u8"half") == u8"반 별: 0 / 5");
    REQUIRE(rating_label(tree, u8"average") == u8"평균: 4.3 / 5");

    // 상태 초기화는 처음 값으로 돌린다.
    send(page, demo::gallery_reset_intent {});
    tree = arranged(page.build_controls());
    REQUIRE(rating_label(tree, u8"whole") == u8"별점: 3 / 5");
    REQUIRE(rating_label(tree, u8"half") == u8"반 별: 3.5 / 5");
}

namespace {
    [[nodiscard]] std::u8string color_part(const std::u8string_view owner, const std::u8string_view part)
    {
        std::u8string text { owner };
        text += u8'\u001f';
        text += part;
        return text;
    }

    // 초안 전체를 이 글로 바꾸는 편집 메시지다. 앱 정책이 칸의 kind를 이 대상으로 옮긴다.
    [[nodiscard]] demo::edit_intent replace_draft(const luil::text_input_target target, const std::u8string_view text)
    {
        luil::text_edit_request request {};
        request.target = target;
        request.command = luil::text::text_edit_command::replace_all;
        request.text = std::u8string { text };
        request.offset = request.text.size();
        return demo::edit_intent { request };
    }

    // 액션 하나를 페이지에 그대로 전한다.
    template<typename Page>
    void deliver_action(Page& page, const luil::input_action& action)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        REQUIRE(message != nullptr);
        REQUIRE(page.handle(*message));
    }
}

TEST_CASE("The gallery color card shows the picker, the presets and the hex field and reads the color back", "[demo][gallery][color-picker]")
{
    demo::gallery_page page {};
    auto tree { arranged(page.build_controls()) };
    REQUIRE(label_text(tree, u8"gallery/color-value") == u8"색: #3366FF80 · 색상 225° · 채도 80% · 명도 100% · 알파 50%");
    const luil::ui_element* const picker { tree.find({ luil::ui_element_kind::color_picker, u8"gallery/color" }) };
    REQUIRE(picker != nullptr);
    REQUIRE(picker->accessibility().value == u8"#3366FF80");
    const luil::ui_element* const hex { tree.find({ demo::kind_gallery_color_hex_input, u8"gallery/color-hex" }) };
    REQUIRE(hex != nullptr);
    REQUIRE(hex->accessibility().value == u8"#3366FF80");
    // 견본 넷이고 첫 견본이 처음 색이다. 알파 막대가 선다.
    for (const std::u8string_view number : { u8"1", u8"2", u8"3", u8"4" })
        REQUIRE(tree.find({ luil::ui_element_kind::color_picker_preset, color_part(u8"gallery/color", number) }) != nullptr);
    REQUIRE(tree.find({ luil::ui_element_kind::color_picker_preset, color_part(u8"gallery/color", u8"1") })->accessibility().name == u8"처음 색");
    REQUIRE(tree.find({ luil::ui_element_kind::color_picker_bar, color_part(u8"gallery/color", u8"alpha") }) != nullptr);

    // 보조 기술이 색상 막대를 120도로 정하면 label과 16진 칸이 함께 바뀐다.
    const luil::ui_element* const hue { tree.find({ luil::ui_element_kind::color_picker_bar, color_part(u8"gallery/color", u8"hue") }) };
    REQUIRE(hue != nullptr);
    const auto plan { luil::plan_access_request(*hue, { luil::access_command::set_value, 120.0f }) };
    REQUIRE(plan.has_value());
    deliver_action(page, first_action(*plan));
    tree = arranged(page.build_controls());
    REQUIRE(label_text(tree, u8"gallery/color-value") == u8"색: #33FF3380 · 색상 120° · 채도 80% · 명도 100% · 알파 50%");
    REQUIRE(tree.find({ demo::kind_gallery_color_hex_input, u8"gallery/color-hex" })->accessibility().value == u8"#33FF3380");

    // 회색 견본은 지금 색상을 지킨다.
    const luil::ui_element* const gray { tree.find({ luil::ui_element_kind::color_picker_preset, color_part(u8"gallery/color", u8"3") }) };
    REQUIRE(gray != nullptr);
    deliver_action(page, first_action((*gray->action(luil::ui_trigger::left_click))({ gray->id() })));
    tree = arranged(page.build_controls());
    REQUIRE(label_text(tree, u8"gallery/color-value") == u8"색: #808080FF · 색상 120° · 채도 0% · 명도 50% · 알파 100%");

    // 초기화는 처음 색으로 돌린다.
    send(page, demo::gallery_reset_intent {});
    tree = arranged(page.build_controls());
    REQUIRE(label_text(tree, u8"gallery/color-value") == u8"색: #3366FF80 · 색상 225° · 채도 80% · 명도 100% · 알파 50%");
}

TEST_CASE("A typed hex color is filtered, committed when the field is left and marks a bad draft", "[demo][gallery][color-picker]")
{
    demo::gallery_page page {};
    // 넣는 글은 16진 글자만 남는다. 칸을 떠나는 훅이 확정 메시지를 낸다.
    send(page, replace_draft(demo::target_gallery_color_hex, u8"색 #00 80 00"));
    auto tree { arranged(page.build_controls()) };
    const luil::ui_element* const hex { tree.find({ demo::kind_gallery_color_hex_input, u8"gallery/color-hex" }) };
    REQUIRE(hex != nullptr);
    REQUIRE(hex->accessibility().value == u8"#008000");
    REQUIRE(hex->focus_leave_action() != nullptr);
    deliver_action(page, first_action((*hex->focus_leave_action())({ hex->id() })));
    tree = arranged(page.build_controls());
    REQUIRE(label_text(tree, u8"gallery/color-value") == u8"색: #008000FF · 색상 120° · 채도 100% · 명도 50% · 알파 100%");
    REQUIRE(tree.find({ demo::kind_gallery_color_hex_input, u8"gallery/color-hex" })->accessibility().value == u8"#008000FF");

    // 읽히지 않는 초안은 칸을 붉게 두고 색은 그대로다.
    send(page, replace_draft(demo::target_gallery_color_hex, u8"#12"));
    send(page, demo::gallery_color_commit_intent {});
    tree = arranged(page.build_controls());
    REQUIRE(tree.find({ demo::kind_gallery_color_hex_input, u8"gallery/color-hex" })->accessibility().invalid);
    REQUIRE(label_text(tree, u8"gallery/color-value") == u8"색: #008000FF · 색상 120° · 채도 100% · 명도 50% · 알파 100%");
    // 다른 페이지의 칸 편집은 받지 않는다.
    REQUIRE(page.handle(luil::app_message { replace_draft(demo::target_popups_color_hex, u8"#000") }) == false);
}

namespace {
    const luil::ui_element_id color_field_id { luil::ui_element_kind::color_field, u8"popups/color-field" };

    [[nodiscard]] std::optional<luil::ui_popup> color_popup_of(const demo::popups_page& page)
    {
        for (luil::ui_popup& popup : page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f }))
            if (popup.id == u8"color-popup")
                return std::move(popup);
        return std::nullopt;
    }
}

TEST_CASE("The color field opens a picker popup under it and asks to focus the plane", "[demo][popups][color-picker]")
{
    demo::popups_page page {};
    auto tree { arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f) };
    REQUIRE(label_text(tree, u8"popups/color-state") == u8"색 칸: 닫힘 · #3366FF");
    REQUIRE(page.make_focus_request().has_value() == false);
    const luil::ui_element* const field { tree.find(color_field_id) };
    REQUIRE(field != nullptr);
    REQUIRE(field->accessibility().name == u8"글자 색");
    REQUIRE(field->accessibility().value == u8"#3366FF");

    // 누르면 여는 메시지이고, 열면 popup 표면의 판으로 초점을 옮기는 요청이 실린다.
    deliver_action(page, first_action((*field->action(luil::ui_trigger::left_click))({ field->id() })));
    const std::optional<luil::focus_request> focus { page.make_focus_request() };
    REQUIRE(focus.has_value());
    REQUIRE(focus->surface == u8"color-popup");
    REQUIRE(focus->target == luil::color_picker_element::plane_id(u8"popups/color"));
    tree = arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f);
    REQUIRE(label_text(tree, u8"popups/color-state") == u8"색 칸: 열림 · #3366FF");
    REQUIRE(tree.find(color_field_id)->accessibility().expanded == true);
    const luil::rect_f bounds { tree.find(color_field_id)->bounds() };
    const std::optional<luil::ui_popup> popup { color_popup_of(page) };
    REQUIRE(popup.has_value());
    REQUIRE(popup->x == bounds.x);
    REQUIRE(popup->y == bounds.y + bounds.height + 2.0f);
    REQUIRE(popup->tree->find(luil::color_picker_element::plane_id(u8"popups/color")) != nullptr);
    REQUIRE(popup->tree->find({ demo::kind_popups_color_hex_input, u8"popups/color-hex" }) != nullptr);
    // 한 번에 하나만 뜬다.
    REQUIRE(page.make_popups(1.0f, { 0.0f, 0.0f, 800.0f, 2000.0f }).size() == 1u);

    // 창 이동·활성 전환에는 남는다. 글을 치는 popup이다.
    REQUIRE(std::holds_alternative<std::monostate>(popup->dismiss(luil::popup_dismiss_reason::surface_moved)));
    REQUIRE(std::holds_alternative<std::monostate>(popup->dismiss(luil::popup_dismiss_reason::activation_changed)));
    // Esc로 닫으면 칸으로 돌아오는 초점 요청을 한 번 더 싣는다.
    deliver_action(page, popup->dismiss(luil::popup_dismiss_reason::escape_key));
    const std::optional<luil::focus_request> back { page.make_focus_request() };
    REQUIRE(back.has_value());
    REQUIRE(back->id == focus->id + 1u);
    REQUIRE(back->surface.empty());
    REQUIRE(back->target == color_field_id);
    REQUIRE(color_popup_of(page).has_value() == false);
    tree = arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f);
    REQUIRE(label_text(tree, u8"popups/color-state") == u8"색 칸: 닫힘 · #3366FF");
}

TEST_CASE("Closing the color popup commits the hex draft typed in it and keeps the hue of a gray", "[demo][popups][color-picker]")
{
    demo::popups_page page {};
    send(page, demo::color_field_intent { true });
    send(page, replace_draft(demo::target_popups_color_hex, u8"#808080"));
    auto tree { arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f) };
    std::optional<luil::ui_popup> popup { color_popup_of(page) };
    REQUIRE(popup.has_value());
    REQUIRE(popup->tree->find({ demo::kind_popups_color_hex_input, u8"popups/color-hex" })->accessibility().value == u8"#808080");

    // 바깥을 눌러 닫으면 popup의 tree가 사라져 떠남 훅이 없다. 닫는 메시지가 초안을 확정하고, 초점은 옮기지 않는다.
    const std::uint64_t opened { page.make_focus_request()->id };
    deliver_action(page, popup->dismiss(luil::popup_dismiss_reason::pointer_press_outside));
    REQUIRE(page.make_focus_request()->id == opened);
    tree = arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f);
    REQUIRE(label_text(tree, u8"popups/color-state") == u8"색 칸: 닫힘 · #808080");

    // 다시 열면 회색에서도 색상은 225도 그대로다.
    send(page, demo::color_field_intent { true });
    tree = arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f);
    popup = color_popup_of(page);
    REQUIRE(popup.has_value());
    const luil::ui_element* const hue { popup->tree->find({ luil::ui_element_kind::color_picker_bar, color_part(u8"popups/color", u8"hue") }) };
    REQUIRE(hue != nullptr);
    REQUIRE(hue->accessibility().range->value == 225.0f);
    REQUIRE(popup->tree->find({ demo::kind_popups_color_hex_input, u8"popups/color-hex" })->accessibility().value == u8"#808080");

    // 칸을 다시 누르는 닫기와 페이지를 떠나는 닫기도 초안을 확정한다.
    send(page, replace_draft(demo::target_popups_color_hex, u8"#f00"));
    send(page, demo::color_field_intent { false });
    tree = arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f);
    REQUIRE(label_text(tree, u8"popups/color-state") == u8"색 칸: 닫힘 · #FF0000");
    send(page, demo::color_field_intent { true });
    send(page, replace_draft(demo::target_popups_color_hex, u8"#00f"));
    page.close_popups();
    tree = arranged(page.build(800.0f, 2000.0f, 1.0f), 800.0f, 2000.0f);
    REQUIRE(label_text(tree, u8"popups/color-state") == u8"색 칸: 닫힘 · #0000FF");
}

TEST_CASE("The theme page reads a style file and keeps the earlier style when one is refused", "[demo][style_json]")
{
    // 데모 빌드가 실은 파일 글 대신 시험 실행 파일에 실은 assets/style.json을 바탕으로 준다 (ui_style_json_tests.cpp와 같은 것).
    const std::u8string source { reinterpret_cast<const char8_t*>(luil_test_base_style), luil_test_base_style_size };
    demo::theme_page page { source };
    const auto status = [&page] {
        return label_text(arranged(page.build(800.0f, 600.0f, 1.0f)), u8"theme-style-status");
    };
    const auto field_text = [&page] {
        const auto tree { arranged(page.build(800.0f, 600.0f, 1.0f)) };
        const luil::ui_element* const field { tree.find({ demo::kind_style_json_input, u8"theme-style-json" }) };
        REQUIRE(field != nullptr);
        return field->accessibility().value;
    };
    REQUIRE(status() == u8"내장값 · dark.windowBackground #1e1e1e");
    REQUIRE(page.style() == nullptr);
    REQUIRE(field_text() == source);

    // 받은 견본은 칸에 실리고 셸이 실을 스타일이 된다.
    send(page, demo::style_sheet_intent { demo::style_sheet_action::sample_background });
    REQUIRE(status() == u8"적용됨 · 오류 0 · dark.windowBackground #1a1b26");
    const std::shared_ptr<const luil::ui_style> applied { page.style() };
    REQUIRE(applied != nullptr);
    REQUIRE(applied->dark.window_background == luil::make_ui_color(0x1a, 0x1b, 0x26));
    REQUIRE(applied->light.window_background == luil::make_ui_color(0xe8, 0xea, 0xf2));
    REQUIRE(applied->metrics.control_corner_radius == 8.0f);
    REQUIRE(field_text() == demo::style_sheet_sample(source, demo::style_sheet_action::sample_background));

    // 막힌 견본은 오류 글을 말하고 쥔 스타일(같은 포인터)을 그대로 둔다.
    send(page, demo::style_sheet_intent { demo::style_sheet_action::sample_unknown_key });
    REQUIRE(status() == u8"거부 · 오류 1 · Unknown key in dark: windowBackgrund · 이전 스타일 유지");
    REQUIRE(page.style() == applied);
    send(page, demo::style_sheet_intent { demo::style_sheet_action::sample_broken });
    REQUIRE(status().starts_with(u8"거부 · 오류 1 · Invalid JSON at byte "));
    REQUIRE(page.style() == applied);

    // 칸을 고쳐 적용하면 그 값이다 (UIA의 SetValue와 같은 편집 길이다).
    luil::text_edit_request request {};
    request.target = demo::target_style_json;
    request.command = luil::text::text_edit_command::replace_all;
    request.text = source;
    request.text.replace(request.text.find(u8"#1e1e1e"), 7, u8"#203040");
    send(page, demo::edit_intent { request });
    send(page, demo::style_sheet_intent { demo::style_sheet_action::apply });
    REQUIRE(status() == u8"적용됨 · 오류 0 · dark.windowBackground #203040");
    REQUIRE(page.style() != applied);

    // 내장값으로 돌아가면 쥔 스타일이 비고 칸이 내장 파일 글이다.
    send(page, demo::style_sheet_intent { demo::style_sheet_action::reset });
    REQUIRE(status() == u8"내장값 · dark.windowBackground #1e1e1e");
    REQUIRE(page.style() == nullptr);
    REQUIRE(field_text() == source);
}

TEST_CASE("The theme page wrap sample switches the policy of a label that leaves its field empty", "[demo][style_json][wrap-policy]")
{
    // 정책 칸이 빈 label을 지금 스타일로 잰 상태 줄이다 (docs/wrap-policy-key-design.md의 확인). core test는 글꼴이 없어 줄 수는
    // 실제 글꼴과 다르다. 정책 낱말만 견주고 줄 수는 고정 폭 측정기로 mobile_demo_page_tests가 잠근다.
    const std::u8string source { reinterpret_cast<const char8_t*>(luil_test_base_style), luil_test_base_style_size };
    demo::theme_page page { source };
    const auto wrap_status = [&page] {
        return label_text(arranged(page.build(800.0f, 600.0f, 1.0f)), u8"theme-wrap-status");
    };
    const auto policy_of = [](const std::u8string& status) {
        return status.substr(0, status.find(u8" · "));
    };
    const auto tree { arranged(page.build(800.0f, 600.0f, 1.0f)) };
    REQUIRE(tree.find({ demo::kind_tool_toggle, u8"theme-style-sample-wrap" }) != nullptr);
    REQUIRE(label_text(tree, u8"theme-wrap-label") == demo::line_break_sample);
    REQUIRE(policy_of(wrap_status()) == u8"스타일 정책 hangul_words");

    // 견본은 받혀 셸이 실을 스타일이 되고, 그 스타일의 정책이 label에 닿는다. 창 바탕은 그대로다.
    send(page, demo::style_sheet_intent { demo::style_sheet_action::sample_wrap });
    REQUIRE(label_text(arranged(page.build(800.0f, 600.0f, 1.0f)), u8"theme-style-status") == u8"적용됨 · 오류 0 · dark.windowBackground #1e1e1e");
    const std::shared_ptr<const luil::ui_style> applied { page.style() };
    REQUIRE(applied != nullptr);
    REQUIRE(applied->text.wrap_policy == luil::line_break_policy::unicode);
    REQUIRE(policy_of(wrap_status()) == u8"스타일 정책 unicode");
    REQUIRE(wrap_status() == demo::measure_style_wrap_status(demo::style_wrap_label_config(), applied.get(), 1.0f));

    // 막힌 견본 뒤에도 앞서 받은 정책이 남고, 내장값으로 돌아가면 어절 정책이다.
    send(page, demo::style_sheet_intent { demo::style_sheet_action::sample_unknown_key });
    REQUIRE(page.style() == applied);
    REQUIRE(policy_of(wrap_status()) == u8"스타일 정책 unicode");
    send(page, demo::style_sheet_intent { demo::style_sheet_action::reset });
    REQUIRE(policy_of(wrap_status()) == u8"스타일 정책 hangul_words");

    // 키를 적지 않은 스타일 파일에도 견본이 키를 넣는다.
    std::u8string without { source };
    const std::u8string_view kept { u8"\"lineBreak\": \"space\"" };
    const std::u8string_view dropped { u8"\"wrapPolicy\": \"hangulWords\"" };
    REQUIRE(without.find(kept) != std::u8string::npos);
    REQUIRE(without.find(dropped) != std::u8string::npos);
    const std::size_t from { without.find(kept) + kept.size() };
    without.erase(from, without.find(dropped) + dropped.size() - from);
    REQUIRE(without.find(u8"wrapPolicy") == std::u8string::npos);
    REQUIRE(luil::parse_ui_style(without).ok());
    const luil::style_parse_result parsed { luil::parse_ui_style(demo::style_sheet_sample(without, demo::style_sheet_action::sample_wrap)) };
    REQUIRE(parsed.ok());
    REQUIRE(parsed.style->text.wrap_policy == luil::line_break_policy::unicode);
}

namespace {
    // 1000x700 창의 콘텐츠 영역에서 조립해 배치한 검사기 페이지다 (배율 1).
    [[nodiscard]] luil::ui_tree inspector_tree(demo::inspector_page& page)
    {
        return arranged(page.build(1000.0f, 700.0f, 1.0f), 1000.0f, 700.0f);
    }

    [[nodiscard]] std::u8string inspector_button(demo::inspector_page& page, const std::u8string& owner)
    {
        const luil::ui_tree tree { inspector_tree(page) };
        const luil::ui_element* const button { tree.find({ demo::kind_layout, owner }) };
        REQUIRE(button != nullptr);
        return button->accessibility().name;
    }

    [[nodiscard]] std::u8string inspector_number(const std::size_t value)
    {
        const std::string digits { std::to_string(value) };
        return std::u8string { digits.begin(), digits.end() };
    }

    // 단추의 left_click 액션을 실행해 낸 앱 메시지를 페이지에 돌려준다 (UIA Invoke가 타는 길과 같다).
    int press_inspector_button(demo::inspector_page& page, const std::u8string& owner)
    {
        const luil::ui_tree tree { inspector_tree(page) };
        const luil::ui_element* const button { tree.find({ demo::kind_layout, owner }) };
        REQUIRE(button != nullptr);
        const luil::ui_action* const action { button->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        int delivered { 0 };
        for (const luil::input_action& each : (*action)(luil::ui_action_context { .element = button->id() }))
        {
            if (const auto* const message { std::get_if<luil::app_message>(&each) }; message != nullptr)
            {
                REQUIRE(page.handle(*message));
                ++delivered;
            }
        }
        return delivered;
    }
}

TEST_CASE("The inspector page starts off and counts the elements of its own tree", "[demo][inspector]")
{
    demo::inspector_page page {};
    const luil::ui_tree tree { inspector_tree(page) };
    REQUIRE(label_text(tree, u8"inspector-state") == u8"검사기: 끔");
    REQUIRE(label_text(tree, u8"inspector-pick") == u8"집기: hovered");
    REQUIRE(label_text(tree, u8"inspector-levels") == u8"조상 단계: 0");
    REQUIRE(label_text(tree, u8"inspector-selected") == u8"선택: 없음");
    const std::size_t elements { luil::inspect_nodes(tree).size() };
    REQUIRE(elements > 20);
    REQUIRE(label_text(tree, u8"inspector-count") == u8"요소 " + inspector_number(elements) + u8"개");
    REQUIRE(page.config().enabled == false);
}

TEST_CASE("The inspector shortcut is Ctrl+Shift+I", "[demo][inspector]")
{
    REQUIRE(demo::inspector_shortcut.key == luil::key_code::key_i);
    REQUIRE(demo::inspector_shortcut.control);
    REQUIRE(demo::inspector_shortcut.shift);
    REQUIRE(demo::inspector_shortcut.alt == false);
    // 명령 팔레트의 Ctrl+Shift+P와 겹치지 않는다.
    REQUIRE(demo::inspector_shortcut != luil::key_chord { luil::key_code::key_p, true, true });
    // 단축키가 내는 메시지를 페이지가 받는다.
    demo::inspector_page page {};
    send(page, demo::inspector_command_intent { demo::inspector_command::toggle });
    REQUIRE(page.config().enabled);
}

TEST_CASE("The inspector buttons run the same actions a UIA invoke would", "[demo][inspector]")
{
    demo::inspector_page page {};
    REQUIRE(press_inspector_button(page, u8"inspector-toggle") == 1);
    REQUIRE(label_text(inspector_tree(page), u8"inspector-state") == u8"검사기: 켬");
    REQUIRE(page.config().enabled);
    REQUIRE(page.config().pick == luil::inspector_pick::hovered);

    REQUIRE(press_inspector_button(page, u8"inspector-up") == 1);
    REQUIRE(press_inspector_button(page, u8"inspector-up") == 1);
    REQUIRE(press_inspector_button(page, u8"inspector-up") == 1);
    REQUIRE(label_text(inspector_tree(page), u8"inspector-levels") == u8"조상 단계: 3");
    REQUIRE(page.config().ancestor_levels == 3);
    REQUIRE(press_inspector_button(page, u8"inspector-down") == 1);
    REQUIRE(label_text(inspector_tree(page), u8"inspector-levels") == u8"조상 단계: 2");

    REQUIRE(inspector_button(page, u8"inspector-outline") == u8"모든 윤곽: 끔");
    REQUIRE(press_inspector_button(page, u8"inspector-outline") == 1);
    REQUIRE(inspector_button(page, u8"inspector-outline") == u8"모든 윤곽: 켬");
    REQUIRE(page.config().outline_all);
    REQUIRE(inspector_button(page, u8"inspector-palette") == u8"팔레트 견본: 끔");
    REQUIRE(press_inspector_button(page, u8"inspector-palette") == 1);
    REQUIRE(inspector_button(page, u8"inspector-palette") == u8"팔레트 견본: 켬");
    REQUIRE(page.config().show_palette);

    // 끄고 다시 켜면 조상 단계도 처음 모습인 0이다.
    REQUIRE(press_inspector_button(page, u8"inspector-toggle") == 1);
    REQUIRE(press_inspector_button(page, u8"inspector-toggle") == 1);
    REQUIRE(label_text(inspector_tree(page), u8"inspector-levels") == u8"조상 단계: 0");
    REQUIRE(page.config().ancestor_levels == 0);
}

TEST_CASE("The inspector selection buttons walk the page tree in drawing order", "[demo][inspector]")
{
    demo::inspector_page page {};
    REQUIRE(press_inspector_button(page, u8"inspector-next") == 1);
    REQUIRE(label_text(inspector_tree(page), u8"inspector-selected") == u8"선택: layout:inspector-page");
    REQUIRE(label_text(inspector_tree(page), u8"inspector-pick") == u8"집기: selected");
    REQUIRE(page.config().pick == luil::inspector_pick::selected);
    REQUIRE(page.config().selected == luil::ui_element_id { demo::kind_layout, u8"inspector-page" });

    REQUIRE(press_inspector_button(page, u8"inspector-child") == 1);
    REQUIRE(label_text(inspector_tree(page), u8"inspector-selected") == u8"선택: text:inspector-help");
    REQUIRE(press_inspector_button(page, u8"inspector-next") == 1);
    REQUIRE(label_text(inspector_tree(page), u8"inspector-selected") == u8"선택: layout:inspector-status");
    REQUIRE(press_inspector_button(page, u8"inspector-child") == 1);
    REQUIRE(label_text(inspector_tree(page), u8"inspector-selected") == u8"선택: text:inspector-state");
    REQUIRE(press_inspector_button(page, u8"inspector-prev") == 1);
    REQUIRE(label_text(inspector_tree(page), u8"inspector-selected") == u8"선택: layout:inspector-status");
    REQUIRE(press_inspector_button(page, u8"inspector-parent") == 1);
    REQUIRE(label_text(inspector_tree(page), u8"inspector-selected") == u8"선택: layout:inspector-page");
    // 뿌리 위로는 갈 곳이 없어 선택이 그대로다.
    REQUIRE(press_inspector_button(page, u8"inspector-parent") == 1);
    REQUIRE(label_text(inspector_tree(page), u8"inspector-selected") == u8"선택: layout:inspector-page");

    // 끄고 다시 켜면 처음 모습이다.
    REQUIRE(press_inspector_button(page, u8"inspector-toggle") == 1);
    REQUIRE(press_inspector_button(page, u8"inspector-toggle") == 1);
    REQUIRE(label_text(inspector_tree(page), u8"inspector-selected") == u8"선택: 없음");
    REQUIRE(label_text(inspector_tree(page), u8"inspector-pick") == u8"집기: hovered");
}

TEST_CASE("The inspector names the demo kinds and reads the sample's inner padding", "[demo][inspector]")
{
    demo::inspector_page page {};
    const luil::inspector_config config { page.config() };
    REQUIRE(luil::kind_display_name(demo::kind_layout, config) == u8"layout");
    REQUIRE(luil::kind_display_name(demo::kind_text, config) == u8"text");
    REQUIRE(luil::kind_display_name(luil::application_element_kind(900), config) == u8"kind#1924");

    const luil::ui_tree tree { inspector_tree(page) };
    const luil::ui_element* const stack { tree.find({ demo::kind_layout, u8"inspector-sample-stack" }) };
    REQUIRE(stack != nullptr);
    const std::u8string text { luil::describe_element(tree, *stack, 1.0f, config) };
    REQUIRE(text.starts_with(u8"layout:inspector-sample-stack  "));
    // 견본 stack의 안쪽 여백 위 8, 오른쪽 12, 아래 8, 왼쪽 12다 (설계 문서의 정보 상자 예시와 같은 수다).
    REQUIRE(text.find(u8"  inset 8 12 8 12\n") != std::u8string::npos);
    REQUIRE(text.find(u8"children 3  ") != std::u8string::npos);

    // 조상 한 단계를 올리면 견본 stack에서 그것을 감싼 panel이 집힌다.
    luil::inspector_config picking { config };
    picking.enabled = true;
    picking.pick = luil::inspector_pick::selected;
    picking.selected = stack->id();
    picking.ancestor_levels = 1;
    const luil::interaction_snapshot interaction {};
    const luil::ui_element* const picked { luil::pick_inspected(tree, interaction, picking) };
    REQUIRE(picked != nullptr);
    REQUIRE(picked->id() == luil::ui_element_id { demo::kind_layout, u8"inspector-sample" });
}

namespace {
    // 데모 앱이 on_key에서 하는 일과 같은 정책이다. 읽기 전용 안내 칸에 초점이 있는 채 닿은 되돌리기 키를 메시지로 바꾼다.
    class undo_key_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            return demo::text_target_of_kind(kind);
        }

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event& event, const luil::interaction_snapshot& snapshot) override
        {
            if (const auto undo { demo::readonly_undo_key_of(event, snapshot, demo::kind_link_view_input) }; undo.has_value())
                return { luil::make_app_action(*undo) };
            return {};
        }
    };

    [[nodiscard]] luil::key_pressed_event undo_chord(const luil::key_code code, const bool control, const bool shift = false)
    {
        luil::key_pressed_event event { code };
        event.control = control;
        event.shift = shift;
        return event;
    }

    // 안내 칸 하나를 든 tree에서 칸을 눌러 초점을 주고 되돌리기 키와 그 밖의 키를 보낸 뒤, 앱이 센 값이다.
    [[nodiscard]] demo::readonly_undo_keys press_undo_keys(const bool read_only)
    {
        undo_key_policy policy {};
        luil::interaction_controller controller { &policy };
        luil::text_input_view view {};
        view.text = u8"읽기 전용 안내";
        luil::text_area_config config {};
        config.read_only = read_only;
        auto area { std::make_unique<luil::text_area_element>(luil::ui_element_id { demo::kind_link_view_input }, std::move(view), std::move(config)) };
        controller.set_tree(std::make_shared<const luil::ui_tree>(arranged(std::move(area), 200.0f, 100.0f)));
        static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 12.0f, luil::pointer_button::left, std::chrono::steady_clock::time_point {} }));
        static_cast<void>(controller.process(luil::pointer_released_event { 20.0f, 12.0f, luil::pointer_button::left, std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { 5 } }));
        REQUIRE(controller.snapshot().focused_input.kind == demo::kind_link_view_input);

        demo::readonly_undo_keys counts {};
        const luil::key_pressed_event keys[] {
            undo_chord(luil::key_code::key_z, true),
            undo_chord(luil::key_code::key_y, true),
            undo_chord(luil::key_code::key_z, true, true),
            // 수식 키 없는 z와 다른 Ctrl 키는 되돌리기가 아니다.
            undo_chord(luil::key_code::key_z, false),
            undo_chord(luil::key_code::key_x, true),
        };
        for (const luil::key_pressed_event& event : keys)
        {
            for (const luil::input_action& action : controller.process(event))
            {
                if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                    static_cast<void>(counts.handle(*message));
            }
        }
        return counts;
    }
}

TEST_CASE("A read-only demo field hands Ctrl+Z and Ctrl+Y to the app and the label counts them", "[demo][read-only]")
{
    // Ctrl+Z 하나, Ctrl+Y 하나, Ctrl+Shift+Z 하나(다시 실행)가 앱 on_key에 닿는다.
    const demo::readonly_undo_keys passed { press_undo_keys(true) };
    REQUIRE(passed.undo_count() == 1);
    REQUIRE(passed.redo_count() == 2);
    REQUIRE(passed.text() == u8"읽기 전용 칸에서 앱이 받은 Ctrl+Z 1회 · Ctrl+Y 2회");

    // 같은 칸이 편집 칸이면 칸이 되돌리기를 가져가 앱까지 오지 않는다.
    const demo::readonly_undo_keys kept { press_undo_keys(false) };
    REQUIRE(kept.undo_count() == 0);
    REQUIRE(kept.redo_count() == 0);
    REQUIRE(kept.text() == u8"읽기 전용 칸에서 앱이 받은 Ctrl+Z 0회 · Ctrl+Y 0회");
}

TEST_CASE("The OS notification card reads the state, the result and the event the shell hands it", "[demo][toasts][notification]")
{
    demo::toasts_page page {};
    auto tree { toasts_tree(page) };
    REQUIRE(label_text(tree, u8"os-notification-status") == u8"OS 알림 상태: 확인 전");
    REQUIRE(label_text(tree, u8"os-notification-result") == u8"OS 알림 결과: 없음");
    REQUIRE(label_text(tree, u8"os-notification-event") == u8"OS 알림 사건: 없음");
    REQUIRE(label_text(tree, u8"os-notification-hint").empty() == false);

    send(page, demo::os_notification_status_intent { luil::notification_availability::available });
    send(page, demo::os_notification_result_intent { luil::platform_result { luil::platform_status::ok, {}, std::nullopt } });
    luil::notification_event pressed { luil::notification_event_kind::activated, u8"demo-1" };
    pressed.launched = true;
    send(page, demo::os_notification_event_intent { pressed });
    tree = toasts_tree(page);
    REQUIRE(label_text(tree, u8"os-notification-status") == u8"OS 알림 상태: 사용 가능");
    REQUIRE(label_text(tree, u8"os-notification-result") == u8"OS 알림 결과: ok");
    REQUIRE(label_text(tree, u8"os-notification-event") == u8"OS 알림 사건: 눌림 demo-1 · 꺼진 앱에서");

    // 꺼진 알림과 실패, 사용자가 닫은 사건과 앞서 버린 수다.
    send(page, demo::os_notification_status_intent { luil::notification_availability::blocked });
    send(page, demo::os_notification_result_intent { luil::platform_result { luil::platform_status::failed, u8"turned off", std::nullopt } });
    luil::notification_event dismissed { luil::notification_event_kind::dismissed, u8"demo-1" };
    dismissed.lost_before = 2;
    send(page, demo::os_notification_event_intent { dismissed });
    tree = toasts_tree(page);
    REQUIRE(label_text(tree, u8"os-notification-status") == u8"OS 알림 상태: 꺼짐");
    REQUIRE(label_text(tree, u8"os-notification-result") == u8"OS 알림 결과: failed · turned off");
    REQUIRE(label_text(tree, u8"os-notification-event") == u8"OS 알림 사건: 닫음 demo-1 · 앞서 버린 사건 2개");
}

TEST_CASE("The OS notification buttons ask the shell to send, replace and close the same notification", "[demo][toasts][notification]")
{
    demo::toasts_page page {};
    const auto tree { toasts_tree(page) };
    const std::pair<std::u8string_view, demo::os_notification_action> buttons[] {
        { u8"os-notification-send", demo::os_notification_action::send },
        { u8"os-notification-replace", demo::os_notification_action::replace },
        { u8"os-notification-close", demo::os_notification_action::close },
    };
    for (const auto& [owner, action] : buttons)
    {
        const luil::ui_element_id id { demo::kind_toast_trigger, std::u8string { owner } };
        const luil::ui_element* const button { tree.find(id) };
        REQUIRE(button != nullptr);
        REQUIRE(button->accessibility().name.starts_with(u8"OS 알림 "));
        const luil::ui_action* const pressed { button->action(luil::ui_trigger::left_click) };
        REQUIRE(pressed != nullptr);
        const luil::input_action result { first_action((*pressed)(luil::ui_action_context { id, 0.0f, 0.0f })) };
        const auto* const request { std::get<luil::app_message>(result).get<demo::os_notification_request_intent>() };
        REQUIRE(request != nullptr);
        REQUIRE(request->action == action);
        // 요청은 셸의 몫이라 페이지가 받지 않는다.
        REQUIRE(page.handle(std::get<luil::app_message>(result)) == false);
    }
}

namespace {
    // 상태 글을 좁은 글로 옮긴다. 실패하면 Catch가 글을 그대로 보인다.
    [[nodiscard]] std::string narrow(const std::u8string_view text)
    {
        return { text.begin(), text.end() };
    }

    [[nodiscard]] std::string rich_state(demo::rich_text_page& page)
    {
        return narrow(label_text(arranged(page.build(800.0f, 600.0f, 1.0f)), u8"rich-state"));
    }

    // 토글을 눌러 나온 메시지를 페이지에 넘긴다. 누름은 tree에 있던 그 액션이다.
    void press_rich_toggle(demo::rich_text_page& page, const std::u8string& owner)
    {
        const luil::ui_tree tree { arranged(page.build(800.0f, 600.0f, 1.0f)) };
        const luil::ui_element_id id { luil::ui_element_kind::check, owner };
        const luil::ui_element* const toggle { tree.find(id) };
        REQUIRE(toggle != nullptr);
        REQUIRE(toggle->preserves_pointer_focus());
        const luil::ui_action* const action { toggle->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        const luil::input_action pressed { first_action((*action)(luil::ui_action_context { id, 0.0f, 0.0f })) };
        REQUIRE(page.handle(std::get<luil::app_message>(pressed)));
    }

    void send_rich_edit(demo::rich_text_page& page, const luil::text::text_edit_command command)
    {
        luil::text_edit_request request {};
        request.target = demo::target_rich_editor;
        request.command = command;
        send(page, demo::edit_intent { request });
    }
}

TEST_CASE("The rich text page draws its state and the toggles change the typing marks or the selection", "[demo][rich-text]")
{
    // 견본 글의 caret은 처음이라 다음 글자 서식은 뒤 글자("굵게")의 굵게다.
    demo::rich_text_page page {};
    REQUIRE(rich_state(page) == narrow(u8"굵게 켜짐 · 기울임 꺼짐 · 밑줄 꺼짐 · 코드 꺼짐 · 링크 꺼짐 · 구간 4 · 링크 1 · 되돌리기 0"));
    {
        const luil::ui_tree tree { arranged(page.build(800.0f, 600.0f, 1.0f)) };
        REQUIRE(tree.find({ luil::ui_element_kind::check, u8"rich-bold" })->accessibility().checked == true);
        REQUIRE(tree.find({ demo::kind_rich_editor_input, u8"rich-editor" }) != nullptr);
    }

    // 고른 곳 없이 누르면 다음 글자의 서식만 바뀐다. 걸음이 아니다.
    press_rich_toggle(page, u8"rich-bold");
    REQUIRE(rich_state(page) == narrow(u8"굵게 꺼짐 · 기울임 꺼짐 · 밑줄 꺼짐 · 코드 꺼짐 · 링크 꺼짐 · 구간 4 · 링크 1 · 되돌리기 0"));

    // 모두 골라 굵게를 누르면 한 걸음이다. Ctrl+Z가 서식을 되돌린다.
    send_rich_edit(page, luil::text::text_edit_command::select_all);
    press_rich_toggle(page, u8"rich-bold");
    REQUIRE(rich_state(page) == narrow(u8"굵게 켜짐 · 기울임 꺼짐 · 밑줄 꺼짐 · 코드 꺼짐 · 링크 꺼짐 · 구간 7 · 링크 1 · 되돌리기 1"));
    // 되돌리면 고른 범위도 그때로 돌아온다. 모두 고른 글은 다 굵지 않아 굵게가 꺼져 보인다.
    send_rich_edit(page, luil::text::text_edit_command::undo);
    REQUIRE(rich_state(page) == narrow(u8"굵게 꺼짐 · 기울임 꺼짐 · 밑줄 꺼짐 · 코드 꺼짐 · 링크 꺼짐 · 구간 4 · 링크 1 · 되돌리기 0"));

    // 링크 토글은 고정 주소를 걸고, 켜진 곳에서 누르면 걷는다.
    send_rich_edit(page, luil::text::text_edit_command::select_all);
    press_rich_toggle(page, u8"rich-link");
    REQUIRE(rich_state(page).ends_with(narrow(u8"링크 켜짐 · 구간 4 · 링크 1 · 되돌리기 1")));
    press_rich_toggle(page, u8"rich-link");
    REQUIRE(rich_state(page).ends_with(narrow(u8"링크 꺼짐 · 구간 4 · 링크 0 · 되돌리기 2")));
}

TEST_CASE("Ctrl+B on the rich text editor reaches the page as the bold command", "[demo][rich-text]")
{
    demo::rich_text_page page {};
    const luil::ui_tree tree { arranged(page.build(800.0f, 600.0f, 1.0f)) };
    const luil::ui_element* const editor { tree.find({ demo::kind_rich_editor_input, u8"rich-editor" }) };
    REQUIRE(editor != nullptr);
    const luil::key_handler* const handler { editor->key_handler_of() };
    REQUIRE(handler != nullptr);
    luil::key_pressed_event bold { luil::key_code::key_b };
    bold.control = true;
    const auto actions { (*handler)(bold) };
    REQUIRE(actions.has_value());
    REQUIRE(page.handle(std::get<luil::app_message>(first_action(*actions))));
    REQUIRE(rich_state(page).starts_with(narrow(u8"굵게 꺼짐")));
    // 칸 밖의 키는 소비하지 않는다.
    REQUIRE((*handler)(luil::key_pressed_event { luil::key_code::key_b }).has_value() == false);
}

namespace {
    [[nodiscard]] std::string rich_clipboard(demo::rich_text_page& page)
    {
        return narrow(label_text(arranged(page.build(800.0f, 600.0f, 1.0f)), u8"rich-clipboard"));
    }

    // 서식 복사·붙여넣기 단추를 눌러 나온 메시지를 페이지에 넘긴다.
    void press_rich_button(demo::rich_text_page& page, const std::u8string& owner, const std::u8string_view name)
    {
        const luil::ui_tree tree { arranged(page.build(800.0f, 600.0f, 1.0f)) };
        const luil::ui_element_id id { demo::kind_layout, owner };
        const luil::ui_element* const button { tree.find(id) };
        REQUIRE(button != nullptr);
        REQUIRE(button->accessibility().name == name);
        REQUIRE(button->preserves_pointer_focus());
        const luil::ui_action* const action { button->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        const luil::input_action pressed { first_action((*action)(luil::ui_action_context { id, 0.0f, 0.0f })) };
        REQUIRE(page.handle(std::get<luil::app_message>(pressed)));
    }

    [[nodiscard]] std::string number_text(const std::size_t value)
    {
        return std::to_string(value);
    }
}

TEST_CASE("The rich text page copies and pastes formatted text through clipboard requests", "[demo][rich-text][clipboard]")
{
    demo::rich_text_page page {};
    REQUIRE(rich_clipboard(page) == narrow(u8"복사·붙여넣기 전"));
    {
        // 서식 칸은 복사·붙여넣기 키를 키 처리기로 넘긴다.
        const luil::ui_tree tree { arranged(page.build(800.0f, 600.0f, 1.0f)) };
        const luil::ui_element* const editor { tree.find({ demo::kind_rich_editor_input, u8"rich-editor" }) };
        REQUIRE(editor != nullptr);
        REQUIRE(editor->text_input()->clipboard_keys_to_parent);
    }

    // 고른 곳이 없으면 넣을 것이 없다.
    press_rich_button(page, u8"rich-copy", u8"서식 복사");
    REQUIRE(page.take_clipboard_request().has_value() == false);
    REQUIRE(rich_clipboard(page) == narrow(u8"복사할 글 없음"));

    // 모두 골라 복사하면 글과 HTML을 한 클립에 넣는 요청이 한 번 나온다. 견본의 굵게 구간은 하나, 링크도 하나다.
    send_rich_edit(page, luil::text::text_edit_command::select_all);
    press_rich_button(page, u8"rich-copy", u8"서식 복사");
    std::optional<luil::clipboard_request> request { page.take_clipboard_request() };
    REQUIRE(request.has_value());
    REQUIRE(page.take_clipboard_request().has_value() == false);
    REQUIRE(std::holds_alternative<luil::clipboard_data_copy_request>(*request));
    // 아래에서 요청 칸을 다시 쓰므로 값으로 옮겨 둔다.
    const luil::clipboard_data_copy_request copied { std::get<luil::clipboard_data_copy_request>(*request) };
    const luil::clipboard_data_copy_request* const copy { &copied };
    const luil::text::text_edit_state& state { page.sample().state() };
    REQUIRE(copy->text == state.text);
    REQUIRE(copy->html == luil::rich_text_to_html(state.text, *state.format));
    const std::size_t html_bytes { copy->html.size() };
    REQUIRE(rich_clipboard(page) == narrow(u8"복사 · HTML ") + number_text(html_bytes) + narrow(u8"바이트 · 굵게 1 · 링크 1"));

    // 끝에 붙여넣는다. 요청은 표식을 단 읽기이고, 답은 표식으로 갈라 온다.
    send_rich_edit(page, luil::text::text_edit_command::move_right);
    press_rich_button(page, u8"rich-paste", u8"서식 붙여넣기");
    request = page.take_clipboard_request();
    REQUIRE(request.has_value());
    const auto* const paste { std::get_if<luil::clipboard_data_paste_request>(&*request) };
    REQUIRE(paste != nullptr);
    REQUIRE(paste->tag == demo::rich_text_clipboard_tag);
    luil::clipboard_data answer {};
    answer.tag = paste->tag;
    answer.text = copy->text;
    answer.html = copy->html;
    send(page, demo::rich_text_clipboard_intent { answer });
    REQUIRE(rich_clipboard(page) == narrow(u8"붙여넣기 · HTML ") + number_text(html_bytes) + narrow(u8"바이트 · 굵게 1 · 링크 1"));
    REQUIRE(rich_state(page).ends_with(narrow(u8"구간 8 · 링크 2 · 되돌리기 1")));
    // 붙인 조각은 한 걸음으로 되돌아간다.
    send_rich_edit(page, luil::text::text_edit_command::undo);
    REQUIRE(rich_state(page).ends_with(narrow(u8"구간 4 · 링크 1 · 되돌리기 0")));

    // HTML이 없는 답은 글로 물러선다.
    luil::clipboard_data plain {};
    plain.tag = demo::rich_text_clipboard_tag;
    plain.text = u8"글";
    send(page, demo::rich_text_clipboard_intent { plain });
    REQUIRE(rich_clipboard(page) == narrow(u8"붙여넣기 · 글만"));
    send(page, demo::rich_text_clipboard_intent { luil::clipboard_data {} });
    REQUIRE(rich_clipboard(page) == narrow(u8"붙여넣을 글 없음"));
}

TEST_CASE("Ctrl+C and Ctrl+Shift+V on the rich text editor reach the page as clipboard commands", "[demo][rich-text][clipboard]")
{
    demo::rich_text_page page {};
    send_rich_edit(page, luil::text::text_edit_command::select_all);
    const luil::ui_tree tree { arranged(page.build(800.0f, 600.0f, 1.0f)) };
    const luil::key_handler* const handler { tree.find({ demo::kind_rich_editor_input, u8"rich-editor" })->key_handler_of() };
    REQUIRE(handler != nullptr);
    luil::key_pressed_event copy { luil::key_code::key_c };
    copy.control = true;
    REQUIRE(page.handle(std::get<luil::app_message>(first_action(*(*handler)(copy)))));
    REQUIRE(page.take_clipboard_request().has_value());
    luil::key_pressed_event plain { luil::key_code::key_v };
    plain.control = true;
    plain.shift = true;
    REQUIRE(page.handle(std::get<luil::app_message>(first_action(*(*handler)(plain)))));
    REQUIRE(std::holds_alternative<luil::clipboard_data_paste_request>(*page.take_clipboard_request()));
    // 서식 없이 붙이면 HTML이 있어도 글을 넣는다.
    luil::clipboard_data answer {};
    answer.tag = demo::rich_text_clipboard_tag;
    answer.text = u8"평문";
    answer.html = u8"<b>굵게</b>";
    send(page, demo::rich_text_clipboard_intent { answer });
    REQUIRE(rich_clipboard(page) == narrow(u8"붙여넣기 · 글만"));
    REQUIRE(page.sample().state().text.starts_with(u8"평문"));
}

namespace {
    [[nodiscard]] std::optional<luil::menu_bar_state> shell_bar_state(const std::vector<luil::input_action>& actions)
    {
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const intent { message->get<demo::shell_menu_state_intent>() }; intent != nullptr)
                    return intent->state;
        return std::nullopt;
    }

    // 막대를 (0, 32)에 세운 tree다. 데모 셸처럼 caption 아래 줄이다.
    [[nodiscard]] luil::ui_tree shell_bar_tree(const demo::shell_menu& menu)
    {
        auto root { std::make_unique<luil::root_element>() };
        auto bar { menu.make_bar() };
        bar->arrange({ .slot = { 0.0f, 32.0f, 500.0f, luil::menu_bar_height }, .scale = 1.0f });
        root->add(std::move(bar));
        auto state { menu.make_state_label() };
        state->arrange({ { 520.0f, 32.0f, 150.0f, luil::menu_bar_height }, 1.0f });
        root->add(std::move(state));
        auto last { menu.make_last_label() };
        last->arrange({ { 680.0f, 32.0f, 220.0f, luil::menu_bar_height }, 1.0f });
        root->add(std::move(last));
        return luil::ui_tree { std::move(root) };
    }

    [[nodiscard]] std::u8string shell_label(const luil::ui_tree& tree, const std::u8string& owner)
    {
        const luil::ui_element* const label { tree.find({ demo::kind_text, owner }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    }
}

TEST_CASE("The demo shell menu bar has File, Edit and View with access keys and state labels", "[demo][menu-bar]")
{
    demo::shell_menu menu {};
    const luil::ui_tree tree { shell_bar_tree(menu) };
    const auto* const bar { luil::active_menu_bar(tree) };
    REQUIRE(bar != nullptr);
    REQUIRE(bar->accessibility().role == luil::access_role::menu_bar);
    REQUIRE(bar->accessibility().name == u8"메뉴 막대");
    struct expected_item
    {
        std::u8string key {};
        std::u8string name {};
        std::u8string access_key {};
    };
    for (const expected_item& item : { expected_item { u8"file", u8"파일", u8"Alt+F" }, expected_item { u8"edit", u8"편집", u8"Alt+E" }, expected_item { u8"view", u8"보기", u8"Alt+V" } })
    {
        const luil::ui_element* const found { tree.find({ luil::ui_element_kind::menu_bar_item, item.key }) };
        REQUIRE(found != nullptr);
        REQUIRE(found->accessibility().name == item.name);
        REQUIRE(found->accessibility().access_key == item.access_key);
        REQUIRE(found->accessibility().expanded == false);
    }
    REQUIRE(shell_label(tree, u8"shell-menu-state") == u8"메뉴 막대: 쉼");
    REQUIRE(shell_label(tree, u8"shell-menu-last") == u8"마지막 메뉴 명령: 없음");
}

TEST_CASE("The demo shell menu labels follow the bar state from the keyboard and the pointer", "[demo][menu-bar]")
{
    demo::shell_menu menu {};
    luil::interaction_controller controller {};
    controller.set_tree(std::make_shared<const luil::ui_tree>(shell_bar_tree(menu)));
    const std::optional<luil::menu_bar_state> raised { shell_bar_state(controller.process(luil::key_pressed_event { luil::key_code::alt_tap })) };
    REQUIRE(raised == luil::menu_bar_state { u8"file", false, true });
    REQUIRE(menu.handle(luil::app_message { demo::shell_menu_state_intent { *raised } }));
    REQUIRE(menu.state_text() == u8"파일 · 키보드");

    REQUIRE(menu.handle(luil::app_message { demo::shell_menu_state_intent { { u8"edit", true, false } } }));
    REQUIRE(menu.state_text() == u8"편집 열림 · 포인터");
    const luil::ui_tree open { shell_bar_tree(menu) };
    REQUIRE(shell_label(open, u8"shell-menu-state") == u8"편집 열림 · 포인터");
    REQUIRE(open.find({ luil::ui_element_kind::menu_bar_item, u8"edit" })->accessibility().expanded == true);

    REQUIRE(menu.handle(luil::app_message { demo::shell_menu_state_intent {} }));
    REQUIRE(menu.state_text() == u8"메뉴 막대: 쉼");
}

TEST_CASE("A demo shell menu opens under its item, records the command and hands shell commands over", "[demo][menu-bar]")
{
    demo::shell_menu menu {};
    REQUIRE(menu.handle(luil::app_message { demo::shell_menu_state_intent { { u8"edit", true, true } } }));
    {
        const luil::ui_tree tree { shell_bar_tree(menu) };
        const std::vector<luil::ui_popup> popups { menu.make_popups(*luil::active_menu_bar(tree), 1.0f, { 0.0f, 0.0f, 1000.0f, 700.0f }, true, luil::theme_preference::system) };
        REQUIRE(popups.size() == 1u);
        const luil::rect_f edit { *luil::active_menu_bar(tree)->item_bounds(u8"edit") };
        REQUIRE(popups[0].x == edit.x);
        REQUIRE(popups[0].y == edit.y + edit.height);
        // 편집 메뉴는 접근 키가 붙은 다섯 명령이다.
        const auto* const undo { popups[0].tree->find({ luil::ui_element_kind::menu_item, u8"edit.undo" }) };
        REQUIRE(undo != nullptr);
        REQUIRE(undo->accessibility().access_key == u8"U");
        REQUIRE(popups[0].tree->ids_of_kind(luil::ui_element_kind::menu_item).size() == 5u);
        // Esc는 메뉴만 닫고 막대는 선 채 남는다.
        REQUIRE(shell_bar_state({ popups[0].dismiss(luil::popup_dismiss_reason::escape_key) }) == luil::menu_bar_state { u8"edit", false, true });
        // 고르면 명령이 실리고 막대가 쉰다.
        const std::vector<luil::input_action> chosen { (*undo->action(luil::ui_trigger::left_click))({ undo->id() }) };
        for (const luil::input_action& action : chosen)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                REQUIRE(menu.handle(*message));
    }
    REQUIRE(menu.last_command_text() == u8"마지막 메뉴 명령: 실행 취소");
    REQUIRE(menu.state_text() == u8"메뉴 막대: 쉼");
    REQUIRE(menu.take_command().has_value() == false);

    // 보기 메뉴의 사이드바는 셸 명령이고, 테마 하위 메뉴는 부모 popup 기준 자리에 선다.
    REQUIRE(menu.handle(luil::app_message { demo::shell_menu_state_intent { { u8"view", true, true } } }));
    REQUIRE(menu.handle(luil::app_message { demo::shell_menu_submenu_intent { u8"view.theme" } }));
    const luil::ui_tree tree { shell_bar_tree(menu) };
    const std::vector<luil::ui_popup> popups { menu.make_popups(*luil::active_menu_bar(tree), 1.0f, { 0.0f, 0.0f, 1000.0f, 700.0f }, false, luil::theme_preference::dark) };
    REQUIRE(popups.size() == 2u);
    const auto* const sidebar { popups[0].tree->find({ luil::ui_element_kind::menu_item, u8"view.sidebar" }) };
    REQUIRE(sidebar->accessibility().checked == false);
    REQUIRE(popups[1].anchor == popups[0].id);
    REQUIRE(popups[1].x == popups[0].width);
    const auto* const dark { popups[1].tree->find({ luil::ui_element_kind::menu_item, u8"theme.dark" }) };
    REQUIRE(dark->accessibility().selected == true);
    REQUIRE(menu.handle(luil::app_message { demo::shell_menu_select_intent { u8"view.sidebar" } }));
    REQUIRE(menu.take_command() == std::u8string { u8"view.sidebar" });
    REQUIRE(menu.take_command().has_value() == false);
    REQUIRE(menu.last_command_text() == u8"마지막 메뉴 명령: 사이드바");
    // 페이지 전환 하위 메뉴도 부모 popup 기준 자리에 서고, 고른 전환이 라디오로 선다. 고르면 셸 명령이다.
    REQUIRE(menu.handle(luil::app_message { demo::shell_menu_submenu_intent { u8"view.page-transition" } }));
    {
        const luil::ui_tree again { shell_bar_tree(menu) };
        const std::vector<luil::ui_popup> transitions {
            menu.make_popups(*luil::active_menu_bar(again), 1.0f, { 0.0f, 0.0f, 1000.0f, 700.0f }, true, luil::theme_preference::dark, demo::shell_menu_page_transition_fade)
        };
        REQUIRE(transitions.size() == 2u);
        REQUIRE(transitions[1].id == u8"shell-menu-page-transition");
        REQUIRE(transitions[1].anchor == transitions[0].id);
        REQUIRE(transitions[1].y > popups[1].y);
        REQUIRE(transitions[1].tree->ids_of_kind(luil::ui_element_kind::menu_item).size() == 5u);
        for (const std::u8string_view key : { demo::shell_menu_page_transition_none, demo::shell_menu_page_transition_slide, demo::shell_menu_page_transition_zoom, demo::shell_menu_page_transition_spring })
            REQUIRE(transitions[1].tree->find({ luil::ui_element_kind::menu_item, std::u8string { key } })->accessibility().selected == false);
        REQUIRE(transitions[1].tree->find({ luil::ui_element_kind::menu_item, std::u8string { demo::shell_menu_page_transition_fade } })->accessibility().selected == true);
    }
    REQUIRE(menu.handle(luil::app_message { demo::shell_menu_select_intent { std::u8string { demo::shell_menu_page_transition_zoom } } }));
    REQUIRE(menu.take_command() == std::u8string { demo::shell_menu_page_transition_zoom });
    REQUIRE(menu.last_command_text() == u8"마지막 메뉴 명령: 확대");
    // 다른 메뉴로 옮기면 열린 하위 메뉴가 닫힌다.
    REQUIRE(menu.handle(luil::app_message { demo::shell_menu_state_intent { { u8"file", true, true } } }));
    const luil::ui_tree moved { shell_bar_tree(menu) };
    REQUIRE(menu.make_popups(*luil::active_menu_bar(moved), 1.0f, { 0.0f, 0.0f, 1000.0f, 700.0f }, false, luil::theme_preference::dark).size() == 1u);
}

namespace {
    // 서식 칸에 초점을 주는 정책이다. 칸이 넘긴 키는 칸에 단 처리기가 받는다.
    class rich_key_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            return demo::text_target_of_kind(kind);
        }
    };

    // 컨트롤러가 낸 앱 메시지를 페이지에 넘긴다. 평문 복사·붙여넣기 요청이 섞였으면 참이다.
    [[nodiscard]] bool deliver_rich_keys(demo::rich_text_page& page, const std::vector<luil::input_action>& actions)
    {
        bool plain { false };
        for (const luil::input_action& action : actions)
        {
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                static_cast<void>(page.handle(*message));
            else if (std::holds_alternative<luil::clipboard_copy_request>(action) || std::holds_alternative<luil::clipboard_paste_request>(action))
                plain = true;
        }
        return plain;
    }

    [[nodiscard]] luil::key_pressed_event rich_chord(const luil::key_code code, const bool control, const bool shift)
    {
        luil::key_pressed_event event { code };
        event.control = control;
        event.shift = shift;
        return event;
    }
}

TEST_CASE("Clipboard keys in the focused rich text editor copy and paste with the formatting", "[demo][rich-text][clipboard]")
{
    demo::rich_text_page page {};
    send_rich_edit(page, luil::text::text_edit_command::select_all);
    auto tree { std::make_shared<const luil::ui_tree>(arranged(page.build(800.0f, 600.0f, 1.0f))) };
    const luil::ui_element* const editor { tree->find({ demo::kind_rich_editor_input, u8"rich-editor" }) };
    REQUIRE(editor != nullptr);
    const luil::rect_f box { editor->bounds() };
    rich_key_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(tree);
    const float x { box.x + box.width * 0.5f };
    const float y { box.y + box.height * 0.5f };
    static_cast<void>(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, std::chrono::steady_clock::time_point {} }));
    static_cast<void>(controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { 5 } }));
    REQUIRE(controller.snapshot().focused_input.kind == demo::kind_rich_editor_input);

    // 칸이 키를 쓰지 않고 처리기로 넘겨 복사는 글과 HTML을 한 클립에 넣는 요청이다. 누름이 낸 caret 이동은 페이지에 주지 않아 모두 고른 채다.
    REQUIRE(deliver_rich_keys(page, controller.process(rich_chord(luil::key_code::key_c, true, false))) == false);
    std::optional<luil::clipboard_request> request { page.take_clipboard_request() };
    REQUIRE(request.has_value());
    const auto* const copy { std::get_if<luil::clipboard_data_copy_request>(&*request) };
    REQUIRE(copy != nullptr);
    REQUIRE(copy->html.empty() == false);

    // Ctrl+Insert도 같은 서식 복사이고, Ctrl+V와 Shift+Insert는 표식을 단 읽기다.
    REQUIRE(deliver_rich_keys(page, controller.process(rich_chord(luil::key_code::insert, true, false))) == false);
    request = page.take_clipboard_request();
    REQUIRE(request.has_value());
    REQUIRE(std::holds_alternative<luil::clipboard_data_copy_request>(*request));
    for (const luil::key_pressed_event& paste : { rich_chord(luil::key_code::key_v, true, false), rich_chord(luil::key_code::insert, false, true) })
    {
        REQUIRE(deliver_rich_keys(page, controller.process(paste)) == false);
        request = page.take_clipboard_request();
        REQUIRE(request.has_value());
        const auto* const read { std::get_if<luil::clipboard_data_paste_request>(&*request) };
        REQUIRE(read != nullptr);
        REQUIRE(read->tag == demo::rich_text_clipboard_tag);
    }
}
