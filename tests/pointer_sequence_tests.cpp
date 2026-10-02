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
} // namespace

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
    REQUIRE(std::get_if<pointer_pressed_event>(&events[1])->button == pointer_button::right);
    REQUIRE(std::get_if<pointer_moved_event>(&events[2]) != nullptr);
    pointer_sample up { make_sample(pointer_phase::up, pointer_device::pen, false) };
    up.barrel = true;
    events = tracker.accept(up);
    REQUIRE(std::get_if<pointer_released_event>(&events[0])->button == pointer_button::right);
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

TEST_CASE("The eraser end runs no control", "[pointer-sequence][pen]")
{
    pointer_sequence_tracker tracker {};
    pointer_sample down { make_sample(pointer_phase::down, pointer_device::pen, true) };
    down.eraser = true;
    REQUIRE(tracker.accept(down).empty());
    REQUIRE(tracker.accept(make_sample(pointer_phase::update, pointer_device::pen, true)).empty());
    REQUIRE(tracker.accept(make_sample(pointer_phase::up, pointer_device::pen, false)).empty());
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
