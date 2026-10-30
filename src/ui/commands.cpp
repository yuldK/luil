#include "luil/ui/commands.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace luil {
    namespace {
        bool matches(const key_chord& chord, const key_pressed_event& event) noexcept
        {
            return chord.key != key_code::none && chord.key == event.key && chord.control == event.control
                && chord.shift == event.shift && chord.alt == event.alt && event.meta == false;
        }

        struct key_name
        {
            key_code key { key_code::none };
            std::u8string_view label {};
        };

        // 영문자·숫자 밖의 이름 키의 글이다. 라벨과 해석이 이 표 하나를 써서 왕복이 언제나 맞는다.
        //  - 플랫폼 대역(`first_platform_key` 위)은 이름으로 옮기지 않는다. 그 값의 뜻이 플랫폼마다 달라(Windows는 VK,
        //    Android는 AKEYCODE) 한 표로 읽으면 Android의 SysRq·미디어 키가 F 키로 적힌다.
        //  - 구두점은 미국 배열의 글자다 (VS Code·Windows 메뉴와 같다).
        constexpr std::array key_names {
            key_name { key_code::enter, u8"Enter" },
            key_name { key_code::escape, u8"Esc" },
            key_name { key_code::tab, u8"Tab" },
            key_name { key_code::space, u8"Space" },
            key_name { key_code::backspace, u8"Backspace" },
            key_name { key_code::delete_forward, u8"Delete" },
            key_name { key_code::insert, u8"Insert" },
            key_name { key_code::home, u8"Home" },
            key_name { key_code::end, u8"End" },
            key_name { key_code::page_up, u8"PageUp" },
            key_name { key_code::page_down, u8"PageDown" },
            key_name { key_code::arrow_left, u8"Left" },
            key_name { key_code::arrow_right, u8"Right" },
            key_name { key_code::arrow_up, u8"Up" },
            key_name { key_code::arrow_down, u8"Down" },
            key_name { key_code::f1, u8"F1" },
            key_name { key_code::f2, u8"F2" },
            key_name { key_code::f3, u8"F3" },
            key_name { key_code::f4, u8"F4" },
            key_name { key_code::f5, u8"F5" },
            key_name { key_code::f6, u8"F6" },
            key_name { key_code::f7, u8"F7" },
            key_name { key_code::f8, u8"F8" },
            key_name { key_code::f9, u8"F9" },
            key_name { key_code::f10, u8"F10" },
            key_name { key_code::f11, u8"F11" },
            key_name { key_code::f12, u8"F12" },
            key_name { key_code::minus, u8"-" },
            key_name { key_code::equal, u8"=" },
            key_name { key_code::comma, u8"," },
            key_name { key_code::period, u8"." },
            key_name { key_code::slash, u8"/" },
            key_name { key_code::semicolon, u8";" },
            key_name { key_code::quote, u8"'" },
            key_name { key_code::bracket_left, u8"[" },
            key_name { key_code::bracket_right, u8"]" },
            key_name { key_code::backslash, u8"\\" },
            key_name { key_code::grave, u8"`" },
            key_name { key_code::numpad_0, u8"Num0" },
            key_name { key_code::numpad_1, u8"Num1" },
            key_name { key_code::numpad_2, u8"Num2" },
            key_name { key_code::numpad_3, u8"Num3" },
            key_name { key_code::numpad_4, u8"Num4" },
            key_name { key_code::numpad_5, u8"Num5" },
            key_name { key_code::numpad_6, u8"Num6" },
            key_name { key_code::numpad_7, u8"Num7" },
            key_name { key_code::numpad_8, u8"Num8" },
            key_name { key_code::numpad_9, u8"Num9" },
            key_name { key_code::numpad_add, u8"Num+" },
            key_name { key_code::numpad_subtract, u8"Num-" },
            key_name { key_code::numpad_multiply, u8"Num*" },
            key_name { key_code::numpad_divide, u8"Num/" },
            key_name { key_code::numpad_decimal, u8"Num." },
        };

        // 해석만 받는 다른 이름이다. 라벨은 늘 위 표의 글을 쓴다.
        constexpr std::array key_aliases {
            key_name { key_code::escape, u8"Escape" },
            key_name { key_code::delete_forward, u8"Del" },
            key_name { key_code::enter, u8"Return" },
        };

        [[nodiscard]] char8_t lowered(const char8_t character) noexcept
        {
            return character >= u8'A' && character <= u8'Z' ? static_cast<char8_t>(character - u8'A' + u8'a') : character;
        }

        [[nodiscard]] bool same_name(const std::u8string_view first, const std::u8string_view second) noexcept
        {
            return first.size() == second.size() && std::equal(first.begin(), first.end(), second.begin(), [](const char8_t a, const char8_t b) { return lowered(a) == lowered(b); });
        }

        [[nodiscard]] bool starts_with_name(const std::u8string_view text, const std::u8string_view prefix) noexcept
        {
            return text.size() >= prefix.size() && same_name(text.substr(0, prefix.size()), prefix);
        }

        [[nodiscard]] std::u8string key_text(const key_code key)
        {
            const std::uint32_t platform { platform_key_of(key) };
            if ((platform >= 'A' && platform <= 'Z') || (platform >= '0' && platform <= '9'))
                return std::u8string(1, static_cast<char8_t>(platform));
            for (const key_name& name : key_names)
                if (name.key == key)
                    return std::u8string { name.label };
            return {};
        }

        [[nodiscard]] key_code key_from_text(const std::u8string_view text) noexcept
        {
            if (text.size() == 1)
            {
                const char8_t character { lowered(text[0]) };
                if (character >= u8'a' && character <= u8'z')
                    return alphanumeric_key_code(static_cast<std::uint32_t>(u8'A' + (character - u8'a')));
                if (character >= u8'0' && character <= u8'9')
                    return alphanumeric_key_code(static_cast<std::uint32_t>(character));
            }
            for (const key_name& name : key_names)
                if (same_name(name.label, text))
                    return name.key;
            for (const key_name& name : key_aliases)
                if (same_name(name.label, text))
                    return name.key;
            return key_code::none;
        }
    }

    std::vector<key_chord> effective_shortcuts(const app_command& command)
    {
        std::vector<key_chord> result {};
        const auto add = [&result](const key_chord& chord) {
            if (chord.key != key_code::none && std::find(result.begin(), result.end(), chord) == result.end())
                result.push_back(chord);
        };
        if (command.user_shortcuts.has_value())
        {
            for (const key_chord& chord : *command.user_shortcuts)
                add(chord);
            return result;
        }
        add(command.shortcut);
        for (const key_chord& chord : command.extra_shortcuts)
            add(chord);
        return result;
    }

    std::optional<std::u8string> match_command(const std::span<const app_command> commands, const key_pressed_event& event, const command_scope active)
    {
        const auto find = [&](const command_scope scope) -> std::optional<std::u8string> {
            for (const auto& command : commands)
            {
                if (command.enabled == false || command.scope != scope)
                    continue;
                for (const key_chord& chord : effective_shortcuts(command))
                    if (matches(chord, event))
                        return command.id;
            }
            return std::nullopt;
        };
        if (auto id { find(active) }; id.has_value())
            return id;
        return active == command_scope::focused ? find(command_scope::window) : std::nullopt;
    }

    std::u8string shortcut_label(const key_chord& chord)
    {
        if (chord.key == key_code::none)
            return {};
        const std::u8string key { key_text(chord.key) };
        if (key.empty())
            return {};
        std::u8string result {};
        if (chord.control)
            result += u8"Ctrl+";
        if (chord.shift)
            result += u8"Shift+";
        if (chord.alt)
            result += u8"Alt+";
        return result + key;
    }

    std::optional<key_chord> parse_shortcut(std::u8string_view text)
    {
        // 앞뒤 공백은 저장한 값의 흔한 군더더기라 넘긴다.
        while (text.empty() == false && text.front() == u8' ')
            text.remove_prefix(1);
        while (text.empty() == false && text.back() == u8' ')
            text.remove_suffix(1);

        key_chord chord {};
        const std::array<std::pair<std::u8string_view, bool*>, 4> modifiers {
            std::pair<std::u8string_view, bool*> { u8"Ctrl+", &chord.control },
            std::pair<std::u8string_view, bool*> { u8"Control+", &chord.control },
            std::pair<std::u8string_view, bool*> { u8"Shift+", &chord.shift },
            std::pair<std::u8string_view, bool*> { u8"Alt+", &chord.alt },
        };
        // 수정자는 "이름+" 꼴로 앞에서 벗긴다. 남은 것이 키다 — "Num+"·"Ctrl+-"처럼 키 이름에 '+'·'-'가 들어 있어
        // '+'로 쪼개면 "Ctrl++"와 "Num+"를 가를 수 없다.
        for (;;)
        {
            bool* flag { nullptr };
            std::size_t length { 0 };
            for (const auto& [name, target] : modifiers)
                if (starts_with_name(text, name) && text.size() > name.size())
                {
                    flag = target;
                    length = name.size();
                    break;
                }
            if (flag == nullptr)
                break;
            if (*flag)
                return std::nullopt;
            *flag = true;
            text.remove_prefix(length);
        }
        chord.key = key_from_text(text);
        if (chord.key == key_code::none)
            return std::nullopt;
        return chord;
    }

    std::u8string label_with_shortcut(const std::u8string_view label, const key_chord& chord)
    {
        const std::u8string shortcut { shortcut_label(chord) };
        if (shortcut.empty())
            return std::u8string { label };
        return std::u8string { label } + u8" (" + shortcut + u8")";
    }

    std::u8string command_tooltip(const app_command& command)
    {
        const std::vector<key_chord> chords { effective_shortcuts(command) };
        return label_with_shortcut(command.label, chords.empty() ? key_chord {} : chords.front());
    }

    bool system_reserved_chord(const key_chord& chord) noexcept
    {
        if (chord.alt && chord.control == false && chord.shift == false)
            return chord.key == key_code::f4 || chord.key == key_code::tab || chord.key == key_code::escape || chord.key == key_code::space;
        if (chord.alt && chord.control && chord.key == key_code::delete_forward)
            return true;
        return chord.control && chord.alt == false && chord.shift == false && chord.key == key_code::escape;
    }

    key_chord chord_of(const key_pressed_event& event) noexcept
    {
        return key_chord { event.key, event.control, event.shift, event.alt };
    }

    menu_item_config menu_item_from(const app_command& command)
    {
        menu_item_config item {};
        item.key = command.id;
        item.label = command.label;
        item.enabled = command.enabled;
        item.kind = command.checkable || command.checked ? menu_item_kind::check : menu_item_kind::action;
        item.checked = command.checked;
        const std::vector<key_chord> chords { effective_shortcuts(command) };
        item.shortcut_text = chords.empty() ? std::u8string {} : shortcut_label(chords.front());
        return item;
    }

    std::vector<chord_conflict> find_conflicts(const std::span<const app_command> commands)
    {
        std::vector<chord_conflict> result {};
        std::vector<std::vector<key_chord>> chords {};
        chords.reserve(commands.size());
        for (const app_command& command : commands)
            chords.push_back(effective_shortcuts(command));
        for (std::size_t first { 0 }; first < commands.size(); ++first)
            for (std::size_t second { first + 1 }; second < commands.size(); ++second)
            {
                if (commands[first].scope != commands[second].scope)
                    continue;
                for (const key_chord& chord : chords[first])
                    if (std::find(chords[second].begin(), chords[second].end(), chord) != chords[second].end())
                        result.push_back({ commands[first].id, commands[second].id, commands[first].scope, chord });
            }
        return result;
    }
}
