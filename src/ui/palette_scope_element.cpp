#include "luil/ui/palette_scope_element.h"

#include <utility>

namespace luil {
    palette_scope_element::palette_scope_element(ui_element_id id, palette_scope_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
    {}

    void palette_scope_element::set_content(std::unique_ptr<ui_element> content)
    {
        content_ = content.get();
        add_child(std::move(content));
    }

    void palette_scope_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        if (content_ != nullptr)
            content_->arrange(context);
    }

    void palette_scope_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const bool applies { config_.palette != nullptr && (context.theme != color_theme::high_contrast || config_.in_high_contrast) };
        if (applies == false)
        {
            draw_children(context, interaction);
            return;
        }
        // 바뀐 팔레트는 이 그리기 동안만 산다 — 문맥이 참조로 든다.
        const ui_color_palette palette { config_.palette(context.palette, context.theme) };
        draw_context scoped { derive_draw_context(context, palette, context.scale) };
        draw_children(scoped, interaction);
    }

    std::optional<size_f> palette_scope_element::preferred_size(const measure_context& context, const float available_width) const
    {
        return content_ != nullptr ? content_->preferred_size(context, available_width) : std::nullopt;
    }
} // namespace luil
