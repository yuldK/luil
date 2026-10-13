#include "luil/ui/ui_interaction.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 시작 거리를 둔 끌기 손잡이다 (`pointer_drag_target::start_distance`).
// 목록 행처럼 누르고 떼면 클릭이고, 누른 채 그 거리를 넘기면 끌기다.
namespace {
    constexpr luil::ui_element_kind kind_row { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_list { luil::application_element_kind(1) };

    struct call
    {
        std::u8string name {};
        luil::ui_action_context context {};
        luil::ui_action_context previous {};
    };

    struct scrolled
    {
        float delta { 0.0f };
    };

    class panel final : public luil::ui_element
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
        {}
    };

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] luil::pointer_pressed_event press(
        const float x, const float y, const int time, const luil::pointer_device device = luil::pointer_device::mouse, const luil::pointer_button button = luil::pointer_button::left)
    {
        luil::pointer_pressed_event event { x, y, button, at(time) };
        event.device = device;
        return event;
    }

    [[nodiscard]] luil::pointer_moved_event move(const float x, const float y, const int time, const luil::pointer_device device = luil::pointer_device::mouse)
    {
        luil::pointer_moved_event event { x, y, at(time) };
        event.device = device;
        event.in_contact = device != luil::pointer_device::mouse;
        return event;
    }

    [[nodiscard]] luil::pointer_released_event release(
        const float x, const float y, const int time, const luil::pointer_device device = luil::pointer_device::mouse, const luil::pointer_button button = luil::pointer_button::left)
    {
        luil::pointer_released_event event { x, y, button, at(time) };
        event.device = device;
        return event;
    }

    [[nodiscard]] luil::ui_action record(std::vector<call>& calls, std::u8string name)
    {
        return [&calls, name = std::move(name)](const luil::ui_action_context& context) -> std::vector<luil::input_action> {
            calls.push_back({ name, context, {} });
            return {};
        };
    }

    [[nodiscard]] std::vector<std::u8string> names(const std::vector<call>& calls)
    {
        std::vector<std::u8string> result {};
        for (const call& value : calls)
            result.push_back(value.name);
        return result;
    }

    // 세로로 흐르는 목록(0,0)–(200,400) 안의 행 (10,10)–(190,40)이다.
    // 행은 클릭·더블 클릭·우클릭과 시작 거리 8px의 끌기 손잡이를 함께 든다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_tree(std::vector<call>& calls, const bool scrolls = false, const bool right_click = true)
    {
        auto root { std::make_unique<panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
        auto list { std::make_unique<panel>(luil::ui_element_id { kind_list, u8"list" }) };
        list->arrange({ { 0.0f, 0.0f, 200.0f, 400.0f }, 1.0f });
        if (scrolls)
        {
            luil::scroll_source source {};
            source.scroll = [](const float delta) { return luil::make_app_action(scrolled { delta }); };
            list->set_scroll_source(std::move(source));
        }

        auto row { std::make_unique<panel>(luil::ui_element_id { kind_row, u8"row" }) };
        row->arrange({ { 10.0f, 10.0f, 180.0f, 30.0f }, 1.0f });
        row->set_action(luil::ui_trigger::left_click, record(calls, u8"click"));
        row->set_action(luil::ui_trigger::double_click, record(calls, u8"double"));
        if (right_click)
            row->set_action(luil::ui_trigger::right_click, record(calls, u8"right"));
        luil::pointer_drag_target target {};
        target.start_distance = 8.0f;
        target.on_press = record(calls, u8"press");
        target.on_move = [&calls](const luil::ui_action_context& previous, const luil::ui_action_context& current) -> std::vector<luil::input_action> {
            calls.push_back({ u8"move", current, previous });
            return {};
        };
        target.on_release = [&calls](const luil::ui_action_context& context, luil::drag_velocity) -> std::vector<luil::input_action> {
            calls.push_back({ u8"release", context, {} });
            return {};
        };
        row->set_pointer_drag_target(std::move(target));
        list->add(std::move(row));
        root->add(std::move(list));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }
} // namespace

