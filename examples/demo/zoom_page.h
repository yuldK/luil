#pragma once

#include "demo/common.h"
#include "luil/ui/zoom_view_element.h"

namespace demo {
    struct zoom_intent
    {
        float factor { 1.0f };
        luil::zoom_point anchor {};
    };
    struct pan_intent
    {
        luil::zoom_point delta {};
    };
    struct zoom_to_intent
    {
        float value { 1.0f };
    };
    // 도면을 `degrees`만큼 돌리자는 메시지다. 두 손가락, Shift+←/→/Home, 단추가 같은 것을 보낸다.
    struct rotate_intent
    {
        float degrees { 0.0f };
        luil::zoom_point anchor {};
    };
    class zoom_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

    private:
        [[nodiscard]] luil::zoom_view_config config() const;
        luil::zoom_view_state state_ { 1.0f, {} };
        luil::zoom_point viewport_ {};
    };
}
