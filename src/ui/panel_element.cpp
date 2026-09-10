#include "luil/ui/panel_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <utility>

namespace luil {
    panel_element::panel_element(ui_element_id id, panel_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
    {}

    void panel_element::set_content(std::unique_ptr<ui_element> content)
    {
        content_ = content.get();
        add_child(std::move(content));
    }

    void panel_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        if (content_ != nullptr)
            content_->arrange(context);
    }

    void panel_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const SkRect shape { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        const float radius { config_.corner_radius * scale };
        const ui_color background { config_.background != nullptr ? config_.background(context.palette) : context.palette.surface_background };
        // 그림자는 상자 밖이라 바탕보다 먼저다.
        if (config_.shadow > 0.0f)
            draw_surface_shadow(context, box, radius, config_.shadow);
        context.canvas.drawRRect(SkRRect::MakeRectXY(shape, radius, radius), solid_paint(background));
        draw_children(context, interaction);
        // 테두리는 내용 위다. 내용이 가장자리까지 채워도 카드의 경계가 남는다.
        if (config_.border == nullptr)
            return;
        SkPaint border { solid_paint(config_.border(context.palette)) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(scale);
        // 획의 중심을 반 픽셀 안으로 들여 획 전체가 상자 안에 든다.
        const float inset { scale / 2.0f };
        const float inner_radius { radius > inset ? radius - inset : 0.0f };
        context.canvas.drawRRect(SkRRect::MakeRectXY(shape.makeInset(inset, inset), inner_radius, inner_radius), border);
    }
} // namespace luil
