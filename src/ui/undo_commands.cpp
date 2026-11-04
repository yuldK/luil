#include "luil/ui/undo_commands.h"

#include "ui/default_text.h"

#include <utility>

namespace luil {
    std::array<app_command, 2> make_undo_commands(undo_command_state state)
    {
        // 앱이 글을 주지 않으면 기본 문구다. 빈 값의 판정은 `empty()`뿐이다 (default_text의 규칙).
        if (state.undo_label.empty())
            state.undo_label = default_text::undo_command_undo();
        if (state.redo_label.empty())
            state.redo_label = default_text::undo_command_redo();
        // key_chord는 hotkey 설계가 칸을 더할 수 있어 지정 초기화로 짓는다.
        app_command undo {
            .id = std::u8string { undo_command_id },
            .label = std::move(state.undo_label),
            .shortcut = { .key = key_code::key_z, .control = true },
            .scope = command_scope::window,
            .enabled = state.can_undo,
        };
        app_command redo {
            .id = std::u8string { redo_command_id },
            .label = std::move(state.redo_label),
            .shortcut = { .key = key_code::key_y, .control = true },
            .scope = command_scope::window,
            .enabled = state.can_redo,
            .extra_shortcuts = { { .key = key_code::key_z, .control = true, .shift = true } },
        };
        return { std::move(undo), std::move(redo) };
    }
}
