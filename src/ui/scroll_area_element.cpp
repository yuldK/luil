#include "luil/ui/scroll_area_element.h"

#include "luil/ui/draw_primitives.h"

#include <algorithm>
#include <utility>

namespace luil {
    scroll_area_element::scroll_area_element(scroll_area_config config)
        : ui_element(ui_element_id { ui_element_kind::scroll_area, config.owner })
        , config_(std::move(config))
    {
        // 흘리기·잘라내기·다듬기는 창의 몫이다 (`list_element`와 같은 조립).
        scroll_view_config view {};
        view.content_height = config_.content_height;
        view.scroll_offset = config_.scroll_offset;
        auto scroll { std::make_unique<scroll_view_element>(ui_element_id { ui_element_kind::scroll_area_view, config_.owner }, view) };
        view_ = scroll.get();
        add_child(std::move(scroll));

        // 흘릴 메시지가 없으면 흘리는 창이 아니다.
        // 막대도 휠도 되살리기도 그 한 값에 함께 달려 있다.
        if (config_.scroll == nullptr)
            return;

        // 휠과 초점 되살리기의 임자가 **이 영역 자신**이다 — 앱이 표를 짓지 않아도
        // `route_wheel`·`route_reveal`이 여기를 찾는다. 다만 그 역할을 실제로 세우는
        // 것은 `arrange`다: 흘릴 것이 있는지를 알아야 임자가 될 수 있고, 그것은 창
        // 높이가 정해진 뒤에야 안다.
        if (config_.bar == scrollbar_visibility::never)
            return;
        scrollbar_config bar {};
        bar.scroll = config_.scroll;
        bar.scroll_to = config_.scroll_to;
        auto scrollbar { std::make_unique<scrollbar_element>(ui_element_id { ui_element_kind::scroll_area_bar, config_.owner }, std::move(bar)) };
        // 자리는 `arrange`가 정한다 — 흘릴 것이 있는지를 알아야 답할 수 있고,
        // 그것은 창 높이가 정해진 뒤에야 안다 (`bar_tab_stop`).
        bar_ = scrollbar.get();
        add_child(std::move(scrollbar));
    }

    void scroll_area_element::set_content(std::unique_ptr<ui_element> content)
    {
        view_->set_content(std::move(content));
    }

    const scroll_metrics& scroll_area_element::metrics() const noexcept
    {
        return metrics_;
    }

    const rect_f& scroll_area_element::viewport() const noexcept
    {
        return view_->viewport();
    }

    bool scroll_area_element::bar_takes_space() const noexcept
    {
        if (bar_ == nullptr)
            return false;
        return config_.bar == scrollbar_visibility::always || metrics_.overflowing();
    }

    float scroll_area_element::scroll_delta_to_reveal(const rect_f& target) const
    {
        // 안쪽 창이 답한다.
        // **바깥인 이 영역을 이름 대도 옳은 값이 나온다**는 것이 요점이다 — 이
        // 영역의 bounds는 막대 칸까지 품어 창보다 넓고, 그것으로 재면 마지막 행이
        // 막대 밑에 남는다 (표를 둘로 나눠야 했던 바로 그 자리다).
        return view_->scroll_delta_to_reveal(target);
    }

