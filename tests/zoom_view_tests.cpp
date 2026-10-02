#include "luil/ui/zoom_view_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_interaction.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <utility>

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
        {}
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
        {}
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
} // namespace

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
        controller.cancel_dropped_gestures();
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
    controller.cancel_dropped_gestures();
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
            controller.cancel_dropped_gestures();
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
    controller.cancel_dropped_gestures();
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
