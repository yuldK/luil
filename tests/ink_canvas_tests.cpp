#include "luil/ui/ink_canvas_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/zoom_view_element.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_button { luil::application_element_kind(1) };
    constexpr luil::ui_element_kind kind_area { luil::application_element_kind(2) };

    struct stroke_seen
    {
        luil::ink_stroke_input input {};
    };

    struct erase_seen
    {
        std::vector<std::uint64_t> ids {};
    };

    struct named_seen
    {
        std::u8string name {};
    };

    class test_panel final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
    };

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] luil::ui_action named_action(const std::u8string name)
    {
        return [name](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(named_seen { name }) }; };
    }

    enum class host
    {
        root,
        area,
        zoom,
    };

    struct scene
    {
        host place { host::root };
        bool mouse { false };
        bool touch { false };
        luil::ink_tool tool { luil::ink_tool::pen };
        bool canvas { true };
        bool enabled { true };
        bool visible { true };
        // 캔버스 자신의 왼쪽 클릭이다 (마우스를 끈 캔버스가 일반 요소인지 본다).
        bool clickable { false };
        luil::ink_layer strokes {};
    };

    // 400×400 창이다. 캔버스는 (0, 0, 300, 300)이고 오른쪽 위에 단추 둘이 있다.
    //  - `area`면 캔버스가 세로로 흘리는 창 안에 있고, `zoom`이면 확대 보기의 내용이다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> scene_tree(const scene& options)
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
        std::unique_ptr<luil::ink_canvas_element> canvas {};
        if (options.canvas)
        {
            canvas = std::make_unique<luil::ink_canvas_element>(luil::ink_canvas_config {
                .owner = u8"ink",
                .strokes = options.strokes,
                .tool = options.tool,
                .mouse = options.mouse,
                .touch = options.touch,
                .eraser_radius = 4.0f,
                .on_stroke = [](const luil::ink_stroke_input& input) { return luil::make_app_action(stroke_seen { input }); },
                .on_erase = [](std::vector<std::uint64_t> ids) { return luil::make_app_action(erase_seen { std::move(ids) }); },
                .access_name = u8"Sketch",
                .access_summary = [](const std::size_t count) { return std::u8string { u8"strokes " } + static_cast<char8_t>(u8'0' + count); },
            });
            canvas->set_enabled(options.enabled);
            canvas->set_visible(options.visible);
            if (options.clickable)
                canvas->set_action(luil::ui_trigger::left_click, named_action(u8"canvas"));
        }
        if (canvas != nullptr && options.place == host::area)
        {
            auto area { std::make_unique<test_panel>(luil::ui_element_id { kind_area, u8"area" }) };
            area->arrange({ { 0.0f, 0.0f, 300.0f, 300.0f }, 1.0f });
            area->set_scroll_source(luil::scroll_source { .scroll = [](const float delta) { return luil::make_app_action(named_seen { delta > 0.0f ? u8"scroll" : u8"scroll-back" }); } });
            canvas->arrange({ { 0.0f, 0.0f, 300.0f, 300.0f }, 1.0f });
            area->add(std::move(canvas));
            root->add(std::move(area));
        }
        else if (canvas != nullptr && options.place == host::zoom)
        {
            const luil::zoom_view_config view_config {
                .owner = u8"view",
                .zoom_by = [](float, luil::zoom_point) { return luil::make_app_action(named_seen { u8"zoom" }); },
                .pan_by = [](luil::zoom_point) { return luil::make_app_action(named_seen { u8"pan" }); },
            };
            auto view { std::make_unique<luil::zoom_view_element>(view_config) };
            view->set_content(std::move(canvas));
            view->arrange({ { 0.0f, 0.0f, 300.0f, 300.0f }, 1.0f });
            root->add(std::move(view));
        }
        else if (canvas != nullptr)
        {
            canvas->arrange({ { 0.0f, 0.0f, 300.0f, 300.0f }, 1.0f });
            root->add(std::move(canvas));
        }
        auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"button" }) };
        button->arrange({ { 320.0f, 0.0f, 60.0f, 40.0f }, 1.0f });
        button->set_action(luil::ui_trigger::left_click, named_action(u8"button"));
        button->set_action(luil::ui_trigger::right_click, named_action(u8"menu"));
        root->add(std::move(button));
        auto other { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"other" }) };
        other->arrange({ { 320.0f, 60.0f, 60.0f, 40.0f }, 1.0f });
        other->set_action(luil::ui_trigger::left_click, named_action(u8"other"));
        root->add(std::move(other));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    [[nodiscard]] luil::pointer_pressed_event pen_press(const float x, const float y, const int time, const std::optional<float> pressure = 0.5f, const bool eraser = false)
    {
        luil::pointer_pressed_event event { x, y, luil::pointer_button::left, at(time) };
        event.device = luil::pointer_device::pen;
        event.pointer_id = 9;
        event.pen = luil::pen_sample { pressure, 10.0f, -20.0f, eraser };
        return event;
    }

    [[nodiscard]] luil::pointer_moved_event pen_move(const float x, const float y, const int time, const std::optional<float> pressure = 0.5f, const bool eraser = false)
    {
        luil::pointer_moved_event event { x, y, at(time) };
        event.device = luil::pointer_device::pen;
        event.pointer_id = 9;
        event.in_contact = true;
        event.pen = luil::pen_sample { pressure, 10.0f, -20.0f, eraser };
        return event;
    }

    [[nodiscard]] luil::pointer_released_event pen_release(const float x, const float y, const int time, const bool eraser = false, const luil::pointer_button button = luil::pointer_button::left)
    {
        luil::pointer_released_event event { x, y, button, at(time) };
        event.device = luil::pointer_device::pen;
        event.pointer_id = 9;
        event.pen = luil::pen_sample { 0.5f, 10.0f, -20.0f, eraser };
        return event;
    }

    [[nodiscard]] luil::pointer_pressed_event touch_press(const float x, const float y, const int time, const std::uint32_t id = 1)
    {
        luil::pointer_pressed_event event { x, y, luil::pointer_button::left, at(time) };
        event.device = luil::pointer_device::touch;
        event.pointer_id = id;
        return event;
    }

    [[nodiscard]] luil::pointer_moved_event touch_move(const float x, const float y, const int time, const std::uint32_t id = 1)
    {
        luil::pointer_moved_event event { x, y, at(time) };
        event.device = luil::pointer_device::touch;
        event.pointer_id = id;
        event.in_contact = true;
        return event;
    }

    [[nodiscard]] luil::pointer_released_event touch_release(const float x, const float y, const int time, const std::uint32_t id = 1)
    {
        luil::pointer_released_event event { x, y, luil::pointer_button::left, at(time) };
        event.device = luil::pointer_device::touch;
        event.pointer_id = id;
        return event;
    }

    [[nodiscard]] std::vector<luil::ink_stroke_input> strokes_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<luil::ink_stroke_input> found {};
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const stroke_seen* const seen { message->get<stroke_seen>() }; seen != nullptr)
                    found.push_back(seen->input);
        return found;
    }

    [[nodiscard]] std::vector<std::vector<std::uint64_t>> erases_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<std::vector<std::uint64_t>> found {};
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const erase_seen* const seen { message->get<erase_seen>() }; seen != nullptr)
                    found.push_back(seen->ids);
        return found;
    }

    [[nodiscard]] std::vector<std::u8string> names_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<std::u8string> found {};
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const named_seen* const seen { message->get<named_seen>() }; seen != nullptr)
                    found.push_back(seen->name);
        return found;
    }

    [[nodiscard]] std::vector<luil::ink_phase> phases_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<luil::ink_phase> found {};
        for (const luil::ink_stroke_input& input : strokes_of(actions))
            found.push_back(input.phase);
        return found;
    }

    // 획 하나가 든 묶음이다. 내용 (10, 50)에서 (100, 50)까지 폭 2다.
    [[nodiscard]] luil::ink_layer one_stroke()
    {
        return luil::ink_layer {}.with(luil::make_ink_stroke(1, { { 10.0f, 50.0f, 1.0f }, { 100.0f, 50.0f, 1.0f } }, { .width = 2.0f }));
    }

    // 누름 관찰을 세는 policy다.
    class press_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::vector<luil::input_action> on_press(const luil::ui_tree&, const luil::ui_element& element, const luil::pointer_pressed_event&) override
        {
            pressed.push_back(element.id().owner);
            return {};
        }

        std::vector<std::u8string> pressed {};
    };
}

