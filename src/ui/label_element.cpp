#include "luil/ui/label_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <utility>

namespace luil {
    namespace {
        SkPaint label_paint(const ui_color_palette& palette, const label_color_role role)
        {
            switch (role)
            {
            case label_color_role::error:
                return solid_paint(palette.error_accent);
            case label_color_role::dim:
                // 흐린 보조 글자의 역할색이다 (element마다 알파를 발명하지 않는다).
                return solid_paint(palette.secondary_foreground);
            case label_color_role::primary:
                break;
            }
            return solid_paint(palette.primary_foreground);
        }
    } // namespace

    label_element::label_element(ui_element_id id, label_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
    {}

    void label_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void label_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        if (config_.text.empty())
            return;
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const SkFont font { sk_ref_sp(context.ui_typeface), config_.font_size * scale };
        const SkPaint paint { label_paint(context.palette, config_.color) };
        const rect_f box { bounds() };

        if (config_.background == label_background_role::notice)
        {
            const SkPaint fill { solid_paint(context.palette.notice_background) };
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), fill);
        }

        // 창이 좁아 slot이 줄면 글자를 잘라 옆 UI를 침범하지 않게 한다.
        const float padding { config_.padding * scale };
        const float text_width { box.width - padding * 2.0f };
        static_cast<void>(draw_text_within(context.canvas, config_.text, box.x + padding, box.y + centered_text_baseline(font, box.height), text_width, font, paint));
    }

    float label_element::height_for(const label_config& config) noexcept
    {
        // 글자 크기 + 위아래 여백이다.
        // 11px 글에서 18px 줄이 되는, 데모가 손으로 쓰던 그 간격이다.
        return config.font_size + 7.0f;
    }
    access_info label_element::accessibility() const
    {
        return { .role = access_role::static_text, .name = config_.text };
    }
} // namespace luil
