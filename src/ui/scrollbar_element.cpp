#include "luil/ui/scrollbar_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <optional>
#include <utility>
#include <vector>

namespace luil {
    scrollbar_element::scrollbar_element(const ui_element_id id, scrollbar_config config)
        : ui_element { id }
        , config_ { std::move(config) }
    {
        // factory가 없으면 끌기 자체를 만들지 않는다 (표시 전용 막대).
        // "없는 것은 두지 않는다" — 잡히는데 아무 일도 없는 조합을 만들지 않는다.
        if (config_.scroll == nullptr)
            return;

        // tooltip은 두지 않는다.
        // 끄는 동안 hover가 유지되어 tooltip이 떠 버린다.
        pointer_drag_target target {};
        // 누른 지점이 thumb 밖이면 그 자리로 한 번 이동하고 이어서 끌린다.
        // thumb 안이면 잡기만 하고 움직이지 않는다.
        target.on_press = [this](const ui_action_context& context) -> std::vector<input_action> {
            if (draggable() == false || (context.y >= thumb_top_ && context.y <= thumb_top_ + thumb_height_))
                return {};
            const float delta { scroll_delta_for(context.y - (thumb_top_ + thumb_height_ * 0.5f)) };
            return { config_.scroll(delta) };
        };
        target.on_move = [this](const ui_action_context& previous, const ui_action_context& current) -> std::vector<input_action> {
            if (draggable() == false)
                return {};
            const float delta { scroll_delta_for(current.y - previous.y) };
            if (delta == 0.0f)
                return {};
            return { config_.scroll(delta) };
        };
        set_pointer_drag_target(std::move(target));

        // 끌 수 있는 막대는 키로도 걸을 수 있다.
        //  - 목록이 자기 안에 두는 막대는 담는 쪽이 자리를 거절한다
        //    (`list_element`) — 목록 자신이 ↑/↓·Home/End를 가진 묶음이다.
        set_tab_stop(true);
        key_step_target steps {};
        steps.axis = focus_axis::vertical;
        steps.on_step = [this](const value_step step) -> std::optional<std::vector<input_action>> {
            const float scrollable { config_.content_height - config_.viewport_height };
            float delta { 0.0f };
            switch (step)
            {
            case value_step::decrease:
                delta = -scrollbar_key_line_step;
                break;
            case value_step::increase:
                delta = scrollbar_key_line_step;
                break;
            case value_step::decrease_page:
                delta = -config_.viewport_height;
                break;
            case value_step::increase_page:
                delta = config_.viewport_height;
                break;
            case value_step::minimum:
                delta = -config_.scroll_offset;
                break;
            case value_step::maximum:
                delta = scrollable - config_.scroll_offset;
                break;
            }
            // 흘릴 것이 없으면(내용이 창보다 짧다) 삼키고 아무 메시지도 내지 않는다.
            // 끌기가 이미 그 규칙이다 (`draggable`).
            if (draggable() == false || delta == 0.0f)
                return std::vector<input_action> {};
            return std::vector<input_action> { config_.scroll(delta) };
        };
        set_key_step_target(std::move(steps));
    }

    void scrollbar_element::set_metrics(const float content_height, const float viewport_height, const float scroll_offset) noexcept
    {
        config_.content_height = content_height;
        config_.viewport_height = viewport_height;
        config_.scroll_offset = scroll_offset;
    }

    void scrollbar_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;

        track_top_ = context.slot.y;
        track_height_ = context.slot.height;
        const float scrollable { (config_.content_height - config_.viewport_height) * scale_ };
        if (scrollable <= 0.0f || track_height_ <= 0.0f)
        {
            thumb_top_ = track_top_;
            thumb_height_ = track_height_;
            return;
        }

