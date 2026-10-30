#include "luil/ui/zoom_view_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_interaction.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
    struct request
    {
        float factor { 1.0f };
        luil::zoom_point anchor {};
        luil::zoom_point delta {};
        float value { 0.0f };
    };

    const request& read(const luil::input_action& action)
    {
        return *std::get<luil::app_message>(action).get<request>();
    }

    luil::zoom_view_config config()
    {
        return {
            .owner = u8"view",
            .zoom_by = [](const float factor, const luil::zoom_point anchor) { return luil::make_app_action(request { .factor = factor, .anchor = anchor }); },
            .pan_by = [](const luil::zoom_point delta) { return luil::make_app_action(request { .delta = delta }); },
            .zoom_to = [](const float value) { return luil::make_app_action(request { .value = value }); },
        };
    }

    class content final : public luil::ui_element
    {
    public:
        explicit content(std::u8string owner = u8"content")
            : ui_element { { luil::application_element_kind(0), std::move(owner) } }
        {
        }
        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }
        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
            scale = context.scale;
            ++arranges;
            for (const auto& child : children())
                child->arrange(context.for_child({ context.slot.x, context.slot.y, 40.0f * context.scale, 40.0f * context.scale }));
        }
        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
        float scale { 1.0f };
        int arranges { 0 };
    };

    std::shared_ptr<const luil::ui_tree> tree(const luil::zoom_wheel_mode wheel = luil::zoom_wheel_mode::zoom, const bool clickable = false, const bool handle = false, const bool scroll = false)
    {
        auto settings { config() };
        settings.wheel = wheel;
        auto view { std::make_unique<luil::zoom_view_element>(settings) };
        if (clickable || handle || scroll)
        {
            auto item { std::make_unique<content>() };
            if (clickable)
                item->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> { luil::make_app_action(request { .value = 7.0f }) }; });
            if (handle)
                item->set_pointer_drag_target(luil::pointer_drag_target {
                    .on_move = [](const luil::ui_action_context&, const luil::ui_action_context&) { return std::vector<luil::input_action> { luil::make_app_action(request { .value = 9.0f }) }; },
                });
            if (scroll)
                item->set_scroll_source(luil::scroll_source { .scroll = [](const float delta) { return luil::make_app_action(request { .value = delta }); } });
            view->set_content(std::move(item));
        }
        view->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
        return std::make_shared<const luil::ui_tree>(std::move(view));
    }

    luil::pointer_pressed_event press(const std::uint32_t id, const float x, const float y = 100.0f, const luil::pointer_device device = luil::pointer_device::touch)
    {
        return { .x = x, .y = y, .device = device, .pointer_id = id };
    }

    luil::pointer_moved_event move(const std::uint32_t id, const float x, const float y = 100.0f, const luil::pointer_device device = luil::pointer_device::touch)
    {
        return { .x = x, .y = y, .device = device, .pointer_id = id, .in_contact = true };
    }

    luil::pointer_released_event release(const std::uint32_t id, const float x, const float y = 100.0f)
    {
        return { .x = x, .y = y, .device = luil::pointer_device::touch, .pointer_id = id };
    }
}

TEST_CASE("Zoom preserves its anchor using the actual bounded ratio", "[zoom]")
{
    auto settings { config() };
    settings.origin = { 10.0f, -20.0f };
    settings.maximum_zoom = 2.0f;
    const auto value { luil::zoom_about(settings, { 400.0f, 400.0f }, 3.0f, { 50.0f, 80.0f }) };
    REQUIRE(value.zoom == 2.0f);
    REQUIRE(value.origin == luil::zoom_point { -30.0f, -120.0f });
    settings.zoom = value.zoom;
    settings.origin = value.origin;
    REQUIRE(luil::zoom_about(settings, { 400.0f, 400.0f }, 2.0f, { 99.0f, 70.0f }).origin == value.origin);
    REQUIRE(luil::zoom_about(settings, {}, 2.0f, { 1.0e20f, -1.0e20f }).origin == value.origin);
    REQUIRE(luil::zoom_about(settings, {}, std::numeric_limits<float>::quiet_NaN(), {}).zoom == 2.0f);
    settings.zoom = settings.minimum_zoom;
    REQUIRE(luil::zoom_about(settings, {}, 0.1f, { 10.0f, 10.0f }).origin == settings.origin);
}

TEST_CASE("Zoom fit centers bounds respects margin and caps scale", "[zoom]")
{
    auto settings { config() };
    settings.content_bounds = luil::rect_f { -100.0f, 0.0f, 400.0f, 200.0f };
    const auto value { luil::zoom_to_fit(settings, { 240.0f, 240.0f }, 20.0f) };
    REQUIRE(value.zoom == 0.5f);
    REQUIRE(value.origin == luil::zoom_point { -50.0f, -50.0f });
    REQUIRE(luil::zoom_to_fit(settings, { 1000.0f, 1000.0f }).zoom == 1.0f);
    REQUIRE(luil::zoom_to_fit(settings, { 1000.0f, 1000.0f }, 0.0f, 2.0f).zoom == 2.0f);
    const auto moved { luil::pan_by(settings, { 240.0f, 240.0f }, { 10000.0f, -10000.0f }) };
    REQUIRE(moved.origin.x < 220.0f);
    REQUIRE(moved.origin.y > -320.0f);
    REQUIRE(luil::zoom_to_fit(settings, { 20.0f, 20.0f }, 16.0f).zoom == settings.zoom);
}

TEST_CASE("Zoom arranges scaled children once and clips their hit tests", "[zoom]")
{
    auto settings { config() };
    settings.zoom = 2.0f;
    settings.content_bounds = luil::rect_f { 0.0f, 0.0f, 100.0f, 100.0f };
    auto view { std::make_unique<luil::zoom_view_element>(settings) };
    auto item { std::make_unique<content>() };
    auto child { std::make_unique<content>() };
    const auto child_id { child->id() };
    child->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; });
    item->add(std::move(child));
    const auto* const observed { item.get() };
    view->set_content(std::move(item));
    view->arrange({ { 20.0f, 30.0f, 400.0f, 400.0f }, 2.0f });
    REQUIRE(observed->scale == 4.0f);
    REQUIRE(observed->arranges == 1);
    REQUIRE(view->hit_test(240.0f, 250.0f)->id() == child_id);
    REQUIRE(view->hit_test(430.0f, 250.0f) == nullptr);
    REQUIRE_THROWS(view->set_content(std::make_unique<content>()));
}

