#include "luil/ui/commands.h"

#include <algorithm>

namespace luil {
    namespace {
        bool matches(const key_chord& chord, const key_pressed_event& event) noexcept
        {
            return chord.key != key_code::none && chord.key == event.key && chord.control == event.control
                && chord.shift == event.shift && chord.alt == event.alt && event.meta == false;
        }
    } // namespace

    std::optional<std::u8string> match_command(const std::span<const app_command> commands, const key_pressed_event& event, const command_scope active)
    {
        const auto find = [&](const command_scope scope) -> std::optional<std::u8string> {
            for (const auto& command : commands)
                if (command.enabled && command.scope == scope && matches(command.shortcut, event))
                    return command.id;
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
        std::u8string key {};
        const std::uint32_t platform { platform_key_of(chord.key) };
        // 플랫폼 대역(`first_platform_key` 위)은 이름으로 옮기지 않는다. 그 값의 뜻이 플랫폼마다 달라(Windows는 VK,
        // Android는 AKEYCODE) 한 표로 읽으면 Android의 SysRq·미디어 키가 F 키로 적힌다. F1–F12는 두 host 모두
        // 이름 키로 보낸다.
        if ((platform >= 'A' && platform <= 'Z') || (platform >= '0' && platform <= '9'))
            key.push_back(static_cast<char8_t>(platform));
        else
        {
            switch (chord.key)
            {
            case key_code::enter: key = u8"Enter"; break;
            case key_code::escape: key = u8"Esc"; break;
            case key_code::tab: key = u8"Tab"; break;
            case key_code::space: key = u8"Space"; break;
            case key_code::delete_forward: key = u8"Delete"; break;
            case key_code::insert: key = u8"Insert"; break;
            case key_code::home: key = u8"Home"; break;
            case key_code::end: key = u8"End"; break;
            case key_code::page_up: key = u8"PageUp"; break;
            case key_code::page_down: key = u8"PageDown"; break;
            case key_code::arrow_left: key = u8"Left"; break;
            case key_code::arrow_right: key = u8"Right"; break;
            case key_code::arrow_up: key = u8"Up"; break;
            case key_code::arrow_down: key = u8"Down"; break;
            case key_code::f1: key = u8"F1"; break;
            case key_code::f2: key = u8"F2"; break;
            case key_code::f3: key = u8"F3"; break;
            case key_code::f4: key = u8"F4"; break;
            case key_code::f5: key = u8"F5"; break;
            case key_code::f6: key = u8"F6"; break;
            case key_code::f7: key = u8"F7"; break;
            case key_code::f8: key = u8"F8"; break;
            case key_code::f9: key = u8"F9"; break;
            case key_code::f10: key = u8"F10"; break;
            case key_code::f11: key = u8"F11"; break;
            case key_code::f12: key = u8"F12"; break;
            default: return {};
            }
        }
        std::u8string result {};
        if (chord.control)
            result += u8"Ctrl+";
        if (chord.shift)
            result += u8"Shift+";
        if (chord.alt)
            result += u8"Alt+";
        return result + key;
    }

    menu_item_config menu_item_from(const app_command& command)
    {
        menu_item_config item {};
        item.key = command.id;
        item.label = command.label;
        item.enabled = command.enabled;
        item.kind = command.checkable || command.checked ? menu_item_kind::check : menu_item_kind::action;
        item.checked = command.checked;
        item.shortcut_text = shortcut_label(command.shortcut);
        return item;
    }

    std::vector<chord_conflict> find_conflicts(const std::span<const app_command> commands)
    {
        std::vector<chord_conflict> result {};
        for (std::size_t first { 0 }; first < commands.size(); ++first)
            for (std::size_t second { first + 1 }; second < commands.size(); ++second)
                if (commands[first].shortcut.key != key_code::none && commands[first].scope == commands[second].scope
                    && commands[first].shortcut == commands[second].shortcut)
                    result.push_back({ commands[first].id, commands[second].id, commands[first].scope });
        return result;
    }
} // namespace luil
