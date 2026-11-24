#include "macos/macos_input.h"

#include <array>

namespace luil::macos {
    namespace {
        // Carbon `Events.h`의 가상 키다. 헤더 없이 값으로 둔다 — core test가 AppKit 없이 표를 잠근다.
        enum : std::uint16_t
        {
            vk_a = 0x00,
            vk_s = 0x01,
            vk_d = 0x02,
            vk_f = 0x03,
            vk_h = 0x04,
            vk_g = 0x05,
            vk_z = 0x06,
            vk_x = 0x07,
            vk_c = 0x08,
            vk_v = 0x09,
            vk_b = 0x0B,
            vk_q = 0x0C,
            vk_w = 0x0D,
            vk_e = 0x0E,
            vk_r = 0x0F,
            vk_y = 0x10,
            vk_t = 0x11,
            vk_1 = 0x12,
            vk_2 = 0x13,
            vk_3 = 0x14,
            vk_4 = 0x15,
            vk_6 = 0x16,
            vk_5 = 0x17,
            vk_equal = 0x18,
            vk_9 = 0x19,
            vk_7 = 0x1A,
            vk_minus = 0x1B,
            vk_8 = 0x1C,
            vk_0 = 0x1D,
            vk_bracket_right = 0x1E,
            vk_o = 0x1F,
            vk_u = 0x20,
            vk_bracket_left = 0x21,
            vk_i = 0x22,
            vk_p = 0x23,
            vk_return = 0x24,
            vk_l = 0x25,
            vk_j = 0x26,
            vk_quote = 0x27,
            vk_k = 0x28,
            vk_semicolon = 0x29,
            vk_backslash = 0x2A,
            vk_comma = 0x2B,
            vk_slash = 0x2C,
            vk_n = 0x2D,
            vk_m = 0x2E,
            vk_period = 0x2F,
            vk_tab = 0x30,
            vk_space = 0x31,
            vk_grave = 0x32,
            vk_delete = 0x33,
            vk_escape = 0x35,
            vk_f17 = 0x40,
            vk_keypad_decimal = 0x41,
            vk_keypad_multiply = 0x43,
            vk_keypad_plus = 0x45,
            vk_keypad_divide = 0x4B,
            vk_keypad_enter = 0x4C,
            vk_keypad_minus = 0x4E,
            vk_f18 = 0x4F,
            vk_f19 = 0x50,
            vk_keypad_0 = 0x52,
            vk_keypad_1 = 0x53,
            vk_keypad_2 = 0x54,
            vk_keypad_3 = 0x55,
            vk_keypad_4 = 0x56,
            vk_keypad_5 = 0x57,
            vk_keypad_6 = 0x58,
            vk_keypad_7 = 0x59,
            vk_f20 = 0x5A,
            vk_keypad_8 = 0x5B,
            vk_keypad_9 = 0x5C,
            vk_f5 = 0x60,
            vk_f6 = 0x61,
            vk_f7 = 0x62,
            vk_f3 = 0x63,
            vk_f8 = 0x64,
            vk_f9 = 0x65,
            vk_f11 = 0x67,
            vk_f13 = 0x69,
            vk_f16 = 0x6A,
            vk_f14 = 0x6B,
            vk_f10 = 0x6D,
            vk_f12 = 0x6F,
            vk_f15 = 0x71,
            vk_help = 0x72,
            vk_home = 0x73,
            vk_page_up = 0x74,
            vk_forward_delete = 0x75,
            vk_f4 = 0x76,
            vk_end = 0x77,
            vk_f2 = 0x78,
            vk_page_down = 0x79,
            vk_f1 = 0x7A,
            vk_left = 0x7B,
            vk_right = 0x7C,
            vk_down = 0x7D,
            vk_up = 0x7E,
        };

        // Carbon 수정자다 (`Events.h`의 `cmdKey`·`shiftKey`·`optionKey`).
        enum : std::uint32_t
        {
            carbon_command = 1U << 8,
            carbon_shift = 1U << 9,
            carbon_option = 1U << 11,
        };

