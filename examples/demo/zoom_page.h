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
} // namespace demo
