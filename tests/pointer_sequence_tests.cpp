#include "host/pointer_sequence.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <variant>

namespace {
    using namespace luil;

    [[nodiscard]] pointer_sample make_sample(const pointer_phase phase, const pointer_device device, const bool in_contact, const float x = 30.0f)
    {
        pointer_sample sample {};
        sample.phase = phase;
        sample.device = device;
        sample.pointer_id = 7;
        sample.in_contact = in_contact;
        sample.x = x;
        sample.y = 40.0f;
        sample.scale = 1.5f;
        sample.time = std::chrono::steady_clock::time_point { std::chrono::milliseconds { 100 } };
        sample.surface = u8"tools";
        return sample;
    }
}

TEST_CASE("A touch contact becomes a press, moves and a release with its id and scale", "[pointer-sequence][touch]")
{
    pointer_sequence_tracker tracker {};
    auto events { tracker.accept(make_sample(pointer_phase::down, pointer_device::touch, true)) };
    REQUIRE(events.size() == 1u);
    const auto* const pressed { std::get_if<pointer_pressed_event>(&events[0]) };
    REQUIRE(pressed != nullptr);
    REQUIRE(pressed->device == pointer_device::touch);
    REQUIRE(pressed->pointer_id == 7u);
    REQUIRE(pressed->scale == 1.5f);
    REQUIRE(pressed->surface == u8"tools");
    REQUIRE(tracker.in_contact(7));

    events = tracker.accept(make_sample(pointer_phase::update, pointer_device::touch, true, 50.0f));
    REQUIRE(events.size() == 1u);
    const auto* const moved { std::get_if<pointer_moved_event>(&events[0]) };
    REQUIRE(moved != nullptr);
    REQUIRE(moved->in_contact);
    REQUIRE(moved->x == 50.0f);

    events = tracker.accept(make_sample(pointer_phase::up, pointer_device::touch, false, 50.0f));
    REQUIRE(events.size() == 1u);
    REQUIRE(std::get_if<pointer_released_event>(&events[0])->pointer_id == 7u);
    REQUIRE(tracker.in_contact(7) == false);
}

TEST_CASE("Touch has no hover and leaving is not a release", "[pointer-sequence][touch]")
{
    pointer_sequence_tracker tracker {};
    REQUIRE(tracker.accept(make_sample(pointer_phase::update, pointer_device::touch, false)).empty());
    static_cast<void>(tracker.accept(make_sample(pointer_phase::down, pointer_device::touch, true)));
    // 접촉 중의 이탈은 경계 통과다. 뗌을 합성하지 않는다.
    REQUIRE(tracker.accept(make_sample(pointer_phase::leave, pointer_device::touch, true)).empty());
    REQUIRE(tracker.in_contact(7));
    // 시작을 보지 못한 접촉의 이동·뗌은 삼킨다.
    pointer_sequence_tracker other {};
    REQUIRE(other.accept(make_sample(pointer_phase::update, pointer_device::touch, true)).empty());
    REQUIRE(other.accept(make_sample(pointer_phase::up, pointer_device::touch, false)).empty());
}

TEST_CASE("A cancelled or capture lost contact is a cancel, not a release", "[pointer-sequence][touch]")
{
    pointer_sequence_tracker tracker {};
    static_cast<void>(tracker.accept(make_sample(pointer_phase::down, pointer_device::touch, true)));
    pointer_sample cancelled { make_sample(pointer_phase::up, pointer_device::touch, false) };
    cancelled.canceled = true;
    auto events { tracker.accept(cancelled) };
    REQUIRE(events.size() == 1u);
    REQUIRE(std::get_if<pointer_cancelled_event>(&events[0]) != nullptr);

    static_cast<void>(tracker.accept(make_sample(pointer_phase::down, pointer_device::touch, true)));
    events = tracker.cancel(7, {}, u8"tools");
    REQUIRE(events.size() == 1u);
    REQUIRE(std::get_if<pointer_cancelled_event>(&events[0])->surface == u8"tools");
    // 남은 시퀀스는 삼킨다.
    REQUIRE(tracker.accept(make_sample(pointer_phase::up, pointer_device::touch, false)).empty());
    REQUIRE(tracker.cancel(7, {}, u8"tools").empty());
}

