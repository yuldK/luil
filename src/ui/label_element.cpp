#include "luil/ui/label_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cmath>
#include <optional>
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

        // label의 글 규칙이다. 바꾼 글은 첫 줄의 자리가 그대로이도록 위에서부터 쌓는다.
        [[nodiscard]] text_block_style block_style(const label_config& config) noexcept
        {
            return text_block_style {
                .font = { .size = config.font_size, .weight = config.weight, .slant = config.slant },
                .align = config.align,
                .vertical = config.wrap ? text_vertical_align::top : text_vertical_align::center,
                .elide = config.elide,
                .wrap = config.wrap,
                .max_lines = config.max_lines,
            };
        }
    }

    label_element::label_element(ui_element_id id, label_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
    {
    }

    label_wrap label_element::measure_wrap(const label_config& config, const measure_context& context, const float available_width)
    {
        return measure_text_block(context, config.text, block_style(config), available_width - config.padding * 2.0f);
    }

    void label_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        if (config_.wrap)
        {
            const float scale { context.scale > 0.0f ? context.scale : 1.0f };
            wrapped_ = measure_wrap(config_, context.measure(), context.slot.width / scale);
        }
    }

    void label_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        if (config_.text.empty())
            return;
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };

        if (config_.background == label_background_role::notice)
        {
            const SkPaint fill { solid_paint(context.palette.notice_background) };
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), fill);
        }
        else if (config_.background == label_background_role::raised)
        {
            SkPaint fill { solid_paint(context.palette.raised_background) };
            fill.setAntiAlias(true);
            const float radius { context.metrics.control_corner_radius * scale };
            context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radius, radius), fill);
        }

        const float padding { config_.padding * scale };
        const rect_f text_box { box.x + padding, box.y, box.width - padding * 2.0f, box.height };
        const SkPaint paint { label_paint(context.palette, config_.color) };
        // 바꾼 줄은 배치가 정한 것을 그대로 쓴다 — 그리기마다 줄을 다시 바꾸지 않는다.
        if (config_.wrap)
            static_cast<void>(draw_text_block(context, config_.text, text_box, block_style(config_), paint, wrapped_));
        else
            static_cast<void>(draw_text_block(context, config_.text, text_box, block_style(config_), paint));
    }

    float label_element::height_for(const label_config& config) noexcept
    {
        // 글자 크기 + 위아래 여백이다.
        // 11px 글에서 18px 줄이 되는, 데모가 손으로 쓰던 그 간격이다. 직접 그리는 글의 기본 줄 높이도 이것이다.
        return text_block_line_height(block_style(config));
    }
    access_info label_element::accessibility() const
    {
        return { .role = access_role::static_text, .name = config_.text };
    }

    std::optional<size_f> label_element::preferred_size(const measure_context& context, const float available_width) const
    {
        if (config_.wrap)
        {
            const label_wrap measured { measure_wrap(config_, context, available_width) };
            const bool broke { measured.lines.size() > 1 || measured.truncated };
            const float width { broke ? available_width : std::ceil(measured.width) + config_.padding * 2.0f };
            return size_f { width, std::max(measured.height, height_for(config_)) };
        }
        static_cast<void>(available_width);
        const font_style style { config_.weight, config_.slant };
        return size_f { std::ceil(context.text_width(config_.text, config_.font_size, style)) + config_.padding * 2.0f, height_for(config_) };
    }
}
