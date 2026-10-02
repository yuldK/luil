#include "host/overlay_input.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

namespace {
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

    template<typename event_type>
    [[nodiscard]] const event_type& as(const luil::raw_input_event& event)
    {
        REQUIRE(std::holds_alternative<event_type>(event));
        return std::get<event_type>(event);
    }
} // namespace

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
