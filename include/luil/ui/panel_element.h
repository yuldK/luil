#pragma once

#include "luil/ui/ui_element.h"

#include <functional>
#include <memory>

namespace luil {
    // 배경 panel의 설정이다.
    struct panel_config
    {
        // 팔레트에서 배경색을 고른다.
        // 비어 있으면 surface_background다 — 구체 색이 아니라 역할을 고르므로
        // 테마가 바뀌어도 element는 그대로다.
        std::function<ui_color(const ui_color_palette&)> background {};
        // 모서리 반지름이다 (논리 픽셀). 0이면 직각이다.
        float corner_radius { 0.0f };
    };

    // 배경을 칠하고 content 하나를 그 안에 배치하는 컨테이너다.
    // 카드·dialog 본문·목록 패널처럼 "색 있는 바탕 위 내용"의 공통 꼴이다.
    class panel_element final : public ui_element
    {
    public:
        panel_element(ui_element_id id, panel_config config);

        // 내용은 panel의 slot 전체를 받는다.
        // 안쪽 여백이 필요하면 content로 padding 있는 stack을 넣는다.
        void set_content(std::unique_ptr<ui_element> content);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        panel_config config_ {};
        ui_element* content_ { nullptr };
    };
} // namespace luil
