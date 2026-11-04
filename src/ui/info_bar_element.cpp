#include "luil/ui/info_bar_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/button_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "ui/default_text.h"
#include "ui/severity_paint.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace luil {
    namespace {
        // 치수는 논리 픽셀이고 그릴 때 배율을 곱한다.
        // 안쪽 가로 여백, 글리프 칸과 글리프·글 사이, 오른쪽 부품 사이와 글 앞 간격이다.
        constexpr float horizontal_padding { 12.0f };
        constexpr float glyph_cell { 16.0f };
        constexpr float glyph_gap { 10.0f };
        constexpr float part_gap { 8.0f };
        // 위·아래 여백의 하한이다. 글이 큰 막대도 글이 테에 붙지 않는다.
        constexpr float vertical_padding_floor { 8.0f };
        constexpr float corner_radius { 6.0f };
        // 토스트와 같은 글리프 크기다.
        constexpr float glyph_font_size { 14.0f };
        // 글 앞쪽 몫이다 (여백 + 글리프 칸 + 간격).
        constexpr float leading_width { horizontal_padding + glyph_cell + glyph_gap };

        [[nodiscard]] bool has_action(const info_bar_config& config) noexcept
        {
            return config.action_label.empty() == false && config.action != nullptr;
        }

        // 0이거나 잘못된 값은 "정하지 않음"이다.
        [[nodiscard]] float control_size_of(const info_bar_config& config) noexcept
        {
            return std::isfinite(config.control_size) && config.control_size > 0.0f ? config.control_size : 0.0f;
        }

        // 제목은 본문과 굵기로 갈린다. 스타일의 `title`이 아니라 고정하는 까닭은 그 기본이 regular라 본문과 갈리지 않아서다.
        [[nodiscard]] text_block_style title_style(const ui_metrics& metrics) noexcept
        {
            return text_block_style { .font = { .size = metrics.body_font_size, .weight = font_weight::semibold } };
        }

        // 본문은 위에서부터 쌓는다. 한 줄 막대는 제목과 같은 첫 줄에 서고, 여러 줄이면 첫 줄이 늘 같은 자리다.
        [[nodiscard]] text_block_style message_style(const ui_metrics& metrics, const std::size_t max_lines) noexcept
        {
            return text_block_style {
                .font = { .size = metrics.body_font_size, .weight = metrics.font_weights.body_text },
                .vertical = text_vertical_align::top,
                .wrap = true,
                .max_lines = max_lines,
            };
        }

        // 동작 단추가 원하는 폭이다. 단추의 식을 옮겨 적지 않고 단추에게 묻는다.
        [[nodiscard]] float action_width_of(const info_bar_config& config, const measure_context& context)
        {
            const text_button_element button { ui_element_id { ui_element_kind::info_bar_action }, text_button_config { .text = config.action_label } };
            const std::optional<size_f> size { button.preferred_size(context, std::numeric_limits<float>::infinity()) };
            return size.has_value() ? size->width : text_button_minimum_width;
        }

        // 한 폭에서 막대의 높이와 단추 자리를 정한 결과다 (논리 픽셀). `measure`와 arrange가 이 식 하나를 쓴다.
        struct bar_plan
        {
            float height { 0.0f };
            bool action_below { false };
            float line_height { 0.0f };
            float top_padding { 0.0f };
            float text_width { 0.0f };
            float title_height { 0.0f };
            text_block_layout message {};
            // 제목과 본문이 차지하는 높이다. 글이 없어도 첫 줄 하나는 있다.
            float rows { 0.0f };
            float action_width { 0.0f };
            float action_height { 0.0f };
            float close_size { 0.0f };
        };

        [[nodiscard]] bar_plan plan_bar(const info_bar_config& config, const measure_context& context, const float width)
        {
            const ui_metrics& metrics { context.style_metrics() };
            const float control { control_size_of(config) };
            const float minimum_height { std::max(info_bar_minimum_height, control) };
            const bool has_close { config.close != nullptr };

            bar_plan plan {};
            plan.close_size = control > 0.0f ? control : info_bar_close_size;
            plan.action_height = control > 0.0f ? control : text_button_height;
            plan.line_height = text_block_line_height(message_style(metrics, 0));
            // 한 줄 막대는 최소 높이이고(한 줄 높이가 24 이하일 때) 첫 줄이 가운데 선다.
            plan.top_padding = std::max(vertical_padding_floor, (minimum_height - plan.line_height) / 2.0f);

            // 맨 오른쪽 부품은 오른쪽 가장자리에서 part_gap을 띄우고, 부품이 없으면 글이 horizontal_padding을 비운다.
            const float usable { std::isfinite(width) ? std::max(0.0f, width) : 0.0f };
            const float close_part { has_close ? plan.close_size + part_gap : 0.0f };
            float text_width { usable - leading_width - (has_close ? part_gap + close_part : horizontal_padding) };
            if (has_action(config))
            {
                plan.action_width = action_width_of(config, context);
                const float beside { usable - leading_width - (part_gap + close_part + plan.action_width + part_gap) };
                plan.action_below = beside < info_bar_side_action_minimum_text;
                if (plan.action_below == false)
                    text_width = beside;
            }
            plan.text_width = std::max(0.0f, text_width);

            plan.title_height = config.title.empty() ? 0.0f : plan.line_height;
            // 글이 설 폭이 없으면 재지 않는다. 한 글자 폭으로 접어 막대가 끝없이 길어지는 것을 막는다.
            if (config.message.empty() == false && plan.text_width > 0.0f)
                plan.message = measure_text_block(context, config.message, message_style(metrics, config.max_lines), plan.text_width);

            // 글이 없어도 첫 줄은 있다. 글리프와 단추가 그 줄에 선다.
            plan.rows = std::max(plan.line_height, plan.title_height + plan.message.height);
            float content { plan.rows };
            if (plan.action_below)
                content += part_gap + plan.action_height;
            plan.height = std::max(minimum_height, 2.0f * plan.top_padding + content);
            return plan;
        }
    }

    info_bar_element::info_bar_element(info_bar_config config)
        : ui_element { ui_element_id { ui_element_kind::info_bar, config.owner } }
        , config_ { std::move(config) }
    {
        // 막대는 내용 위에 놓이는 표면이다. 단추가 없어도 몸통 클릭이 아래 element로 새지 않는다.
        set_hit_opaque(true);

        if (has_action(config_))
        {
            auto action { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::info_bar_action, config_.owner }, text_button_config { .text = config_.action_label }) };
            // 토스트와 달리 상주 알림이다. 곧 사라지지 않으므로 초점이 얹혀도 증발하지 않는다.
            action->set_cursor(ui_cursor::hand);
            action->set_action(ui_trigger::left_click, config_.action);
            action_ = action.get();
            add_child(std::move(action));
        }

        if (config_.close != nullptr)
        {
            auto close { std::make_unique<button_element>(ui_element_id { ui_element_kind::info_bar_close, config_.owner }, button_config { .glyph = codicons::icon_close, .role = button_visual_role::toolbar }) };
            const std::u8string name { default_text::text_or_default(config_.close_tooltip, default_text::info_bar_close()) };
            close->set_tooltip(name);
            close->set_access_name(name);
            close->set_cursor(ui_cursor::hand);
            close->set_action(ui_trigger::left_click, config_.close);
            close_ = close.get();
            add_child(std::move(close));
        }
    }

    info_bar_size info_bar_element::measure(const info_bar_config& config, const measure_context& context, const float width)
    {
        const bar_plan plan { plan_bar(config, context, width) };
        return { plan.height, plan.action_below };
    }

    void info_bar_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const bar_plan plan { plan_bar(config_, context.measure(), context.slot.width / scale) };
        placement_.line_height = plan.line_height;
        placement_.top_padding = plan.top_padding;
        placement_.text_width = plan.text_width;
        placement_.title_height = plan.title_height;
        placement_.message = plan.message;

        // 글리프와 닫기 단추, 옆에 선 동작 단추는 첫 줄의 가운데에 맞춘다. 여러 줄이면 막대 가운데가 아니라 첫 줄에 붙는다.
        const rect_f slot { context.slot };
        const float row_top { slot.y + plan.top_padding * scale };
        const float row_center { row_top + plan.line_height * scale / 2.0f };
        const float close_size { plan.close_size * scale };
        const float right_edge { slot.x + slot.width - part_gap * scale };
        if (close_ != nullptr)
            close_->arrange(context.for_child({ right_edge - close_size, row_center - close_size / 2.0f, close_size, close_size }));
        if (action_ == nullptr)
            return;

        const float action_height { plan.action_height * scale };
        if (plan.action_below)
        {
            // 글의 왼쪽에 맞추고 마지막 글 줄과 part_gap 떨어진다. 폭이 모자라면 오른쪽 가장자리 안으로 줄인다.
            const float left { slot.x + leading_width * scale };
            const float width { std::min(plan.action_width * scale, std::max(0.0f, slot.x + slot.width - part_gap * scale - left)) };
            action_->arrange(context.for_child({ left, row_top + (plan.rows + part_gap) * scale, width, action_height }));
            return;
        }
        const float width { plan.action_width * scale };
        const float right { close_ != nullptr ? right_edge - close_size - part_gap * scale : right_edge };
        action_->arrange(context.for_child({ right - width, row_center - action_height / 2.0f, width, action_height }));
    }

    void info_bar_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;

        // 몸은 심각도와 무관한 중립 바탕이다. 고대비가 이 바탕을 창 배경으로 접어도 테가 막대를 남긴다.
        const float radius { corner_radius * scale };
        context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radius, radius), solid_paint(context.palette.raised_background));
        const ui_color severity { severity_color(context.palette, config_.severity) };
        SkPaint border { solid_paint(severity) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(scale);
        // 테가 막대 안쪽에 온전히 들어오도록 반 픽셀 오므린다.
        const SkRect inset { SkRect::MakeXYWH(box.x + scale / 2.0f, box.y + scale / 2.0f, std::max(0.0f, box.width - scale), std::max(0.0f, box.height - scale)) };
        context.canvas.drawRRect(SkRRect::MakeRectXY(inset, std::max(0.0f, radius - scale / 2.0f), std::max(0.0f, radius - scale / 2.0f)), border);

        // 글리프는 늘 그린다. 색만으로 뜻을 말하지 않는다.
        const float row_top { box.y + placement_.top_padding * scale };
        const float line_height { placement_.line_height * scale };
        if (context.codicon_typeface != nullptr)
        {
            const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), glyph_font_size * scale };
            draw_centered_glyph(context.canvas, severity_glyph(config_.severity), { box.x + horizontal_padding * scale, row_top, glyph_cell * scale, line_height }, glyph_font, solid_paint(severity));
        }

        const float text_left { box.x + leading_width * scale };
        const float text_width { placement_.text_width * scale };
        if (text_width > 0.0f)
        {
            const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
            static_cast<void>(draw_text_block(context, config_.title, { text_left, row_top, text_width, line_height }, title_style(context.metrics), foreground));
            if (placement_.message.lines.empty() == false)
            {
                const float title_height { placement_.title_height * scale };
                const rect_f message_box { text_left, row_top + title_height, text_width, placement_.message.height * scale };
                static_cast<void>(draw_text_block(context, config_.message, message_box, message_style(context.metrics, config_.max_lines), foreground, placement_.message));
            }
        }

        draw_children(context, interaction);
    }

    access_info info_bar_element::accessibility() const
    {
        std::u8string name {};
        const auto append = [&name](const std::u8string_view part) {
            if (part.empty())
                return;
            if (name.empty() == false)
                name += u8' ';
            name += part;
        };
        append(default_text::text_or_default(config_.severity_name, default_text::severity_name(config_.severity)));
        append(config_.title);
        append(config_.message);
        return { .role = access_role::alert, .name = std::move(name) };
    }

    std::optional<size_f> info_bar_element::preferred_size(const measure_context& context, const float available_width) const
    {
        // 가로 stack은 자식에게 폭 제한을 두지 않고 묻는다. 줄바꿈은 폭이 있어야 높이가 나온다.
        if (std::isfinite(available_width) == false)
            return std::nullopt;
        return size_f { available_width, plan_bar(config_, context, available_width).height };
    }
}
