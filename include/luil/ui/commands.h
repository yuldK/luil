#pragma once

#include "luil/ui/menu_element.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace luil {
    struct key_chord
    {
        key_code key { key_code::none };
        bool control { false };
        bool shift { false };
        bool alt { false };
        [[nodiscard]] bool operator==(const key_chord&) const = default;
    };

    enum class command_scope { window, focused, modal };

    struct app_command
    {
        std::u8string id {};
        std::u8string label {};
        key_chord shortcut {};
        command_scope scope { command_scope::window };
        bool enabled { true };
        bool checked { false };
        // 꺼진 체크 명령도 표시할 수 있어야 한다.
        bool checkable { false };
    };

    struct chord_conflict
    {
        std::u8string first {};
        std::u8string second {};
        command_scope scope { command_scope::window };
    };

    // 가장 안쪽 컨트롤이 소비하지 않은 키를 앱 정책의 마지막 단계에서 맞춘다.
    // modal 범위는 window 명령으로 물러서지 않는다.
    [[nodiscard]] std::optional<std::u8string> match_command(std::span<const app_command> commands, const key_pressed_event& event, command_scope active);
    [[nodiscard]] menu_item_config menu_item_from(const app_command& command);
    [[nodiscard]] std::vector<chord_conflict> find_conflicts(std::span<const app_command> commands);
    [[nodiscard]] std::u8string shortcut_label(const key_chord& chord);
} // namespace luil
