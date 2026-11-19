#include "host/overlay_input.h"

#include "luil/app/menu_popup.h"
#include "luil/ui/panel_element.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    struct menu_selection
    {
        std::u8string key {};
    };

    [[nodiscard]] luil::overlay_input_router menu_router()
    {
        luil::overlay_input_router router {};
        router.set_areas({ { u8"menu", { 100.0f, 200.0f, 300.0f, 400.0f } } });
        return router;
    }

    [[nodiscard]] luil::pointer_pressed_event touch_press(const float x, const float y, const std::uint32_t id = 1)
    {
        luil::pointer_pressed_event event {};
        event.x = x;
        event.y = y;
        event.device = luil::pointer_device::touch;
        event.pointer_id = id;
        return event;
    }

    [[nodiscard]] luil::pointer_moved_event touch_move(const float x, const float y)
    {
        return { .x = x, .y = y, .time = std::chrono::steady_clock::time_point {} + 20ms
            , .device = luil::pointer_device::touch, .pointer_id = 1, .in_contact = true
        };
    }

    [[nodiscard]] luil::pointer_released_event touch_release(const float x, const float y)
    {
        return { .x = x, .y = y, .time = std::chrono::steady_clock::time_point {} + 40ms
            , .device = luil::pointer_device::touch, .pointer_id = 1
        };
    }

    template<typename event_type>
    [[nodiscard]] const event_type& as(const luil::raw_input_event& event)
    {
        REQUIRE(std::holds_alternative<event_type>(event));
        return std::get<event_type>(event);
    }
}

TEST_CASE("A press inside a popup lands in its surface and coordinates", "[overlay]")
{
    luil::overlay_input_router router { menu_router() };
    const luil::routed_input routed { router.route(touch_press(150.0f, 250.0f)) };
    REQUIRE(routed.pressed_outside == false);
    REQUIRE(routed.events.size() == 1u);
    const auto& pressed { as<luil::pointer_pressed_event>(routed.events[0]) };
    REQUIRE(pressed.surface == u8"menu");
    REQUIRE(pressed.x == 50.0f);
    REQUIRE(pressed.y == 50.0f);
}

TEST_CASE("A press outside every popup goes to the main surface and asks to dismiss", "[overlay]")
{
    luil::overlay_input_router router { menu_router() };
    const luil::routed_input routed { router.route(touch_press(20.0f, 20.0f)) };
    REQUIRE(routed.pressed_outside);
    const auto& pressed { as<luil::pointer_pressed_event>(routed.events[0]) };
    REQUIRE(pressed.surface.empty());
    REQUIRE(pressed.x == 20.0f);

    // popup이 없으면 닫을 것도 없다.
    luil::overlay_input_router empty {};
    REQUIRE(empty.route(touch_press(20.0f, 20.0f)).pressed_outside == false);
}

TEST_CASE("A contact stays with the layer it pressed", "[overlay]")
{
    luil::overlay_input_router router { menu_router() };
    static_cast<void>(router.route(touch_press(150.0f, 250.0f)));

    // 밖으로 끌어도 popup의 접촉이다.
    luil::pointer_moved_event moved {};
    moved.x = 20.0f;
    moved.y = 20.0f;
    moved.device = luil::pointer_device::touch;
    moved.pointer_id = 1;
    moved.in_contact = true;
    auto routed { router.route(moved) };
    REQUIRE(as<luil::pointer_moved_event>(routed.events[0]).surface == u8"menu");
    REQUIRE(as<luil::pointer_moved_event>(routed.events[0]).x == -80.0f);

    luil::pointer_released_event released {};
    released.x = 20.0f;
    released.y = 20.0f;
    released.device = luil::pointer_device::touch;
    released.pointer_id = 1;
    routed = router.route(released);
    REQUIRE(as<luil::pointer_released_event>(routed.events[0]).surface == u8"menu");

    // 뗀 뒤의 새 누름은 자리로 다시 고른다.
    REQUIRE(as<luil::pointer_pressed_event>(router.route(touch_press(20.0f, 20.0f, 2)).events[0]).surface.empty());

    // 취소도 누른 layer로 간다.
    static_cast<void>(router.route(touch_press(150.0f, 250.0f, 3)));
    routed = router.route(luil::pointer_cancelled_event { luil::pointer_device::touch, 3, {}, {} });
    REQUIRE(as<luil::pointer_cancelled_event>(routed.events[0]).surface == u8"menu");
}