        // 글자를 만들지 않는 키다. 수정자와 상관없이 늘 키로 간다.
        [[nodiscard]] key_code control_key(const std::uint16_t code) noexcept
        {
            switch (code)
            {
            case vk_return:
            case vk_keypad_enter:
                return key_code::enter;
            case vk_tab:
                return key_code::tab;
            case vk_escape:
                return key_code::escape;
            case vk_delete:
                return key_code::backspace;
            case vk_forward_delete:
                return key_code::delete_forward;
            case vk_help:
                // 옛 확장 키보드에서 Insert 자리에 있던 키다.
                return key_code::insert;
            case vk_home:
                return key_code::home;
            case vk_end:
                return key_code::end;
            case vk_page_up:
                return key_code::page_up;
            case vk_page_down:
                return key_code::page_down;
            case vk_left:
                return key_code::arrow_left;
            case vk_right:
                return key_code::arrow_right;
            case vk_up:
                return key_code::arrow_up;
            case vk_down:
                return key_code::arrow_down;
            case vk_f1:
                return key_code::f1;
            case vk_f2:
                return key_code::f2;
            case vk_f3:
                return key_code::f3;
            case vk_f4:
                return key_code::f4;
            case vk_f5:
                return key_code::f5;
            case vk_f6:
                return key_code::f6;
            case vk_f7:
                return key_code::f7;
            case vk_f8:
                return key_code::f8;
            case vk_f9:
                return key_code::f9;
            case vk_f10:
                return key_code::f10;
            case vk_f11:
                return key_code::f11;
            case vk_f12:
                return key_code::f12;
            default:
                return key_code::none;
            }
        }

        // 글자를 만드는 키의 자리 이름이다 (미국 배열).
        [[nodiscard]] key_code positional_key(const std::uint16_t code) noexcept
        {
            switch (code)
            {
            case vk_a:
                return key_code::key_a;
            case vk_b:
                return key_code::key_b;
            case vk_c:
                return key_code::key_c;
            case vk_d:
                return key_code::key_d;
            case vk_e:
                return key_code::key_e;
            case vk_f:
                return key_code::key_f;
            case vk_g:
                return key_code::key_g;
            case vk_h:
                return key_code::key_h;
            case vk_i:
                return key_code::key_i;
            case vk_j:
                return key_code::key_j;
            case vk_k:
                return key_code::key_k;
            case vk_l:
                return key_code::key_l;
            case vk_m:
                return key_code::key_m;
            case vk_n:
                return key_code::key_n;
            case vk_o:
                return key_code::key_o;
            case vk_p:
                return key_code::key_p;
            case vk_q:
                return key_code::key_q;
            case vk_r:
                return key_code::key_r;
            case vk_s:
                return key_code::key_s;
            case vk_t:
                return key_code::key_t;
            case vk_u:
                return key_code::key_u;
            case vk_v:
                return key_code::key_v;
            case vk_w:
                return key_code::key_w;
            case vk_x:
                return key_code::key_x;
            case vk_y:
                return key_code::key_y;
            case vk_z:
                return key_code::key_z;
            case vk_0:
                return key_code::key_0;
            case vk_1:
                return key_code::key_1;
            case vk_2:
                return key_code::key_2;
            case vk_3:
                return key_code::key_3;
            case vk_4:
                return key_code::key_4;
            case vk_5:
                return key_code::key_5;
            case vk_6:
                return key_code::key_6;
            case vk_7:
                return key_code::key_7;
            case vk_8:
                return key_code::key_8;
            case vk_9:
                return key_code::key_9;
            case vk_space:
                return key_code::space;
            case vk_minus:
                return key_code::minus;
            case vk_equal:
                return key_code::equal;
            case vk_comma:
                return key_code::comma;
            case vk_period:
                return key_code::period;
            case vk_slash:
                return key_code::slash;
            case vk_semicolon:
                return key_code::semicolon;
            case vk_quote:
                return key_code::quote;
            case vk_bracket_left:
                return key_code::bracket_left;
            case vk_bracket_right:
                return key_code::bracket_right;
            case vk_backslash:
                return key_code::backslash;
            case vk_grave:
                return key_code::grave;
            case vk_keypad_0:
                return key_code::numpad_0;
            case vk_keypad_1:
                return key_code::numpad_1;
            case vk_keypad_2:
                return key_code::numpad_2;
            case vk_keypad_3:
                return key_code::numpad_3;
            case vk_keypad_4:
                return key_code::numpad_4;
            case vk_keypad_5:
                return key_code::numpad_5;
            case vk_keypad_6:
                return key_code::numpad_6;
            case vk_keypad_7:
                return key_code::numpad_7;
            case vk_keypad_8:
                return key_code::numpad_8;
            case vk_keypad_9:
                return key_code::numpad_9;
            case vk_keypad_plus:
                return key_code::numpad_add;
            case vk_keypad_minus:
                return key_code::numpad_subtract;
            case vk_keypad_multiply:
                return key_code::numpad_multiply;
            case vk_keypad_divide:
                return key_code::numpad_divide;
            case vk_keypad_decimal:
                return key_code::numpad_decimal;
            default:
                return key_code::none;
            }
        }
    }