TEST_CASE("A press released within the start distance is a click, not a drag", "[ui][interaction][pointer-drag]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    static_cast<void>(controller.process(press(20.0f, 20.0f, 0)));
    // 누르는 순간에는 아무것도 부르지 않는다. 누름 표시는 선다.
    REQUIRE(calls.empty());
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id { kind_row, u8"row" });
    // 거리 안의 떨림이다.
    static_cast<void>(controller.process(move(25.0f, 23.0f, 10)));
    REQUIRE(calls.empty());
    static_cast<void>(controller.process(release(25.0f, 23.0f, 20)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"click" });
    // 누른 element가 초점을 받는다 (보통 누름과 같다).
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_row, u8"row" });
}

TEST_CASE("Two quick clicks within the start distance are a double click", "[ui][interaction][pointer-drag]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    static_cast<void>(controller.process(press(20.0f, 20.0f, 0)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 30)));
    static_cast<void>(controller.process(press(21.0f, 20.0f, 120)));
    static_cast<void>(controller.process(move(22.0f, 21.0f, 130)));
    static_cast<void>(controller.process(release(22.0f, 21.0f, 150)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"click", u8"double" });

    // 우클릭도 그대로다 (끌기는 왼쪽 버튼만이다).
    calls.clear();
    static_cast<void>(controller.process(press(20.0f, 20.0f, 2000, luil::pointer_device::mouse, luil::pointer_button::right)));
    static_cast<void>(controller.process(move(60.0f, 20.0f, 2010)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 2020, luil::pointer_device::mouse, luil::pointer_button::right)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"right" });
}

TEST_CASE("Crossing the start distance presses at the start and moves to the pointer", "[ui][interaction][pointer-drag]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    luil::pointer_pressed_event down { press(20.0f, 20.0f, 0) };
    down.control = true;
    static_cast<void>(controller.process(down));
    static_cast<void>(controller.process(move(24.0f, 20.0f, 10)));
    REQUIRE(calls.empty());

    luil::pointer_moved_event crossing { move(30.0f, 26.0f, 20) };
    crossing.shift = true;
    static_cast<void>(controller.process(crossing));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"press", u8"move" });
    // 누름은 누른 자리와 누른 순간의 수식 키다.
    REQUIRE(calls[0].context.x == 20.0f);
    REQUIRE(calls[0].context.y == 20.0f);
    REQUIRE(calls[0].context.control);
    REQUIRE(calls[0].context.shift == false);
    // 넘은 이동은 누른 자리부터의 이동이다.
    REQUIRE(calls[1].previous.x == 20.0f);
    REQUIRE(calls[1].previous.y == 20.0f);
    REQUIRE(calls[1].context.x == 30.0f);
    REQUIRE(calls[1].context.y == 26.0f);
    REQUIRE(calls[1].context.shift);

    // 그 뒤로는 지금 계약 그대로다. element 밖으로 나가도 이어진다.
    static_cast<void>(controller.process(move(80.0f, 120.0f, 30)));
    REQUIRE(calls.size() == 3u);
    REQUIRE(calls[2].previous.x == 30.0f);
    REQUIRE(calls[2].context.y == 120.0f);

    static_cast<void>(controller.process(release(80.0f, 120.0f, 40)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"press", u8"move", u8"move", u8"release" });
    REQUIRE(calls[3].context.x == 80.0f);
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});
}

TEST_CASE("A drag that returns to its start and releases is still not a click", "[ui][interaction][pointer-drag]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    static_cast<void>(controller.process(press(20.0f, 20.0f, 0)));
    static_cast<void>(controller.process(move(40.0f, 20.0f, 10)));
    static_cast<void>(controller.process(move(20.0f, 20.0f, 20)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 30)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"press", u8"move", u8"move", u8"release" });

    // 다음 클릭이 앞의 끌기와 이어져 더블 클릭이 되지도 않는다.
    calls.clear();
    static_cast<void>(controller.process(press(20.0f, 20.0f, 100)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 120)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"click" });
}

TEST_CASE("A drag between two clicks breaks the click streak", "[ui][interaction][pointer-drag]")
{
    // 클릭 → 끌기 → 같은 자리 클릭이 더블 클릭 시간 안에 와도 끌기가 앞 클릭의 연타 기록을 지운다.
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    static_cast<void>(controller.process(press(20.0f, 20.0f, 0)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 10)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"click" });

    calls.clear();
    static_cast<void>(controller.process(press(20.0f, 20.0f, 100)));
    static_cast<void>(controller.process(move(40.0f, 20.0f, 110)));
    static_cast<void>(controller.process(release(40.0f, 20.0f, 200)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"press", u8"move", u8"release" });

    calls.clear();
    static_cast<void>(controller.process(press(20.0f, 20.0f, 300)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 310)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"click" });
}