TEST_CASE("Hover moving between layers leaves the one it came from", "[overlay]")
{
    luil::overlay_input_router router { menu_router() };
    luil::pointer_moved_event moved {};
    moved.device = luil::pointer_device::mouse;
    moved.x = 20.0f;
    moved.y = 20.0f;
    REQUIRE(router.route(moved).events.size() == 1u);

    // 주 표면에서 popup으로 들어간다. 주 표면의 hover가 거둬진다.
    moved.x = 150.0f;
    moved.y = 250.0f;
    auto routed { router.route(moved) };
    REQUIRE(routed.events.size() == 2u);
    REQUIRE(as<luil::pointer_left_event>(routed.events[0]).surface.empty());
    REQUIRE(as<luil::pointer_moved_event>(routed.events[1]).surface == u8"menu");

    // 창을 떠나면 지금 호버하던 popup의 이탈이다.
    routed = router.route(luil::pointer_left_event { {}, luil::pointer_device::mouse });
    REQUIRE(as<luil::pointer_left_event>(routed.events[0]).surface == u8"menu");
}

TEST_CASE("A wheel outside the popups asks to dismiss them", "[overlay]")
{
    luil::overlay_input_router router { menu_router() };
    luil::mouse_wheel_event wheel {};
    wheel.x = 20.0f;
    wheel.y = 20.0f;
    REQUIRE(router.route(wheel).wheel_outside);
    wheel.x = 150.0f;
    wheel.y = 250.0f;
    const auto routed { router.route(wheel) };
    REQUIRE(routed.wheel_outside == false);
    REQUIRE(as<luil::mouse_wheel_event>(routed.events[0]).surface == u8"menu");
}

TEST_CASE("The topmost popup wins where popups overlap", "[overlay]")
{
    luil::overlay_input_router router {};
    router.set_areas({ { u8"below", { 0.0f, 0.0f, 200.0f, 200.0f } }, { u8"above", { 100.0f, 100.0f, 200.0f, 200.0f } } });
    REQUIRE(as<luil::pointer_pressed_event>(router.route(touch_press(150.0f, 150.0f)).events[0]).surface == u8"above");
    REQUIRE(as<luil::pointer_pressed_event>(router.route(touch_press(50.0f, 50.0f, 2)).events[0]).surface == u8"below");
}