TEST_CASE("A pen contact draws begin, extend and end with pressure and tilt", "[ink][canvas]")
{
    press_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(scene_tree({}));

    auto actions { controller.process(pen_press(30.0f, 40.0f, 0)) };
    auto seen { strokes_of(actions) };
    REQUIRE(seen.size() == 1u);
    REQUIRE(seen[0].phase == luil::ink_phase::begin);
    REQUIRE(seen[0].device == luil::pointer_device::pen);
    REQUIRE(seen[0].pointer_id == 9u);
    REQUIRE(seen[0].point.x == 30.0f);
    REQUIRE(seen[0].point.y == 40.0f);
    REQUIRE(seen[0].point.pressure == 0.5f);
    REQUIRE(seen[0].pressure_reported);
    REQUIRE(seen[0].tilt_x == 10.0f);
    REQUIRE(seen[0].tilt_y == -20.0f);
    REQUIRE(seen[0].time == at(0));
    // 누름 관찰은 다른 누름과 같다. 캔버스는 누름 표시를 세우지 않는다.
    REQUIRE(policy.pressed == std::vector<std::u8string> { u8"ink" });
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});

    seen = strokes_of(controller.process(pen_move(60.0f, 45.0f, 10, 0.8f)));
    REQUIRE(seen.size() == 1u);
    REQUIRE(seen[0].phase == luil::ink_phase::extend);
    REQUIRE(seen[0].point.x == 60.0f);
    REQUIRE(seen[0].point.pressure == 0.8f);
    // 쥔 동안 캔버스 밖으로 나간 점도 같은 획이다.
    seen = strokes_of(controller.process(pen_move(350.0f, 20.0f, 20)));
    REQUIRE(seen.size() == 1u);
    REQUIRE(seen[0].point.x == 350.0f);

    actions = controller.process(pen_release(350.0f, 20.0f, 30));
    seen = strokes_of(actions);
    REQUIRE(seen.size() == 1u);
    REQUIRE(seen[0].phase == luil::ink_phase::end);
    REQUIRE(seen[0].interrupted == false);
    // 단추 위에서 뗐어도 클릭이 아니다.
    REQUIRE(names_of(actions).empty());

    // 압력을 알리지 않는 펜은 `width` 그대로(압력 1)다.
    seen = strokes_of(controller.process(pen_press(30.0f, 40.0f, 100, std::nullopt)));
    REQUIRE(seen[0].point.pressure == 1.0f);
    REQUIRE(seen[0].pressure_reported == false);
}

