#include "android/android_input.h"

#include <catch2/catch_test_macros.hpp>

#include <android/input.h>
#include <android/keycodes.h>

#include <time.h>

#include <chrono>
#include <cstdint>
#include <variant>
#include <vector>

namespace {
    constexpr luil::android::surface_mapping phone { 0.0f, 100.0f, 3.5f };

    [[nodiscard]] luil::android::motion_input motion(const std::int32_t action, const std::int64_t time_ns, std::vector<luil::android::motion_pointer> pointers)
    {
        luil::android::motion_input input {};
        input.source = AINPUT_SOURCE_TOUCHSCREEN;
        input.action = action;
        input.current = { time_ns, std::move(pointers) };
        return input;
    }

    [[nodiscard]] luil::android::motion_pointer finger(const std::int32_t id, const float x, const float y)
    {
        return { id, AMOTION_EVENT_TOOL_TYPE_FINGER, x, y };
    }

    [[nodiscard]] std::int32_t pointer_action(const std::int32_t action, const std::int32_t index)
    {
        return action | (index << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    }

    [[nodiscard]] luil::android::key_input key(const std::int32_t code, const std::int32_t meta = 0, const std::int32_t unicode = 0)
    {
        luil::android::key_input input {};
        input.action = AKEY_EVENT_ACTION_DOWN;
        input.key_code = code;
        input.meta_state = meta;
        input.unicode_char = unicode;
        input.time_ns = 5'000'000;
        return input;
    }

    template<typename event_type>
    [[nodiscard]] const event_type& as(const luil::raw_input_event& event)
    {
        REQUIRE(std::holds_alternative<event_type>(event));
        return std::get<event_type>(event);
    }
} // namespace

TEST_CASE("Android event times are on the steady clock", "[android][input]")
{
    // libc++의 steady_clock은 CLOCK_MONOTONIC이다. 그래서 이벤트 시각을 옮기기만 한다.
    timespec now {};
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    const std::int64_t monotonic { static_cast<std::int64_t>(now.tv_sec) * 1'000'000'000 + now.tv_nsec };
    const auto difference { std::chrono::steady_clock::now() - luil::android::event_time(monotonic) };
    REQUIRE(difference >= std::chrono::nanoseconds { 0 });
    REQUIRE(difference < std::chrono::milliseconds { 50 });
}

TEST_CASE("Android touches become presses in tree coordinates", "[android][input]")
{
    luil::android::input_translator translator {};
    auto events { translator.translate(motion(AMOTION_EVENT_ACTION_DOWN, 1'000'000, { finger(0, 110.0f, 300.0f) }), phone) };
    REQUIRE(events.size() == 1u);
    const auto& pressed { as<luil::pointer_pressed_event>(events[0]) };
    // 안전 영역의 원점만큼 뺀 자리이고, 배율은 밀도다.
    REQUIRE(pressed.x == 110.0f);
    REQUIRE(pressed.y == 200.0f);
    REQUIRE(pressed.device == luil::pointer_device::touch);
    REQUIRE(pressed.button == luil::pointer_button::left);
    REQUIRE(pressed.scale == 3.5f);
    REQUIRE(pressed.time == luil::android::event_time(1'000'000));

    events = translator.translate(motion(AMOTION_EVENT_ACTION_UP, 2'000'000, { finger(0, 112.0f, 302.0f) }), phone);
    REQUIRE(events.size() == 1u);
    REQUIRE(as<luil::pointer_released_event>(events[0]).y == 202.0f);
}

TEST_CASE("Only the indexed pointer goes down or up", "[android][input]")
{
    luil::android::input_translator translator {};
    static_cast<void>(translator.translate(motion(AMOTION_EVENT_ACTION_DOWN, 0, { finger(0, 10.0f, 110.0f) }), phone));
    auto events { translator.translate(motion(pointer_action(AMOTION_EVENT_ACTION_POINTER_DOWN, 1), 10, { finger(0, 12.0f, 110.0f), finger(5, 50.0f, 150.0f) }), phone) };
    REQUIRE(events.size() == 2u);
    REQUIRE(as<luil::pointer_moved_event>(events[0]).pointer_id == 0u);
    REQUIRE(as<luil::pointer_pressed_event>(events[1]).pointer_id == 5u);

    events = translator.translate(motion(pointer_action(AMOTION_EVENT_ACTION_POINTER_UP, 0), 20, { finger(0, 12.0f, 110.0f), finger(5, 52.0f, 150.0f) }), phone);
    REQUIRE(events.size() == 2u);
    REQUIRE(as<luil::pointer_released_event>(events[0]).pointer_id == 0u);
    REQUIRE(as<luil::pointer_moved_event>(events[1]).pointer_id == 5u);
}

TEST_CASE("Batched history arrives oldest first before the current sample", "[android][input]")
{
    luil::android::input_translator translator {};
    static_cast<void>(translator.translate(motion(AMOTION_EVENT_ACTION_DOWN, 0, { finger(0, 10.0f, 110.0f) }), phone));
    luil::android::motion_input input { motion(AMOTION_EVENT_ACTION_MOVE, 30, { finger(0, 40.0f, 110.0f) }) };
    input.history = { { 10, { finger(0, 20.0f, 110.0f) } }, { 20, { finger(0, 30.0f, 110.0f) } } };
    const auto events { translator.translate(input, phone) };
    REQUIRE(events.size() == 3u);
    REQUIRE(as<luil::pointer_moved_event>(events[0]).x == 20.0f);
    REQUIRE(as<luil::pointer_moved_event>(events[1]).x == 30.0f);
    REQUIRE(as<luil::pointer_moved_event>(events[2]).x == 40.0f);
    REQUIRE(as<luil::pointer_moved_event>(events[1]).time == luil::android::event_time(20));
    REQUIRE(as<luil::pointer_moved_event>(events[2]).in_contact);
}

TEST_CASE("A system cancel ends every contact without a release", "[android][input]")
{
    luil::android::input_translator translator {};
    static_cast<void>(translator.translate(motion(AMOTION_EVENT_ACTION_DOWN, 0, { finger(0, 10.0f, 110.0f) }), phone));
    const auto events { translator.translate(motion(AMOTION_EVENT_ACTION_CANCEL, 10, { finger(0, 10.0f, 110.0f) }), phone) };
    REQUIRE(events.size() == 1u);
    REQUIRE(as<luil::pointer_cancelled_event>(events[0]).device == luil::pointer_device::touch);
    // 끝난 접촉의 이동은 삼킨다.
    REQUIRE(translator.translate(motion(AMOTION_EVENT_ACTION_MOVE, 20, { finger(0, 30.0f, 110.0f) }), phone).empty());
}

TEST_CASE("A pen hovers, right clicks with the barrel and swallows the eraser", "[android][input]")
{
    luil::android::input_translator translator {};
    luil::android::motion_input hover { motion(AMOTION_EVENT_ACTION_HOVER_MOVE, 0, { { 2, AMOTION_EVENT_TOOL_TYPE_STYLUS, 30.0f, 140.0f } }) };
    auto events { translator.translate(hover, phone) };
    REQUIRE(events.size() == 1u);
    REQUIRE(as<luil::pointer_moved_event>(events[0]).device == luil::pointer_device::pen);
    REQUIRE(as<luil::pointer_moved_event>(events[0]).in_contact == false);

    luil::android::motion_input down { motion(AMOTION_EVENT_ACTION_DOWN, 10, { { 2, AMOTION_EVENT_TOOL_TYPE_STYLUS, 30.0f, 140.0f } }) };
    down.button_state = AMOTION_EVENT_BUTTON_STYLUS_PRIMARY;
    events = translator.translate(down, phone);
    REQUIRE(events.size() == 1u);
    REQUIRE(as<luil::pointer_pressed_event>(events[0]).button == luil::pointer_button::right);
    events = translator.translate(motion(AMOTION_EVENT_ACTION_UP, 20, { { 2, AMOTION_EVENT_TOOL_TYPE_STYLUS, 30.0f, 140.0f } }), phone);
    REQUIRE(as<luil::pointer_released_event>(events[0]).button == luil::pointer_button::right);

    REQUIRE(translator.translate(motion(AMOTION_EVENT_ACTION_DOWN, 30, { { 2, AMOTION_EVENT_TOOL_TYPE_ERASER, 30.0f, 140.0f } }), phone).empty());
    REQUIRE(translator.translate(motion(AMOTION_EVENT_ACTION_UP, 40, { { 2, AMOTION_EVENT_TOOL_TYPE_ERASER, 30.0f, 140.0f } }), phone).empty());

    events = translator.translate(motion(AMOTION_EVENT_ACTION_HOVER_EXIT, 50, { { 2, AMOTION_EVENT_TOOL_TYPE_STYLUS, 30.0f, 140.0f } }), phone);
    REQUIRE(events.size() == 1u);
    REQUIRE(as<luil::pointer_left_event>(events[0]).device == luil::pointer_device::pen);
}

TEST_CASE("A mouse presses with buttons and scrolls in wheel detents", "[android][input]")
{
    luil::android::input_translator translator {};
    const auto mouse = [](const std::int32_t action, const std::int32_t button_state = 0, const std::int32_t action_button = 0) {
        luil::android::motion_input input { motion(action, 0, { { 0, AMOTION_EVENT_TOOL_TYPE_MOUSE, 60.0f, 160.0f } }) };
        input.source = AINPUT_SOURCE_MOUSE;
        input.button_state = button_state;
        input.action_button = action_button;
        return input;
    };
    auto events { translator.translate(mouse(AMOTION_EVENT_ACTION_HOVER_MOVE), phone) };
    REQUIRE(as<luil::pointer_moved_event>(events[0]).device == luil::pointer_device::mouse);
    // 버튼을 누를 때 오는 hover 끝은 창을 떠난 것이 아니다.
    REQUIRE(translator.translate(mouse(AMOTION_EVENT_ACTION_HOVER_EXIT, AMOTION_EVENT_BUTTON_PRIMARY), phone).empty());
    // 실제 마우스는 DOWN과 버튼 동작을 함께 보낸다. 누름·뗌은 한 번씩만 나간다.
    events = translator.translate(mouse(AMOTION_EVENT_ACTION_DOWN, AMOTION_EVENT_BUTTON_PRIMARY), phone);
    REQUIRE(as<luil::pointer_pressed_event>(events[0]).button == luil::pointer_button::left);
    REQUIRE(as<luil::pointer_pressed_event>(events[0]).y == 60.0f);
    REQUIRE(translator.translate(mouse(AMOTION_EVENT_ACTION_BUTTON_PRESS, AMOTION_EVENT_BUTTON_PRIMARY, AMOTION_EVENT_BUTTON_PRIMARY), phone).empty());
    events = translator.translate(mouse(AMOTION_EVENT_ACTION_BUTTON_RELEASE, 0, AMOTION_EVENT_BUTTON_PRIMARY), phone);
    REQUIRE(as<luil::pointer_released_event>(events[0]).button == luil::pointer_button::left);
    REQUIRE(translator.translate(mouse(AMOTION_EVENT_ACTION_UP), phone).empty());
    // 주입된 마우스(`input mouse tap`)는 버튼 상태 없이 DOWN·UP만 보낸다. 왼쪽이다.
    events = translator.translate(mouse(AMOTION_EVENT_ACTION_DOWN), phone);
    REQUIRE(as<luil::pointer_pressed_event>(events[0]).button == luil::pointer_button::left);
    events = translator.translate(mouse(AMOTION_EVENT_ACTION_UP), phone);
    REQUIRE(as<luil::pointer_released_event>(events[0]).button == luil::pointer_button::left);
    events = translator.translate(mouse(AMOTION_EVENT_ACTION_DOWN, AMOTION_EVENT_BUTTON_SECONDARY), phone);
    REQUIRE(as<luil::pointer_pressed_event>(events[0]).button == luil::pointer_button::right);
    REQUIRE(translator.translate(mouse(AMOTION_EVENT_ACTION_BUTTON_PRESS, AMOTION_EVENT_BUTTON_SECONDARY, AMOTION_EVENT_BUTTON_SECONDARY), phone).empty());

    luil::android::motion_input scroll { mouse(AMOTION_EVENT_ACTION_SCROLL) };
    scroll.vertical_scroll = 1.0f;
    scroll.horizontal_scroll = -0.5f;
    scroll.meta_state = AMETA_CTRL_ON;
    events = translator.translate(scroll, phone);
    REQUIRE(events.size() == 2u);
    REQUIRE(as<luil::mouse_wheel_event>(events[0]).delta == 120.0f);
    REQUIRE(as<luil::mouse_wheel_event>(events[0]).control);
    REQUIRE(as<luil::mouse_wheel_event>(events[1]).horizontal);
    REQUIRE(as<luil::mouse_wheel_event>(events[1]).delta == -60.0f);

    // 창이 사라지면 눌린 버튼은 화면 밖 뗌으로, hover는 이탈로 거둔다.
    events = translator.cancel_all(luil::android::event_time(100));
    REQUIRE(events.size() == 2u);
    REQUIRE(as<luil::pointer_released_event>(events[0]).button == luil::pointer_button::right);
    REQUIRE(std::holds_alternative<luil::pointer_left_event>(events[1]));
}

TEST_CASE("Leaving the window cancels a held touch", "[android][input]")
{
    luil::android::input_translator translator {};
    static_cast<void>(translator.translate(motion(AMOTION_EVENT_ACTION_DOWN, 0, { finger(3, 10.0f, 110.0f) }), phone));
    const auto events { translator.cancel_all(luil::android::event_time(10)) };
    REQUIRE(events.size() == 1u);
    REQUIRE(as<luil::pointer_cancelled_event>(events[0]).pointer_id == 3u);
    REQUIRE(translator.cancel_all(luil::android::event_time(20)).empty());
}

TEST_CASE("Android keys follow the Windows key and character split", "[android][input]")
{
    const luil::android::input_translator translator {};
    // 이름 있는 키는 키다.
    auto events { translator.translate(key(AKEYCODE_DPAD_DOWN)) };
    REQUIRE(events.size() == 1u);
    REQUIRE(as<luil::key_pressed_event>(events[0]).key == luil::key_code::arrow_down);
    REQUIRE(as<luil::key_pressed_event>(translator.translate(key(AKEYCODE_F5))[0]).key == luil::key_code::f5);
    REQUIRE(as<luil::key_pressed_event>(translator.translate(key(AKEYCODE_TAB, AMETA_SHIFT_ON))[0]).shift);

    // 그냥 친 글자는 글자뿐이다. Ctrl과 함께면 단축키뿐이고 편집 역할을 Ctrl이 맡는다.
    events = translator.translate(key(AKEYCODE_A, 0, 'a'));
    REQUIRE(events.size() == 1u);
    REQUIRE(as<luil::character_typed_event>(events[0]).character == U'a');
    events = translator.translate(key(AKEYCODE_C, AMETA_CTRL_ON | AMETA_CTRL_LEFT_ON, 'c'));
    REQUIRE(events.size() == 1u);
    const auto& copy { as<luil::key_pressed_event>(events[0]) };
    REQUIRE(copy.key == luil::key_code::key_c);
    REQUIRE(copy.primary_shortcut_down());
    REQUIRE(copy.word_navigation_down());

    // Backspace는 U+0008, Ctrl+Backspace는 키와 U+007F다 (Win32의 WM_CHAR와 같다).
    events = translator.translate(key(AKEYCODE_DEL));
    REQUIRE(events.size() == 1u);
    REQUIRE(as<luil::character_typed_event>(events[0]).character == U'\b');
    events = translator.translate(key(AKEYCODE_DEL, AMETA_CTRL_ON));
    REQUIRE(events.size() == 2u);
    REQUIRE(as<luil::character_typed_event>(events[1]).character == char32_t { 0x7F });

    // Space는 누름이면서 글자다.
    events = translator.translate(key(AKEYCODE_SPACE, 0, ' '));
    REQUIRE(events.size() == 2u);
    REQUIRE(as<luil::key_pressed_event>(events[0]).key == luil::key_code::space);
    REQUIRE(as<luil::character_typed_event>(events[1]).character == U' ');

    // 편집 전용 키는 Ctrl 단축키다.
    events = translator.translate(key(AKEYCODE_PASTE));
    REQUIRE(events.size() == 1u);
    REQUIRE(as<luil::key_pressed_event>(events[0]).key == luil::key_code::key_v);
    REQUIRE(as<luil::key_pressed_event>(events[0]).primary_shortcut_down());
    REQUIRE(as<luil::key_pressed_event>(translator.translate(key(AKEYCODE_COPY))[0]).key == luil::key_code::key_c);
    REQUIRE(as<luil::key_pressed_event>(translator.translate(key(AKEYCODE_CUT))[0]).key == luil::key_code::key_x);

    // 수정자 키 자체와 뗌은 이벤트가 아니다. 자동 반복은 표시한다.
    REQUIRE(translator.translate(key(AKEYCODE_SHIFT_LEFT, AMETA_SHIFT_ON)).empty());
    luil::android::key_input released { key(AKEYCODE_DPAD_DOWN) };
    released.action = AKEY_EVENT_ACTION_UP;
    REQUIRE(translator.translate(released).empty());
    luil::android::key_input repeated { key(AKEYCODE_DPAD_DOWN) };
    repeated.repeat_count = 2;
    REQUIRE(as<luil::key_pressed_event>(translator.translate(repeated)[0]).repeat);
}