    void scroll_area_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };

        // 치수는 **창을 배치하기 전에** 나온다.
        //
        // 흘릴 것이 있는지는 내용 높이와 창 높이만의 함수이고 둘 다 여기서
        // 이미 알고 있다 — 그러려고 `layout_metrics.h`가 그 식을 순수 함수로
        // 떼어 두었다. 창을 한 번 배치해 보고 물으면 그 답을 얻자고 배치를 두
        // 번 하게 되고, 내용의 `arrange`가 한 frame에 두 번 불린다. 배치 중에
        // 쌓는 내용(가상 목록이 행을 짓는 자리)이 그것을 두 배로 센다.
        metrics_.content_height = config_.content_height;
        metrics_.viewport_height = context.slot.height / scale;
        metrics_.maximum_scroll = metrics_.content_height > metrics_.viewport_height ? metrics_.content_height - metrics_.viewport_height : 0.0f;
        metrics_.scroll_offset = clamp_scroll(metrics_.content_height, metrics_.viewport_height, config_.scroll_offset);

        // **흘릴 것이 없으면 임자가 아니다.**
        //
        // 휠의 임자는 좌표를 덮는 가장 안쪽의 흘리는 컨테이너인데, 그것이 지금
        // 움직일 수 없는 창이면 휠은 거기서 죽는다 — 앱은 0으로 다듬을 메시지를
        // 하나 받고, 그 사각형 위에서는 바깥 판이 굴러가지 않는다. 흘릴 수 있는지를
        // 아는 것은 창 높이를 방금 잰 이쪽뿐이라 여기가 그 판정의 자리다.
        //  - `set_visible`·`set_tab_stop`과 같은 build 시 설정이다. tree는 아직
        //    게시되지 않았다.
        //  - 되살리기도 함께 꺼진다. 들일 것이 없는 창은 되살릴 것도 없다.
        if (config_.scroll != nullptr)
        {
            if (metrics_.overflowing())
            {
                scroll_source source {};
                source.scroll = config_.scroll;
                source.scale = scale;
                set_scroll_source(std::move(source));
            }
            else
                set_scroll_source(std::nullopt);
        }

        const bool takes_space { bar_takes_space() };
        // 막대의 폭은 **창보다 넓을 수 없다.** 아주 좁은 칸에서 다듬지 않으면
        // 막대의 왼쪽 끝이 영역 밖으로 나가, 화면에는 남의 자리에 그려지고
        // 휠은 영역 밖이라 아무도 받지 않는다.
        const float bar_width { takes_space ? std::min(scroll_area_bar_width * scale, context.slot.width) : 0.0f };
        const float view_width { context.slot.width - bar_width };
        view_->arrange(context.for_child({ context.slot.x, context.slot.y, view_width, context.slot.height }));

        if (bar_ == nullptr)
            return;
        // 자리를 차지하지 않는 막대는 숨긴다. 숨긴 것은 그려지지도 눌리지도
        // 않으므로 "보이지 않는데 잡히는" 칸이 남지 않는다
        // (`tab_bar_element`가 넘침 버튼에 세운 규칙과 같다).
        bar_->set_visible(takes_space);
        bar_->set_enabled(takes_space);
        // **흘릴 것이 없으면 자리가 아니다.** `always`로 자리를 지키는 막대는
        // 짧은 내용에서도 보이지만 끌 수도 누를 수도 없다 (`draggable()`이 거짓).
        // 그것이 Tab의 자리로 남으면 사용자는 아무 일도 하지 않는 자리를 한 번
        // 더 지나고, 거기서 누른 키는 조용히 사라진다.
        bar_->set_tab_stop(config_.bar_tab_stop && metrics_.overflowing());
        // 막대가 재는 창 높이는 **창이 실제로 받은 높이**다 (`list_element`와 같은 줄).
        bar_->set_metrics(metrics_.content_height, metrics_.viewport_height, metrics_.scroll_offset);
        bar_->arrange(context.for_child({ context.slot.x + context.slot.width - bar_width, context.slot.y, bar_width, context.slot.height }));
    }

    void scroll_area_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 영역 자체는 바탕을 칠하지 않는다.
        // 표면 색과 모서리는 담는 쪽(panel)의 몫이다 (`list_element`와 같다).
        draw_children(context, interaction);
        // 가장자리는 내용 위에 겹친다. 치수는 `arrange`가 잰 그 값이라 창·막대·그림자가
        // 같은 offset을 본다 — 앱이 앞서 든 값으로 그리면 한 frame 낡은 그림자가 선다.
        draw_scroll_edges(context, bounds(), metrics_.scroll_offset, metrics_.maximum_scroll, config_.edges);
    }
} // namespace luil
