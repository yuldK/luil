#include "luil/ui/label_element.h"
#include "luil/ui/scrollbar_element.h"
#include "luil/ui/spinner_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

TEST_CASE("Horizontal scrollbars use x for geometry dragging and key steps", "[ui][scrollbar]")
{
    float delta {};
    luil::scrollbar_config config {};
    config.content_height = 400.0f;
    config.viewport_height = 100.0f;
    config.scroll_offset = 150.0f;
    config.axis = luil::scrollbar_axis::horizontal;
    config.scroll = [&delta](const float value) { delta = value; return luil::input_action {}; };
    luil::scrollbar_element bar { { luil::application_element_kind(0) }, config };
    bar.arrange({ { 20.0f, 10.0f, 400.0f, 20.0f }, 2.0f });
    REQUIRE(bar.thumb_start() == 170.0f);
    REQUIRE(bar.thumb_length() == 100.0f);
    REQUIRE(bar.key_step()->axis == luil::focus_axis::horizontal);
    REQUIRE(bar.accessibility().orientation == luil::access_orientation::horizontal);
    static_cast<void>(bar.pointer_drag()->on_move({}, { .x = 150.0f, .y = 999.0f }));
    REQUIRE(delta == 150.0f);
    static_cast<void>(bar.key_step()->on_step(luil::value_step::decrease_page));
    REQUIRE(delta == -100.0f);
    static_cast<void>(bar.key_step()->on_step(luil::value_step::maximum));
    REQUIRE(delta == 150.0f);
}

TEST_CASE("A hidden or empty spinner does not schedule another frame", "[ui][spinner]")
{
    luil::spinner_element spinner { { luil::application_element_kind(0) }, { u8"Loading" } };
    REQUIRE(spinner.next_update({}, {}).has_value() == false);
    spinner.arrange({ { 0.0f, 0.0f, 24.0f, 24.0f } });
    REQUIRE(spinner.next_update({}, {}).has_value());
    REQUIRE(spinner.accessibility().role == luil::access_role::progress);
    REQUIRE(spinner.accessibility().name == u8"Loading");
    REQUIRE(spinner.accessibility().range.has_value() == false);
    spinner.set_visible(false);
    REQUIRE(spinner.next_update({}, {}).has_value() == false);
}

TEST_CASE("Middle clicks do not execute actions or replace an active left press", "[ui][pointer]")
{
    auto element { std::make_unique<luil::label_element>(luil::ui_element_id { luil::application_element_kind(0) }, luil::label_config {}) };
    element->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) {
        return std::vector<luil::input_action> { luil::app_message { 7 } };
    });
    element->set_bounds({ 0.0f, 0.0f, 100.0f, 100.0f });
    auto tree { std::make_shared<luil::ui_tree>(std::move(element)) };
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    REQUIRE(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::middle }).empty());
    REQUIRE(controller.process(luil::pointer_released_event { 10.0f, 10.0f, luil::pointer_button::middle }).empty());
    static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::left }));
    const auto pressed { controller.snapshot().pressed };
    static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::middle }));
    REQUIRE(controller.snapshot().pressed == pressed);
    REQUIRE(controller.process(luil::pointer_released_event { 10.0f, 10.0f, luil::pointer_button::left }).size() == 1u);
}
