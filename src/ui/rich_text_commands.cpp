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
