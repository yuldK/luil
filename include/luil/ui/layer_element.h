#pragma once

#include "luil/ui/layout_metrics.h"
#include "luil/ui/ui_element.h"

#include <memory>
#include <optional>
#include <vector>

namespace luil {
    // 겹침 배치의 자식 하나가 서는 자리다 (길이는 논리 픽셀).
    //  - **없음은 언제나 0으로 말한다** (`stack_item`과 같은 규칙).
    struct layer_item
    {
        // 안쪽 영역 안의 자리다. `stretch`가 아니면 `width`·`height`, 없으면 자식이 원하는 크기에 선다
        // (답이 없으면 채운다).
        layout_alignment horizontal { layout_alignment::stretch };
        layout_alignment vertical { layout_alignment::stretch };
        // 0보다 크면 그 길이다 (`stretch`에서도 그 길이로 정렬의 처음에 선다).
        float width { 0.0f };
        float height { 0.0f };
        // 안쪽 영역에서 이 자식만 더 들이는 여백이다 (모서리 배지·떠 있는 버튼의 거리).
        edge_insets margin {};
    };

    // 자식들을 **같은 자리에 겹쳐** 놓는 배치 컨테이너다 (WPF 한 칸 `Grid`, Qt `QStackedLayout::StackAll`).
    //
    // 그림 위의 배지, 빈 목록 위의 안내 글, 내용 위에 떠 있는 버튼처럼 한 자리에 둘 이상이 서는 조립이다. stack은
    // 자리를 나눠 갖고, 이것은 자리를 함께 쓴다.
    //  - 담은 순서가 그리는 순서다. 뒤에 담은 것이 위에 그려지고 먼저 눌린다 (기본 hit test).
    //  - 자식마다 자리를 따로 정한다. 겹친 자식끼리는 서로를 모른다.
    class layer_element final : public ui_element
    {
    public:
        layer_element(ui_element_id id, edge_insets padding = {});

        void add(std::unique_ptr<ui_element> child, layer_item item = {});

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 자식들의 원하는 크기(정한 길이와 여백 포함) 중 가장 큰 폭·높이와 여백이다. 아무도 답하지 않으면 답하지 않는다.
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;

    private:
        struct entry
        {
            ui_element* child { nullptr };
            layer_item item {};
        };

        edge_insets padding_ {};
        std::vector<entry> entries_ {};
    };
}
