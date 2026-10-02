#include "win32/surface_input.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <variant>

namespace {
    using namespace luil;
    using namespace luil::win32;

    [[nodiscard]] pointer_message make_message(const UINT message, const WPARAM word_parameter = 0)
    {
        pointer_message value {};
        value.message = message;
        value.word_parameter = word_parameter;
        value.x = 12.0f;
        value.y = 34.0f;
        value.time = std::chrono::steady_clock::time_point { std::chrono::milliseconds { 500 } };
        value.surface = u8"tools";
        return value;
    }
} // namespace

TEST_CASE("Wheel messages preserve control shift and horizontal direction", "[surface-input][zoom]")
{
    const auto translated { translate_pointer_message(make_message(WM_MOUSEHWHEEL, MAKEWPARAM(MK_CONTROL | MK_SHIFT, 120))) };
    REQUIRE(translated.has_value());
    const auto& wheel { std::get<mouse_wheel_event>(*translated) };
    REQUIRE(wheel.horizontal);
    REQUIRE(wheel.control);
    REQUIRE(wheel.shift);
    REQUIRE(wheel.delta == 120.0f);
    REQUIRE(wheel.x == 12.0f);
    REQUIRE(wheel.surface == u8"tools");
}

TEST_CASE("Pointer messages keep the surface tag and client coordinates", "[surface-input]")
{
    const auto moved { translate_pointer_message(make_message(WM_MOUSEMOVE)) };
    REQUIRE(moved.has_value());
    const auto* const value { std::get_if<pointer_moved_event>(&*moved) };
    REQUIRE(value != nullptr);
    REQUIRE(value->x == 12.0f);
    REQUIRE(value->y == 34.0f);
    REQUIRE(value->surface == u8"tools");
    REQUIRE(value->time == std::chrono::steady_clock::time_point { std::chrono::milliseconds { 500 } });
}

TEST_CASE("A second press arrives as an ordinary press", "[surface-input]")
{
    // 더블 클릭 판정은 interaction controller의 몫이라
    // 창 계층은 두 번째 누름을 같은 누름으로 넘긴다.
    const auto pressed { translate_pointer_message(make_message(WM_LBUTTONDBLCLK)) };
    REQUIRE(pressed.has_value());
    const auto* const value { std::get_if<pointer_pressed_event>(&*pressed) };
    REQUIRE(value != nullptr);
    REQUIRE(value->button == pointer_button::left);
    REQUIRE(value->shift == false);
}

TEST_CASE("Shift travels with the press", "[surface-input]")
{
    const auto pressed { translate_pointer_message(make_message(WM_RBUTTONDOWN, MK_SHIFT)) };
    REQUIRE(pressed.has_value());
    const auto* const value { std::get_if<pointer_pressed_event>(&*pressed) };
    REQUIRE(value != nullptr);
    REQUIRE(value->button == pointer_button::right);
    REQUIRE(value->shift);
}

TEST_CASE("Releases carry the matching button", "[surface-input]")
{
    const auto left { translate_pointer_message(make_message(WM_LBUTTONUP)) };
    REQUIRE(left.has_value());
    REQUIRE(std::get_if<pointer_released_event>(&*left) != nullptr);
    REQUIRE(std::get<pointer_released_event>(*left).button == pointer_button::left);

    const auto right { translate_pointer_message(make_message(WM_RBUTTONUP)) };
    REQUIRE(right.has_value());
    REQUIRE(std::get<pointer_released_event>(*right).button == pointer_button::right);
}

TEST_CASE("Wheel delta arrives in WHEEL_DELTA units", "[surface-input]")
{
    const auto up { translate_pointer_message(make_message(WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA))) };
    REQUIRE(up.has_value());
    const auto* const value { std::get_if<mouse_wheel_event>(&*up) };
    REQUIRE(value != nullptr);
    REQUIRE(value->delta == static_cast<float>(WHEEL_DELTA));
    REQUIRE(value->surface == u8"tools");

    // 아래로 굴리면 음수다.
    const auto down { translate_pointer_message(make_message(WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA))) };
    REQUIRE(down.has_value());
    REQUIRE(std::get<mouse_wheel_event>(*down).delta == -static_cast<float>(WHEEL_DELTA));
}

TEST_CASE("Unknown messages are left to the window", "[surface-input]")
{
    REQUIRE(translate_pointer_message(make_message(WM_MOUSELEAVE)).has_value() == false);
    REQUIRE(translate_pointer_message(make_message(WM_KEYDOWN)).has_value() == false);
    REQUIRE(pointer_button_of(WM_MOUSEMOVE) == pointer_button::none);
}