TEST_CASE("Pens use the same start distance as the mouse", "[ui][interaction][pointer-drag][pen]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    static_cast<void>(controller.process(press(20.0f, 20.0f, 0, luil::pointer_device::pen)));
    static_cast<void>(controller.process(move(23.0f, 20.0f, 10, luil::pointer_device::pen)));
    static_cast<void>(controller.process(release(23.0f, 20.0f, 20, luil::pointer_device::pen)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"click" });

    calls.clear();
    static_cast<void>(controller.process(press(20.0f, 20.0f, 1000, luil::pointer_device::pen)));
    static_cast<void>(controller.process(move(20.0f, 40.0f, 1010, luil::pointer_device::pen)));
    static_cast<void>(controller.process(release(20.0f, 40.0f, 1020, luil::pointer_device::pen)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"press", u8"move", u8"release" });
}

TEST_CASE("A touch tap on a distance handle is a click and a double tap a double click", "[ui][interaction][pointer-drag][touch]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls, true));

    static_cast<void>(controller.process(press(20.0f, 20.0f, 0, luil::pointer_device::touch)));
    static_cast<void>(controller.process(move(24.0f, 22.0f, 10, luil::pointer_device::touch)));
    static_cast<void>(controller.process(release(24.0f, 22.0f, 30, luil::pointer_device::touch)));
    static_cast<void>(controller.process(press(22.0f, 20.0f, 150, luil::pointer_device::touch)));
    static_cast<void>(controller.process(release(22.0f, 20.0f, 170, luil::pointer_device::touch)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"click", u8"double" });
}

TEST_CASE("A fast touch swipe scrolls the list instead of dragging the row", "[ui][interaction][pointer-drag][touch]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls, true));

    static_cast<void>(controller.process(press(20.0f, 20.0f, 0, luil::pointer_device::touch)));
    const auto actions { controller.process(move(20.0f, 60.0f, 50, luil::pointer_device::touch)) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<scrolled>() != nullptr);
    static_cast<void>(controller.process(release(20.0f, 60.0f, 80, luil::pointer_device::touch)));
    REQUIRE(calls.empty());
}

TEST_CASE("A held touch that then moves past the tolerance drags the row", "[ui][interaction][pointer-drag][touch]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    // 우클릭이 없는 행이라 오래 눌러도 메뉴가 서지 않는다.
    controller.set_tree(make_tree(calls, true, false));

    static_cast<void>(controller.process(press(20.0f, 20.0f, 0, luil::pointer_device::touch)));
    static_cast<void>(controller.advance(at(700)));
    // 스크롤의 시간 창(400ms)이 지난 뒤의 이동이다.
    static_cast<void>(controller.process(move(20.0f, 50.0f, 800, luil::pointer_device::touch)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"press", u8"move" });
    REQUIRE(calls[0].context.y == 20.0f);
    REQUIRE(calls[1].previous.y == 20.0f);
    REQUIRE(calls[1].context.y == 50.0f);
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_row, u8"row" });

    static_cast<void>(controller.process(move(20.0f, 20.0f, 820, luil::pointer_device::touch)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 840, luil::pointer_device::touch)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"press", u8"move", u8"move", u8"release" });
}

TEST_CASE("A held touch with a menu becomes a long press, not a drag", "[ui][interaction][pointer-drag][touch]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls, true));

    static_cast<void>(controller.process(press(20.0f, 20.0f, 0, luil::pointer_device::touch)));
    static_cast<void>(controller.advance(at(700)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"right" });
    // 메뉴가 선 접촉의 이동·뗌은 삼킨다.
    static_cast<void>(controller.process(move(20.0f, 60.0f, 800, luil::pointer_device::touch)));
    static_cast<void>(controller.process(release(20.0f, 60.0f, 820, luil::pointer_device::touch)));
    REQUIRE(names(calls) == std::vector<std::u8string> { u8"right" });
}
