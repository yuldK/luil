#include "luil/ui/rich_text_commands.h"

#include "ui/default_text.h"

#include <utility>

namespace luil {
    namespace {
        // key_chord는 끝에 칸이 더해져도 깨지지 않게 지정 초기화로 짓는다.
        [[nodiscard]] app_command format_command(const std::u8string_view id, const std::u8string& label, const std::u8string_view fallback, const key_code key, const bool checkable, const bool checked)
        {
            return app_command {
                .id = std::u8string { id },
                .label = std::u8string { default_text::text_or_default(label, fallback) },
                .shortcut = { .key = key, .control = key != key_code::none },
                .scope = command_scope::focused,
                .enabled = true,
                .checked = checkable && checked,
                .checkable = checkable,
            };
        }

        // 칸이 넘긴 복사·잘라내기·붙여넣기 키다 (docs/rich-edit-exchange-design.md). 명령표와 따로 고정이다.
        //  - Ctrl+C·X·V(Shift 포함, Ctrl+Shift+V는 서식 없이 붙이기), Ctrl+Insert(Shift 포함), Shift+Insert, Shift+Delete다.
        [[nodiscard]] std::optional<rich_text_command> rich_text_clipboard_command_of(const key_pressed_event& event) noexcept
        {
            if (event.alt || event.meta)
                return std::nullopt;
            const bool primary { event.primary_shortcut_down() };
            if (primary)
            {
                switch (event.key)
                {
                case key_code::key_c:
                    return rich_text_command::copy;
                case key_code::key_x:
                    return rich_text_command::cut;
                case key_code::key_v:
                    return event.shift ? rich_text_command::paste_plain : rich_text_command::paste;
                case key_code::insert:
                    // controller가 Insert에 Ctrl이나 Shift가 서면 칸 밖으로 넘긴다. 플래그 없는 칸의 Insert 갈래가
                    // Ctrl을 먼저 보듯 Ctrl+Insert는 Shift와 상관없이 복사이고, 넘어온 키를 버려 흘리지 않는다.
                    return rich_text_command::copy;
                default:
                    return std::nullopt;
                }
            }
            if (event.shift && event.control == false)
            {
                if (event.key == key_code::insert)
                    return rich_text_command::paste;
                if (event.key == key_code::delete_forward)
                    return rich_text_command::cut;
            }
            return std::nullopt;
        }
    }

    std::vector<app_command> make_rich_text_commands(const rich_text_state& state, const rich_text_command_labels& labels)
    {
        std::vector<app_command> commands {};
        commands.reserve(6);
        commands.push_back(format_command(rich_text_bold_command_id, labels.bold, default_text::rich_text_bold(), key_code::key_b, true, state.bold));
        commands.push_back(format_command(rich_text_italic_command_id, labels.italic, default_text::rich_text_italic(), key_code::key_i, true, state.italic));
        commands.push_back(format_command(rich_text_underline_command_id, labels.underline, default_text::rich_text_underline(), key_code::key_u, true, state.underline));
        commands.push_back(format_command(rich_text_code_command_id, labels.code, default_text::rich_text_code(), key_code::none, true, state.code));
        commands.push_back(format_command(rich_text_link_command_id, labels.link, default_text::rich_text_link(), key_code::key_k, true, state.link));
        commands.push_back(format_command(rich_text_clear_format_command_id, labels.clear_format, default_text::rich_text_clear_format(), key_code::none, false, false));
        return commands;
    }

    std::optional<rich_text_command> rich_text_command_of(const std::u8string_view id)
    {
        if (id == rich_text_bold_command_id)
            return rich_text_command::bold;
        if (id == rich_text_italic_command_id)
            return rich_text_command::italic;
        if (id == rich_text_underline_command_id)
            return rich_text_command::underline;
        if (id == rich_text_code_command_id)
            return rich_text_command::code;
        if (id == rich_text_link_command_id)
            return rich_text_command::link;
        if (id == rich_text_clear_format_command_id)
            return rich_text_command::clear_format;
        return std::nullopt;
    }

    key_handler make_rich_text_key_handler(std::vector<app_command> commands, std::function<input_action(rich_text_command)> make_action)
    {
        if (make_action == nullptr)
            return {};
        return [commands = std::move(commands), make_action = std::move(make_action)](const key_pressed_event& event) -> std::optional<std::vector<input_action>> {
            // 편집의 표준 키는 명령표보다 먼저 본다. 사용자가 다시 정하지 않는다.
            if (const std::optional<rich_text_command> clipboard { rich_text_clipboard_command_of(event) }; clipboard.has_value())
                return std::vector<input_action> { make_action(*clipboard) };
            const std::optional<std::u8string> id { match_command(commands, event, command_scope::focused) };
            if (id.has_value() == false)
                return std::nullopt;
            const std::optional<rich_text_command> command { rich_text_command_of(*id) };
            if (command.has_value() == false)
                return std::nullopt;
            return std::vector<input_action> { make_action(*command) };
        };
    }
}