TEST_CASE("Mouse draws only on canvases that turned it on", "[ink][canvas][mouse]")
{
    luil::interaction_controller controller {};
    controller.set_tree(scene_tree({ .clickable = true }));
    // 끈 캔버스는 일반 요소다. 클릭 경로를 탄다.
    REQUIRE(strokes_of(controller.process(luil::pointer_pressed_event { 30.0f, 40.0f, luil::pointer_button::left, at(0) })).empty());
    REQUIRE(strokes_of(controller.process(luil::pointer_moved_event { 32.0f, 40.0f, at(5) })).empty());
    auto actions { controller.process(luil::pointer_released_event { 32.0f, 40.0f, luil::pointer_button::left, at(10) }) };
    REQUIRE(strokes_of(actions).empty());
    REQUIRE(names_of(actions) == std::vector<std::u8string> { u8"canvas" });

    controller.set_tree(scene_tree({ .mouse = true, .clickable = true }));
    REQUIRE(phases_of(controller.process(luil::pointer_pressed_event { 30.0f, 40.0f, luil::pointer_button::left, at(1000) })) == std::vector { luil::ink_phase::begin });
    const auto moved { strokes_of(controller.process(luil::pointer_moved_event { 50.0f, 40.0f, at(1005) })) };
    REQUIRE(moved.size() == 1u);
    REQUIRE(moved[0].device == luil::pointer_device::mouse);
    REQUIRE(moved[0].pressure_reported == false);
    actions = controller.process(luil::pointer_released_event { 50.0f, 40.0f, luil::pointer_button::left, at(1010) });
    REQUIRE(phases_of(actions) == std::vector { luil::ink_phase::end });
    REQUIRE(names_of(actions).empty());
    // 오른쪽 버튼은 그리지 않는다.
    REQUIRE(strokes_of(controller.process(luil::pointer_pressed_event { 30.0f, 40.0f, luil::pointer_button::right, at(2000) })).empty());
}

