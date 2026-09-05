#include "luil/ui/choice_group_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace {
    struct select_intent
    {
        std::u8string value {};
    };

    [[nodiscard]] luil::choice_group_config make_config(const luil::choice_style style)
    {
        luil::choice_group_config config {};
        config.style = style;
        config.items = { { u8"latte", u8"라떼" }, { u8"mocha", u8"모카" }, { u8"drip", u8"드립" } };
        config.selected = u8"mocha";
        config.select = [](const std::u8string& value) { return luil::input_action { luil::app_message { select_intent { value } } }; };
        return config;
    }
} // namespace

TEST_CASE("A radio choice group stacks one row per value", "[ui][choice]")
{
    luil::choice_group_config config { make_config(luil::choice_style::radio) };
    REQUIRE(luil::choice_group_element::height_for(config) == 3.0f * luil::choice_radio_row_height);

    luil::choice_group_element group { std::move(config) };
    group.arrange({ { 0.0f, 100.0f, 300.0f, 999.0f }, 2.0f });
    REQUIRE(group.children().size() == 3);
    REQUIRE(group.bounds().height == 3.0f * luil::choice_radio_row_height * 2.0f);

    // 행은 위에서 아래로 쌓이고 폭을 다 쓴다.
    const luil::rect_f first { group.children()[0]->bounds() };
    const luil::rect_f second { group.children()[1]->bounds() };
    REQUIRE(first.y == 100.0f);
    REQUIRE(first.height == luil::choice_radio_row_height * 2.0f);
    REQUIRE(first.width == 300.0f);
    REQUIRE(second.y == first.y + first.height);
    REQUIRE(group.children()[0]->id() == luil::ui_element_id { luil::ui_element_kind::choice, u8"latte" });
}

TEST_CASE("A toggle choice group splits the slot width evenly", "[ui][choice]")
{
    luil::choice_group_config config { make_config(luil::choice_style::toggle) };
    REQUIRE(luil::choice_group_element::height_for(config) == luil::choice_toggle_height);

    luil::choice_group_element group { std::move(config) };
    group.arrange({ { 0.0f, 0.0f, 308.0f, 999.0f }, 1.0f });
    // 세 버튼과 두 간격이 폭을 나눠 갖는다: (308 - 2*4) / 3 = 100.
    const luil::rect_f first { group.children()[0]->bounds() };
    const luil::rect_f second { group.children()[1]->bounds() };
    const luil::rect_f third { group.children()[2]->bounds() };
    REQUIRE(first.width == 100.0f);
    REQUIRE(first.height == luil::choice_toggle_height);
    REQUIRE(second.x == 104.0f);
    REQUIRE(third.x == 208.0f);
}

TEST_CASE("A toggle hit lands on the covering button and gaps hit nothing", "[ui][choice][hit]")
{
    // 폭 308, 버튼 100 + 간격 4: latte 0..100, mocha 104..204, drip 208..308.
    luil::choice_group_element group { make_config(luil::choice_style::toggle) };
    group.arrange({ { 0.0f, 0.0f, 308.0f, 999.0f }, 1.0f });
    const float middle { luil::choice_toggle_height / 2.0f };

    // 경계 바로 안쪽은 그 버튼의 것이다.
    const luil::ui_element* const first { group.hit_test(99.0f, middle) };
    REQUIRE(first != nullptr);
    REQUIRE(first->id().owner == u8"latte");
    const luil::ui_element* const second { group.hit_test(105.0f, middle) };
    REQUIRE(second != nullptr);
    REQUIRE(second->id().owner == u8"mocha");

    // 버튼 사이 간격은 아무의 것도 아니다.
    // 그룹은 위에 뜨는 표면이 아니라 흡수하지 않는다 — 뒤 배치가 받는 것이 맞다.
    REQUIRE(group.hit_test(102.0f, middle) == nullptr);
}

TEST_CASE("A choice click carries its value in the select message", "[ui][choice]")
{
    const luil::choice_group_element group { make_config(luil::choice_style::radio) };
    const luil::ui_element* const drip { group.children()[2].get() };
    REQUIRE(drip->cursor() == luil::ui_cursor::hand);

    const luil::ui_action* const action { drip->action(luil::ui_trigger::left_click) };
    REQUIRE(action != nullptr);
    auto actions { (*action)({ drip->id(), 0.0f, 0.0f }) };
    REQUIRE(actions.size() == 1);
    auto* const message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<select_intent>()->value == u8"drip");

    // 토글 묶음도 같은 메시지를 낸다.
    const luil::choice_group_element toggles { make_config(luil::choice_style::toggle) };
    const luil::ui_element* const latte { toggles.children()[0].get() };
    auto toggled { (*latte->action(luil::ui_trigger::left_click))({ latte->id(), 0.0f, 0.0f }) };
    auto* const toggle_message { std::get_if<luil::app_message>(&toggled[0]) };
    REQUIRE(toggle_message != nullptr);
    REQUIRE(toggle_message->get<select_intent>()->value == u8"latte");
}

TEST_CASE("A choice group without a select factory builds inert choices", "[ui][choice]")
{
    luil::choice_group_config config { make_config(luil::choice_style::radio) };
    config.select = nullptr;
    const luil::choice_group_element group { std::move(config) };
    REQUIRE(group.children().size() == 3);
    REQUIRE(group.children()[0]->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(group.children()[0]->interactive() == false);
}

TEST_CASE("A choice group is one tab stop that enters at the selected value", "[ui][choice][focus]")
{
    // 라디오는 세로로 쌓이고 토글은 가로로 늘어선다 — 방향은 style이 이미 안다.
    luil::choice_group_element radio { make_config(luil::choice_style::radio) };
    REQUIRE(radio.focus_group() == luil::focus_axis::vertical);
    luil::choice_group_element toggle { make_config(luil::choice_style::toggle) };
    REQUIRE(toggle.focus_group() == luil::focus_axis::horizontal);

    auto group { std::make_unique<luil::choice_group_element>(make_config(luil::choice_style::radio)) };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(group), { 0.0f, 0.0f, 300.0f, 200.0f }, 1.0f) };
    // 묶음은 자리 하나로 접히고, 그 자리는 선택된 값이다.
    REQUIRE(tree.focus_order() == std::vector<luil::ui_element_id> { luil::ui_element_id { luil::ui_element_kind::choice, u8"mocha" } });
}
