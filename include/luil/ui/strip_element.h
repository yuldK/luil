#pragma once

#include "luil/ui/layout_metrics.h"
#include "luil/ui/ui_element.h"

#include <memory>

namespace luil {
    struct strip_config
    {
        // 내용 전체 폭이다 (논리 픽셀).
        // 측정 단계가 없으므로 담는 쪽이 알려 준다
        // (`scroll_view_config::content_height`와 같은 계약이다).
        float content_width { 0.0f };
        // 지금 흘러간 양이다 (논리 픽셀).
        // 범위 밖 값은 arrange가 다듬는다.
        float scroll_offset { 0.0f };
    };

    // 내용을 가로로 흘려 보여 주는 띠다.
    //
    // 내용은 한 덩어리로 받아 흘러간 만큼 왼쪽으로 민 자리에 배치한다.
    // 자기 bounds 밖은 그리기도 hit test도 잘라 내므로 넘치는 내용이 새지 않는다.
    // 항목을 늘어놓는 것은 내용의 몫이다 — 가로 `stack_element`를 넣는다.
    // 스크롤 막대도 한쪽 끝의 고정 버튼도 담지 않는다.
    //  - 무엇을 옆에 둘지는 배치의 문제라 담는 쪽이 정한다. "고정 폭 하나 +
    //    나머지"는 가로 stack 두 칸이라 조합으로 만들어진다.
    //  - 띠가 그 자리를 스스로 떼면 "얼마를 떼야 넘치는가"가 자기참조가 된다.
    //
    // `scroll_view_element`에 방향을 넣지 않고 따로 세운 이유는 둘이다.
    //  1. **문맥 계약이 다르다.** `arrange_context::scroll_offset`은 축이 없는
    //     스칼라이고 sticky 머리행이 그 값을 **y에** 더한다. 가로 값을 같은 자리에
    //     실으면 몇 계층 아래에서 조용히 어긋나므로, 이 띠는 자식 문맥을
    //     `for_child`로 그냥 잇고 **자기 스크롤 값을 싣지 않는다.** 세로 창이
    //     문맥을 새로 짓는 유일한 자리라는 계약도 그대로 남는다.
    //  2. **옆에 서는 것이 다르다.** 세로는 세로 전용 `scrollbar_element`와 짝이고
    //     가로는 넘침 버튼과 짝이다.
    class strip_element final : public ui_element
    {
    public:
        strip_element(ui_element_id id, strip_config config);

        void set_content(std::unique_ptr<ui_element> content);

        // 이 띠에서 흘릴 수 있는 최대치다 (논리 픽셀).
        // arrange 뒤에 유효하며 앱이 스크롤 값을 다듬을 때 쓴다.
        //  - 내용이 없어도 `content_width`로 계산한다. 담는 쪽이 알려 준 값이
        //    내용의 유무보다 앞선다 (측정 단계가 없다는 계약의 결과다).
        [[nodiscard]] float maximum_scroll() const noexcept;
        // arrange가 범위 안으로 다듬은 스크롤 값이다 (논리 픽셀).
        [[nodiscard]] float scroll_offset() const noexcept;

        // 내용에 주는 자리는 `{ slot.x - offset*scale, slot.y,
        // max(content_width*scale, slot.width), slot.height }`다.
        // 내용이 slot보다 짧아도 slot 폭을 다 받는다 (세로 창과 같은 규칙).
        // 그 자리를 띠 안으로 들이는 스크롤 변화량이다 (논리 픽셀).
        // 가로 띠라 x·width만 본다. arrange 뒤에 유효하다.
        [[nodiscard]] float scroll_delta_to_reveal(const rect_f& target) const override;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        strip_config config_ {};
        // 앱이 넣지 않았으면 nullptr다.
        ui_element* content_ { nullptr };
        float maximum_scroll_ { 0.0f };
        // 물리 bounds를 논리 델타로 옮길 때 쓴다 (세로 창과 같은 규칙).
        float scale_ { 1.0f };
    };
} // namespace luil
