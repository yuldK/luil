#include "luil/ui/spinner_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/draw_primitives.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#include <utility>

namespace luil {
    spinner_element::spinner_element(const ui_element_id id, spinner_config config)
        : ui_element { id }, config_ { std::move(config) }
    {}

    void spinner_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void spinner_element::draw(draw_context& context, const interaction_snapshot&) const
    {
        if (visible() == false || config_.size <= 0.0f)
            return;
        const SkFont font { sk_ref_sp(context.codicon_typeface), config_.size * context.scale };
        // 움직임 줄이기면 멈춘 글리프로 선다 (`draw_context::reduce_motion`). `next_update`도 frame을 청하지 않는다.
        if (context.reduce_motion)
            draw_centered_glyph(context.canvas, codicons::icon_loading, bounds(), font, solid_paint(context.palette.accent));
        else
            draw_spinning_glyph(context.canvas, codicons::icon_loading, bounds(), font, solid_paint(context.palette.accent), context.now);
    }

    std::optional<std::chrono::steady_clock::time_point> spinner_element::next_update(const update_context& context, const interaction_snapshot&) const
    {
        if (context.reduce_motion || visible() == false || config_.size <= 0.0f || bounds().width <= 0.0f || bounds().height <= 0.0f)
            return std::nullopt;
        return context.now + std::chrono::milliseconds { 16 };
    }

    access_info spinner_element::accessibility() const
    {
        return { .role = access_role::progress, .name = config_.name };
    }
} // namespace luil
