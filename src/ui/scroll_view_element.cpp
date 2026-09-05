#include "luil/ui/scroll_view_element.h"

#include <utility>

namespace luil {
    scroll_view_element::scroll_view_element(const ui_element_id id, scroll_view_config config)
        : ui_element { id }
        , config_ { config }
    {
        // 넘치는 내용이 창 밖으로 새지 않게 한다.
        // 그리기와 hit test가 함께 잘리므로 보이지 않는 자식은 눌리지도 않는다.
        set_clip_children(true);
    }

    void scroll_view_element::set_content(std::unique_ptr<ui_element> content)
    {
        content_ = content.get();
        add_child(std::move(content));
    }

    const rect_f& scroll_view_element::viewport() const noexcept
    {
        return viewport_;
    }

    float scroll_view_element::maximum_scroll() const noexcept
    {
        return maximum_scroll_;
    }

    float scroll_view_element::scroll_offset() const noexcept
    {
        return config_.scroll_offset;
    }

    float scroll_view_element::scroll_delta_to_reveal(const rect_f& target) const
    {
        // 창의 세로 범위가 곧 자기 bounds다 (arrange가 slot을 그대로 받는다).
        // 답은 논리 픽셀이라 물리 차이를 배율로 나눈다 — 이 나눗셈이 빠지면
        // 고DPI 화면에서 두 배로 튄다.
        const rect_f box { bounds() };
        return luil::scroll_delta_to_reveal(target.y, target.height, box.y, box.height) / scale_;
    }

    void scroll_view_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        scale_ = scale;
        set_bounds(context.slot);
        viewport_ = context.slot;

        // 넘치는 만큼만 흘릴 수 있다.
        // 다듬은 값을 남겨 두어 담는 쪽이 같은 값을 스크롤 막대에 넘길 수 있게 한다.
        // 다듬는 식은 공개 함수 clamp_scroll과 같다 — 앱이 tree를 만들기 전에
        // 같은 식으로 다듬을 수 있어야 막대와 내용이 어긋나지 않는다.
        const float content_height { config_.content_height * scale };
        maximum_scroll_ = content_height > context.slot.height ? (content_height - context.slot.height) / scale : 0.0f;
        config_.scroll_offset = clamp_scroll(config_.content_height, context.slot.height / scale, config_.scroll_offset);

        if (content_ == nullptr)
            return;

        // 내용은 흘러간 만큼 위로 올린 자리에 통째로 배치한다.
        // 내용이 받은 slot으로 보이는 범위를 알 수 있어 가상화도 여기서 가능하다.
        const rect_f slot {
            context.slot.x,
            context.slot.y - config_.scroll_offset * scale,
            context.slot.width,
            content_height > context.slot.height ? content_height : context.slot.height,
        };
        // 자식 문맥을 잇지 않고 **새로 정하는** 유일한 자리다.
        // 아래의 sticky 머리행은 자기를 감싼 가장 가까운 스크롤 창의 값을 봐야 한다.
        content_->arrange({ slot, scale, config_.scroll_offset });
    }

    void scroll_view_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }
} // namespace luil
