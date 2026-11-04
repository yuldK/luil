#include "host/inspector_overlay.h"

#include "ui/inspect_geometry.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/text_block.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        // 선과 글의 치수다 (논리 픽셀). 그릴 때 `context.scale`을 곱한다.
        constexpr float picked_line_width { 2.0f };
        constexpr float halo_width { 1.0f };
        constexpr float parent_line_width { 1.0f };
        constexpr float all_line_width { 1.0f };
        constexpr float dash_length { 4.0f };
        constexpr float dash_gap { 3.0f };
        constexpr float info_font_size { 11.0f };
        constexpr float info_line_height { 15.0f };
        constexpr float info_padding { 6.0f };
        constexpr float info_margin { 4.0f };
        constexpr float info_minimum_width { 200.0f };
        constexpr float swatch_size { 12.0f };
        constexpr float swatch_gap { 6.0f };
        // 견본 판의 열 사이다.
        constexpr float swatch_column_gap { 12.0f };
        // 이보다 작은 표면(메뉴 popup)은 상자 없이 윤곽만 긋는다.
        constexpr float roomy_width { 240.0f };
        constexpr float roomy_height { 140.0f };
        // `outline_all`의 선은 집은 요소의 선과 갈리도록 옅게 긋는다.
        constexpr float all_line_alpha { 0.4f };

        [[nodiscard]] SkRect to_sk_rect(const rect_f& box) noexcept
        {
            return SkRect::MakeXYWH(box.x, box.y, box.width, box.height);
        }

        [[nodiscard]] bool overlaps(const rect_f& first, const rect_f& second) noexcept
        {
            return first.x < second.x + second.width && second.x < first.x + first.width && first.y < second.y + second.height && second.y < first.y + first.height;
        }

        // 상자 안쪽으로 `width` 두께의 띠를 긋는다. 상자가 두 배 두께보다 작으면 통째로 채운다.
        //  - 선은 늘 상자 안에 든다. 잘려 보이지 않는 자리로 새지 않는다 (`draw_focus_ring`과 같은 이유).
        void stroke_inside(SkCanvas& canvas, const rect_f& box, const float width, const SkPaint& line)
        {
            if (box.width < width * 2.0f || box.height < width * 2.0f)
            {
                SkPaint fill { line };
                fill.setStyle(SkPaint::kFill_Style);
                canvas.drawRect(to_sk_rect(box), fill);
                return;
            }
            SkPaint stroke { line };
            stroke.setStyle(SkPaint::kStroke_Style);
            stroke.setStrokeWidth(width);
            canvas.drawRect(SkRect::MakeXYWH(box.x + width / 2.0f, box.y + width / 2.0f, box.width - width, box.height - width), stroke);
        }

        // 상자 바깥으로 `width` 두께의 띠를 긋는다. 집은 요소의 선 바깥에 서는 후광이다.
        void stroke_outside(SkCanvas& canvas, const rect_f& box, const float width, const SkPaint& line)
        {
            SkPaint stroke { line };
            stroke.setStyle(SkPaint::kStroke_Style);
            stroke.setStrokeWidth(width);
            canvas.drawRect(SkRect::MakeXYWH(box.x - width / 2.0f, box.y - width / 2.0f, box.width + width, box.height + width), stroke);
        }

        // 상자 안쪽을 따라 점선을 긋는다. 짧고 긴 마디를 번갈아 놓는 조각 사각형이라 효과 모듈이 필요 없다.
        void dash_inside(SkCanvas& canvas, const rect_f& box, const float width, const float dash, const float gap, const SkPaint& line)
        {
            const float step { dash + gap };
            const float right { box.x + box.width };
            const float bottom { box.y + box.height };
            for (float x { box.x }; x < right; x += step)
            {
                const float length { std::min(dash, right - x) };
                canvas.drawRect(SkRect::MakeXYWH(x, box.y, length, width), line);
                canvas.drawRect(SkRect::MakeXYWH(x, bottom - width, length, width), line);
            }
            for (float y { box.y }; y < bottom; y += step)
            {
                const float length { std::min(dash, bottom - y) };
                canvas.drawRect(SkRect::MakeXYWH(box.x, y, width, length), line);
                canvas.drawRect(SkRect::MakeXYWH(right - width, y, width, length), line);
            }
        }

        void append_hex(std::u8string& text, const ui_color color)
        {
            constexpr std::u8string_view digits { u8"0123456789ABCDEF" };
            text += u8"#";
            for (int shift { 28 }; shift >= 0; shift -= 4)
                text.push_back(digits[(color >> static_cast<unsigned>(shift)) & 0xFu]);
        }

        [[nodiscard]] std::vector<std::u8string> split_lines(const std::u8string& text)
        {
            std::vector<std::u8string> lines {};
            std::size_t start { 0 };
            while (start <= text.size())
            {
                const std::size_t end { text.find(u8'\n', start) };
                if (end == std::u8string::npos)
                {
                    lines.push_back(text.substr(start));
                    break;
                }
                lines.push_back(text.substr(start, end - start));
                start = end + 1;
            }
            return lines;
        }

        // 줄 모음을 담은 상자 하나의 크기다 (물리 픽셀).
        struct text_panel
        {
            std::vector<std::u8string> lines {};
            float width { 0.0f };
            float height { 0.0f };
            // 한 열의 줄 수와 다음 열까지의 거리다. 줄은 열을 위에서 아래로 채운 뒤 다음 열로 넘어간다.
            std::size_t rows { 0 };
            float column_step { 0.0f };
        };

        // 줄 하나가 놓일 자리의 왼쪽 위다 (물리 픽셀).
        [[nodiscard]] SkPoint panel_line_origin(const text_panel& panel, const rect_f& box, const std::size_t index, const float scale) noexcept
        {
            const std::size_t column { index / panel.rows };
            const std::size_t row { index % panel.rows };
            return SkPoint::Make(
                box.x + info_padding * scale + static_cast<float>(column) * panel.column_step
                , box.y + info_padding * scale + static_cast<float>(row) * info_line_height * scale
            );
        }

        // 판의 크기를 줄들과 글꼴로 정한다. 폭은 가장 넓은 줄을 따르되 흔들리지 않도록 최소 폭을 둔다.
        [[nodiscard]] text_panel measure_panel(std::vector<std::u8string> lines, const SkFont& font, const float scale, const float extra_width, const rect_f& surface)
        {
            float widest { 0.0f };
            for (const std::u8string& line : lines)
                widest = std::max(widest, measure_text(line, font));
            text_panel panel {};
            panel.width = std::max(widest + extra_width + info_padding * 2.0f * scale, info_minimum_width * scale);
            panel.height = static_cast<float>(lines.size()) * info_line_height * scale + info_padding * 2.0f * scale;
            panel.rows = lines.size();
            panel.lines = std::move(lines);
            // 표면 밖으로 나가지 않는다.
            panel.width = std::min(panel.width, surface.width - info_margin * 2.0f * scale);
            panel.height = std::min(panel.height, surface.height - info_margin * 2.0f * scale);
            return panel;
        }

        // 바탕과 테를 칠한 판 위에 줄들을 쓴다. `text_inset`은 줄 앞에 남기는 폭이다 (견본 사각 자리).
        void draw_panel(draw_context& context, const text_panel& panel, const rect_f& box, const SkFont& font, const float text_inset)
        {
            const float scale { context.scale };
            SkCanvas& canvas { context.canvas };
            canvas.drawRect(to_sk_rect(box), solid_paint(context.palette.tooltip_background));
            stroke_inside(canvas, box, scale, solid_paint(context.palette.tooltip_border));

            SkAutoCanvasRestore restore { &canvas, true };
            canvas.clipRect(to_sk_rect(box));
            const SkPaint text_paint { solid_paint(context.palette.primary_foreground) };
            const float baseline { centered_text_baseline(font, info_line_height * scale) };
            for (std::size_t index { 0 }; index < panel.lines.size(); ++index)
            {
                const SkPoint origin { panel_line_origin(panel, box, index, scale) };
                draw_text(canvas, panel.lines[index], origin.x() + text_inset, origin.y() + baseline, font, text_paint);
            }
        }

        // 집은 요소의 선들이다. 안쪽 여백 띠, 부모의 점선, 2px 실선과 바깥 후광 차례로 겹친다.
        void draw_picked(draw_context& context, const std::vector<inspect_node>& nodes, const inspect_node& picked)
        {
            const float scale { context.scale };
            SkCanvas& canvas { context.canvas };
            const rect_f visible { *picked.visible_bounds };

            // 안쪽 여백: 집은 요소에서 자식 합집합을 뺀 띠다. 고대비의 `accent_soft`는 불투명 highlight라 칠하면 요소를 덮는다.
            if (context.theme != color_theme::high_contrast)
            {
                if (const std::optional<rect_f> content { inspect_content_bounds(*picked.element) }; content.has_value())
                {
                    SkAutoCanvasRestore restore { &canvas, true };
                    canvas.clipRect(to_sk_rect(visible));
                    const SkPaint band_paint { solid_paint(context.palette.accent_soft) };
                    for (const rect_f& band : bands_outside(picked.bounds, *content))
                        canvas.drawRect(to_sk_rect(band), band_paint);
                }
            }

            if (picked.parent != no_inspect_parent && nodes[picked.parent].visible_bounds.has_value())
                dash_inside(canvas, *nodes[picked.parent].visible_bounds, parent_line_width * scale, dash_length * scale, dash_gap * scale, solid_paint(context.palette.primary_foreground));

            stroke_outside(canvas, visible, halo_width * scale, solid_paint(context.palette.window_background));
            stroke_inside(canvas, visible, picked_line_width * scale, solid_paint(context.palette.accent));
        }

        void draw_info(draw_context& context, const ui_tree& tree, const ui_element& picked, const std::optional<rect_f>& picked_visible, const inspector_config& config, const rect_f& surface)
        {
            const float scale { context.scale };
            const SkFont font { make_text_font(context, text_font { .size = info_font_size, .face = text_layout_face::code }) };
            const text_panel panel { measure_panel(split_lines(describe_element(tree, picked, scale, config)), font, scale, 0.0f, surface) };
            // 왼쪽 위에 놓고, 집은 요소의 보이는 영역과 겹치면 왼쪽 아래로 옮긴다.
            rect_f box { surface.x + info_margin * scale, surface.y + info_margin * scale, panel.width, panel.height };
            if (picked_visible.has_value() && overlaps(box, *picked_visible))
                box.y = surface.y + surface.height - info_margin * scale - panel.height;
            draw_panel(context, panel, box, font, 0.0f);
        }

        void draw_palette(draw_context& context, const rect_f& surface)
        {
            const float scale { context.scale };
            const SkFont font { make_text_font(context, text_font { .size = info_font_size, .face = text_layout_face::code }) };
            const std::vector<inspector_swatch> swatches { inspector_palette_swatches(context.palette) };
            std::vector<std::u8string> lines {};
            lines.reserve(swatches.size());
            float widest { 0.0f };
            for (const inspector_swatch& swatch : swatches)
            {
                std::u8string line { swatch.name };
                line += u8" ";
                append_hex(line, swatch.color);
                widest = std::max(widest, measure_text(line, font));
                lines.push_back(std::move(line));
            }

            const float inset { (swatch_size + swatch_gap) * scale };
            const float column_width { widest + inset };
            const float column_gap { swatch_column_gap * scale };
            const float padding { info_padding * scale };
            const float available_width { surface.width - info_margin * 2.0f * scale };
            // 표면의 높이가 모자라면 열마다 들어가는 줄까지만 쓴다.
            const std::size_t rows_that_fit { static_cast<std::size_t>(std::max(0.0f, (surface.height - (info_margin + info_padding) * 2.0f * scale) / (info_line_height * scale))) };
            const inspect_palette_grid grid { inspect_palette_grid_for(lines.size(), column_width, column_gap, available_width - padding * 2.0f, rows_that_fit) };
            if (grid.rows == 0u)
                return;
            lines.resize(std::min(lines.size(), grid.rows * grid.columns));

            text_panel panel {};
            panel.rows = grid.rows;
            panel.column_step = column_width + column_gap;
            const float content_width { column_width * static_cast<float>(grid.columns) + column_gap * static_cast<float>(grid.columns - 1u) };
            panel.width = std::min(std::max(content_width + padding * 2.0f, info_minimum_width * scale), available_width);
            panel.height = std::min(static_cast<float>(grid.rows) * info_line_height * scale + padding * 2.0f, surface.height - info_margin * 2.0f * scale);
            panel.lines = std::move(lines);
            // 오른쪽 위에 선다.
            const rect_f box { surface.x + surface.width - info_margin * scale - panel.width, surface.y + info_margin * scale, panel.width, panel.height };
            draw_panel(context, panel, box, font, inset);

            SkCanvas& canvas { context.canvas };
            SkAutoCanvasRestore restore { &canvas, true };
            canvas.clipRect(to_sk_rect(box));
            const float line_height { info_line_height * scale };
            const SkPaint border_paint { solid_paint(context.palette.tooltip_border) };
            for (std::size_t index { 0 }; index < panel.lines.size(); ++index)
            {
                const SkPoint origin { panel_line_origin(panel, box, index, scale) };
                const rect_f square { origin.x(), origin.y() + (line_height - swatch_size * scale) / 2.0f, swatch_size * scale, swatch_size * scale };
                canvas.drawRect(to_sk_rect(square), solid_paint(swatches[index].color));
                stroke_inside(canvas, square, scale, border_paint);
            }
        }
    }

    void draw_inspector(draw_context& context, const ui_tree& tree, const interaction_snapshot& interaction, const inspector_config& config)
    {
        if (config.enabled == false || tree.root() == nullptr)
            return;
        const float scale { context.scale };
        if (scale <= 0.0f)
            return;
        const std::vector<inspect_node> nodes { inspect_nodes(tree) };
        SkCanvas& canvas { context.canvas };

        // 보이는 모든 요소의 윤곽이다. 집은 요소의 선보다 먼저 그려 그 아래에 깔린다.
        if (config.outline_all)
        {
            const SkPaint faint { solid_paint(with_alpha(context.palette.secondary_foreground, all_line_alpha)) };
            for (const inspect_node& node : nodes)
            {
                if (node.visible_path && node.visible_bounds.has_value())
                    stroke_inside(canvas, *node.visible_bounds, all_line_width * scale, faint);
            }
        }

        const ui_element* const picked { pick_inspected(tree, interaction, config) };
        const inspect_node* picked_node { nullptr };
        if (picked != nullptr)
        {
            for (const inspect_node& node : nodes)
            {
                if (node.element == picked)
                {
                    picked_node = &node;
                    break;
                }
            }
        }
        const bool has_target { picked_node != nullptr && picked_node->visible_bounds.has_value() };
        if (has_target)
            draw_picked(context, nodes, *picked_node);

        // 작은 표면(메뉴 popup)은 윤곽만 긋는다.
        const rect_f surface { tree.root()->bounds() };
        if (surface.width < roomy_width * scale || surface.height < roomy_height * scale)
            return;
        if (has_target && config.show_info)
            draw_info(context, tree, *picked, picked_node->visible_bounds, config, surface);
        if (config.show_palette)
            draw_palette(context, surface);
    }
}
