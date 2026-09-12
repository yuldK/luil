#include "luil/ui/glyph_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <cmath>
#include <utility>

namespace luil {
    glyph_element::glyph_element(ui_element_id id, glyph_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
    {}

    void glyph_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void glyph_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        const rect_f box { bounds() };
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float size { config_.font_size * scale };
        if (config_.glyph == 0 || config_.glyph > 0x10FFFF || (config_.glyph >= 0xD800 && config_.glyph <= 0xDFFF)
            || std::isfinite(size) == false || size <= 0.0f || box.width <= 0.0f || box.height <= 0.0f)
            return;
        const sk_sp<SkTypeface> typeface { config_.typeface != nullptr ? config_.typeface : sk_ref_sp(context.codicon_typeface) };
        if (typeface == nullptr)
            return;
        const SkFont font { typeface, size };
        const SkPaint paint { solid_paint(config_.color.value_or(context.palette.primary_foreground)) };
        const SkAutoCanvasRestore restore { &context.canvas, true };
        context.canvas.clipRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height));
        draw_centered_glyph(context.canvas, config_.glyph, box, font, paint);
    }

    access_info glyph_element::accessibility() const
    {
        if (config_.description.empty())
            return {};
        return { .role = access_role::image, .name = config_.description };
    }
} // namespace luil
