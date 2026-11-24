#pragma once

#include "luil/ui/ui_events.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace luil::macos {
    // 키 이벤트 하나에서 복사해 둔 값이다. AppKit을 모르게 둔다 — 변환을 core test로 돌린다.
    struct key_input
    {
        // 하드웨어 가상 키다 (Carbon `kVK_*`, 배열과 상관없는 자리).
        std::uint16_t key_code { 0 };
        // 수정자를 뺀 글자의 첫 글자다 (`charactersIgnoringModifiers`). 단축키 이름을 배열에 맞춘다.
        char32_t base_character { 0 };
        bool command { false };
        bool control { false };
        bool option { false };
        bool shift { false };
        bool repeat { false };
        std::chrono::steady_clock::time_point time {};
    };

    // 가상 키 하나의 이름 키다. 글자를 만드는 키는 수정자가 있을 때만 쓰는 이름이다 (`key_event_for`).
    //  - 영문자·숫자는 `base_character`가 ASCII면 그 글자로, 아니면(한글 배열 등) 자리로 고른다.
    [[nodiscard]] key_code named_key(const key_input& input) noexcept;

    // 글자를 만드는 키인가 (영문자·숫자·구두점·키패드·Space). 수정자 없이는 글자 경로(`insertText:`)가 맡는다.
    [[nodiscard]] bool character_key(std::uint16_t key_code) noexcept;

    // 키 누름 이벤트다. 보낼 것이 없으면 nullopt다.
    //  - Command가 주 단축키(`primary_shortcut`)이고 Option이 낱말 이동(`word_navigation`)이다. Control은 Control이다.
    //  - Command+←·→는 줄 처음·끝(Home·End), Command+↑·↓는 글 처음·끝(주 단축키를 단 Home·End)이다 (macOS의 글 편집 관례).
    //  - 글자 키는 Command·Control이 함께일 때, 또는 `key_capture`일 때만 키다. Option만 누른 글자 키는 글자(`option` 글자)다.
    [[nodiscard]] std::optional<key_pressed_event> key_event_for(const key_input& input, bool key_capture) noexcept;

    // Backspace(⌫)와 그 글자다. Option+⌫는 낱말 지우기(U+007F, Windows의 Ctrl+Backspace)다.
    [[nodiscard]] bool backspace_key(std::uint16_t key_code) noexcept;
    [[nodiscard]] char32_t backspace_character(const key_input& input) noexcept;

    // AppKit 기능 키의 사설 글자(U+F700~U+F8FF)다. `insertText:`로 와도 글자가 아니다.
    [[nodiscard]] constexpr bool function_key_character(const char32_t character) noexcept
    {
        return character >= 0xF700 && character <= 0xF8FF;
    }
}
