#pragma once

// 두 데모가 함께 쓰는 읽기 전용 칸의 되돌리기 키 확인이다. 읽기 전용 칸에 초점이 있어도 Ctrl+Z·Ctrl+Y는 칸이
// 삼키지 않고 앱 on_key로 흘러, 앱의 실행 취소 stack이 받을 수 있다. 앱이 그 키를 세어 글로 보인다.

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"

#include <optional>
#include <string>

namespace demo {
    // 읽기 전용 칸에 초점이 있는 채 앱 on_key에 닿은 실행 취소(Ctrl+Z)나 다시 실행(Ctrl+Y, Ctrl+Shift+Z)이다.
    struct readonly_undo_key_intent
    {
        bool redo { false };
    };

    // 초점이 `field` 칸에 있는 채 앱 on_key에 닿은 키가 되돌리기 키이면 그 메시지를 돌려준다.
    //  - 편집 칸이면 칸이 이 키를 먼저 가져가 on_key에 닿지 않는다. 닿았다는 것이 읽기 전용 칸이 넘겼다는 증거다.
    [[nodiscard]] inline std::optional<readonly_undo_key_intent> readonly_undo_key_of(const luil::key_pressed_event& event, const luil::interaction_snapshot& snapshot, const luil::ui_element_kind field)
    {
        if (snapshot.focused_input.kind != field || event.primary_shortcut_down() == false)
            return std::nullopt;
        if (event.key == luil::key_code::key_z)
            return readonly_undo_key_intent { event.shift };
        if (event.key == luil::key_code::key_y)
            return readonly_undo_key_intent { true };
        return std::nullopt;
    }

    class readonly_undo_keys
    {
    public:
        bool handle(const luil::app_message& message)
        {
            const auto* const key { message.get<readonly_undo_key_intent>() };
            if (key == nullptr)
                return false;
            if (key->redo)
                ++redo_;
            else
                ++undo_;
            return true;
        }

        // label이 그리고 UIA Name으로 읽히는 글이다.
        [[nodiscard]] std::u8string text() const
        {
            return u8"읽기 전용 칸에서 앱이 받은 Ctrl+Z " + number(undo_) + u8"회 · Ctrl+Y " + number(redo_) + u8"회";
        }

        [[nodiscard]] int undo_count() const noexcept
        {
            return undo_;
        }

        [[nodiscard]] int redo_count() const noexcept
        {
            return redo_;
        }

    private:
        [[nodiscard]] static std::u8string number(const int value)
        {
            const std::string text { std::to_string(value) };
            return { text.begin(), text.end() };
        }

        int undo_ { 0 };
        int redo_ { 0 };
    };
}
