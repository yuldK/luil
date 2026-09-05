#pragma once

#include "luil/ui/layout_metrics.h"
#include "luil/ui/ui_element.h"

#include <memory>

namespace luil {
    struct scroll_view_config
    {
        // 내용 전체 높이다 (논리 픽셀).
        // 측정 단계가 없으므로 담는 쪽이 알려 준다.
        float content_height { 0.0f };
        // 지금 흘러간 양이다 (논리 픽셀).
        // 범위 밖 값은 arrange가 다듬는다.
        float scroll_offset { 0.0f };
    };

    // 내용을 세로로 흘려 보여 주는 창이다.
    //
    // 내용은 한 덩어리로 받아 흘러간 만큼 위로 올린 자리에 배치한다.
    // 자기 bounds 밖은 그리기도 hit test도 잘라 내므로 넘치는 내용이 새지 않는다.
    // 스크롤 막대는 담지 않는다.
    //  - 막대를 둘지와 어디에 둘지는 배치의 문제라 담는 쪽이 정한다
    //    (`scrollbar_element`를 옆에 놓는다).
    //
    // 아주 긴 목록은 내용 컨테이너가 받은 slot으로 보이는 범위를 계산해
    // 걸치는 것만 자식으로 만들면 된다 (가상화는 내용의 몫이다).
    class scroll_view_element final : public ui_element
    {
    public:
        scroll_view_element(ui_element_id id, scroll_view_config config);

        void set_content(std::unique_ptr<ui_element> content);

        // 내용이 보이는 자리다 (물리 픽셀).
        // arrange 뒤에 유효하다.
        [[nodiscard]] const rect_f& viewport() const noexcept;
        // 이 창에서 흘릴 수 있는 최대치다 (논리 픽셀).
        // arrange 뒤에 유효하며 스크롤 값을 다듬을 때 쓴다.
        [[nodiscard]] float maximum_scroll() const noexcept;
        // arrange가 범위 안으로 다듬은 스크롤 값이다 (논리 픽셀).
        [[nodiscard]] float scroll_offset() const noexcept;

        // 그 자리를 창 안으로 들이는 스크롤 변화량이다 (논리 픽셀).
        // 세로 창이라 y·height만 본다. arrange 뒤에 유효하다
        // (배율을 `arrange`가 보관한다).
        [[nodiscard]] float scroll_delta_to_reveal(const rect_f& target) const override;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        scroll_view_config config_ {};
        ui_element* content_ { nullptr };
        rect_f viewport_ {};
        float maximum_scroll_ { 0.0f };
        // 물리 bounds를 논리 델타로 옮길 때 쓴다.
        // `arrange`가 채우므로 그 전에는 1이다.
        float scale_ { 1.0f };
    };
} // namespace luil
