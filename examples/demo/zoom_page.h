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
    // 줌 단추의 글을 영어로 쓸지다. 누른 때의 반대 언어를 절대 값으로 담아 거듭 와도 결과가 같다.
    struct zoom_labels_intent
    {
        bool english { false };
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
        // 줌 단추의 글을 영어로 주는가. 거짓이면 config 칸을 비워 기본 문구가 서게 한다.
        bool english_labels_ { false };
    };
}