TEST_CASE("Zoom wheel modes and nested scroll own only one request", "[zoom]")
{
    luil::interaction_controller controller {};
    controller.set_tree(tree());
    auto actions { controller.process(luil::mouse_wheel_event { .x = 100.0f, .y = 100.0f, .delta = 120.0f }) };
    REQUIRE(actions.size() == 1);
    REQUIRE(read(actions[0]).factor == Catch::Approx(1.2f));
    REQUIRE(read(actions[0]).anchor == luil::zoom_point { -100.0f, -100.0f });
    controller.set_tree(tree(luil::zoom_wheel_mode::pan));
    actions = controller.process(luil::mouse_wheel_event { .x = 100.0f, .y = 100.0f, .delta = 120.0f, .shift = true });
    REQUIRE(read(actions[0]).delta.x > 0.0f);
    actions = controller.process(luil::mouse_wheel_event { .x = 100.0f, .y = 100.0f, .delta = 120.0f, .horizontal = true });
    REQUIRE(read(actions[0]).delta.x < 0.0f);
    actions = controller.process(luil::mouse_wheel_event { .x = 100.0f, .y = 100.0f, .delta = -120.0f, .control = true });
    REQUIRE(read(actions[0]).factor < 1.0f);
    controller.set_tree(tree(luil::zoom_wheel_mode::zoom, false, false, true));
    actions = controller.process(luil::mouse_wheel_event { .x = 240.0f, .y = 240.0f, .delta = 120.0f, .control = true });
    REQUIRE(actions.size() == 1);
    REQUIRE(read(actions[0]).value < 0.0f);
    luil::interaction_policy text_only_policy {};
    luil::interaction_controller with_policy { &text_only_policy };
    with_policy.set_tree(tree(luil::zoom_wheel_mode::zoom, false, false, true));
    const auto routed { with_policy.process(luil::mouse_wheel_event { .x = 240.0f, .y = 240.0f, .delta = 120.0f }) };
    REQUIRE(routed.size() == 1);
    REQUIRE(read(routed[0]).value < 0.0f);
}

TEST_CASE("Zoom empty drag is immediate for touch mouse and pen", "[zoom]")
{
    for (const auto device : { luil::pointer_device::touch, luil::pointer_device::mouse, luil::pointer_device::pen })
    {
        luil::interaction_controller controller {};
        controller.set_tree(tree());
        REQUIRE(controller.process(press(1, 100.0f, 100.0f, device)).empty());
        const auto actions { controller.process(move(1, 101.0f, 100.0f, device)) };
        REQUIRE(actions.size() == 1);
        REQUIRE(read(actions[0]).delta.x == 1.0f);
        static_cast<void>(controller.cancel_dropped_gestures());
        REQUIRE(controller.process(move(1, 150.0f, 100.0f, device)).empty());
    }
}

TEST_CASE("Touch converts a click to view pan without a time window and keeps handles", "[zoom]")
{
    luil::interaction_controller controller {};
    controller.set_tree(tree(luil::zoom_wheel_mode::zoom, true));
    static_cast<void>(controller.process(press(1, 240.0f, 240.0f)));
    luil::touch_gesture_config direct_pan {};
    direct_pan.pan_enabled = false;
    REQUIRE(controller.set_touch_config(direct_pan));
    auto event { move(1, 270.0f, 240.0f) };
    event.time = std::chrono::steady_clock::time_point {} + std::chrono::seconds { 3 };
    const auto actions { controller.process(event) };
    REQUIRE(actions.size() == 1);
    REQUIRE(read(actions[0]).delta.x == 30.0f);
    REQUIRE(controller.process(release(1, 270.0f, 240.0f)).empty());
    controller.set_tree(tree(luil::zoom_wheel_mode::zoom, true, true));
    static_cast<void>(controller.process(press(1, 240.0f, 240.0f)));
    static_cast<void>(controller.process(press(2, 100.0f)));
    const auto handled { controller.process(move(1, 250.0f, 240.0f)) };
    REQUIRE(handled.size() == 1);
    REQUIRE(read(handled[0]).value == 9.0f);
}

TEST_CASE("Pinch sends midpoint pan then anchored ratio and resumes either finger", "[zoom][pinch]")
{
    for (const bool first_up : { false, true })
    {
        luil::interaction_controller controller {};
        controller.set_tree(tree());
        static_cast<void>(controller.process(press(1, 100.0f)));
        static_cast<void>(controller.process(press(2, 200.0f)));
        static_cast<void>(controller.process(press(3, 300.0f)));
        REQUIRE(controller.process(move(3, 350.0f)).empty());
        const auto actions { controller.process(move(2, 250.0f)) };
        REQUIRE(actions.size() == 2);
        REQUIRE(read(actions[0]).delta.x == 25.0f);
        REQUIRE(read(actions[1]).factor == 1.5f);
        REQUIRE(read(actions[1]).anchor == luil::zoom_point { -25.0f, -100.0f });
        REQUIRE(controller.process(release(first_up ? 1 : 2, first_up ? 100.0f : 250.0f)).empty());
        const auto resumed { controller.process(move(first_up ? 2 : 1, first_up ? 260.0f : 110.0f)) };
        REQUIRE(resumed.size() == 1);
        REQUIRE(read(resumed[0]).delta.x == 10.0f);
        REQUIRE(controller.process(release(first_up ? 2 : 1, first_up ? 260.0f : 110.0f)).empty());
        REQUIRE(controller.process(move(3, 360.0f)).empty());
    }
}

TEST_CASE("Pinch ignores close contacts different owners and cancels both", "[zoom][pinch]")
{
    luil::interaction_controller controller {};
    controller.set_tree(tree());
    static_cast<void>(controller.process(press(1, 100.0f)));
    static_cast<void>(controller.process(press(2, 110.0f)));
    auto actions { controller.process(move(2, 112.0f)) };
    REQUIRE(actions.size() == 1);
    REQUIRE(read(actions[0]).delta.x == 1.0f);
    static_cast<void>(controller.process(luil::pointer_cancelled_event { .device = luil::pointer_device::touch, .pointer_id = 2 }));
    REQUIRE(controller.process(move(1, 120.0f)).empty());
    REQUIRE(controller.process(release(2, 112.0f)).empty());
    static_cast<void>(controller.process(press(1, 100.0f)));
    static_cast<void>(controller.process(press(2, 500.0f)));
    REQUIRE(controller.process(move(2, 200.0f)).empty());
    actions = controller.process(move(1, 110.0f));
    REQUIRE(actions.size() == 1);
    REQUIRE(read(actions[0]).delta.x == 10.0f);
    static_cast<void>(controller.cancel_dropped_gestures());
    static_cast<void>(controller.process(press(1, 100.0f)));
    static_cast<void>(controller.process(press(2, 200.0f)));
    luil::touch_gesture_config touch {};
    touch.pinch_enabled = false;
    REQUIRE(controller.set_touch_config(touch));
    REQUIRE(controller.process(move(1, 120.0f)).empty());
    touch.minimum_pinch_distance = 0.0f;
    REQUIRE(luil::valid_touch_gesture_config(touch) == false);
}

