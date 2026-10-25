#pragma once

#include "demo/common.h"
#include "luil/ui/commands.h"

namespace demo {
    struct gallery_toggle_intent
    {
        std::u8string key {};
    };

    struct gallery_value_intent
    {
        float value { 0.0f };
        bool relative { false };
        // 어느 막대의 값인지다. 비어 있으면 진행률 막대다.
        std::u8string key {};
    };

    // 단축키 잡기 칸이 받은 키다 (`ui_element::set_key_capture`). 비어 있으면 지우기다.
    struct gallery_shortcut_intent
    {
        luil::key_chord chord {};
    };

    // 머무름 카드의 미리보기를 연다·닫는다 (`ui_element::set_hover_dwell`).
    struct gallery_preview_intent
    {
        bool open { false };
    };

    // 숨은 도구 막대 띠에 포인터가 들었다·나갔다 (`ui_element::set_hover_region`).
    struct gallery_toolbar_intent
    {
        bool shown { false };
    };

    struct gallery_scroll_intent
    {
        bool controls { false };
        float delta { 0.0f };
    };

    struct gallery_reset_intent
    {};

    struct gallery_choice_intent
    {
        std::u8string value {};
    };

    // 기능 색인과 조작 가능한 컨트롤 갤러리다. 모든 상태는 앱에 있고 공용 요소를 조립한다.
    class gallery_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build_overview(float width);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build_controls();

    private:
        bool autosave_ { true };
        bool bold_ { false };
        bool italic_ { false };
        bool pinned_ { true };
        float value_ { 65.0f };
        // 자석 막대의 값이다: 볼륨(%), 좌우 균형(-100..100), 재생 위치(초).
        float volume_ { 72.0f };
        float balance_ { -18.0f };
        float position_ { 400.0f };
        // 단축키 잡기 칸의 값과, 예약 키를 눌렀을 때의 안내다.
        luil::key_chord shortcut_ { luil::key_code::key_p, true, true };
        std::u8string shortcut_note_ {};
        // 머무름 미리보기와 숨은 도구 막대의 표시 상태다.
        bool preview_open_ { false };
        bool toolbar_shown_ { false };
        float overview_scroll_ { 0.0f };
        float controls_scroll_ { 0.0f };
        std::u8string choice_ { u8"comfortable" };
    };
} // namespace demo
