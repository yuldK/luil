#include "luil/ui/theme_picker_element.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace luil {
    namespace {
        constexpr float label_height { 30.0f };
        constexpr float minimum_card_width { 144.0f };
        constexpr std::array preferences { theme_preference::light, theme_preference::system, theme_preference::dark };

        [[nodiscard]] ui_element_id card_id(const theme_picker_config& config, const theme_preference value)
        {
            return { ui_element_kind::choice, config.owner + u8"/" + std::u8string { theme_preference_name(value) } };
        }

        [[nodiscard]] float preview_height(const theme_picker_config& config) noexcept
        {
            return std::max(48.0f, config.preview_height);
        }

        [[nodiscard]] bool vertical(const theme_picker_config& config, const float width) noexcept
        {
            return width < minimum_card_width * 3.0f + std::max(0.0f, config.gap) * 2.0f;
        }

        constexpr float swatch_size { 32.0f };
        constexpr float swatch_gap { 8.0f };

        [[nodiscard]] std::size_t swatch_columns(const float width) noexcept
        {
            return static_cast<std::size_t>(std::max(1.0f, std::floor((width + swatch_gap) / (swatch_size + swatch_gap))));
        }

        class accent_swatch final : public ui_element
        {
        public:
            accent_swatch(const accent_picker_config& config, const accent_definition accent)
                : ui_element { ui_element_id { ui_element_kind::choice, config.owner + u8"/" + std::u8string { accent.id } } }
                , color_ { accent.swatch }
                , selected_ { accent.id == config.selected }
                , name_ { accent.label }
            {
                set_tooltip(name_);
                set_search_label(name_);
                if (config.select)
                {
                    set_cursor(ui_cursor::hand);
                    set_action(ui_trigger::left_click, [select = config.select, key = std::u8string { accent.id }](const ui_action_context&) { return std::vector<input_action> { select(key) }; });
                }
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const float cx { box.x + box.width / 2.0f };
                const float cy { box.y + box.height / 2.0f };
                const float radius { std::min(box.width, box.height) / 2.0f - 5.0f * scale };
                if (radius <= 0.0f)
                    return;
                context.canvas.drawCircle(cx, cy, radius, solid_paint(color_));
                if (selected_ || interaction.hovered == id())
                {
                    SkPaint ring { solid_paint(context.palette.primary_foreground) };
                    ring.setStyle(SkPaint::kStroke_Style);
                    ring.setStrokeWidth((selected_ ? 2.0f : 1.0f) * scale);
                    context.canvas.drawCircle(cx, cy, radius + 3.0f * scale, ring);
                }
                if (selected_)
                {
                    SkPaint tick { solid_paint(is_dark_background(color_) ? make_ui_color(255, 255, 255) : make_ui_color(0, 0, 0)) };
                    tick.setStrokeWidth(1.8f * scale);
                    tick.setStrokeCap(SkPaint::kRound_Cap);
                    context.canvas.drawLine(cx - 4.0f * scale, cy, cx - 1.0f * scale, cy + 3.0f * scale, tick);
                    context.canvas.drawLine(cx - 1.0f * scale, cy + 3.0f * scale, cx + 5.0f * scale, cy - 4.0f * scale, tick);
                }
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::radio_button, .name = name_, .selected = selected_ };
            }

        private:
            ui_color color_ {};
            bool selected_ { false };
            std::u8string name_ {};
        };

        class theme_card final : public ui_element
        {
        public:
            theme_card(const theme_picker_config& config, const theme_preference value, std::u8string label)
                : ui_element { card_id(config, value) }
                , value_ { value }
                , label_ { std::move(label) }
                , selected_ { value == config.selected }
                , accent_ { accent_for(config.accent_id) }
            {
                set_search_label(label_);
                if (config.select)
                {
                    set_cursor(ui_cursor::hand);
                    set_action(ui_trigger::left_click, [select = config.select, value](const ui_action_context&) { return std::vector<input_action> { select(value) }; });
                }
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::radio_button, .name = label_, .selected = selected_ };
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect preview { SkRect::MakeXYWH(box.x + scale, box.y + scale, std::max(0.0f, box.width - 2.0f * scale), std::max(0.0f, box.height - (label_height + 2.0f) * scale)) };
                const SkRRect shape { SkRRect::MakeRectXY(preview, 8.0f * scale, 8.0f * scale) };
                context.canvas.save();
                context.canvas.clipRRect(shape, true);
                const auto paint_preview = [&](const color_theme theme) {
                    const ui_color_palette palette { color_palette_for(theme, accent_) };
                    context.canvas.drawRect(preview, solid_paint(palette.window_background));
                    const auto block = [&](const float x, const float y, const float width, const float height, const ui_color color) {
                        context.canvas.drawRoundRect(SkRect::MakeXYWH(preview.x() + x * preview.width(), preview.y() + y * preview.height(), width * preview.width(), height * preview.height()),
                            1.5f * scale, 1.5f * scale, solid_paint(color));
                    };
                    block(0.06f, 0.08f, 0.22f, 0.035f, palette.primary_foreground);
                    block(0.06f, 0.21f, 0.13f, 0.035f, palette.accent);
                    block(0.24f, 0.21f, 0.11f, 0.035f, palette.secondary_foreground);
                    block(0.40f, 0.21f, 0.13f, 0.035f, palette.secondary_foreground);
                    block(0.06f, 0.34f, 0.58f, 0.56f, palette.surface_background);
                    block(0.69f, 0.34f, 0.25f, 0.56f, palette.surface_background);
                    for (int line = 0; line < 4; ++line)
                        block(0.11f, 0.42f + static_cast<float>(line) * 0.10f, line % 2 == 0 ? 0.43f : 0.34f, 0.026f, palette.secondary_foreground);
                    block(0.73f, 0.40f, 0.17f, 0.20f, palette.accent);
                    block(0.73f, 0.68f, 0.16f, 0.026f, palette.secondary_foreground);
                    block(0.73f, 0.78f, 0.12f, 0.026f, palette.secondary_foreground);
                };
                paint_preview(value_ == theme_preference::dark ? color_theme::dark : color_theme::light);
                if (value_ == theme_preference::system)
                {
                    context.canvas.save();
                    SkPathBuilder diagonal {};
                    diagonal.moveTo(preview.x() + preview.width() * 0.62f, preview.top());
                    diagonal.lineTo(preview.right(), preview.top());
                    diagonal.lineTo(preview.right(), preview.bottom());
                    diagonal.lineTo(preview.x() + preview.width() * 0.38f, preview.bottom());
                    diagonal.close();
                    context.canvas.clipPath(diagonal.detach(), true);
                    paint_preview(color_theme::dark);
                    context.canvas.restore();
                }
                context.canvas.restore();
                const bool hovered { interaction.hovered == id() };
                SkPaint border { solid_paint(selected_ ? context.palette.accent : (hovered ? context.palette.primary_foreground : context.palette.control_border)) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth((selected_ ? 2.0f : 1.0f) * scale);
                context.canvas.drawRRect(shape, border);
                if (selected_)
                {
                    const float cx { preview.right() - 16.0f * scale };
                    const float cy { preview.top() + 16.0f * scale };
                    context.canvas.drawCircle(cx, cy, 10.0f * scale, solid_paint(context.palette.accent));
                    SkPaint tick { solid_paint(context.palette.accent_foreground) };
                    tick.setStyle(SkPaint::kStroke_Style);
                    tick.setStrokeWidth(1.8f * scale);
                    tick.setStrokeCap(SkPaint::kRound_Cap);
                    context.canvas.drawLine(cx - 4.0f * scale, cy, cx - 1.0f * scale, cy + 3.0f * scale, tick);
                    context.canvas.drawLine(cx - 1.0f * scale, cy + 3.0f * scale, cx + 5.0f * scale, cy - 4.0f * scale, tick);
                }
                const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(context.metrics.body_font_size, scale) };
                const float label_width { measure_text(label_, font) };
                const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
                static_cast<void>(draw_text_within(context.canvas, label_, box.x + std::max(0.0f, (box.width - label_width) / 2.0f),
                    preview.bottom() + centered_text_baseline(font, label_height * scale), box.width, font, foreground));
            }

        private:
            theme_preference value_ {};
            std::u8string label_ {};
            bool selected_ { false };
            accent_definition accent_ {};
        };
    } // namespace

    theme_picker_element::theme_picker_element(theme_picker_config config)
        : ui_element { ui_element_id { ui_element_kind::choice_group, config.owner } }
        , config_ { std::move(config) }
    {
        set_focus_group(focus_axis::horizontal);
        set_focus_entry(card_id(config_, config_.selected));
        for (std::size_t index = 0; index < preferences.size(); ++index)
            add_child(std::make_unique<theme_card>(config_, preferences[index], config_.labels[index]));
    }

    float theme_picker_element::height_for(const theme_picker_config& config, const float width) noexcept
    {
        const float height { preview_height(config) + label_height };
        return vertical(config, width) ? height * 3.0f + std::max(0.0f, config.gap) * 2.0f : height;
    }

    void theme_picker_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const bool stacked { vertical(config_, context.slot.width / scale) };
        set_focus_group(stacked ? focus_axis::vertical : focus_axis::horizontal);
        set_bounds({ context.slot.x, context.slot.y, context.slot.width, height_for(config_, context.slot.width / scale) * scale });
        const float gap { std::max(0.0f, config_.gap) * scale };
        const float width { stacked ? context.slot.width : std::max(0.0f, (context.slot.width - gap * 2.0f) / 3.0f) };
        const float height { (preview_height(config_) + label_height) * scale };
        for (std::size_t index = 0; index < children().size(); ++index)
        {
            const float position { static_cast<float>(index) };
            children()[index]->arrange(
                context.for_child({ context.slot.x + (stacked ? 0.0f : position * (width + gap)), context.slot.y + (stacked ? position * (height + gap) : 0.0f), width, height }));
        }
    }

    void theme_picker_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }

    access_info theme_picker_element::accessibility() const
    {
        return { .role = access_role::radio_group, .name = config_.name };
    }
    accent_picker_element::accent_picker_element(accent_picker_config config)
        : ui_element { ui_element_id { ui_element_kind::choice_group, config.owner } }
        , config_ { std::move(config) }
    {
        set_focus_group(focus_axis::horizontal);
        set_focus_entry({ ui_element_kind::choice, config_.owner + u8"/" + config_.selected });
        if (config_.include_system)
            if (const auto system { system_accent() }; system.has_value())
                add_child(std::make_unique<accent_swatch>(config_, *system));
        for (const accent_definition& accent : accent_catalog())
            add_child(std::make_unique<accent_swatch>(config_, accent));
    }

    float accent_picker_element::height_for(const accent_picker_config& config, const float width) noexcept
    {
        const std::size_t count { accent_catalog().size() + (config.include_system && system_accent().has_value() ? 1u : 0u) };
        const std::size_t columns { swatch_columns(width) };
        const std::size_t rows { (count + columns - 1u) / columns };
        return rows == 0 ? 0.0f : static_cast<float>(rows) * (swatch_size + swatch_gap) - swatch_gap;
    }

    void accent_picker_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const std::size_t columns { swatch_columns(context.slot.width / scale) };
        const std::size_t rows { (children().size() + columns - 1u) / columns };
        const float height { rows == 0 ? 0.0f : static_cast<float>(rows) * (swatch_size + swatch_gap) - swatch_gap };
        set_bounds({ context.slot.x, context.slot.y, context.slot.width, height * scale });
        for (std::size_t index = 0; index < children().size(); ++index)
            children()[index]->arrange(context.for_child({ context.slot.x + static_cast<float>(index % columns) * (swatch_size + swatch_gap) * scale,
                context.slot.y + static_cast<float>(index / columns) * (swatch_size + swatch_gap) * scale, std::min(swatch_size * scale, context.slot.width), swatch_size * scale }));
    }

    void accent_picker_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }

    access_info accent_picker_element::accessibility() const
    {
        return { .role = access_role::radio_group, .name = config_.name };
    }
} // namespace luil