TEST_CASE("Fingers scroll and pan around canvases that leave touch off", "[ink][canvas][touch]")
{
    SECTION("A scrolling window")
    {
        luil::interaction_controller controller {};
        controller.set_tree(scene_tree({ .place = host::area }));
        REQUIRE(strokes_of(controller.process(touch_press(60.0f, 120.0f, 0))).empty());
        const auto actions { controller.process(touch_move(60.0f, 90.0f, 30)) };
        REQUIRE(strokes_of(actions).empty());
        REQUIRE(names_of(actions) == std::vector<std::u8string> { u8"scroll" });
    }
    SECTION("A zoom view")
    {
        luil::interaction_controller controller {};
        controller.set_tree(scene_tree({ .place = host::zoom }));
        // 내용 경계가 없는 보기는 내용 원점을 보기 가운데에 둔다. 캔버스는 (150, 150)부터다.
        REQUIRE(strokes_of(controller.process(touch_press(200.0f, 200.0f, 0))).empty());
        std::vector<std::u8string> names {};
        for (int step { 1 }; step <= 4; ++step)
            for (const std::u8string& name : names_of(controller.process(touch_move(200.0f + 10.0f * static_cast<float>(step), 200.0f, 1000 * step))))
                names.push_back(name);
        REQUIRE(names.empty() == false);
        REQUIRE(names.back() == u8"pan");
    }
    SECTION("A canvas that takes fingers draws and does not scroll")
    {
        luil::interaction_controller controller {};
        controller.set_tree(scene_tree({ .place = host::area, .touch = true }));
        REQUIRE(phases_of(controller.process(touch_press(60.0f, 120.0f, 0))) == std::vector { luil::ink_phase::begin });
        auto actions { controller.process(touch_move(60.0f, 90.0f, 30)) };
        REQUIRE(phases_of(actions) == std::vector { luil::ink_phase::extend });
        REQUIRE(names_of(actions).empty());
        // 그리는 동안의 둘째 손가락은 삼킨다.
        REQUIRE(controller.process(touch_press(200.0f, 200.0f, 35, 2)).empty());
        REQUIRE(controller.process(touch_move(210.0f, 200.0f, 36, 2)).empty());
        actions = controller.process(touch_release(60.0f, 90.0f, 40));
        REQUIRE(phases_of(actions) == std::vector { luil::ink_phase::end });
        REQUIRE(names_of(actions).empty());
    }
}

