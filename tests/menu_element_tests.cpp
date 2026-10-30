#include "luil/ui/menu_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

namespace {
    struct select_intent
    {
        std::u8string key {};
    };

    [[nodiscard]] luil::menu_config make_config()
    {
        luil::menu_config config {};
        config.items.push_back({ u8"open", u8"열기" });
        config.items.push_back({ u8"rename", u8"이름 바꾸기" });
        luil::menu_item_config remove {};
        remove.key = u8"delete";
        remove.label = u8"지우기";
        remove.enabled = false;
        remove.separator_above = true;
        config.items.push_back(std::move(remove));
        config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { select_intent { key } } }; };
        return config;
    }
}

TEST_CASE("A menu stacks its items and counts separators into its height", "[ui][menu]")
{
    const luil::menu_config config { make_config() };
    REQUIRE(luil::menu_element::height_for(config) == 2.0f * luil::menu_padding + 3.0f * luil::menu_item_height + luil::menu_separator_height);

    luil::menu_element menu { make_config() };
    menu.arrange({ { 0.0f, 0.0f, 160.0f, luil::menu_element::height_for(config) }, 1.0f });
    REQUIRE(menu.children().size() == 3);
    // 첫 항목은 여백 아래에서 시작하고, 구분선 있는 항목은 그만큼 더 내려간다.
    REQUIRE(menu.children()[0]->bounds().y == luil::menu_padding);
    REQUIRE(menu.children()[1]->bounds().y == luil::menu_padding + luil::menu_item_height);
    REQUIRE(menu.children()[2]->bounds().y == luil::menu_padding + 2.0f * luil::menu_item_height + luil::menu_separator_height);
}

TEST_CASE("A menu item carries its key and disabled items cannot act", "[ui][menu]")
{
    const luil::menu_element menu { make_config() };
    const luil::ui_element* const rename { menu.children()[1].get() };
    REQUIRE(rename->id() == luil::ui_element_id { luil::ui_element_kind::menu_item, u8"rename" });

    auto actions { (*rename->action(luil::ui_trigger::left_click))({ rename->id(), 0.0f, 0.0f }) };
    REQUIRE(actions.size() == 1);
    auto* const message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<select_intent>()->key == u8"rename");

    // 비활성 항목은 hit는 되지만 실행이 막히도록 enabled가 거짓이다.
    REQUIRE(menu.children()[2]->enabled() == false);
}

TEST_CASE("A menu absorbs hits on its padding and separators", "[ui][menu][hit]")
{
    // 배치: padding 4 + open(26) + rename(26) + 구분선(9) + delete(26).
    luil::menu_element menu { make_config() };
    menu.arrange({ { 0.0f, 0.0f, 160.0f, luil::menu_element::height_for(make_config()) }, 1.0f });

    // 항목 위에서는 그 항목이 임자다 (open: y 4..30).
    const luil::ui_element* const item { menu.hit_test(80.0f, 10.0f) };
    REQUIRE(item != nullptr);
    REQUIRE(item->id().kind == luil::ui_element_kind::menu_item);
    REQUIRE(item->id().owner == u8"open");

    // 구분선(rename 아래 y 56..65)과 위 padding은 메뉴가 흡수한다.
    // 위에 뜬 표면의 빈 자리 클릭이 아래 element로 새면 안 된다.
    const luil::ui_element* const separator { menu.hit_test(80.0f, 60.0f) };
    REQUIRE(separator != nullptr);
    REQUIRE(separator->id().kind == luil::ui_element_kind::menu);
    const luil::ui_element* const padding { menu.hit_test(80.0f, 2.0f) };
    REQUIRE(padding != nullptr);
    REQUIRE(padding->id().kind == luil::ui_element_kind::menu);
}