        thumb_height_ = track_height_ * (config_.viewport_height / config_.content_height);
        const float minimum { scrollbar_minimum_thumb * scale_ };
        if (thumb_height_ < minimum)
            thumb_height_ = minimum > track_height_ ? track_height_ : minimum;
        const float ratio { config_.scroll_offset * scale_ / scrollable };
        thumb_top_ = track_top_ + (track_height_ - thumb_height_) * (ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio));
    }

    float scrollbar_element::thumb_top() const noexcept
    {
        return thumb_top_;
    }

    float scrollbar_element::thumb_height() const noexcept
    {
        return thumb_height_;
    }

    bool scrollbar_element::draggable() const noexcept
    {
        return config_.content_height - config_.viewport_height > 0.0f && track_height_ - thumb_height_ > 0.0f;
    }

    float scrollbar_element::scroll_delta_for(const float pixels) const noexcept
    {
        if (draggable() == false)
            return 0.0f;
        const float scrollable { (config_.content_height - config_.viewport_height) * scale_ };
        // thumb가 움직일 수 있는 거리와 내용이 움직일 수 있는 거리의 비율이다.
        return pixels * (scrollable / (track_height_ - thumb_height_)) / scale_;
    }

    void scrollbar_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        if (thumb_height_ <= 0.0f || track_height_ <= 0.0f)
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const bool hovered { interaction.hovered == id() };
        const bool pressed { interaction.pressed == id() };

        // hit 영역은 잡기 쉽도록 넓고, 보이는 막대는 그 안에서 오른쪽에 붙인다.
        const float width { scrollbar_visual_width * scale };
        const float left { box.x + box.width - width };
        const SkRect shape { SkRect::MakeXYWH(left, thumb_top_, width, thumb_height_) };
        const float radius { width * 0.5f };

        float alpha { 0.28f };
        if (pressed)
            alpha = 0.62f;
        else if (hovered)
            alpha = 0.45f;
        const SkPaint thumb { solid_paint(with_alpha(context.palette.primary_foreground, alpha)) };
        context.canvas.drawRRect(SkRRect::MakeRectXY(shape, radius, radius), thumb);
    }
    access_info scrollbar_element::accessibility() const
    {
        // 범위는 흘릴 수 있는 양이다. 내용이 창보다 짧으면 0이다.
        const float maximum { config_.content_height > config_.viewport_height ? config_.content_height - config_.viewport_height : 0.0f };
        const float clamped { config_.scroll_offset < 0.0f ? 0.0f : (config_.scroll_offset > maximum ? maximum : config_.scroll_offset) };
        // 이름은 `search_label` → 툴팁 순서다 (`ui_element::access_name`) —
        // 막대는 글을 세우지 않아 툴팁 말고 이름을 담을 자리가 필요하다.
        return { .role = access_role::scroll_bar, .name = access_name(), .range = access_range { 0.0f, maximum, clamped } };
    }

    std::optional<std::vector<input_action>> scrollbar_element::access_actions(const access_request& request) const
    {
        if (request.command != access_command::set_value)
            return ui_element::access_actions(request);
        // 자리 정하기는 **절대 메시지**만 탄다 — 델타 환산은 오래된 발행본 기준의
        // 변화량이 겹쳐 쌓인다 (막대와 같은 이유, accessibility-action-design.md).
        // 흘릴 것이 없으면 아무 일도 하지 않는다 — 끌기와 키가 이미 그 규칙이다.
        //  - **끌 수 있는가**로 묻는 것이 값 정하기에 넉넉해 보이지만, 그래야
        //    "보조 기술로 할 수 있는 일은 사람이 할 수 있는 일의 부분집합"이
        //    남는다. 키 걸음(`on_step`)도 같은 술어로 삼킨다.
        const std::optional<access_range> range { accessibility().range };
        if (config_.scroll_to == nullptr || draggable() == false || range.has_value() == false)
            return std::nullopt;
        // 범위와 지금 자리는 읽기가 답한 그것이다 (식이 두 벌이 되지 않는다).
        const float target { request.value < range->minimum ? range->minimum : (request.value > range->maximum ? range->maximum : request.value) };
        if (target == range->value)
            return std::vector<input_action> {};
        return std::vector<input_action> { config_.scroll_to(target) };
    }
} // namespace luil