TEST_CASE("A pen inside a zoom view draws instead of moving the view", "[ink][canvas][zoom]")
{
    luil::interaction_controller controller {};
    controller.set_tree(scene_tree({ .place = host::zoom }));
    // 내용 경계가 없는 보기는 내용 원점을 보기 가운데에 둔다. 캔버스는 (150, 150)부터다.
    const auto pressed { strokes_of(controller.process(pen_press(200.0f, 200.0f, 0))) };
    REQUIRE(pressed.size() == 1u);
    REQUIRE(pressed[0].phase == luil::ink_phase::begin);
    REQUIRE(pressed[0].point.x == 50.0f);
    const auto actions { controller.process(pen_move(260.0f, 200.0f, 10)) };
    REQUIRE(phases_of(actions) == std::vector { luil::ink_phase::extend });
    REQUIRE(names_of(actions).empty());
}

TEST_CASE("The eraser end and the eraser tool erase the strokes they touch", "[ink][canvas][eraser]")
{
    SECTION("Eraser end")
    {
        luil::interaction_controller controller {};
        controller.set_tree(scene_tree({ .strokes = one_stroke() }));
        // 획에서 멀리 누르면 닿은 것이 없다. 두 표본 사이를 쓸면 그 선분이 가로지른 획이 닿는다.
        auto actions { controller.process(pen_press(50.0f, 10.0f, 0, 0.5f, true)) };
        REQUIRE(actions.empty());
        actions = controller.process(pen_move(50.0f, 90.0f, 10, 0.5f, true));
        REQUIRE(erases_of(actions) == std::vector<std::vector<std::uint64_t>> { { 1 } });
        REQUIRE(strokes_of(actions).empty());
        REQUIRE(controller.process(pen_release(50.0f, 90.0f, 20, true)).empty());
    }
    SECTION("Eraser tool")
    {
        luil::interaction_controller controller {};
        controller.set_tree(scene_tree({ .tool = luil::ink_tool::eraser, .strokes = one_stroke() }));
        const auto actions { controller.process(pen_press(50.0f, 52.0f, 0)) };
        REQUIRE(erases_of(actions) == std::vector<std::vector<std::uint64_t>> { { 1 } });
        REQUIRE(strokes_of(actions).empty());
    }
    SECTION("A canvas that refuses the eraser swallows it like a control")
    {
        luil::interaction_controller controller {};
        controller.set_tree(scene_tree({ .strokes = one_stroke() }));
        // 캔버스가 아닌 자리(빈 곳)의 지우개는 아무 일도 하지 않는다.
        REQUIRE(controller.process(pen_press(390.0f, 390.0f, 0, 0.5f, true)).empty());
        REQUIRE(controller.process(pen_move(50.0f, 50.0f, 10, 0.5f, true)).empty());
        REQUIRE(controller.process(pen_release(50.0f, 50.0f, 20, true)).empty());
    }
}

TEST_CASE("An eraser over a button changes no click, focus, inertia or touch gesture", "[ink][canvas][eraser]")
{
    press_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(scene_tree({ .place = host::area }));
    // 다른 단추를 눌러 초점을 둔다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 340.0f, 80.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_released_event { 340.0f, 80.0f, luil::pointer_button::left, at(5) }));
    const luil::ui_element_id focused { controller.snapshot().focused };
    REQUIRE(focused == luil::ui_element_id { kind_button, u8"other" });
    policy.pressed.clear();

    SECTION("Click and focus")
    {
        REQUIRE(controller.process(pen_press(340.0f, 20.0f, 100, 0.5f, true)).empty());
        REQUIRE(controller.process(pen_move(342.0f, 20.0f, 110, 0.5f, true)).empty());
        REQUIRE(controller.process(pen_release(342.0f, 20.0f, 120, true)).empty());
        REQUIRE(controller.snapshot().focused == focused);
        REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});
        REQUIRE(policy.pressed.empty());
        // 그 뒤의 펜촉 클릭은 지금처럼 클릭이다.
        static_cast<void>(controller.process(pen_press(340.0f, 20.0f, 200)));
        REQUIRE(names_of(controller.process(pen_release(340.0f, 20.0f, 210))) == std::vector<std::u8string> { u8"button" });
    }
    SECTION("Inertia")
    {
        static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 100)));
        static_cast<void>(controller.process(touch_move(60.0f, 90.0f, 130)));
        static_cast<void>(controller.process(touch_release(60.0f, 80.0f, 140)));
        REQUIRE(controller.next_deadline().has_value());
        REQUIRE(controller.process(pen_press(340.0f, 20.0f, 145, 0.5f, true)).empty());
        REQUIRE(names_of(controller.advance(at(156))) == std::vector<std::u8string> { u8"scroll" });
    }
    SECTION("Touch gesture")
    {
        static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 100)));
        REQUIRE(controller.process(pen_press(340.0f, 20.0f, 105, 0.5f, true)).empty());
        REQUIRE(names_of(controller.process(touch_move(60.0f, 90.0f, 130))) == std::vector<std::u8string> { u8"scroll" });
    }
}

