#include "luil/ui/ui_interaction.h"

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 포인터 동작이 싣는 수식 키(Ctrl·Shift·Alt)다 (`ui_action_context`).
// 클릭은 뗀 순간, 끌기 손잡이는 그 콜백의 순간, 끌기의 시작은 임계 거리를 넘은 이동의 순간, 놓기는 놓는 순간이다.
namespace {
    constexpr luil::ui_element_kind kind_row { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_handle { luil::application_element_kind(1) };
    constexpr luil::ui_element_kind kind_folder { luil::application_element_kind(2) };

    struct modifiers
    {
        bool control { false };
        bool shift { false };
        bool alt { false };
    };

    constexpr modifiers none {};
    constexpr modifiers control { true, false, false };
    constexpr modifiers shift { false, true, false };
    constexpr modifiers alt { false, false, true };
    constexpr modifiers control_shift { true, true, false };
    constexpr modifiers shift_alt { false, true, true };

    // 콜백 하나가 받은 문맥이다.
    struct call
    {
        std::u8string name {};
        luil::ui_action_context context {};
    };

    [[nodiscard]] bool same(const luil::ui_action_context& context, const modifiers expected)
    {
        return context.control == expected.control && context.shift == expected.shift && context.alt == expected.alt;
    }

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

    [[nodiscard]] luil::pointer_pressed_event press(const float x, const float y, const int time, const modifiers keys = none, const luil::pointer_button button = luil::pointer_button::left,
        const luil::pointer_device device = luil::pointer_device::mouse)
    {
        luil::pointer_pressed_event event { x, y, button, at(time), keys.shift };
        event.device = device;
        event.control = keys.control;
        event.alt = keys.alt;
        return event;
    }

    [[nodiscard]] luil::pointer_moved_event move(const float x, const float y, const int time, const modifiers keys = none, const luil::pointer_device device = luil::pointer_device::mouse)
    {
        luil::pointer_moved_event event { x, y, at(time) };
        event.device = device;
        event.in_contact = device != luil::pointer_device::mouse;
        event.control = keys.control;
        event.shift = keys.shift;
        event.alt = keys.alt;
        return event;
    }

    [[nodiscard]] luil::pointer_released_event release(const float x, const float y, const int time, const modifiers keys = none, const luil::pointer_button button = luil::pointer_button::left,
        const luil::pointer_device device = luil::pointer_device::mouse)
    {
        luil::pointer_released_event event { x, y, button, at(time) };
        event.device = device;
        event.control = keys.control;
        event.shift = keys.shift;
        event.alt = keys.alt;
        return event;
    }

    [[nodiscard]] luil::ui_action record(std::vector<call>& calls, std::u8string name)
    {
        return [&calls, name = std::move(name)](const luil::ui_action_context& context) -> std::vector<luil::input_action> {
            calls.push_back({ name, context });
            return {};
        };
    }

    // 위: 클릭·더블 클릭·우클릭을 받는 행 (10,10)–(110,40)
    // 가운데: 끌기 손잡이 (10,60)–(110,90)
    // 아래 왼쪽: 끌 수 있는 행, 아래 오른쪽: 놓을 수 있는 폴더 행
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_tree(std::vector<call>& calls)
    {
        auto root { std::make_unique<panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });

        auto row { std::make_unique<panel>(luil::ui_element_id { kind_row, u8"row" }) };
        row->arrange({ { 10.0f, 10.0f, 100.0f, 30.0f }, 1.0f });
        row->set_action(luil::ui_trigger::left_click, record(calls, u8"click"));
        row->set_action(luil::ui_trigger::double_click, record(calls, u8"double"));
        row->set_action(luil::ui_trigger::right_click, record(calls, u8"right"));
        root->add(std::move(row));

        auto handle { std::make_unique<panel>(luil::ui_element_id { kind_handle, u8"handle" }) };
        handle->arrange({ { 10.0f, 60.0f, 100.0f, 30.0f }, 1.0f });
        luil::pointer_drag_target target {};
        target.on_press = record(calls, u8"press");
        target.on_move = [&calls](const luil::ui_action_context& previous, const luil::ui_action_context& current) -> std::vector<luil::input_action> {
            calls.push_back({ u8"move-previous", previous });
            calls.push_back({ u8"move", current });
            return {};
        };
        target.on_release = [&calls](const luil::ui_action_context& context, luil::drag_velocity) -> std::vector<luil::input_action> {
            calls.push_back({ u8"release", context });
            return {};
        };
        handle->set_pointer_drag_target(std::move(target));
        root->add(std::move(handle));

        auto item { std::make_unique<panel>(luil::ui_element_id { kind_row, u8"item" }) };
        item->arrange({ { 10.0f, 200.0f, 100.0f, 30.0f }, 1.0f });
        luil::drag_source source {};
        source.make_payload = [&calls](const luil::ui_action_context& context) {
            calls.push_back({ u8"payload", context });
            luil::drag_payload payload {};
            payload.source = context.element;
            payload.dragged_owner = u8"item";
            return payload;
        };
        item->set_drag_source(std::move(source));
        root->add(std::move(item));

        auto folder { std::make_unique<panel>(luil::ui_element_id { kind_folder, u8"folder" }) };
        folder->arrange({ { 200.0f, 200.0f, 100.0f, 30.0f }, 1.0f });
        luil::drop_target drop {};
        drop.accepts = [](const luil::drag_payload& payload) { return payload.dragged_owner == u8"item"; };
        drop.on_drop = [&calls](const luil::drag_payload&, const luil::ui_action_context& context) -> std::vector<luil::input_action> {
            calls.push_back({ u8"drop", context });
            return {};
        };
        folder->set_drop_target(std::move(drop));
        root->add(std::move(folder));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }
} // namespace