TEST_CASE("Zoom keyboard and absolute accessibility use current view factories", "[zoom]")
{
    luil::interaction_controller controller {};
    const auto view { tree() };
    controller.set_tree(view);
    static_cast<void>(controller.process(luil::access_focus_event { .target = { luil::ui_element_kind::zoom_view, u8"view" } }));
    auto actions { controller.process(luil::character_typed_event { .character = U'+' }) };
    REQUIRE(actions.size() == 1);
    REQUIRE(read(actions[0]).factor == Catch::Approx(1.2f));
    actions = controller.process(luil::key_pressed_event { .key = luil::key_code::arrow_left });
    REQUIRE(read(actions[0]).delta.x == 32.0f);
    actions = controller.process(luil::key_pressed_event { .key = luil::key_code::page_down });
    REQUIRE(read(actions[0]).delta.y == -360.0f);
    REQUIRE(controller.process(luil::key_pressed_event { .key = luil::key_code::arrow_left, .control = true }).empty());
    const auto info { view->root()->accessibility() };
    REQUIRE(info.role == luil::access_role::pane);
    REQUIRE(info.range == luil::access_range { 0.1f, 10.0f, 1.0f });
    const auto planned { luil::plan_access_request(*view->root(), { luil::access_command::set_value, 5.0f }) };
    REQUIRE(planned.has_value());
    REQUIRE(read((*planned)[0]).value == 5.0f);
}

TEST_CASE("Zoom routing obeys nested views visibility and opaque barriers", "[zoom]")
{
    auto outer { std::make_unique<luil::zoom_view_element>(config()) };
    auto inner_settings { config() };
    inner_settings.owner = u8"inner";
    inner_settings.zoom_by = [](const float factor, const luil::zoom_point anchor) { return luil::make_app_action(request { .factor = factor, .anchor = anchor, .value = 2.0f }); };
    auto inner { std::make_unique<luil::zoom_view_element>(inner_settings) };
    auto* const observed { inner.get() };
    outer->set_content(std::move(inner));
    outer->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    luil::ui_tree nested { std::move(outer) };
    const luil::mouse_wheel_event wheel { .x = 250.0f, .y = 250.0f, .delta = 120.0f };
    REQUIRE(read(luil::route_wheel(nested, wheel, -40.0f)[0]).value == 2.0f);
    observed->set_visible(false);
    REQUIRE(read(luil::route_wheel(nested, wheel, -40.0f)[0]).value == 0.0f);
    observed->set_visible(true);
    observed->set_enabled(false);
    REQUIRE(read(luil::route_wheel(nested, wheel, -40.0f)[0]).value == 0.0f);
    auto parent { std::make_unique<content>() };
    auto view { std::make_unique<luil::zoom_view_element>(config()) };
    parent->add(std::move(view));
    auto scrim { std::make_unique<content>(u8"scrim") };
    scrim->set_hit_opaque(true);
    parent->add(std::move(scrim));
    parent->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    luil::ui_tree blocked { std::move(parent) };
    REQUIRE(luil::route_wheel(blocked, luil::mouse_wheel_event { .x = 10.0f, .y = 10.0f, .delta = 120.0f }, -40.0f).empty());
}

TEST_CASE("A second touch on a different view cannot join the pinch", "[zoom][pinch]")
{
    auto root { std::make_unique<content>() };
    root->arrange({ { 0.0f, 0.0f, 400.0f, 200.0f }, 1.0f });
    auto first { std::make_unique<luil::zoom_view_element>(config()) };
    first->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    root->add(std::move(first));
    auto settings { config() };
    settings.owner = u8"second";
    auto second { std::make_unique<luil::zoom_view_element>(settings) };
    second->arrange({ { 200.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    root->add(std::move(second));
    luil::interaction_controller controller {};
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));
    static_cast<void>(controller.process(press(1, 100.0f)));
    static_cast<void>(controller.process(press(2, 250.0f)));
    REQUIRE(controller.process(move(2, 270.0f)).empty());
    const auto actions { controller.process(move(1, 120.0f)) };
    REQUIRE(actions.size() == 1);
    REQUIRE(read(actions[0]).delta.x == 20.0f);
}

TEST_CASE("Pinch cancellation covers queue loss surface removal and other devices", "[zoom][pinch]")
{
    for (int cause { 0 }; cause < 4; ++cause)
    {
        luil::interaction_controller controller {};
        controller.set_tree(tree());
        static_cast<void>(controller.process(press(1, 100.0f)));
        static_cast<void>(controller.process(press(2, 200.0f)));
        if (cause == 0)
            static_cast<void>(controller.cancel_dropped_gestures());
        else if (cause == 1)
        {
            controller.set_tree(nullptr);
            controller.set_tree(tree());
        }
        else
            static_cast<void>(controller.process(press(4, 50.0f, 50.0f, cause == 2 ? luil::pointer_device::mouse : luil::pointer_device::pen)));
        REQUIRE(controller.process(move(1, 110.0f)).empty());
        REQUIRE(controller.process(move(2, 220.0f)).empty());
        REQUIRE(controller.process(release(1, 110.0f)).empty());
        REQUIRE(controller.process(release(2, 220.0f)).empty());
    }
}

TEST_CASE("Moving content cannot hand an owned zoom drag to a child scroller", "[zoom]")
{
    luil::interaction_controller controller {};
    controller.set_tree(tree());
    static_cast<void>(controller.process(press(1, 100.0f)));
    auto settings { config() };
    settings.origin = { -200.0f, -200.0f };
    auto view { std::make_unique<luil::zoom_view_element>(settings) };
    auto child { std::make_unique<content>() };
    child->set_scroll_source(luil::scroll_source { .scroll = [](const float delta) { return luil::make_app_action(request { .value = delta }); } });
    view->set_content(std::move(child));
    view->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(view)));
    const auto actions { controller.process(move(1, 120.0f)) };
    REQUIRE(actions.size() == 1);
    REQUIRE(read(actions[0]).delta.x == 20.0f);
}

