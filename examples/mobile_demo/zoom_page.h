#pragma once

#include "luil/ui/zoom_view_element.h"
#include "mobile_demo/common.h"

#include <optional>

namespace mobile_demo {
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
    // 보기가 알린 경계 쓸기다. 가로면 쪽을 넘긴다.
    struct edge_swipe_intent
    {
        luil::zoom_edge_swipe swipe {};
    };
    // 쪽 보기다: 도면 한 장이 한 쪽이고, 맞춤 상태에서 가로로 쓸거나 확대한 채 끝에 닿은 뒤 한 번 더 쓸면 다음
    // 쪽으로 간다. 받은 경계 쓸기를 화면에 적는다. 두 번 탭은 맞춤과 두 배를 오간다.
    class zoom_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

    private:
        [[nodiscard]] luil::zoom_view_config config() const;
        luil::zoom_view_state state_ { 1.0f, {} };
        luil::zoom_point viewport_ {};
        // 처음 보는 창에서 쪽을 맞춤으로 연다.
        bool fitted_ { false };
        int page_ { 1 };
        std::optional<luil::zoom_edge_swipe> last_swipe_ {};
    };
}