TEST_CASE("A pen barrel switch cancels the old button before pressing the new one", "[pointer-sequence][pen]")
{
    pointer_sequence_tracker tracker {};
    static_cast<void>(tracker.accept(make_sample(pointer_phase::down, pointer_device::pen, true)));
    pointer_sample barrel { make_sample(pointer_phase::update, pointer_device::pen, true) };
    barrel.barrel = true;
    auto events { tracker.accept(barrel) };
    REQUIRE(events.size() == 3u);
    REQUIRE(std::get_if<pointer_cancelled_event>(&events[0])->device == pointer_device::pen);
    // 배럴 전환의 취소는 표식을 단다. 잉크 캔버스가 그리던 획을 남길지 가르는 근거다.
    REQUIRE(std::get_if<pointer_cancelled_event>(&events[0])->button_switch);
    REQUIRE(std::get_if<pointer_pressed_event>(&events[1])->button == pointer_button::right);
    REQUIRE(std::get_if<pointer_moved_event>(&events[2]) != nullptr);
    pointer_sample up { make_sample(pointer_phase::up, pointer_device::pen, false) };
    up.barrel = true;
    events = tracker.accept(up);
    REQUIRE(std::get_if<pointer_released_event>(&events[0])->button == pointer_button::right);

    // 보통 취소(시스템 취소·접촉 상실·조회 실패)는 표식이 없다.
    static_cast<void>(tracker.accept(make_sample(pointer_phase::down, pointer_device::pen, true)));
    pointer_sample cancelled { make_sample(pointer_phase::update, pointer_device::pen, true) };
    cancelled.canceled = true;
    events = tracker.accept(cancelled);
    REQUIRE(events.size() == 1u);
    REQUIRE(std::get_if<pointer_cancelled_event>(&events[0])->button_switch == false);
    static_cast<void>(tracker.accept(make_sample(pointer_phase::down, pointer_device::pen, true)));
    events = tracker.cancel(7, {}, u8"tools");
    REQUIRE(std::get_if<pointer_cancelled_event>(&events[0])->button_switch == false);
}

TEST_CASE("Pen pressure and tilt travel on presses, moves and releases but not on touch", "[pointer-sequence][pen][ink]")
{
    pointer_sequence_tracker tracker {};
    pointer_sample sample { make_sample(pointer_phase::down, pointer_device::pen, true) };
    sample.pressure = 0.5f;
    sample.tilt_x = 10.0f;
    sample.tilt_y = -20.0f;
    auto events { tracker.accept(sample) };
    const auto& pressed { std::get<pointer_pressed_event>(events.front()) };
    REQUIRE(pressed.pen.has_value());
    REQUIRE(pressed.pen->pressure == 0.5f);
    REQUIRE(pressed.pen->tilt_x == 10.0f);
    REQUIRE(pressed.pen->tilt_y == -20.0f);
    REQUIRE(pressed.pen->eraser == false);

    // 1을 넘는 압력(Android)은 1로 접는다.
    sample.phase = pointer_phase::update;
    sample.pressure = 1.2f;
    events = tracker.accept(sample);
    REQUIRE(std::get<pointer_moved_event>(events.front()).pen->pressure == 1.0f);

    // 압력을 알리지 않는 장치는 비어 있다.
    sample.phase = pointer_phase::up;
    sample.in_contact = false;
    sample.pressure.reset();
    events = tracker.accept(sample);
    const auto& released { std::get<pointer_released_event>(events.front()) };
    REQUIRE(released.pen.has_value());
    REQUIRE(released.pen->pressure.has_value() == false);
    REQUIRE(released.pen->tilt_x == 10.0f);

    // 펜 hover의 압력은 0이다.
    pointer_sample hover { make_sample(pointer_phase::update, pointer_device::pen, false) };
    hover.pressure = 0.7f;
    events = tracker.accept(hover);
    REQUIRE(std::get<pointer_moved_event>(events.front()).pen->pressure == 0.0f);

    pointer_sample touch { make_sample(pointer_phase::down, pointer_device::touch, true) };
    touch.pressure = 0.5f;
    events = tracker.accept(touch);
    REQUIRE(std::get<pointer_pressed_event>(events.front()).pen.has_value() == false);
}

TEST_CASE("Pen hover moves without contact and leaves on exit", "[pointer-sequence][pen]")
{
    pointer_sequence_tracker tracker {};
    auto events { tracker.accept(make_sample(pointer_phase::update, pointer_device::pen, false)) };
    REQUIRE(events.size() == 1u);
    REQUIRE(std::get_if<pointer_moved_event>(&events[0])->in_contact == false);
    events = tracker.accept(make_sample(pointer_phase::leave, pointer_device::pen, false));
    REQUIRE(events.size() == 1u);
    REQUIRE(std::get_if<pointer_left_event>(&events[0])->device == pointer_device::pen);
}