TEST_CASE("Touch item drag has priority and mouse click never becomes view pan", "[zoom]")
{
    luil::interaction_controller controller {};
    controller.set_tree(tree(luil::zoom_wheel_mode::zoom, true));
    static_cast<void>(controller.process(press(0, 240.0f, 240.0f, luil::pointer_device::mouse)));
    REQUIRE(controller.process(move(0, 275.0f, 240.0f, luil::pointer_device::mouse)).empty());
    static_cast<void>(controller.cancel_dropped_gestures());
    auto view { std::make_unique<luil::zoom_view_element>(config()) };
    auto item { std::make_unique<content>() };
    item->set_drag_source(luil::drag_source { .make_payload = [](const luil::ui_action_context&) { return luil::drag_payload {}; } });
    view->set_content(std::move(item));
    view->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(view)));
    static_cast<void>(controller.process(press(1, 240.0f, 240.0f)));
    static_cast<void>(controller.process(press(2, 100.0f)));
    REQUIRE(controller.process(move(1, 270.0f, 240.0f)).empty());
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(controller.process(move(2, 150.0f)).empty());
}

TEST_CASE("Zoom controls fit and keyboard zero send ordered current-state requests", "[zoom]")
{
    auto settings { config() };
    settings.zoom = 2.0f;
    settings.origin = { 10.0f, 20.0f };
    settings.content_bounds = luil::rect_f { 0.0f, 0.0f, 400.0f, 200.0f };
    luil::zoom_controls_element controls { { .view = settings, .viewport = { 240.0f, 240.0f }, .fit_margin = 20.0f } };
    controls.arrange({ { 0.0f, 0.0f, 400.0f, 32.0f }, 1.0f });
    REQUIRE(controls.children().size() == 4);
    REQUIRE(controls.children().back()->bounds().x + controls.children().back()->bounds().width < 400.0f);
    const auto* const fit { controls.children().back().get() };
    const auto actions { luil::plan_access_request(*fit, { luil::access_command::invoke }) };
    REQUIRE(actions.has_value());
    REQUIRE(actions->size() == 2);
    settings.origin = luil::zoom_about(settings, { 240.0f, 240.0f }, read((*actions)[0]).factor, read((*actions)[0]).anchor).origin;
    settings.zoom *= read((*actions)[0]).factor;
    const auto value { luil::pan_by(settings, { 240.0f, 240.0f }, read((*actions)[1]).delta) };
    REQUIRE(value.zoom == 0.5f);
    REQUIRE(value.origin == luil::zoom_point { -100.0f, -50.0f });
    settings.content_bounds.reset();
    luil::zoom_controls_element unbounded { { .view = settings } };
    REQUIRE(unbounded.children().size() == 3);
    auto view { std::make_unique<luil::zoom_view_element>(settings) };
    view->arrange({ { 0.0f, 0.0f, 240.0f, 240.0f }, 1.0f });
    luil::interaction_controller controller {};
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(view)));
    static_cast<void>(controller.process(luil::access_focus_event { .target = { luil::ui_element_kind::zoom_view, u8"view" } }));
    const auto reset { controller.process(luil::character_typed_event { .character = U'0' }) };
    REQUIRE(reset.size() == 1);
    REQUIRE(read(reset[0]).factor == 2.0f);
    controls.arrange({ { 0.0f, 0.0f, 30.0f, 32.0f }, 2.0f });
    REQUIRE(controls.children().back()->bounds().x + controls.children().back()->bounds().width <= Catch::Approx(30.0f));
}

namespace {
    // 경계 쓸기를 알리는 쪽 보기다: 100×100 쪽이 400×400 창의 가운데에 맞춰 있다.
    luil::zoom_view_config page_config(const float zoom = 1.0f, const luil::zoom_point origin = {})
    {
        auto settings { config() };
        settings.zoom = zoom;
        settings.origin = origin;
        settings.content_bounds = luil::rect_f { -50.0f, -50.0f, 100.0f, 100.0f };
        settings.bounds = luil::zoom_bounds::contain;
        settings.edge_swipe = [](const luil::zoom_edge_swipe& swipe) { return luil::make_app_action(swipe); };
        return settings;
    }

    std::shared_ptr<const luil::ui_tree> page_tree(const luil::zoom_view_config& settings)
    {
        auto view { std::make_unique<luil::zoom_view_element>(settings) };
        view->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
        return std::make_shared<const luil::ui_tree>(std::move(view));
    }

    std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    // 손가락 하나로 (x0, 200)에서 (x1, 200)까지 고르게 쓸고 마지막 자리에서 뗀다. 처음부터 끝까지 `duration`이다.
    std::vector<luil::input_action> swipe(
        luil::interaction_controller& controller, const float from, const float to, const int duration, const luil::pointer_device device = luil::pointer_device::touch, const int pause = 0)
    {
        std::vector<luil::input_action> actions {};
        const auto keep = [&actions](std::vector<luil::input_action> more) {
            for (luil::input_action& action : more)
                actions.push_back(std::move(action));
        };
        luil::pointer_pressed_event down { .x = from, .y = 200.0f, .time = at(0), .device = device, .pointer_id = 1 };
        keep(controller.process(down));
        for (int step { 1 }; step <= 4; ++step)
        {
            const float x { from + (to - from) * static_cast<float>(step) / 4.0f };
            keep(controller.process(luil::pointer_moved_event { .x = x, .y = 200.0f, .time = at(duration * step / 4), .device = device, .pointer_id = 1, .in_contact = true }));
        }
        keep(controller.process(luil::pointer_released_event { .x = to, .y = 200.0f, .time = at(duration + pause), .device = device, .pointer_id = 1 }));
        return actions;
    }

    std::vector<luil::zoom_edge_swipe> swipes_in(const std::vector<luil::input_action>& actions)
    {
        std::vector<luil::zoom_edge_swipe> found {};
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const value { message->get<luil::zoom_edge_swipe>() }; value != nullptr)
                    found.push_back(*value);
        return found;
    }
}