TEST_CASE("Lost canvases, system cancels, barrel switches and queue loss close the stroke", "[ink][canvas][cancel]")
{
    luil::interaction_controller controller {};
    controller.set_tree(scene_tree({}));

    SECTION("The canvas disappears")
    {
        static_cast<void>(controller.process(pen_press(30.0f, 40.0f, 0)));
        controller.set_tree(scene_tree({ .canvas = false }));
        REQUIRE(phases_of(controller.process(pen_move(40.0f, 40.0f, 10))) == std::vector { luil::ink_phase::cancel });
        // 접촉의 나머지는 삼킨다. 단추 위에서 떼도 클릭이 아니다.
        REQUIRE(controller.process(pen_move(340.0f, 20.0f, 20)).empty());
        REQUIRE(controller.process(pen_release(340.0f, 20.0f, 30)).empty());
    }
    SECTION("The canvas is disabled, hidden or its surface tree arrives without it")
    {
        static_cast<void>(controller.process(pen_press(30.0f, 40.0f, 0)));
        controller.set_tree(scene_tree({ .enabled = false }));
        REQUIRE(phases_of(controller.process(pen_move(40.0f, 40.0f, 10))) == std::vector { luil::ink_phase::cancel });
        controller.set_tree(scene_tree({}));
        static_cast<void>(controller.process(pen_press(30.0f, 40.0f, 100)));
        controller.set_tree(scene_tree({ .visible = false }));
        // 표면이 사라지면 이벤트가 더 오지 않는다. 새 tree를 받은 뒤의 재판정이 닫는다.
        REQUIRE(phases_of(controller.refresh_hover_tracking()) == std::vector { luil::ink_phase::cancel });
        REQUIRE(controller.process(pen_move(40.0f, 40.0f, 110)).empty());
    }
    SECTION("A system cancel drops the stroke")
    {
        static_cast<void>(controller.process(pen_press(30.0f, 40.0f, 0)));
        REQUIRE(phases_of(controller.process(luil::pointer_cancelled_event { luil::pointer_device::pen, 9, {}, at(10) })) == std::vector { luil::ink_phase::cancel });
    }
    SECTION("A barrel switch keeps the received stroke and swallows the rest")
    {
        static_cast<void>(controller.process(pen_press(30.0f, 40.0f, 0)));
        static_cast<void>(controller.process(pen_move(50.0f, 40.0f, 5)));
        const auto ended { strokes_of(controller.process(luil::pointer_cancelled_event { luil::pointer_device::pen, 9, {}, at(10), true })) };
        REQUIRE(ended.size() == 1u);
        REQUIRE(ended[0].phase == luil::ink_phase::end);
        REQUIRE(ended[0].interrupted);
        REQUIRE(ended[0].point.x == 50.0f);
        // 뒤따르는 오른쪽 다시 누름과 그 이동·뗌은 삼킨다 (우클릭이 아니다).
        luil::pointer_pressed_event right { pen_press(340.0f, 20.0f, 11) };
        right.button = luil::pointer_button::right;
        REQUIRE(controller.process(right).empty());
        REQUIRE(controller.process(pen_move(340.0f, 20.0f, 12)).empty());
        REQUIRE(controller.process(pen_release(340.0f, 20.0f, 13, false, luil::pointer_button::right)).empty());
        // 새 접촉은 다시 그린다.
        REQUIRE(phases_of(controller.process(pen_press(30.0f, 40.0f, 100))) == std::vector { luil::ink_phase::begin });
    }
    SECTION("Input queue loss keeps the received stroke and swallows the rest")
    {
        static_cast<void>(controller.process(pen_press(30.0f, 40.0f, 0)));
        static_cast<void>(controller.process(pen_move(70.0f, 40.0f, 5)));
        const auto ended { strokes_of(controller.cancel_dropped_gestures()) };
        REQUIRE(ended.size() == 1u);
        REQUIRE(ended[0].phase == luil::ink_phase::end);
        REQUIRE(ended[0].interrupted);
        REQUIRE(ended[0].point.x == 70.0f);
        REQUIRE(controller.process(pen_move(80.0f, 40.0f, 10)).empty());
        REQUIRE(controller.process(pen_release(80.0f, 40.0f, 20)).empty());
    }
}

