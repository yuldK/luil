#include "luil/ui/dropdown_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <variant>
#include <vector>

namespace {
    struct toggle_intent
    {
        bool open { false };
    };
} // namespace

TEST_CASE("A dropdown toggles only when the config gives an action", "[ui][dropdown]")
{
    const luil::dropdown_element inert { luil::dropdown_config {} };
    REQUIRE(inert.action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(inert.interactive() == false);

    luil::dropdown_config config {};
    config.owner = u8"renderer";
    config.text = u8"CPU";
    config.toggle = [](const luil::ui_action_context&) -> std::vector<luil::input_action> {
        return { luil::input_action { luil::app_message { toggle_intent { true } } } };
    };
    luil::dropdown_element dropdown { std::move(config) };
    dropdown.arrange({ { 10.0f, 10.0f, 160.0f, luil::dropdown_height }, 1.0f });
    REQUIRE(dropdown.id() == luil::ui_element_id { luil::ui_element_kind::dropdown, u8"renderer" });
    REQUIRE(dropdown.cursor() == luil::ui_cursor::hand);
    REQUIRE(dropdown.bounds().height == luil::dropdown_height);

    // 칸 어디를 눌러도 여닫자는 메시지가 난다.
    REQUIRE(dropdown.hit_test(100.0f, 20.0f) == &dropdown);
    auto actions { (*dropdown.action(luil::ui_trigger::left_click))({ dropdown.id(), 100.0f, 20.0f }) };
    REQUIRE(actions.size() == 1);
    auto* const message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<toggle_intent>()->open);
}
