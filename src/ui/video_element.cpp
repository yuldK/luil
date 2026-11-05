#include "luil/ui/video_element.h"

#include "luil/ui/draw_primitives.h"
#include "luil/ui/label_element.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <utility>

namespace luil {
    video_element::video_element(const ui_element_id id, video_config config)
        : ui_element { id }, config_ { std::move(config) }
    {
        set_clip_children(true);
        if (config_.placeholder.empty() == false)
            add_child(std::make_unique<label_element>(ui_element_id {}, label_config { .text = config_.placeholder }));
    }

    void video_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        for (const auto& child : children())
            child->arrange(context);
    }

    void video_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const auto box { bounds() };
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(context.palette.surface_background));
        draw_children(context, interaction);
    }

    access_info video_element::accessibility() const
    {
        return { .role = access_role::pane, .name = config_.name };
    }
}