TEST_CASE("Contain bounds center a small page and hide empty space around a large one", "[zoom][edge]")
{
    const luil::zoom_point viewport { 400.0f, 400.0f };
    // 창보다 작은 쪽은 어디로 끌어도 가운데에 있다. 끈 만큼이 전부 막힌다.
    REQUIRE(luil::pan_by(page_config(), viewport, { 80.0f, -30.0f }).origin == luil::zoom_point {});
    REQUIRE(luil::pan_blocked(page_config(), viewport, { 80.0f, -30.0f }) == luil::zoom_point { 80.0f, -30.0f });
    // 여덟 배로 키우면 800×800이다. 빈 곳이 보이지 않는 데까지만 옮긴다.
    REQUIRE(luil::pan_by(page_config(8.0f), viewport, { 900.0f, -900.0f }).origin == luil::zoom_point { 200.0f, -200.0f });
    // 맞춤도 가운데다. 겹침 방식의 기존 다듬기는 그대로다.
    REQUIRE(luil::zoom_to_fit(page_config(), viewport).origin == luil::zoom_point {});
    auto overlap { page_config() };
    overlap.bounds = luil::zoom_bounds::overlap;
    REQUIRE(luil::pan_blocked(overlap, viewport, { 80.0f, -30.0f }) == luil::zoom_point {});
}

TEST_CASE("Only the movement past an edge is blocked and each axis is apart", "[zoom][edge]")
{
    const luil::zoom_point viewport { 400.0f, 400.0f };
    // 여덟 배로 키워 오른쪽 끝을 보고 있다 (쪽의 오른쪽 끝이 창의 오른쪽 끝이다).
    const auto right_edge { page_config(8.0f, { -200.0f, 0.0f }) };
    // 왼쪽으로 더 밀면 전부 막히고, 오른쪽으로는 전부 옮겨진다.
    REQUIRE(luil::pan_blocked(right_edge, viewport, { -50.0f, 0.0f }) == luil::zoom_point { -50.0f, 0.0f });
    REQUIRE(luil::pan_blocked(right_edge, viewport, { 30.0f, 0.0f }) == luil::zoom_point {});
    // 끝까지 20 남았으면 넘친 몫만 막힌다.
    REQUIRE(luil::pan_blocked(page_config(8.0f, { -180.0f, 0.0f }), viewport, { -50.0f, 0.0f }) == luil::zoom_point { -30.0f, 0.0f });

    // 가로로 긴 쪽은 가로로 흐르고 세로는 가운데에 묶인다. 막힌 것은 세로뿐이다.
    auto wide { page_config() };
    wide.content_bounds = luil::rect_f { -500.0f, -50.0f, 1000.0f, 100.0f };
    REQUIRE(luil::pan_blocked(wide, viewport, { -40.0f, 25.0f }) == luil::zoom_point { 0.0f, 25.0f });
}

TEST_CASE("An edge swipe needs distance, an axis that stayed put and speed", "[zoom][edge]")
{
    const luil::zoom_edge_swipe_config rule {};
    REQUIRE(rule.minimum_distance == 32.0f);
    REQUIRE(rule.minimum_velocity == 300.0f);
    // 왼쪽으로 빠르게 쓸어 오른쪽 끝에 막혔다.
    const auto left { luil::judge_edge_swipe(rule, {}, { -120.0f, 3.0f }, { -900.0f, 10.0f }) };
    REQUIRE(left.horizontal == -1);
    REQUIRE(left.vertical == 0);
    REQUIRE(left.blocked == luil::zoom_point { -120.0f, 3.0f });
    // 아래로 쓸어 위 끝에 막혔다 — 세로만이다.
    REQUIRE(luil::judge_edge_swipe(rule, {}, { 0.0f, 80.0f }, { 0.0f, 600.0f }).vertical == 1);
    // 덜 막혔다, 그 축이 이미 많이 옮겨졌다(끝까지 끈 접촉), 느렸다, 반대쪽으로 튕겼다.
    REQUIRE(luil::judge_edge_swipe(rule, {}, { -20.0f, 0.0f }, { -900.0f, 0.0f }).horizontal == 0);
    REQUIRE(luil::judge_edge_swipe(rule, { -150.0f, 0.0f }, { -120.0f, 0.0f }, { -900.0f, 0.0f }).horizontal == 0);
    REQUIRE(luil::judge_edge_swipe(rule, {}, { -120.0f, 0.0f }, { -100.0f, 0.0f }).horizontal == 0);
    REQUIRE(luil::judge_edge_swipe(rule, {}, { -120.0f, 0.0f }, { 900.0f, 0.0f }).horizontal == 0);
    // 조금 옮겨진 뒤 막힌 것은 끝에 거의 닿아 있던 것이라 쓸기다.
    REQUIRE(luil::judge_edge_swipe(rule, { -10.0f, 0.0f }, { -120.0f, 0.0f }, { -900.0f, 0.0f }).horizontal == -1);
}

TEST_CASE("A fitted page reports a quick touch swipe against its edge once", "[zoom][edge]")
{
    luil::interaction_controller controller {};
    controller.set_tree(page_tree(page_config()));
    // 왼쪽으로 120을 0.1초에 쓴다. 가운데에 묶인 쪽이라 전부 막힌다. 이동 메시지는 그대로 나가고 앱이 다듬는다.
    const auto actions { swipe(controller, 260.0f, 140.0f, 100) };
    const auto found { swipes_in(actions) };
    REQUIRE(found.size() == 1u);
    REQUIRE(found[0].horizontal == -1);
    REQUIRE(found[0].vertical == 0);
    REQUIRE(found[0].blocked.x == Catch::Approx(-120.0f));
    REQUIRE(found[0].velocity.x == Catch::Approx(-1200.0f));
    // 끌다 멈춘 뒤 뗀 것은 쓸기가 아니다 (마지막 0.1초에 움직임이 없다).
    REQUIRE(swipes_in(swipe(controller, 260.0f, 140.0f, 100, luil::pointer_device::touch, 300)).empty());
    // 펜도 같다. 마우스 끌기는 알리지 않는다.
    REQUIRE(swipes_in(swipe(controller, 260.0f, 140.0f, 100, luil::pointer_device::pen)).size() == 1u);
    REQUIRE(swipes_in(swipe(controller, 260.0f, 140.0f, 100, luil::pointer_device::mouse)).empty());
}

