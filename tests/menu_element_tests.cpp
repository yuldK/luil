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
} // namespace

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
