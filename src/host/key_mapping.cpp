#include "host/key_mapping.h"

namespace luil {
    key_code named_key_from_win32_virtual(const std::uint32_t virtual_key) noexcept
    {
        // winuser.h의 VK_* 값이다.
        switch (virtual_key)
        {
        case 0x08: // VK_BACK
            return key_code::backspace;
        case 0xBA: // VK_OEM_1
            return key_code::semicolon;
        case 0xBB: // VK_OEM_PLUS
            return key_code::equal;
        case 0xBC: // VK_OEM_COMMA
            return key_code::comma;
        case 0xBD: // VK_OEM_MINUS
            return key_code::minus;
        case 0xBE: // VK_OEM_PERIOD
            return key_code::period;
        case 0xBF: // VK_OEM_2
            return key_code::slash;
        case 0xC0: // VK_OEM_3
            return key_code::grave;
        case 0xDB: // VK_OEM_4
            return key_code::bracket_left;
        case 0xDC: // VK_OEM_5
            return key_code::backslash;
        case 0xDD: // VK_OEM_6
            return key_code::bracket_right;
        case 0xDE: // VK_OEM_7
            return key_code::quote;
        case 0x6A: // VK_MULTIPLY
            return key_code::numpad_multiply;
        case 0x6B: // VK_ADD
            return key_code::numpad_add;
        case 0x6D: // VK_SUBTRACT
            return key_code::numpad_subtract;
        case 0x6E: // VK_DECIMAL
            return key_code::numpad_decimal;
        case 0x6F: // VK_DIVIDE
            return key_code::numpad_divide;
        default:
            break;
        }
        // VK_NUMPAD0..VK_NUMPAD9다. NumLock이 꺼진 키패드는 화살표·Home 같은 이동 키로 온다.
        if (virtual_key >= 0x60 && virtual_key <= 0x69)
            return static_cast<key_code>(static_cast<std::uint32_t>(key_code::numpad_0) + (virtual_key - 0x60));
        return key_code::none;
    }

    key_code named_key_from_android(const std::int32_t key_code_value) noexcept
    {
        // android/keycodes.h의 AKEYCODE_* 값이다.
        switch (key_code_value)
        {
        case 67: // AKEYCODE_DEL
            return key_code::backspace;
        case 68: // AKEYCODE_GRAVE
            return key_code::grave;
        case 69: // AKEYCODE_MINUS
            return key_code::minus;
        case 70: // AKEYCODE_EQUALS
            return key_code::equal;
        case 71: // AKEYCODE_LEFT_BRACKET
            return key_code::bracket_left;
        case 72: // AKEYCODE_RIGHT_BRACKET
            return key_code::bracket_right;
        case 73: // AKEYCODE_BACKSLASH
            return key_code::backslash;
        case 74: // AKEYCODE_SEMICOLON
            return key_code::semicolon;
        case 75: // AKEYCODE_APOSTROPHE
            return key_code::quote;
        case 76: // AKEYCODE_SLASH
            return key_code::slash;
        case 55: // AKEYCODE_COMMA
            return key_code::comma;
        case 56: // AKEYCODE_PERIOD
            return key_code::period;
        case 154: // AKEYCODE_NUMPAD_DIVIDE
            return key_code::numpad_divide;
        case 155: // AKEYCODE_NUMPAD_MULTIPLY
            return key_code::numpad_multiply;
        case 156: // AKEYCODE_NUMPAD_SUBTRACT
            return key_code::numpad_subtract;
        case 157: // AKEYCODE_NUMPAD_ADD
            return key_code::numpad_add;
        case 158: // AKEYCODE_NUMPAD_DOT
            return key_code::numpad_decimal;
        default:
            break;
        }
        // AKEYCODE_NUMPAD_0..AKEYCODE_NUMPAD_9다.
        if (key_code_value >= 144 && key_code_value <= 153)
            return static_cast<key_code>(static_cast<std::uint32_t>(key_code::numpad_0) + static_cast<std::uint32_t>(key_code_value - 144));
        return key_code::none;
    }
}
