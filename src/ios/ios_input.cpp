#include "ios/ios_input.h"

namespace luil::ios {
    namespace {
        // USB HID 키보드 쪽의 사용 번호다 (`UIKeyboardHIDUsage`). 헤더 없이 값으로 둔다 — core test가 UIKit 없이 표를 잠근다.
        enum : std::uint32_t
        {
            hid_a = 0x04,
            hid_z = 0x1D,
            hid_1 = 0x1E,
            hid_0 = 0x27,
            hid_return = 0x28,
            hid_escape = 0x29,
            hid_backspace = 0x2A,
            hid_tab = 0x2B,
            hid_space = 0x2C,
            hid_minus = 0x2D,
            hid_equal = 0x2E,
            hid_bracket_left = 0x2F,
            hid_bracket_right = 0x30,
            hid_backslash = 0x31,
            hid_non_us_pound = 0x32,
            hid_semicolon = 0x33,
            hid_quote = 0x34,
            hid_grave = 0x35,
            hid_comma = 0x36,
            hid_period = 0x37,
            hid_slash = 0x38,
            hid_f1 = 0x3A,
            hid_f12 = 0x45,
            hid_insert = 0x49,
            hid_home = 0x4A,
            hid_page_up = 0x4B,
            hid_delete_forward = 0x4C,
            hid_end = 0x4D,
            hid_page_down = 0x4E,
            hid_right = 0x4F,
            hid_left = 0x50,
            hid_down = 0x51,
            hid_up = 0x52,
            hid_keypad_divide = 0x54,
            hid_keypad_multiply = 0x55,
            hid_keypad_minus = 0x56,
            hid_keypad_plus = 0x57,
            hid_keypad_enter = 0x58,
            hid_keypad_1 = 0x59,
            hid_keypad_9 = 0x61,
            hid_keypad_0 = 0x62,
            hid_keypad_decimal = 0x63,
            hid_non_us_backslash = 0x64,
            hid_keypad_equal = 0x67,
            hid_f13 = 0x68,
            hid_f20 = 0x6F,
            hid_help = 0x75,
        };

        // 영문자 a~z의 Carbon 자리다 (미국 배열의 물리 자리 순서가 아니라 글자 순서로 적는다).
        constexpr std::uint16_t letters[] {
            0x00, 0x0B, 0x08, 0x02, 0x0E, 0x03, 0x05, 0x04, 0x22, 0x26, 0x28, 0x25, 0x2E,
            0x2D, 0x1F, 0x23, 0x0C, 0x0F, 0x01, 0x11, 0x20, 0x09, 0x0D, 0x07, 0x10, 0x06,
        };
        // 1~9, 0의 Carbon 자리다 (HID 순서).
        constexpr std::uint16_t digits[] { 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19, 0x1D };
        // F1~F12의 Carbon 자리다.
        constexpr std::uint16_t functions[] { 0x7A, 0x78, 0x63, 0x76, 0x60, 0x61, 0x62, 0x64, 0x65, 0x6D, 0x67, 0x6F };
        // F13~F20의 Carbon 자리다.
        constexpr std::uint16_t high_functions[] { 0x69, 0x6B, 0x71, 0x6A, 0x40, 0x4F, 0x50, 0x5A };
        // 키패드 1~9의 Carbon 자리다.
        constexpr std::uint16_t keypad_digits[] { 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5B, 0x5C };
    }

    std::optional<std::uint16_t> carbon_key_of_hid(const std::uint32_t usage) noexcept
    {
        if (usage >= hid_a && usage <= hid_z)
            return letters[usage - hid_a];
        if (usage >= hid_1 && usage <= hid_0)
            return digits[usage - hid_1];
        if (usage >= hid_f1 && usage <= hid_f12)
            return functions[usage - hid_f1];
        if (usage >= hid_f13 && usage <= hid_f20)
            return high_functions[usage - hid_f13];
        if (usage >= hid_keypad_1 && usage <= hid_keypad_9)
            return keypad_digits[usage - hid_keypad_1];
        switch (usage)
        {
        case hid_return:
            return 0x24;
        case hid_escape:
            return 0x35;
        case hid_backspace:
            return 0x33;
        case hid_tab:
            return 0x30;
        case hid_space:
            return 0x31;
        case hid_minus:
            return 0x1B;
        case hid_equal:
            return 0x18;
        case hid_bracket_left:
            return 0x21;
        case hid_bracket_right:
            return 0x1E;
        // 미국 배열 밖의 #(ISO 자판의 Enter 옆)은 Carbon에서 백슬래시 자리와 같다.
        case hid_backslash:
        case hid_non_us_pound:
            return 0x2A;
        case hid_semicolon:
            return 0x29;
        case hid_quote:
            return 0x27;
        case hid_grave:
            return 0x32;
        case hid_comma:
            return 0x2B;
        case hid_period:
            return 0x2F;
        case hid_slash:
            return 0x2C;
        // ISO 자판의 왼쪽 Shift 옆 키다 (Carbon `kVK_ISO_Section`).
        case hid_non_us_backslash:
            return 0x0A;
        case hid_insert:
        case hid_help:
            return 0x72;
        case hid_home:
            return 0x73;
        case hid_page_up:
            return 0x74;
        case hid_delete_forward:
            return 0x75;
        case hid_end:
            return 0x77;
        case hid_page_down:
            return 0x79;
        case hid_right:
            return 0x7C;
        case hid_left:
            return 0x7B;
        case hid_down:
            return 0x7D;
        case hid_up:
            return 0x7E;
        case hid_keypad_divide:
            return 0x4B;
        case hid_keypad_multiply:
            return 0x43;
        case hid_keypad_minus:
            return 0x4E;
        case hid_keypad_plus:
            return 0x45;
        case hid_keypad_enter:
            return 0x4C;
        case hid_keypad_0:
            return 0x52;
        case hid_keypad_decimal:
            return 0x41;
        case hid_keypad_equal:
            return 0x51;
        default:
            return std::nullopt;
        }
    }

    bool text_system_key(const std::uint32_t usage) noexcept
    {
        if (usage >= hid_a && usage <= hid_0)
            return true;
        if (usage >= hid_keypad_divide && usage <= hid_keypad_decimal)
            return true;
        switch (usage)
        {
        case hid_return:
        case hid_backspace:
        case hid_space:
        case hid_minus:
        case hid_equal:
        case hid_bracket_left:
        case hid_bracket_right:
        case hid_backslash:
        case hid_non_us_pound:
        case hid_semicolon:
        case hid_quote:
        case hid_grave:
        case hid_comma:
        case hid_period:
        case hid_slash:
        case hid_non_us_backslash:
        case hid_keypad_equal:
            return true;
        default:
            return false;
        }
    }
}
