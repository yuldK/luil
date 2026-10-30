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
        // 기본 단축키다. 메뉴·tooltip에 보이는 것은 실제로 쓰이는 첫 단축키다 (`effective_shortcuts`).
        key_chord shortcut {};
        command_scope scope { command_scope::window };
        bool enabled { true };
        bool checked { false };
        // 꺼진 체크 명령도 표시할 수 있어야 한다.
        bool checkable { false };
        // 기본 단축키 말고도 같은 명령을 부르는 단축키다 (Ctrl+Y와 Ctrl+Shift+Z처럼).
        std::vector<key_chord> extra_shortcuts {};
        // 사용자가 다시 정한 단축키다. 있으면 기본·추가 단축키 대신 이것만 쓴다 (빈 목록이면 단축키가 없다).
        //  - 값은 앱이 저장한다. 문자열로 저장하려면 `shortcut_label`·`parse_shortcut`이 왕복한다.
        std::optional<std::vector<key_chord>> user_shortcuts {};
    };

    struct chord_conflict
    {
        std::u8string first {};
        std::u8string second {};
        command_scope scope { command_scope::window };
        // 두 명령이 함께 쓰는 단축키다.
        key_chord chord {};
    };

    // 명령이 실제로 쓰는 단축키다. 사용자가 다시 정했으면 그것이고, 아니면 기본 단축키와 추가 단축키다.
    // 비어 있는(`key_code::none`) 것과 겹친 것은 뺀다.
    [[nodiscard]] std::vector<key_chord> effective_shortcuts(const app_command& command);

    // 가장 안쪽 컨트롤이 소비하지 않은 키를 앱 정책의 마지막 단계에서 맞춘다.
    // modal 범위는 window 명령으로 물러서지 않는다.
    [[nodiscard]] std::optional<std::u8string> match_command(std::span<const app_command> commands, const key_pressed_event& event, command_scope active);
    [[nodiscard]] menu_item_config menu_item_from(const app_command& command);
    // 같은 범위에서 같은 단축키를 쓰는 명령 쌍이다. 명령마다 실제로 쓰는 단축키 전부를 본다.
    [[nodiscard]] std::vector<chord_conflict> find_conflicts(std::span<const app_command> commands);

    // 단축키의 글이다: "Ctrl+Shift+P", "Ctrl+-", "Alt+Num5". 이름이 없는 키(플랫폼 대역)는 빈 글이다.
    //  - 수정자는 언제나 Ctrl·Shift·Alt 차례다.
    [[nodiscard]] std::u8string shortcut_label(const key_chord& chord);
    // `shortcut_label`의 반대다. 대소문자를 가리지 않고 "Control"·"Escape"·"Del"·"Return"도 받는다.
    // 모르는 이름·키 없는 글·같은 수정자를 두 번 쓴 글은 없다(nullopt).
    //  - `parse_shortcut(shortcut_label(chord)) == chord`가 이름 있는 모든 키에서 성립한다 (구두점·키패드 포함).
    [[nodiscard]] std::optional<key_chord> parse_shortcut(std::u8string_view text);

    // tooltip 끝에 단축키를 괄호로 붙인다: "닫기 (Ctrl+W)". 단축키가 없으면 글 그대로다 (app-screens 규칙).
    [[nodiscard]] std::u8string label_with_shortcut(std::u8string_view label, const key_chord& chord);
    // 명령의 tooltip이다. 실제로 쓰는 첫 단축키를 붙인다.
    [[nodiscard]] std::u8string command_tooltip(const app_command& command);

    // OS가 예약한 조합인가 (Alt+F4·Alt+Tab·Alt+Esc·Ctrl+Esc·Alt+Space·Ctrl+Alt+Delete).
    // 키 잡기 칸(`key_capture_target`)이 받아도 단축키로 정하지 말고 예약 키라고 보여 준다. 그 가운데 일부는
    // OS가 먼저 가져가 아예 오지 않는다.
    [[nodiscard]] bool system_reserved_chord(const key_chord& chord) noexcept;
    // 키 이벤트의 단축키다 (잡은 키를 저장할 때).
    [[nodiscard]] key_chord chord_of(const key_pressed_event& event) noexcept;
}