TEST_CASE("A click carries the modifiers held when the button went up", "[ui][interaction][modifiers]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    // Ctrl을 누른 채 누르고 뗀 클릭이다.
    static_cast<void>(controller.process(press(20.0f, 20.0f, 0, control)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 50, control)));
    REQUIRE(calls.size() == 1u);
    REQUIRE(calls[0].name == u8"click");
    REQUIRE(same(calls[0].context, control));
    REQUIRE(calls[0].context.element == luil::ui_element_id { kind_row, u8"row" });

    // 누른 뒤에 Shift를 더 눌러 뗐다 — 뗀 순간의 값이 이긴다.
    calls.clear();
    static_cast<void>(controller.process(press(20.0f, 20.0f, 2000, none)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 2050, shift)));
    REQUIRE(calls.size() == 1u);
    REQUIRE(same(calls[0].context, shift));

    // 누를 때 눌렀던 Alt를 먼저 떼고 놓으면 수식 키가 없다.
    calls.clear();
    static_cast<void>(controller.process(press(20.0f, 20.0f, 4000, alt)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 4050, none)));
    REQUIRE(calls.size() == 1u);
    REQUIRE(same(calls[0].context, none));
}

TEST_CASE("A double click and a right click carry their release modifiers", "[ui][interaction][modifiers]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    static_cast<void>(controller.process(press(20.0f, 20.0f, 0)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 30)));
    static_cast<void>(controller.process(press(20.0f, 20.0f, 100, shift_alt)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 130, shift_alt)));
    REQUIRE(calls.size() == 2u);
    REQUIRE(calls[0].name == u8"click");
    REQUIRE(same(calls[0].context, none));
    REQUIRE(calls[1].name == u8"double");
    REQUIRE(same(calls[1].context, shift_alt));

    calls.clear();
    static_cast<void>(controller.process(press(20.0f, 20.0f, 3000, alt, luil::pointer_button::right)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 3050, control_shift, luil::pointer_button::right)));
    REQUIRE(calls.size() == 1u);
    REQUIRE(calls[0].name == u8"right");
    REQUIRE(same(calls[0].context, control_shift));
}

TEST_CASE("A drag handle hears the modifiers of each callback's moment", "[ui][interaction][modifiers]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    static_cast<void>(controller.process(press(20.0f, 70.0f, 0, control)));
    REQUIRE(calls.size() == 1u);
    REQUIRE(calls[0].name == u8"press");
    REQUIRE(same(calls[0].context, control));

    // 앞 문맥은 앞 순간(누름)의 Ctrl, 지금 문맥은 이 이동의 Shift다.
    static_cast<void>(controller.process(move(40.0f, 70.0f, 20, shift)));
    REQUIRE(calls.size() == 3u);
    REQUIRE(calls[1].name == u8"move-previous");
    REQUIRE(same(calls[1].context, control));
    REQUIRE(calls[2].name == u8"move");
    REQUIRE(same(calls[2].context, shift));

    static_cast<void>(controller.process(move(60.0f, 70.0f, 40, alt)));
    REQUIRE(same(calls[3].context, shift));
    REQUIRE(same(calls[4].context, alt));

    static_cast<void>(controller.process(release(60.0f, 70.0f, 60, control_shift)));
    REQUIRE(calls.size() == 6u);
    REQUIRE(calls[5].name == u8"release");
    REQUIRE(same(calls[5].context, control_shift));
}

TEST_CASE("A cancelled drag handle ends with the last modifiers it saw", "[ui][interaction][modifiers]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    static_cast<void>(controller.process(press(20.0f, 70.0f, 0, none, luil::pointer_button::left, luil::pointer_device::pen)));
    static_cast<void>(controller.process(move(30.0f, 70.0f, 20, alt, luil::pointer_device::pen)));
    calls.clear();
    static_cast<void>(controller.process(luil::pointer_cancelled_event { luil::pointer_device::pen, 0, {}, at(40) }));
    REQUIRE(calls.size() == 1u);
    REQUIRE(calls[0].name == u8"release");
    REQUIRE(same(calls[0].context, alt));
}