TEST_CASE("A zoomed page reports only the swipe that starts at its edge", "[zoom][edge]")
{
    luil::interaction_controller controller {};
    // 여덟 배로 키워 가운데를 보고 있다. 왼쪽으로 300을 쓸면 200은 옮겨지고 100이 막힌다 — 끝에 닿은 쓸기는
    // 쪽을 넘기지 않는다.
    controller.set_tree(page_tree(page_config(8.0f)));
    REQUIRE(swipes_in(swipe(controller, 350.0f, 50.0f, 100)).empty());
    // 앱이 다듬어 오른쪽 끝에 선 상태에서 한 번 더 쓸면 알린다.
    controller.set_tree(page_tree(page_config(8.0f, { -200.0f, 0.0f })));
    const auto found { swipes_in(swipe(controller, 300.0f, 200.0f, 100)) };
    REQUIRE(found.size() == 1u);
    REQUIRE(found[0].horizontal == -1);
    // 반대쪽으로는 옮겨지므로 알리지 않는다.
    REQUIRE(swipes_in(swipe(controller, 200.0f, 300.0f, 100)).empty());
}

TEST_CASE("A contact that pinched never reports an edge swipe", "[zoom][edge][pinch]")
{
    luil::interaction_controller controller {};
    controller.set_tree(page_tree(page_config()));
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 260.0f, .y = 200.0f, .time = at(0), .device = luil::pointer_device::touch, .pointer_id = 1 }));
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 100.0f, .y = 200.0f, .time = at(10), .device = luil::pointer_device::touch, .pointer_id = 2 }));
    static_cast<void>(controller.process(luil::pointer_moved_event { .x = 200.0f, .y = 200.0f, .time = at(40), .device = luil::pointer_device::touch, .pointer_id = 1, .in_contact = true }));
    REQUIRE(swipes_in(controller.process(luil::pointer_released_event { .x = 100.0f, .y = 200.0f, .time = at(50), .device = luil::pointer_device::touch, .pointer_id = 2 })).empty());
    // 남은 손가락이 빠르게 쓸고 떼어도 핀치가 섞인 접촉이다.
    static_cast<void>(controller.process(luil::pointer_moved_event { .x = 120.0f, .y = 200.0f, .time = at(80), .device = luil::pointer_device::touch, .pointer_id = 1, .in_contact = true }));
    REQUIRE(swipes_in(controller.process(luil::pointer_released_event { .x = 60.0f, .y = 200.0f, .time = at(100), .device = luil::pointer_device::touch, .pointer_id = 1 })).empty());
}

TEST_CASE("A view without an edge swipe message measures nothing", "[zoom][edge]")
{
    auto settings { page_config() };
    settings.edge_swipe = {};
    luil::interaction_controller controller {};
    controller.set_tree(page_tree(settings));
    const auto actions { swipe(controller, 260.0f, 140.0f, 100) };
    REQUIRE(swipes_in(actions).empty());
    // 이동 메시지는 지금과 같다.
    REQUIRE(actions.size() == 4u);
    REQUIRE(controller.next_deadline().has_value() == false);
}

namespace {
    // 두 번 탭 확대를 켠 쪽 보기다. 100×100 쪽은 맞춤 상한 1에서 100%가 맞춤이다.
    luil::zoom_view_config tap_config(const float zoom = 1.0f)
    {
        auto settings { page_config(zoom) };
        settings.edge_swipe = {};
        settings.bounds = luil::zoom_bounds::overlap;
        settings.double_tap.enabled = true;
        return settings;
    }

    // 쪽 안에 클릭(값 7)이나 클릭·더블 클릭(값 7·8)이 있는 내용을 담는다.
    std::shared_ptr<const luil::ui_tree> tap_tree(const luil::zoom_view_config& settings, const bool clickable = false, const bool double_click = false)
    {
        auto view { std::make_unique<luil::zoom_view_element>(settings) };
        if (clickable)
        {
            auto item { std::make_unique<content>() };
            item->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> { luil::make_app_action(request { .value = 7.0f }) }; });
            if (double_click)
                item->set_action(luil::ui_trigger::double_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> { luil::make_app_action(request { .value = 8.0f }) }; });
            view->set_content(std::move(item));
        }
        view->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
        return std::make_shared<const luil::ui_tree>(std::move(view));
    }

    // (x, y)를 `start`에 눌러 30ms 뒤 뗀다.
    std::vector<luil::input_action> tap(
        luil::interaction_controller& controller, const float x, const float y, const int start, const std::uint32_t id = 1, const luil::pointer_device device = luil::pointer_device::touch)
    {
        std::vector<luil::input_action> actions { controller.process(luil::pointer_pressed_event { .x = x, .y = y, .time = at(start), .device = device, .pointer_id = id }) };
        for (luil::input_action& action : controller.process(luil::pointer_released_event { .x = x, .y = y, .time = at(start + 30), .device = device, .pointer_id = id }))
            actions.push_back(std::move(action));
        return actions;
    }

    std::vector<request> requests_in(const std::vector<luil::input_action>& actions)
    {
        std::vector<request> found {};
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const value { message->get<request>() }; value != nullptr)
                    found.push_back(*value);
        return found;
    }
}

TEST_CASE("Double tap zoom stays off unless the view asks for it", "[zoom][double_tap]")
{
    auto settings { tap_config() };
    settings.double_tap.enabled = false;
    luil::interaction_controller controller {};
    controller.set_tree(tap_tree(settings));
    REQUIRE(tap(controller, 250.0f, 220.0f, 0).empty());
    REQUIRE(tap(controller, 250.0f, 220.0f, 100).empty());
}

TEST_CASE("Two taps zoom about the tapped point and two more go back to fit", "[zoom][double_tap]")
{
    luil::interaction_controller controller {};
    controller.set_tree(tap_tree(tap_config()));
    // 첫 탭은 아무것도 내지 않는다. 둘째 탭이 탭한 자리를 기준으로 두 배로 키운다.
    REQUIRE(tap(controller, 250.0f, 220.0f, 0).empty());
    const auto zoomed { requests_in(tap(controller, 252.0f, 221.0f, 150)) };
    REQUIRE(zoomed.size() == 1u);
    REQUIRE(zoomed[0].factor == 2.0f);
    REQUIRE(zoomed[0].anchor == luil::zoom_point { 52.0f, 21.0f });

    // 확대 상태에서 두 번 탭하면 맞춤으로 돌아간다 (가운데 기준 비율 뒤 위치 이동 — 맞춤 단추와 같은 메시지).
    controller.set_tree(tap_tree(tap_config(2.0f)));
    REQUIRE(tap(controller, 100.0f, 100.0f, 1000).empty());
    const auto fitted { requests_in(tap(controller, 100.0f, 100.0f, 1100)) };
    REQUIRE(fitted.empty() == false);
    REQUIRE(fitted[0].factor == 0.5f);
    // 셋째 탭은 새 짝의 첫 탭이다.
    REQUIRE(tap(controller, 100.0f, 100.0f, 1200).empty());
}

