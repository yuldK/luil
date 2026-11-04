#pragma once

#include "luil/ui/page_transition.h"
#include "luil/ui/ui_element.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace luil {
    struct page_transition_config
    {
        // 요소 id의 owner다 (`{ page_transition, owner }`, 덮개는 `{ page_transition_cover, owner }`).
        std::u8string owner {};
        page_transition transition {};
    };

    // 새 화면(content)을 담고, 전환이 도는 동안 이전 화면을 시각만으로 함께 그린다 (docs/page-transition-design.md).
    //  - 이전 tree는 자식이 아니다. 색인·hit·초점·접근 tree에 없다. 빈 snapshot으로 그려 hover·누름·초점 테가 없다.
    //  - prior가 있는 동안(시각과 무관하게) 마지막 자식으로 칠하지 않는 덮개(`hit_opaque`)를 둔다. 누름·hover·끌어 놓기가
    //    덮개에서 멈춘다. 그림이 움직이는 동안 hit·초점 테·UIA 좌표는 새 화면의 최종 자리라서다. 키는 막지 않는다.
    //  - 자식을 자기 bounds로 자른다 (`clip_children`). 밀리는 화면이 칸 밖으로 새지 않는다.
    //  - 그대로 새 화면만 그리는 때: prior가 없을 때, 끝났을 때, 움직임 줄이기(`draw_context::reduce_motion`), 이전 root의
    //    크기가 요소와 다를 때(창 크기·배율·회전이 바뀐 도중). 그때는 예고도 없다.
    //  - prior가 빈 config로 늘 감싸도 된다. 그때는 새 화면만 그리는 껍질이다.
    class page_transition_element final : public ui_element
    {
    public:
        page_transition_element(page_transition_config config, std::unique_ptr<ui_element> content);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const override;

    private:
        // 이전 화면을 그릴 수 있는가다 (prior가 있고 root가 요소와 같은 크기다).
        [[nodiscard]] bool prior_fits() const noexcept;

        page_transition transition_ {};
        ui_element* content_ { nullptr };
        ui_element* cover_ { nullptr };
    };
}