TEST_CASE("A menu without a select factory builds inert items", "[ui][menu]")
{
    luil::menu_config config { make_config() };
    config.select = nullptr;
    const luil::menu_element menu { std::move(config) };
    REQUIRE(menu.children()[0]->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(menu.children()[0]->interactive() == false);
}

TEST_CASE("Menu check and radio items expose actions with their own selection contracts", "[ui][menu][accessibility]")
{
    auto config { make_config() };
    config.items[0].kind = luil::menu_item_kind::check;
    config.items[0].checked = true;
    config.items[1].kind = luil::menu_item_kind::radio;
    config.items[1].checked = false;
    luil::menu_element menu { config };
    REQUIRE(menu.children()[0]->accessibility().checked == true);
    REQUIRE(menu.children()[0]->accessibility().selected.has_value() == false);
    REQUIRE(menu.children()[1]->accessibility().selected == false);
    REQUIRE(menu.accessibility().selection_container);
    REQUIRE(luil::plan_access_request(*menu.children()[0], { luil::access_command::toggle }).has_value());
    REQUIRE(luil::plan_access_request(*menu.children()[1], { luil::access_command::select }).has_value());
    const luil::menu_element plain { make_config() };
    REQUIRE(plain.accessibility().selection_container == false);
}

TEST_CASE("A submenu parent item reports whether its submenu is open and expands by its key", "[ui][menu][accessibility]")
{
    std::u8string opened_by {};
    auto config { make_config() };
    config.items[0].children.push_back({ u8"nested", u8"Nested" });
    config.open_submenu = [&opened_by](const std::u8string& key) {
        opened_by = key;
        return luil::input_action { luil::app_message { select_intent { key } } };
    };
    const luil::menu_element closed { config };
    REQUIRE(closed.children()[0]->accessibility().expanded == false);
    // 잎 항목은 펼침 상태가 없다.
    REQUIRE(closed.children()[1]->accessibility().expanded.has_value() == false);
    const auto expand { luil::plan_access_request(*closed.children()[0], { luil::access_command::expand }) };
    REQUIRE(expand.has_value());
    REQUIRE(expand->size() == 1u);
    REQUIRE(opened_by == u8"open");

    config.opened_key = u8"open";
    const luil::menu_element opened { config };
    REQUIRE(opened.children()[0]->accessibility().expanded == true);
    // 이미 펼쳐져 있으면 할 일이 없다.
    const auto again { luil::plan_access_request(*opened.children()[0], { luil::access_command::expand }) };
    REQUIRE(again.has_value());
    REQUIRE(again->empty());
}

TEST_CASE("Menu width reserves the longest label and shortcut on the same row", "[ui][menu]")
{
    luil::menu_config config {};
    config.items.push_back({ .key = u8"a", .label = u8"Long label" });
    config.items.push_back({ .key = u8"b", .label = u8"Open", .shortcut_text = u8"Ctrl+A" });
    const luil::text_measurer measure { [](const std::u8string_view text, float) { return static_cast<float>(text.size()) * 10.0f; } };
    REQUIRE(luil::menu_element::width_for(config, measure) == 162.0f);
}

TEST_CASE("A menu sends its after-select message only for leaf items", "[ui][menu]")
{
    // 고르면 닫히는 메뉴다. 하위 메뉴를 여는 항목은 고른 것이 아니라 닫기가 따라가지 않는다.
    luil::menu_config config { make_config() };
    luil::menu_item_config parent {};
    parent.key = u8"more";
    parent.label = u8"더 보기";
    parent.children.push_back({ u8"child", u8"자식" });
    config.items.push_back(std::move(parent));
    config.open_submenu = [](const std::u8string& key) { return luil::input_action { luil::app_message { select_intent { u8"open:" + key } } }; };
    config.after_select = [](const luil::ui_action_context&) { return std::vector<luil::input_action> { luil::input_action { luil::app_message { select_intent { u8"close" } } } }; };
    const luil::menu_element menu { std::move(config) };
    const auto keys_of = [](const std::vector<luil::input_action>& actions) {
        std::vector<std::u8string> keys {};
        for (const luil::input_action& action : actions)
            keys.push_back(std::get<luil::app_message>(action).get<select_intent>()->key);
        return keys;
    };
    const luil::ui_element& open { *menu.children()[0] };
    REQUIRE(keys_of((*open.action(luil::ui_trigger::left_click))({ open.id() })) == std::vector<std::u8string> { u8"open", u8"close" });
    const luil::ui_element& more { *menu.children()[3] };
    REQUIRE(keys_of((*more.action(luil::ui_trigger::left_click))({ more.id() })) == std::vector<std::u8string> { u8"open:more" });
}