TEST_CASE("The eraser end travels as marked events", "[pointer-sequence][pen][ink]")
{
    // 일반 컨트롤의 삼킴은 controller가 맡는다 (tests/ink_canvas_tests.cpp). 추적기는 표식만 단다.
    pointer_sequence_tracker tracker {};
    pointer_sample down { make_sample(pointer_phase::down, pointer_device::pen, true) };
    down.eraser = true;
    auto events { tracker.accept(down) };
    REQUIRE(events.size() == 1u);
    const auto& pressed { std::get<pointer_pressed_event>(events.front()) };
    REQUIRE(pressed.button == pointer_button::left);
    REQUIRE(pressed.pen->eraser);

    // 지우개인지는 누를 때 정한다. 접촉 중 표본의 표식이 바뀌어도 그대로다.
    events = tracker.accept(make_sample(pointer_phase::update, pointer_device::pen, true));
    REQUIRE(events.size() == 1u);
    REQUIRE(std::get<pointer_moved_event>(events.front()).pen->eraser);

    // 지우개 접촉에는 배럴 전환이 없다. 다시 누르지 않는다.
    pointer_sample barrel { make_sample(pointer_phase::update, pointer_device::pen, true) };
    barrel.barrel = true;
    barrel.eraser = true;
    events = tracker.accept(barrel);
    REQUIRE(events.size() == 1u);
    REQUIRE(std::get_if<pointer_moved_event>(&events.front()) != nullptr);

    events = tracker.accept(make_sample(pointer_phase::up, pointer_device::pen, false));
    REQUIRE(events.size() == 1u);
    REQUIRE(std::get<pointer_released_event>(events.front()).pen->eraser);
    REQUIRE(std::get<pointer_released_event>(events.front()).button == pointer_button::left);

    // 접촉 중 취소와 `cancel()`도 취소를 낸다.
    static_cast<void>(tracker.accept(down));
    pointer_sample cancelled { make_sample(pointer_phase::up, pointer_device::pen, false) };
    cancelled.canceled = true;
    events = tracker.accept(cancelled);
    REQUIRE(events.size() == 1u);
    REQUIRE(std::get_if<pointer_cancelled_event>(&events.front()) != nullptr);
    static_cast<void>(tracker.accept(down));
    REQUIRE(tracker.cancel(7, {}, u8"tools").size() == 1u);

    // hover에도 지우개 표식이 실린다.
    pointer_sample hover { make_sample(pointer_phase::update, pointer_device::pen, false) };
    hover.eraser = true;
    events = tracker.accept(hover);
    REQUIRE(std::get<pointer_moved_event>(events.front()).pen->eraser);
}

TEST_CASE("Pen presses retain Shift through a barrel switch", "[pointer-sequence][pen]")
{
    pointer_sequence_tracker tracker {};
    pointer_sample sample { make_sample(pointer_phase::down, pointer_device::pen, true) };
    sample.shift = true;
    auto events { tracker.accept(sample) };
    REQUIRE(std::get<pointer_pressed_event>(events.front()).shift);
    sample.phase = pointer_phase::update;
    sample.barrel = true;
    events = tracker.accept(sample);
    REQUIRE(std::get<pointer_pressed_event>(events[1]).shift);
}

TEST_CASE("Touch and pen events carry the keyboard modifiers of their moment", "[pointer-sequence][modifiers]")
{
    pointer_sequence_tracker tracker {};
    pointer_sample sample { make_sample(pointer_phase::down, pointer_device::touch, true) };
    sample.control = true;
    auto events { tracker.accept(sample) };
    REQUIRE(std::get<pointer_pressed_event>(events.front()).control);
    REQUIRE(std::get<pointer_pressed_event>(events.front()).alt == false);

    sample.phase = pointer_phase::update;
    sample.control = false;
    sample.alt = true;
    events = tracker.accept(sample);
    const auto& moved { std::get<pointer_moved_event>(events.front()) };
    REQUIRE(moved.control == false);
    REQUIRE(moved.alt);

    sample.phase = pointer_phase::up;
    sample.in_contact = false;
    sample.shift = true;
    events = tracker.accept(sample);
    const auto& released { std::get<pointer_released_event>(events.front()) };
    REQUIRE(released.shift);
    REQUIRE(released.alt);
    REQUIRE(released.control == false);
}