TEST_CASE("Swiping a clipped menu cancels the pressed row and exposes its final command", "[overlay][popup][touch]")
{
    luil::menu_config menu {};
    menu.items = { { .key = u8"top", .label = u8"Top" }, { .key = u8"middle", .label = u8"Middle" }, { .key = u8"last", .label = u8"Last" } };
    menu.select = [](const std::u8string& key) { return luil::make_app_action(menu_selection { key }); };
    const luil::ui_popup popup { luil::make_menu_popup(std::move(menu), { .id = u8"menu", .width = 160.0f }) };
    const float viewport { popup.height / 3.0f };
    luil::overlay_area area { popup.id, { 10.0f, 20.0f, popup.width, viewport }, 0.0f, popup.height - viewport, popup.tree.get() };
    luil::overlay_input_router router {};
    router.set_areas({ area });
    luil::interaction_controller controller {};
    controller.set_surface_trees({ { popup.id, popup.tree } });

    const auto pressed { router.route(touch_press(20.0f, 20.0f + viewport / 2.0f)) };
    REQUIRE(pressed.events.size() == 1u);
    REQUIRE(controller.process(pressed.events.front()).empty());
    REQUIRE(controller.snapshot().pressed != luil::ui_element_id {});

    const auto moved { router.route(touch_move(20.0f, 20.0f - popup.height)) };
    REQUIRE(moved.events.size() == 1u);
    REQUIRE(as<luil::pointer_cancelled_event>(moved.events.front()).surface == popup.id);
    REQUIRE(controller.process(moved.events.front()).empty());
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});
    REQUIRE(moved.scroll.has_value());
    REQUIRE(moved.scroll->offset == area.max_scroll_y);
    area.scroll_y = moved.scroll->offset;
    router.set_areas({ area });

    const auto release { router.route(touch_release(20.0f, 20.0f - popup.height)) };
    REQUIRE(release.events.empty());
    REQUIRE_FALSE(release.scroll.has_value());

    const float last_row_y { 20.0f + viewport / 2.0f };
    const auto outside { router.route(luil::mouse_wheel_event { .x = 20.0f, .y = 21.0f + viewport, .delta = 120.0f }) };
    REQUIRE(outside.wheel_outside);
    const auto tap { router.route(touch_press(20.0f, last_row_y)) };
    REQUIRE(as<luil::pointer_pressed_event>(tap.events.front()).y == viewport / 2.0f + area.scroll_y);
    REQUIRE(controller.process(tap.events.front()).empty());
    const auto lifted { router.route(touch_release(20.0f, last_row_y)) };
    const auto actions { controller.process(lifted.events.front()) };
    REQUIRE(actions.size() == 1u);
    const auto* const selected { std::get<luil::app_message>(actions.front()).get<menu_selection>() };
    REQUIRE(selected != nullptr);
    REQUIRE(selected->key == u8"last");
}

TEST_CASE("The popup scrolls after an inner scroll area reaches the direction's boundary", "[overlay][popup][scroll]")
{
    const bool at_end { GENERATE(false, true) };
    auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { luil::ui_element_kind::root }, luil::panel_config {}) };
    luil::scroll_source source {};
    source.at_end = at_end;
    source.scroll = [](float) { return luil::input_action {}; };
    panel->set_scroll_source(std::move(source));
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(panel), { 0.0f, 0.0f, 150.0f, 300.0f }, 1.0f) };
    luil::overlay_input_router router {};
    router.set_areas({ { u8"custom", { 10.0f, 20.0f, 150.0f, 100.0f }, 60.0f, 200.0f, &tree } });

    const auto wheeled { router.route(luil::mouse_wheel_event { .x = 30.0f, .y = 50.0f, .delta = -120.0f }) };
    REQUIRE(wheeled.scroll.has_value() == at_end);
    if (at_end)
        REQUIRE(wheeled.scroll->offset > 60.0f);
    else
        REQUIRE(as<luil::mouse_wheel_event>(wheeled.events.front()).y == 90.0f);

    static_cast<void>(router.route(touch_press(30.0f, 50.0f)));
    const auto moved { router.route(touch_move(30.0f, 25.0f)) };
    REQUIRE(moved.scroll.has_value() == at_end);
    if (at_end)
        REQUIRE(moved.scroll->offset == 85.0f);
    else
        REQUIRE(as<luil::pointer_moved_event>(moved.events.front()).y == 65.0f);
}

TEST_CASE("Overlay focus reveal uses the viewport and clamps at the end of the content", "[overlay][popup][focus]")
{
    REQUIRE(luil::overlay_scroll_to_show(50.0f, 200.0f, 100.0f, { 0.0f, 70.0f, 40.0f, 20.0f }) == 50.0f);
    REQUIRE(luil::overlay_scroll_to_show(50.0f, 200.0f, 100.0f, { 0.0f, 20.0f, 40.0f, 30.0f }) == 20.0f);
    REQUIRE(luil::overlay_scroll_to_show(50.0f, 200.0f, 100.0f, { 0.0f, 150.0f, 40.0f, 30.0f }) == 80.0f);
    REQUIRE(luil::overlay_scroll_to_show(50.0f, 200.0f, 100.0f, { 0.0f, 290.0f, 40.0f, 20.0f }) == 200.0f);
}
