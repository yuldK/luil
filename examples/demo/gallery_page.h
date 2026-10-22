#pragma once

#include "demo/common.h"

namespace demo {
    struct gallery_toggle_intent
    {
        std::u8string key {};
    };

    struct gallery_value_intent
    {
        float value { 0.0f };
        bool relative { false };
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
        float overview_scroll_ { 0.0f };
        float controls_scroll_ { 0.0f };
        std::u8string choice_ { u8"comfortable" };
    };
} // namespace demo
