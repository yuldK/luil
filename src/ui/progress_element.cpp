#include "luil/ui/progress_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <utility>

namespace luil {
    progress_element::progress_element(const ui_element_id id, progress_config config)
        : ui_element { id }
        , config_ { std::move(config) }
    {}

    void progress_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;
    }

    void progress_element::draw(draw_context& context, const interaction_snapshot&) const
    {
        const rect_f box { bounds() };
        const float thickness { height_for(config_) * scale_ };
        if (box.width <= 0.0f || thickness <= 0.0f)
            return;

        // 슬롯이 두께보다 높으면 가운데 눕는다.
        // 담는 쪽이 height_for로 맞춰 주는 것이 보통이지만, 라벨과 같은 줄에 놓는
        // 자리에서도 그 줄의 가운데에 서야 한다.
        const float top { box.y + (box.height - thickness) / 2.0f };
        const float radius { thickness / 2.0f };
        const SkRect track { SkRect::MakeXYWH(box.x, top, box.width, thickness) };
        context.canvas.drawRRect(SkRRect::MakeRectXY(track, radius, radius), solid_paint(context.palette.input_background));

        const float filled { box.width * clamp_value(config_.value) };
        if (filled <= 0.0f)
            return;
        // 채움이 짧으면 SkRRect가 반지름을 함께 줄이므로 둥근 끝이 트랙을 넘지 않는다.
        context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, top, filled, thickness), radius, radius), solid_paint(context.palette.accent));
    }
    access_info progress_element::accessibility() const
    {
        return { .role = access_role::progress, .name = tooltip(), .range = access_range { 0.0f, 1.0f, clamp_value(config_.value) } };
    }
} // namespace luil