TEST_CASE("A pen press cancels a finger stroke that began first", "[ink][canvas][touch]")
{
    luil::interaction_controller controller {};
    controller.set_tree(scene_tree({ .touch = true }));
    REQUIRE(phases_of(controller.process(touch_press(200.0f, 200.0f, 0))) == std::vector { luil::ink_phase::begin });
    const auto seen { strokes_of(controller.process(pen_press(30.0f, 40.0f, 10))) };
    REQUIRE(seen.size() == 2u);
    REQUIRE(seen[0].phase == luil::ink_phase::cancel);
    REQUIRE(seen[0].device == luil::pointer_device::touch);
    REQUIRE(seen[1].phase == luil::ink_phase::begin);
    REQUIRE(seen[1].device == luil::pointer_device::pen);
    // 손바닥의 나머지 이벤트는 삼킨다. 펜이 쥔 동안의 터치 누름도 삼킨다.
    REQUIRE(controller.process(touch_move(210.0f, 200.0f, 20)).empty());
    REQUIRE(controller.process(touch_release(210.0f, 200.0f, 30)).empty());
    REQUIRE(controller.process(touch_press(100.0f, 100.0f, 40, 3)).empty());
    REQUIRE(phases_of(controller.process(pen_move(40.0f, 40.0f, 50))) == std::vector { luil::ink_phase::extend });
}

TEST_CASE("Ink canvases are named images that summarize their stroke count", "[ink][canvas][access]")
{
    luil::ink_layer layer { one_stroke() };
    layer = layer.with(luil::make_ink_stroke(2, { { 0.0f, 0.0f, 1.0f } }, {}));
    const std::shared_ptr<const luil::ui_tree> tree { scene_tree({ .strokes = layer }) };
    const luil::ui_element* const canvas { tree->find({ luil::ui_element_kind::ink_canvas, u8"ink" }) };
    REQUIRE(canvas != nullptr);
    const luil::access_info info { canvas->accessibility() };
    REQUIRE(info.role == luil::access_role::image);
    REQUIRE(info.name == u8"Sketch");
    REQUIRE(info.value == u8"strokes 2");
    // Tab 자리가 아니다. 키보드로 그리지 않는다.
    REQUIRE(canvas->tab_stop() == false);

    // 설정의 이름이 비면 요소의 접근 이름으로 물러선다.
    luil::ink_canvas_element named { luil::ink_canvas_config { .owner = u8"named" } };
    named.set_access_name(u8"Signature");
    REQUIRE(named.accessibility().name == u8"Signature");

    // 이름과 요약을 어느 쪽도 주지 않으면 라이브러리 기본 문구다 (default-text-design.md).
    const luil::ink_canvas_element plain { luil::ink_canvas_config { .owner = u8"plain", .strokes = layer } };
    REQUIRE(plain.accessibility().name == u8"잉크 캔버스");
    REQUIRE(plain.accessibility().value == u8"획 2개");
    REQUIRE(named.accessibility().value == u8"획 0개");
}