TEST_CASE("A drag payload is made with the start's modifiers and dropped with the release's", "[ui][interaction][modifiers]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    static_cast<void>(controller.process(press(20.0f, 210.0f, 0, shift)));
    // 임계 거리를 넘는 이동이 끌기를 세운다. 자리는 누른 자리, 수식 키는 이 순간이다.
    static_cast<void>(controller.process(move(40.0f, 210.0f, 20, control)));
    REQUIRE(calls.size() == 1u);
    REQUIRE(calls[0].name == u8"payload");
    REQUIRE(same(calls[0].context, control));
    REQUIRE(calls[0].context.x == 20.0f);
    REQUIRE(controller.snapshot().drag.has_value());

    static_cast<void>(controller.process(move(220.0f, 210.0f, 40, control)));
    // 놓는 순간 Ctrl을 떼면 이동, 쥐고 있으면 복사다 — 앱은 놓는 순간의 값을 본다.
    static_cast<void>(controller.process(release(220.0f, 210.0f, 60, alt)));
    REQUIRE(calls.size() == 2u);
    REQUIRE(calls[1].name == u8"drop");
    REQUIRE(same(calls[1].context, alt));
    REQUIRE(calls[1].context.element == luil::ui_element_id { kind_folder, u8"folder" });

    calls.clear();
    static_cast<void>(controller.process(press(20.0f, 210.0f, 1000)));
    static_cast<void>(controller.process(move(40.0f, 210.0f, 1020)));
    static_cast<void>(controller.process(move(220.0f, 210.0f, 1040)));
    static_cast<void>(controller.process(release(220.0f, 210.0f, 1060, control)));
    REQUIRE(calls.size() == 2u);
    REQUIRE(same(calls[0].context, none));
    REQUIRE(same(calls[1].context, control));
}

TEST_CASE("Touch taps, long presses and drops carry the hardware keyboard's modifiers", "[ui][interaction][modifiers][touch]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    static_cast<void>(controller.process(press(20.0f, 20.0f, 0, none, luil::pointer_button::left, luil::pointer_device::touch)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 50, control, luil::pointer_button::left, luil::pointer_device::touch)));
    REQUIRE(calls.size() == 1u);
    REQUIRE(calls[0].name == u8"click");
    REQUIRE(same(calls[0].context, control));

    // 길게 누르기는 이벤트 없이 시간이 찬다. 마지막 이벤트의 수식 키다.
    calls.clear();
    static_cast<void>(controller.process(press(20.0f, 20.0f, 2000, none, luil::pointer_button::left, luil::pointer_device::touch)));
    static_cast<void>(controller.process(move(21.0f, 20.0f, 2010, shift, luil::pointer_device::touch)));
    static_cast<void>(controller.advance(at(2700)));
    REQUIRE(calls.size() == 1u);
    REQUIRE(calls[0].name == u8"right");
    REQUIRE(same(calls[0].context, shift));
    static_cast<void>(controller.process(release(21.0f, 20.0f, 2800, none, luil::pointer_button::left, luil::pointer_device::touch)));

    // 터치 끌기(스크롤 후보가 없는 자리)의 놓기도 놓는 순간이다.
    calls.clear();
    static_cast<void>(controller.process(press(20.0f, 210.0f, 5000, none, luil::pointer_button::left, luil::pointer_device::touch)));
    static_cast<void>(controller.process(move(50.0f, 210.0f, 5020, alt, luil::pointer_device::touch)));
    static_cast<void>(controller.process(move(220.0f, 210.0f, 5040, alt, luil::pointer_device::touch)));
    static_cast<void>(controller.process(release(220.0f, 210.0f, 5060, control, luil::pointer_button::left, luil::pointer_device::touch)));
    REQUIRE(calls.size() == 2u);
    REQUIRE(calls[0].name == u8"payload");
    REQUIRE(same(calls[0].context, alt));
    REQUIRE(calls[1].name == u8"drop");
    REQUIRE(same(calls[1].context, control));
}

TEST_CASE("Shift+Space runs the focused control with Shift", "[ui][interaction][modifiers][keyboard]")
{
    std::vector<call> calls {};
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(calls));

    // 눌러서 초점을 세운다 (클릭은 기록에서 지운다).
    static_cast<void>(controller.process(press(20.0f, 20.0f, 0)));
    static_cast<void>(controller.process(release(20.0f, 20.0f, 10)));
    calls.clear();

    luil::key_pressed_event space { luil::key_code::space, false, true, false, false, at(1000) };
    static_cast<void>(controller.process(space));
    REQUIRE(calls.size() == 1u);
    REQUIRE(calls[0].name == u8"click");
    REQUIRE(same(calls[0].context, shift));
}
