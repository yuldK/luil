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

    std::uint32_t win32_virtual_from_key(const key_code key) noexcept
    {
        // 영문자·숫자는 VK가 ASCII와 같고, 플랫폼 대역은 값에서 대역의 시작을 뺀 것이 VK다.
        if (const std::uint32_t platform { platform_key_of(key) }; platform != 0)
        {
            // 수정자 키는 걸 키가 아니다 (앱 안 경로도 수정자 키를 사건으로 삼지 않는다). 0xFF는 VK가 아니다.
            const bool modifier { (platform >= 0x10 && platform <= 0x12) || platform == 0x5B || platform == 0x5C || (platform >= 0xA0 && platform <= 0xA5) };
            return (modifier || platform > 0xFE) ? 0u : platform;
        }
        // winuser.h의 VK_* 값이다. F5는 이름 키 사이에 있어 순서로 셀 수 없으므로 하나씩 적는다.
        switch (key)
        {
        case key_code::backspace:
            return 0x08; // VK_BACK
        case key_code::tab:
            return 0x09; // VK_TAB
        case key_code::enter:
            return 0x0D; // VK_RETURN
        case key_code::escape:
            return 0x1B; // VK_ESCAPE
        case key_code::space:
            return 0x20; // VK_SPACE
        case key_code::page_up:
            return 0x21; // VK_PRIOR
        case key_code::page_down:
            return 0x22; // VK_NEXT
        case key_code::end:
            return 0x23; // VK_END
        case key_code::home:
            return 0x24; // VK_HOME
        case key_code::arrow_left:
            return 0x25; // VK_LEFT
        case key_code::arrow_up:
            return 0x26; // VK_UP
        case key_code::arrow_right:
            return 0x27; // VK_RIGHT
        case key_code::arrow_down:
            return 0x28; // VK_DOWN
        case key_code::insert:
            return 0x2D; // VK_INSERT
        case key_code::delete_forward:
            return 0x2E; // VK_DELETE
        case key_code::numpad_multiply:
            return 0x6A; // VK_MULTIPLY
        case key_code::numpad_add:
            return 0x6B; // VK_ADD
        case key_code::numpad_subtract:
            return 0x6D; // VK_SUBTRACT
        case key_code::numpad_decimal:
            return 0x6E; // VK_DECIMAL
        case key_code::numpad_divide:
            return 0x6F; // VK_DIVIDE
        case key_code::f1:
            return 0x70;
        case key_code::f2:
            return 0x71;
        case key_code::f3:
            return 0x72;
        case key_code::f4:
            return 0x73;
        case key_code::f5:
            return 0x74;
        case key_code::f6:
            return 0x75;
        case key_code::f7:
            return 0x76;
        case key_code::f8:
            return 0x77;
        case key_code::f9:
            return 0x78;
        case key_code::f10:
            return 0x79;
        case key_code::f11:
            return 0x7A;
        case key_code::f12:
            return 0x7B;
        case key_code::semicolon:
            return 0xBA; // VK_OEM_1
        case key_code::equal:
            return 0xBB; // VK_OEM_PLUS
        case key_code::comma:
            return 0xBC; // VK_OEM_COMMA
        case key_code::minus:
            return 0xBD; // VK_OEM_MINUS
        case key_code::period:
            return 0xBE; // VK_OEM_PERIOD
        case key_code::slash:
            return 0xBF; // VK_OEM_2
        case key_code::grave:
            return 0xC0; // VK_OEM_3
        case key_code::bracket_left:
            return 0xDB; // VK_OEM_4
        case key_code::backslash:
            return 0xDC; // VK_OEM_5
        case key_code::bracket_right:
            return 0xDD; // VK_OEM_6
        case key_code::quote:
            return 0xDE; // VK_OEM_7
        default:
            break;
        }
        // VK_NUMPAD0..VK_NUMPAD9다. 열 개가 이어진 이름이다.
        if (key >= key_code::numpad_0 && key <= key_code::numpad_9)
            return 0x60u + (static_cast<std::uint32_t>(key) - static_cast<std::uint32_t>(key_code::numpad_0));
        return 0;
    }
}