TEST_CASE("The second tap must land near the first and soon after it", "[zoom][double_tap]")
{
    luil::interaction_controller controller {};
    controller.set_tree(tap_tree(tap_config()));
    // 허용치(12)보다 멀다. 둘째 탭이 새 첫 탭이 되고, 그 곁의 셋째 탭이 짝이다.
    REQUIRE(tap(controller, 200.0f, 200.0f, 0).empty());
    REQUIRE(tap(controller, 220.0f, 200.0f, 100).empty());
    REQUIRE(requests_in(tap(controller, 230.0f, 200.0f, 200)).size() == 1u);

    // 앞 탭을 뗀 뒤 더블 클릭 시간(500ms)을 넘겨 눌렀다.
    REQUIRE(tap(controller, 200.0f, 200.0f, 1000).empty());
    REQUIRE(tap(controller, 200.0f, 200.0f, 1531).empty());
    // 시간 안이면 짝이다.
    REQUIRE(requests_in(tap(controller, 200.0f, 200.0f, 2000)).size() == 1u);
}

TEST_CASE("A pinch or a moving contact is not the first tap", "[zoom][double_tap][pinch]")
{
    luil::interaction_controller controller {};
    controller.set_tree(tap_tree(tap_config()));
    // 두 손가락이 닿았다 떨어졌다 (움직이지 않은 핀치). 그 뒤의 탭은 첫 탭이다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 150.0f, .y = 200.0f, .time = at(0), .device = luil::pointer_device::touch, .pointer_id = 1 }));
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 250.0f, .y = 200.0f, .time = at(10), .device = luil::pointer_device::touch, .pointer_id = 2 }));
    static_cast<void>(controller.process(luil::pointer_released_event { .x = 250.0f, .y = 200.0f, .time = at(40), .device = luil::pointer_device::touch, .pointer_id = 2 }));
    REQUIRE(requests_in(controller.process(luil::pointer_released_event { .x = 150.0f, .y = 200.0f, .time = at(50), .device = luil::pointer_device::touch, .pointer_id = 1 })).empty());
    REQUIRE(tap(controller, 150.0f, 200.0f, 100).empty());
    REQUIRE(requests_in(tap(controller, 150.0f, 200.0f, 200)).size() == 1u);

    // 탭과 탭 사이에 핀치가 들면 짝이 끊긴다.
    REQUIRE(tap(controller, 150.0f, 200.0f, 1000).empty());
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 150.0f, .y = 200.0f, .time = at(1100), .device = luil::pointer_device::touch, .pointer_id = 3 }));
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 250.0f, .y = 200.0f, .time = at(1110), .device = luil::pointer_device::touch, .pointer_id = 4 }));
    static_cast<void>(controller.process(luil::pointer_released_event { .x = 250.0f, .y = 200.0f, .time = at(1140), .device = luil::pointer_device::touch, .pointer_id = 4 }));
    static_cast<void>(controller.process(luil::pointer_released_event { .x = 150.0f, .y = 200.0f, .time = at(1150), .device = luil::pointer_device::touch, .pointer_id = 3 }));
    REQUIRE(tap(controller, 150.0f, 200.0f, 1200).empty());

    // 끈 접촉(허용치를 넘겨 옮긴 것)도 첫 탭이 아니다.
    REQUIRE(tap(controller, 150.0f, 200.0f, 3000).empty());
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 150.0f, .y = 200.0f, .time = at(3100), .device = luil::pointer_device::touch, .pointer_id = 5 }));
    static_cast<void>(controller.process(luil::pointer_moved_event { .x = 190.0f, .y = 200.0f, .time = at(3120), .device = luil::pointer_device::touch, .pointer_id = 5, .in_contact = true }));
    static_cast<void>(controller.process(luil::pointer_released_event { .x = 190.0f, .y = 200.0f, .time = at(3130), .device = luil::pointer_device::touch, .pointer_id = 5 }));
    REQUIRE(tap(controller, 150.0f, 200.0f, 3200).empty());
}

TEST_CASE("The first tap still clicks content and its double click comes first", "[zoom][double_tap]")
{
    luil::interaction_controller controller {};
    // 내용(쪽 자리 150~250)의 클릭은 늦추지 않는다. 둘째 탭은 클릭이 아니라 확대다.
    controller.set_tree(tap_tree(tap_config(), true));
    const auto first { requests_in(tap(controller, 200.0f, 200.0f, 0)) };
    REQUIRE(first.size() == 1u);
    REQUIRE(first[0].value == 7.0f);
    const auto second { requests_in(tap(controller, 200.0f, 200.0f, 100)) };
    REQUIRE(second.size() == 1u);
    REQUIRE(second[0].factor == 2.0f);

    // 내용에 더블 클릭이 있으면 그것이 이긴다.
    controller.set_tree(tap_tree(tap_config(), true, true));
    REQUIRE(requests_in(tap(controller, 200.0f, 200.0f, 1000))[0].value == 7.0f);
    const auto double_clicked { requests_in(tap(controller, 200.0f, 200.0f, 1100)) };
    REQUIRE(double_clicked.size() == 1u);
    REQUIRE(double_clicked[0].value == 8.0f);
}

TEST_CASE("A pen keeps its double click and does not zoom", "[zoom][double_tap][pen]")
{
    luil::interaction_controller controller {};
    controller.set_tree(tap_tree(tap_config()));
    REQUIRE(tap(controller, 200.0f, 200.0f, 0, 9, luil::pointer_device::pen).empty());
    REQUIRE(tap(controller, 200.0f, 200.0f, 100, 9, luil::pointer_device::pen).empty());
    // 펜 사이에 낀 터치 탭도 짝을 잇지 않는다.
    REQUIRE(tap(controller, 200.0f, 200.0f, 1000).empty());
    REQUIRE(tap(controller, 200.0f, 200.0f, 1100, 9, luil::pointer_device::pen).empty());
    REQUIRE(tap(controller, 200.0f, 200.0f, 1200).empty());
}