TEST_CASE("Named keys map to the shared vocabulary", "[surface-input]")
{
    REQUIRE(named_key_from_virtual(VK_UP) == key_code::arrow_up);
    REQUIRE(named_key_from_virtual(VK_TAB) == key_code::tab);
    REQUIRE(named_key_from_virtual(VK_ESCAPE) == key_code::escape);
    REQUIRE(named_key_from_virtual(VK_F1) == key_code::f1);
    REQUIRE(named_key_from_virtual(VK_F12) == key_code::f12);
    REQUIRE(named_key_from_virtual('S') == key_code::none);
}

TEST_CASE("Character keys only become events under Ctrl or Alt", "[surface-input]")
{
    // 그냥 치는 글자는 WM_CHAR로 흘러야 그 글자를 칠 수 있다.
    REQUIRE(modified_key_from_virtual('C', false, false, true) == key_code::none);
    REQUIRE(modified_key_from_virtual('C', true, false, true) == key_code::key_c);
    REQUIRE(modified_key_from_virtual('S', true, false, true) == key_code::key_s);
    REQUIRE(modified_key_from_virtual('7', true, false, true) == key_code::key_7);
    REQUIRE(modified_key_from_virtual(VK_SPACE, false, true, true) == key_code::space);
    // Space는 예외다. 초점을 가진 컨트롤을 실행하는 키라 수정자 없이도 온다 —
    // 텍스트 박스에서는 controller가 키 쪽을 흘려보내고 WM_CHAR만 먹는다.
    REQUIRE(modified_key_from_virtual(VK_SPACE, false, false, true) == key_code::space);
}

TEST_CASE("Platform-specific keys still travel in the platform band", "[surface-input]")
{
    const key_code key { modified_key_from_virtual(VK_OEM_1, true, false, true) };
    REQUIRE(key == platform_key_code(VK_OEM_1));
    REQUIRE(platform_key_of(key) == VK_OEM_1);
}

TEST_CASE("Modifier keys themselves are not events", "[surface-input]")
{
    REQUIRE(modified_key_from_virtual(VK_SHIFT, false, false, false) == key_code::none);
    REQUIRE(modified_key_from_virtual(VK_CONTROL, true, false, false) == key_code::none);
    REQUIRE(modified_key_from_virtual(VK_MENU, false, true, false) == key_code::none);
}

TEST_CASE("Insert keeps its legacy clipboard meaning", "[surface-input]")
{
    // Ctrl+Insert·Shift+Insert는 수정자 없이도 같은 키다.
    REQUIRE(modified_key_from_virtual(VK_INSERT, false, false, false) == key_code::insert);
}

TEST_CASE("Pointer messages map onto the shared contact phases", "[surface-input][touch]")
{
    REQUIRE(pointer_phase_of(WM_POINTERDOWN) == pointer_phase::down);
    REQUIRE(pointer_phase_of(WM_POINTERUPDATE) == pointer_phase::update);
    REQUIRE(pointer_phase_of(WM_POINTERUP) == pointer_phase::up);
    REQUIRE(pointer_phase_of(WM_POINTERLEAVE) == pointer_phase::leave);
    // 들어옴은 추적기가 쓰지 않는다. 시퀀스를 소비만 한다.
    REQUIRE_FALSE(pointer_phase_of(WM_POINTERENTER).has_value());
    REQUIRE_FALSE(pointer_phase_of(WM_MOUSEMOVE).has_value());
}

TEST_CASE("Pointer times move onto the steady clock", "[surface-input][touch]")
{
    const std::chrono::steady_clock::time_point now { std::chrono::seconds { 100 } };
    // 성능 카운터: 주파수 1000에서 250 차이는 250ms 전이다.
    REQUIRE(pointer_counter_time(9'750, 10'000, 1'000, now) == now - std::chrono::milliseconds { 250 });
    // 값이 없거나 지금보다 뒤면 지금이다.
    REQUIRE(pointer_counter_time(0, 10'000, 1'000, now) == now);
    REQUIRE(pointer_counter_time(10'500, 10'000, 1'000, now) == now);
    // 큰 카운터도 넘치지 않는다.
    REQUIRE(pointer_counter_time(0xFFFF'FFFF'0000'0000ull, 0xFFFF'FFFF'0098'9680ull, 10'000'000ull, now) == now - std::chrono::seconds { 1 });

    // tick은 한 바퀴 돈 경계에서도 나이를 옳게 잰다.
    REQUIRE(pointer_tick_time(0xFFFF'FFF0u, 0x0000'0010u, now) == now - std::chrono::milliseconds { 32 });
    REQUIRE(pointer_tick_time(1'000u, 1'200u, now) == now - std::chrono::milliseconds { 200 });
    REQUIRE(pointer_tick_time(1'300u, 1'200u, now) == now);
}
