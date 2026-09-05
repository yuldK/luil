#include "luil/ui/strip_element.h"

#include <utility>

namespace luil {
    strip_element::strip_element(const ui_element_id id, strip_config config)
        : ui_element { id }
        , config_ { config }
    {
        // 넘치는 내용이 띠 밖으로 새지 않게 한다.
        // 그리기와 hit test가 함께 잘리므로 보이지 않는 자식은 눌리지도 않는다.
        set_clip_children(true);
    }

    void strip_element::set_content(std::unique_ptr<ui_element> content)
    {
        content_ = content.get();
        add_child(std::move(content));
    }

    float strip_element::maximum_scroll() const noexcept
    {
        return maximum_scroll_;
    }

    float strip_element::scroll_offset() const noexcept
    {
        return config_.scroll_offset;
    }

    float strip_element::scroll_delta_to_reveal(const rect_f& target) const
    {
        // 세로 창과 같은 식이고 축만 다르다 (`layout_metrics.h`가 한 벌로 두는 이유).
        const rect_f box { bounds() };
        return luil::scroll_delta_to_reveal(target.x, target.width, box.x, box.width) / scale_;
    }

    void strip_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        scale_ = scale;
        set_bounds(context.slot);

        // 넘치는 만큼만 흘릴 수 있다.
        // 다듬은 값을 남겨 두어 담는 쪽이 같은 값을 앱 상태에 되돌릴 수 있게 한다.
        //
        // 최대치는 **끝까지 흘린 값**이라 다듬기와 같은 함수로 낸다.
        //  - 최대치를 따로 세면 다듬는 식이 두 벌이 되고, 두 벌은 언젠가 어긋난다.
        //    한 함수만 부르면 어긋날 자리가 없다.
        const float viewport_width { context.slot.width / scale };
        maximum_scroll_ = clamp_scroll(config_.content_width, viewport_width, config_.content_width);
        config_.scroll_offset = clamp_scroll(config_.content_width, viewport_width, config_.scroll_offset);

        if (content_ == nullptr)
            return;

        // 내용은 흘러간 만큼 왼쪽으로 민 자리에 통째로 배치한다.
        // 내용이 slot보다 짧아도 slot 폭을 다 받는다.
        const float content_width { config_.content_width * scale };
        const rect_f slot {
            context.slot.x - config_.scroll_offset * scale,
            context.slot.y,
            content_width > context.slot.width ? content_width : context.slot.width,
            context.slot.height,
        };
        // **자기 스크롤 값을 문맥에 싣지 않는다.**
        // `arrange_context::scroll_offset`은 축이 없는 스칼라이고 sticky 머리행이
        // 그 값을 y에 더한다 — 가로 값을 거기 실으면 몇 계층 아래가 조용히 어긋난다.
        content_->arrange(context.for_child(slot));
    }

    void strip_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }
} // namespace luil