TEST_CASE("Double tap helper zooms from the base and returns to it", "[zoom][double_tap]")
{
    const luil::zoom_point viewport { 400.0f, 400.0f };
    auto settings { tap_config() };
    settings.double_tap.factor = 3.0f;
    settings.maximum_zoom = 2.5f;
    // 기준(맞춤 100%)에서 세 배를 청해도 상한 2.5에서 멈춘다.
    const auto up { requests_in(luil::double_tap_zoom(settings, viewport, { 10.0f, 20.0f })) };
    REQUIRE(up.size() == 1u);
    REQUIRE(up[0].factor == 2.5f);
    REQUIRE(up[0].anchor == luil::zoom_point { 10.0f, 20.0f });
    // 내용 경계가 없으면 기준은 100%다. 다른 배율에서는 100%로 돌아간다.
    settings.content_bounds.reset();
    settings.zoom = 2.0f;
    const auto back { requests_in(luil::double_tap_zoom(settings, viewport, { 10.0f, 20.0f })) };
    REQUIRE(back.size() == 1u);
    REQUIRE(back[0].factor == 0.5f);
    // 잘못된 배수는 보기가 받지 않는다.
    settings.double_tap.factor = 0.0f;
    REQUIRE_THROWS_AS(luil::zoom_view_element { settings }, std::invalid_argument);
}

TEST_CASE("Fitting the width or the height keeps the reading place on the other axis", "[zoom][fit]")
{
    // 폭 100 × 높이 400의 긴 쪽이다. 창 240 정사각, 여백 20이면 폭 맞춤 2배, 높이 맞춤 0.5배다.
    // 지금은 1배이고 내용 (50, 50)이 창 가운데에 있다 (원점은 창 가운데 기준).
    auto settings { config() };
    settings.content_bounds = luil::rect_f { 0.0f, 0.0f, 100.0f, 400.0f };
    settings.origin = { -50.0f, -50.0f };
    const luil::zoom_point viewport { 240.0f, 240.0f };

    SECTION("overlap")
    {
        const auto width { luil::zoom_to_fit(settings, viewport, 20.0f, 4.0f, luil::zoom_fit::width) };
        REQUIRE(width.zoom == 2.0f);
        // 가로는 가운데로, 세로는 읽던 자리(내용 y 50)가 그대로 창 가운데다.
        REQUIRE(width.origin == luil::zoom_point { -100.0f, -100.0f });
        const auto height { luil::zoom_to_fit(settings, viewport, 20.0f, 4.0f, luil::zoom_fit::height) };
        REQUIRE(height.zoom == 0.5f);
        REQUIRE(height.origin == luil::zoom_point { -25.0f, -100.0f });
        // 전체 맞춤은 지금까지와 같다.
        const auto whole { luil::zoom_to_fit(settings, viewport, 20.0f, 4.0f) };
        REQUIRE(whole.zoom == 0.5f);
        REQUIRE(whole.origin == luil::zoom_point { -25.0f, -100.0f });
        // 맞춘 축도 상한으로 자른다.
        REQUIRE(luil::zoom_to_fit(settings, viewport, 20.0f, 1.5f, luil::zoom_fit::width).zoom == 1.5f);
    }

    SECTION("contain")
    {
        // contain은 지금 상태부터 다듬는다. 1배의 세로 400은 창보다 길어 위쪽 끝에 붙고(원점 y -120), 창 가운데의
        // 내용은 y 120이다. 폭 맞춤은 그 자리를 지켜 -240이고, 그것이 다시 빈 곳 없는 범위 안이다.
        settings.bounds = luil::zoom_bounds::contain;
        const auto width { luil::zoom_to_fit(settings, viewport, 20.0f, 4.0f, luil::zoom_fit::width) };
        REQUIRE(width.zoom == 2.0f);
        REQUIRE(width.origin == luil::zoom_point { -100.0f, -240.0f });
        const auto height { luil::zoom_to_fit(settings, viewport, 20.0f, 4.0f, luil::zoom_fit::height) };
        REQUIRE(height.origin == luil::zoom_point { -25.0f, -100.0f });
    }

    SECTION("a low window still fits the width")
    {
        // 높이가 여백 두 배보다 낮아도 폭은 맞춘다. 전체 맞춤은 서지 않는다.
        const luil::zoom_point low { 240.0f, 30.0f };
        REQUIRE(luil::zoom_to_fit(settings, low, 20.0f, 4.0f, luil::zoom_fit::width).zoom == 2.0f);
        REQUIRE(luil::zoom_to_fit(settings, low, 20.0f, 4.0f).zoom == settings.zoom);
        REQUIRE(luil::zoom_to_fit(settings, low, 20.0f, 4.0f, luil::zoom_fit::height).zoom == settings.zoom);
    }
}

TEST_CASE("Fit actions reach the fitted state through zoom then pan", "[zoom][fit]")
{
    auto settings { config() };
    settings.content_bounds = luil::rect_f { 0.0f, 0.0f, 100.0f, 400.0f };
    // 가로가 가운데에서 벗어나 있다. 가운데 기준 확대가 원점을 (-60, -100)으로 옮긴 뒤, 남은 가로 -40만큼 민다.
    settings.origin = { -30.0f, -50.0f };
    const luil::zoom_point viewport { 240.0f, 240.0f };
    const auto actions { luil::zoom_fit_actions(settings, viewport, 20.0f, 4.0f, luil::zoom_fit::width) };
    REQUIRE(actions.size() == 2u);
    REQUIRE(read(actions[0]).factor == 2.0f);
    REQUIRE(read(actions[1]).delta == luil::zoom_point { -40.0f, 0.0f });
    const auto zoomed { luil::zoom_about(settings, viewport, read(actions[0]).factor, read(actions[0]).anchor) };
    settings.zoom = zoomed.zoom;
    settings.origin = zoomed.origin;
    const auto moved { luil::pan_by(settings, viewport, read(actions[1]).delta) };
    REQUIRE(moved.zoom == 2.0f);
    REQUIRE(moved.origin == luil::zoom_point { -100.0f, -100.0f });

    // 이미 그 상태면 할 일이 없다. 경계나 메시지가 없어도 그렇다.
    settings.origin = moved.origin;
    REQUIRE(luil::zoom_fit_actions(settings, viewport, 20.0f, 4.0f, luil::zoom_fit::width).empty());
    settings.pan_by = {};
    REQUIRE(luil::zoom_fit_actions(settings, viewport, 20.0f, 4.0f, luil::zoom_fit::height).empty());
}
