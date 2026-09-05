#include "win32/surface_input.h"

#include <windowsx.h>

#include <utility>

namespace luil::win32 {
    std::optional<raw_input_event> translate_pointer_message(const pointer_message& message)
    {
        switch (message.message)
        {
        case WM_MOUSEMOVE: {
            pointer_moved_event moved {};
            moved.x = message.x;
            moved.y = message.y;
            moved.time = message.time;
            moved.surface = message.surface;
            return raw_input_event { std::move(moved) };
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK: {
            pointer_pressed_event pressed {};
            pressed.x = message.x;
            pressed.y = message.y;
            pressed.button = pointer_button_of(message.message);
            pressed.time = message.time;
            // Shift+클릭은 텍스트 박스에서 선택을 그 자리까지 늘린다.
            pressed.shift = (message.word_parameter & MK_SHIFT) != 0;
            pressed.surface = message.surface;
            return raw_input_event { std::move(pressed) };
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            pointer_released_event released {};
            released.x = message.x;
            released.y = message.y;
            released.button = pointer_button_of(message.message);
            released.time = message.time;
            released.surface = message.surface;
            return raw_input_event { std::move(released) };
        }
        case WM_MOUSEWHEEL: {
            mouse_wheel_event wheel {};
            wheel.x = message.x;
            wheel.y = message.y;
            wheel.delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(message.word_parameter));
            wheel.time = message.time;
            wheel.surface = message.surface;
            return raw_input_event { std::move(wheel) };
        }
        default:
            return std::nullopt;
        }
    }

    pointer_button pointer_button_of(const UINT message) noexcept
    {
        switch (message)
        {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
            return pointer_button::left;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_RBUTTONUP:
            return pointer_button::right;
        default:
            return pointer_button::none;
        }
    }

    key_code named_key_from_virtual(const WPARAM virtual_key) noexcept
    {
        switch (virtual_key)
        {
        case VK_UP:
            return key_code::arrow_up;
        case VK_DOWN:
            return key_code::arrow_down;
        case VK_RETURN:
            return key_code::enter;
        case VK_ESCAPE:
            return key_code::escape;
        case VK_LEFT:
            return key_code::arrow_left;
        case VK_RIGHT:
            return key_code::arrow_right;
        case VK_HOME:
            return key_code::home;
        case VK_END:
            return key_code::end;
        case VK_DELETE:
            return key_code::delete_forward;
        case VK_TAB:
            return key_code::tab;
        case VK_PRIOR:
            return key_code::page_up;
        case VK_NEXT:
            return key_code::page_down;
        default:
            break;
        }
        if (virtual_key >= VK_F1 && virtual_key <= VK_F12)
        {
            constexpr key_code function_keys[] {
                key_code::f1,
                key_code::f2,
                key_code::f3,
                key_code::f4,
                key_code::f5,
                key_code::f6,
                key_code::f7,
                key_code::f8,
                key_code::f9,
                key_code::f10,
                key_code::f11,
                key_code::f12,
            };
            return function_keys[virtual_key - VK_F1];
        }
        return key_code::none;
    }

    key_code modified_key_from_virtual(const WPARAM virtual_key, const bool control, const bool alt, const bool character_key) noexcept
    {
        // Insert는 예전 클립보드 배치라 Ctrl이나 Shift와 함께일 때만 뜻이 있다.
        if (virtual_key == VK_INSERT)
            return key_code::insert;
        // Space는 글자이면서 **누름**이다. 초점을 가진 컨트롤을 실행하는 키라
        // 글자 키 관문 앞에서 꺼낸다 (keyboard-focus-design.md).
        //  - 텍스트 박스에서는 같은 Space가 키와 문자로 두 번 오고,
        //    controller가 키 쪽을 흘려보내 한 번만 먹는다.
        if (virtual_key == VK_SPACE)
            return key_code::space;
        if (character_key && control == false && alt == false)
            return key_code::none;
        switch (virtual_key)
        {
        case 'A':
            return key_code::key_a;
        case 'C':
            return key_code::key_c;
        case 'V':
            return key_code::key_v;
        case 'X':
            return key_code::key_x;
        case 'Y':
            return key_code::key_y;
        case 'Z':
            return key_code::key_z;
        default:
            // 수정자 키 자체는 이벤트가 아니다.
            if (virtual_key == VK_SHIFT || virtual_key == VK_CONTROL || virtual_key == VK_MENU || virtual_key == VK_LWIN || virtual_key == VK_RWIN)
                return key_code::none;
            return platform_key_code(static_cast<std::uint32_t>(virtual_key));
        }
    }
} // namespace luil::win32