    bool character_key(const std::uint16_t key_code) noexcept
    {
        return positional_key(key_code) != luil::key_code::none;
    }

    key_code named_key(const key_input& input) noexcept
    {
        if (const key_code control { control_key(input.key_code) }; control != key_code::none)
            return control;
        // 배열이 영문자를 내면 그 글자가 단축키의 이름이다 (드보락의 Command+C는 C 글자 자리다).
        //  - 키패드 숫자는 자리 이름으로 둔다. 같은 "1"이라도 키패드 키다.
        const key_code positional { positional_key(input.key_code) };
        const bool keypad { positional >= key_code::numpad_0 && positional <= key_code::numpad_decimal };
        if (keypad == false)
        {
            char32_t character { input.base_character };
            if (character >= U'a' && character <= U'z')
                character -= U'a' - U'A';
            if ((character >= U'A' && character <= U'Z') || (character >= U'0' && character <= U'9'))
                return alphanumeric_key_code(static_cast<std::uint32_t>(character));
        }
        return positional;
    }

    std::optional<key_pressed_event> key_event_for(const key_input& input, const bool key_capture) noexcept
    {
        key_code key { named_key(input) };
        if (key == key_code::none)
            return std::nullopt;
        bool primary { input.command };
        bool command { input.command };
        // Command+화살표는 줄·글의 처음과 끝이다. Command를 단 화살표를 앱 단축키로 보내지 않는다.
        if (input.command && input.control == false)
            switch (key)
            {
            case key_code::arrow_left:
                key = key_code::home;
                primary = false;
                command = false;
                break;
            case key_code::arrow_right:
                key = key_code::end;
                primary = false;
                command = false;
                break;
            case key_code::arrow_up:
                key = key_code::home;
                command = false;
                break;
            case key_code::arrow_down:
                key = key_code::end;
                command = false;
                break;
            default:
                break;
            }
        // 글자 키는 단축키 수정자가 있을 때만 키다. Space는 실행 키라 늘 키다 (Win32와 같다).
        if (character_key(input.key_code) && key != key_code::space && input.command == false && input.control == false && key_capture == false)
            return std::nullopt;
        key_pressed_event event { key, input.control, input.shift, input.option, input.repeat, input.time };
        event.primary_shortcut = primary;
        event.word_navigation = input.option;
        event.meta = command;
        return event;
    }

    bool backspace_key(const std::uint16_t key_code) noexcept
    {
        return key_code == vk_delete;
    }

    char32_t backspace_character(const key_input& input) noexcept
    {
        return input.option ? char32_t { 0x7F } : U'\b';
    }

    std::optional<std::uint16_t> carbon_key_of(const key_code key) noexcept
    {
        if (key == key_code::none)
            return std::nullopt;
        // 플랫폼 대역의 Win32 F13~F20(`VK_F13`=0x7C)이다. F21 이상은 Carbon 자리가 없다.
        constexpr std::uint32_t first_platform { static_cast<std::uint32_t>(key_code::first_platform_key) };
        if (const std::uint32_t value { static_cast<std::uint32_t>(key) }; value >= first_platform)
        {
            constexpr std::array<std::uint16_t, 8> function_keys { vk_f13, vk_f14, vk_f15, vk_f16, vk_f17, vk_f18, vk_f19, vk_f20 };
            const std::uint32_t virtual_key { value - first_platform };
            if (virtual_key >= 0x7C && virtual_key < 0x7C + function_keys.size())
                return function_keys[virtual_key - 0x7C];
            return std::nullopt;
        }
        // 두 표를 차례로 훑는다. 같은 이름을 내는 자리가 둘인 것은 Enter뿐이고 작은 값(Return)이 먼저다.
        for (std::uint16_t code = 0; code < 0x80; ++code)
        {
            if (control_key(code) == key || positional_key(code) == key)
                return code;
        }
        return std::nullopt;
    }

    std::uint32_t carbon_hotkey_modifiers_of(const key_chord& chord) noexcept
    {
        std::uint32_t modifiers { 0 };
        if (chord.control)
            modifiers |= carbon_command;
        if (chord.shift)
            modifiers |= carbon_shift;
        if (chord.alt)
            modifiers |= carbon_option;
        return modifiers;
    }
}
