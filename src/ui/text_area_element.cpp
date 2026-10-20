#include "luil/ui/text_area_element.h"

#include "luil/ui/draw_primitives.h"
#include "luil/ui/font_scale.h"

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
#include <utility>
#include <vector>

namespace luil {
    namespace {
        // 한 줄의 구간이다 (UTF-8 byte offset). `end`는 그 줄의 줄 바꿈 앞이다.
        struct text_line
        {
            std::size_t begin { 0 };
            std::size_t end { 0 };
        };

        [[nodiscard]] std::vector<text_line> split_lines(const std::u8string_view text)
        {
            std::vector<text_line> lines {};
            std::size_t begin { 0 };
            for (std::size_t offset = 0; offset < text.size(); ++offset)
                if (text[offset] == u8'\n')
                {
                    lines.push_back({ begin, offset });
                    begin = offset + 1;
                }
            lines.push_back({ begin, text.size() });
            return lines;
        }

        // offset이 선 줄이다. 줄 바꿈 바로 뒤는 다음 줄의 처음이다.
        [[nodiscard]] std::size_t line_of(const std::vector<text_line>& lines, const std::size_t offset) noexcept
        {
            for (std::size_t index = 0; index < lines.size(); ++index)
                if (offset <= lines[index].end)
                    return index;
            return lines.size() - 1;
        }

        // 줄 안에서 `target`(그 줄의 왼쪽 끝 기준 픽셀)에 가장 가까운 문자 경계다 (줄 안의 offset).
        //  - 한 줄 칸(`text_input_element::offset_at`)과 같은 규칙이다: 경계를 모아 폭으로 이분 탐색하고,
        //    가운데에서 가까운 쪽을 고른다.
        [[nodiscard]] std::size_t nearest_boundary(const std::u8string_view line, const float target, const std::function<float(std::u8string_view)>& measure)
        {
            if (target <= 0.0f || line.empty())
                return 0;
            std::vector<std::size_t> boundaries { 0 };
            for (std::size_t offset = 1; offset < line.size(); ++offset)
                if ((static_cast<unsigned int>(line[offset]) & 0xC0u) != 0x80u)
                    boundaries.push_back(offset);
            boundaries.push_back(line.size());
            const auto after { std::upper_bound(boundaries.begin(), boundaries.end(), target, [&](const float position, const std::size_t offset) { return position < measure(line.substr(0, offset)); }) };
            if (after == boundaries.end())
                return line.size();
            if (after == boundaries.begin())
                return *after;
            const std::size_t before { *(after - 1) };
            const float after_width { measure(line.substr(0, *after)) };
            return target - measure(line.substr(0, before)) < after_width - target ? before : *after;
        }

        // 줄을 그리는 몸통이다. 그리는 것은 칸이 하고, 이것은 흘린 만큼 밀린 자리만 갖는다 —
        // 흘리는 창(`scroll_area_element`)이 내용을 그 자리에 배치하고 자른다.
        class text_area_body final : public ui_element
        {
        public:
            using ui_element::ui_element;

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context&, const interaction_snapshot&) const override
            {
            }
        };
    } // namespace

    std::size_t text_area_line_count(const std::u8string_view text) noexcept
    {
        return static_cast<std::size_t>(std::count(text.begin(), text.end(), u8'\n')) + 1u;
    }

    float text_area_content_height(const std::u8string_view text) noexcept
    {
        return static_cast<float>(text_area_line_count(text)) * text_area_line_height + 2.0f * text_area_padding_y;
    }

    text_area_element::text_area_element(const ui_element_id id, text_input_view value, text_area_config config)
        : ui_element { id }
        , value_ { std::move(value) }
        , config_ { std::move(config) }
    {
        // 누름·끌기·더블 클릭은 interaction controller가 직접 다룬다 (`text_input_element`와 같다).
        // 여기서는 클릭을 흡수해 hit test의 대상이 되게만 한다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });

        // 내용 높이는 글의 줄 수만의 함수다. 조합 중이면 조합 글이 섞인 표시 문서로 잰다 — 그려지는 줄이 그것이다.
        scroll_area_config area {};
        area.owner = u8"text-area:" + id.owner;
        area.content_height = text_area_content_height(value_.displayed_text());
        area.scroll_offset = config_.scroll_offset;
        area.scroll = config_.scroll;
        area.scroll_to = config_.scroll_to;
        // 칸이 세로 키를 가진다 — 막대가 Tab의 자리를 하나 더 갖지 않는다 (목록과 같은 판정).
        area.bar_tab_stop = false;
        auto scroll_area { std::make_unique<scroll_area_element>(std::move(area)) };
        auto body { std::make_unique<text_area_body>(ui_element_id { ui_element_kind::none }) };
        body_ = body.get();
        scroll_area->set_content(std::move(body));
        area_ = scroll_area.get();
        add_child(std::move(scroll_area));
    }

    float text_area_element::scale() const noexcept
    {
        return scale_;
    }

    float text_area_element::font_size() const noexcept
    {
        return scaled_font_size(text_area_font_size, scale_);
    }

    std::u8string_view text_area_element::displayed_text() const noexcept
    {
        return value_.displayed_text();
    }

    std::size_t text_area_element::displayed_caret() const noexcept
    {
        const std::u8string_view text { displayed_text() };
        const std::size_t caret { value_.composing ? value_.composition_caret : value_.caret };
        return caret < text.size() ? caret : text.size();
    }

    float text_area_element::horizontal_scroll(const std::function<float(std::u8string_view)>& measure, const std::u8string_view text, const std::size_t caret) const
    {
        // 한 줄 칸의 `scroll_for`와 같은 식을 caret이 선 줄에 쓴다. 값이 글·caret·폭만으로 정해져 그리기와
        // 포인터 판정과 IME 질의가 같은 결과를 낸다.
        const std::vector<text_line> lines { split_lines(text) };
        const text_line line { lines[line_of(lines, caret)] };
        const std::u8string_view content { text.substr(line.begin, line.end - line.begin) };
        const float visible { area_->viewport().width - 2.0f * text_area_padding_x * scale_ };
        if (visible <= 0.0f)
            return 0.0f;
        const float total { measure(content) };
        const float caret_x { measure(content.substr(0, std::min(caret, line.end) - line.begin)) };
        float scroll { caret_x - visible };
        scroll = std::max(scroll, 0.0f);
        scroll = std::min(scroll, caret_x);
        scroll = std::min(scroll, std::max(total - visible, 0.0f));
        return scroll;
    }

    void text_area_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;
        // 흘리는 창이 칸 전체다. 여백은 몸통 안쪽의 글 자리가 진다 — 막대는 칸의 오른쪽 끝에 붙는다.
        area_->arrange(context.for_child(context.slot));
    }

    std::optional<text_input_snapshot> text_area_element::text_input() const
    {
        text_input_snapshot snapshot {
            value_.text,
            value_.caret,
            value_.anchor,
            value_.composing,
            value_.composition_text,
            value_.composition_caret,
            value_.applied_sequence,
        };
        snapshot.multiline = true;
        return snapshot;
    }

    std::optional<rect_f> text_area_element::text_span_bounds(const text_span_query& query, const text_measurer& measurer) const
    {
        if (measurer == nullptr)
            return std::nullopt;
        // 질의한 문서(조합 글 포함)를 그대로 잰다 — 확정 글로 재면 조합이 시작되는 순간 offset이 문서를 넘는다.
        const float size { font_size() };
        const auto measure { [&measurer, size](const std::u8string_view value) { return measurer(value, size); } };
        const std::u8string_view text { query.text };
        const std::size_t begin { std::min(query.begin, text.size()) };
        const std::size_t end { std::max(std::min(query.end, text.size()), begin) };
        const std::vector<text_line> lines { split_lines(text) };
        const std::size_t index { line_of(lines, begin) };
        const text_line line { lines[index] };
        const float left { area_->viewport().x + text_area_padding_x * scale_ - horizontal_scroll(measure, text, std::min(query.caret, text.size())) };
        const float line_height { text_area_line_height * scale_ };
        const float top { body_->bounds().y + text_area_padding_y * scale_ + static_cast<float>(index) * line_height };
        const std::u8string_view content { text.substr(line.begin, line.end - line.begin) };
        const float from { left + measure(content.substr(0, begin - line.begin)) };
        const float to { left + measure(content.substr(0, std::min(end, line.end) - line.begin)) };
        return rect_f { from, top, to - from, line_height };
    }

    std::optional<std::size_t> text_area_element::offset_at_point(const float x, const float y, const text_measurer& measurer) const
    {
        const std::u8string_view text { value_.text };
        if (measurer == nullptr)
            return text.size();
        const float size { font_size() };
        const auto measure { [&measurer, size](const std::u8string_view value) { return measurer(value, size); } };
        const std::vector<text_line> lines { split_lines(text) };
        const float line_height { text_area_line_height * scale_ };
        const float top { body_->bounds().y + text_area_padding_y * scale_ };
        // 첫 줄 위는 글의 처음, 마지막 줄 아래는 글의 끝이다 — 첫 줄에서 ↑, 마지막 줄에서 ↓가 그 끝으로 간다.
        if (y < top)
            return std::size_t { 0 };
        const float row { std::floor((y - top) / line_height) };
        if (row >= static_cast<float>(lines.size()))
            return text.size();
        const text_line line { lines[static_cast<std::size_t>(row)] };
        const std::u8string_view content { text.substr(line.begin, line.end - line.begin) };
        const float left { area_->viewport().x + text_area_padding_x * scale_ - horizontal_scroll(measure, text, std::min(value_.caret, text.size())) };
        return line.begin + nearest_boundary(content, x - left, measure);
    }

    std::optional<std::size_t> text_area_element::offset_at(const float x, const text_measurer& measurer) const
    {
        // y가 없으면 caret이 선 줄에서 판정한다.
        const std::vector<text_line> lines { split_lines(value_.text) };
        const std::size_t index { line_of(lines, std::min(value_.caret, value_.text.size())) };
        const float line_height { text_area_line_height * scale_ };
        const float y { body_->bounds().y + text_area_padding_y * scale_ + (static_cast<float>(index) + 0.5f) * line_height };
        return offset_at_point(x, y, measurer);
    }

    void text_area_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const bool focused { interaction.focused_input == id() };
        const rect_f box { bounds() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        const float radius { context.metrics.control_corner_radius * scale };
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.input_background));
        // 초점을 받은 칸은 테두리를 강조한다 (한 줄 칸과 같은 표시).
        SkPaint border { solid_paint(focused ? context.palette.accent : context.palette.input_border) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.0f * scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

        const SkFont font { sk_ref_sp(context.ui_typeface), font_size() };
        const auto measure { [&font](const std::u8string_view value) { return measure_text(value, font); } };
        const std::u8string_view text { displayed_text() };
        const std::vector<text_line> lines { split_lines(text) };
        const rect_f viewport { area_->viewport() };
        const float line_height { text_area_line_height * scale };
        const float top { body_->bounds().y + text_area_padding_y * scale };
        const float left { viewport.x + text_area_padding_x * scale - horizontal_scroll(measure, text, displayed_caret()) };
        const auto baseline_of = [&font, top, line_height](const std::size_t index) { return top + static_cast<float>(index) * line_height + centered_text_baseline(font, line_height); };

        context.canvas.save();
        context.canvas.clipRect(SkRect::MakeXYWH(viewport.x, viewport.y, viewport.width, viewport.height));

        if (text.empty() && focused == false && config_.placeholder.empty() == false)
        {
            SkPaint placeholder { solid_paint(context.palette.primary_foreground) };
            placeholder.setAlphaf(0.5f);
            static_cast<void>(draw_text_within(context.canvas, config_.placeholder, viewport.x + text_area_padding_x * scale, baseline_of(0), viewport.width - 2.0f * text_area_padding_x * scale, font,
                placeholder));
        }

        // 창에 걸치는 줄만 그린다. 긴 글에서도 그리기가 창 크기에 머문다.
        const float first_row { std::floor((viewport.y - top) / line_height) };
        const std::size_t first { first_row > 0.0f ? static_cast<std::size_t>(first_row) : 0u };
        const float last_row { std::ceil((viewport.y + viewport.height - top) / line_height) };
        const std::size_t last { std::min(lines.size(), last_row > 0.0f ? static_cast<std::size_t>(last_row) + 1u : 0u) };

        // 조합 중에는 선택 대신 조합 구간에 밑줄을 긋는다 — 그동안 선택은 IME의 것이다.
        const std::size_t selection_begin { value_.composing ? 0u : std::min(value_.caret, value_.anchor) };
        const std::size_t selection_end { value_.composing ? 0u : std::max(value_.caret, value_.anchor) };
        const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
        // 고른 줄 바꿈을 보이는 폭이다. 빈 줄을 골라도 고른 줄이 보인다.
        const float newline_width { 4.0f * scale };
        for (std::size_t index = first; index < last; ++index)
        {
            const text_line line { lines[index] };
            const std::u8string_view content { text.substr(line.begin, line.end - line.begin) };
            const float row_top { top + static_cast<float>(index) * line_height };
            std::optional<SkRect> selection {};
            if (selection_begin != selection_end && selection_begin <= line.end && selection_end > line.begin)
            {
                const std::size_t from_offset { std::max(selection_begin, line.begin) - line.begin };
                const std::size_t to_offset { std::min(selection_end, line.end) - line.begin };
                const float from { left + measure(content.substr(0, from_offset)) };
                float to { left + measure(content.substr(0, to_offset)) };
                if (selection_end > line.end)
                    to += newline_width;
                selection = SkRect::MakeXYWH(from, row_top, to - from, line_height);
                context.canvas.drawRect(*selection, solid_paint(context.palette.selection_background));
            }
            if (content.empty() == false)
            {
                draw_text(context.canvas, content, left, baseline_of(index), font, foreground);
                // 고른 구간의 글자는 그 바탕의 짝이다 (한 줄 칸과 같은 규칙 — 고대비에서만 바뀐다).
                if (selection.has_value() && enabled() && context.palette.selection_foreground != context.palette.primary_foreground)
                {
                    context.canvas.save();
                    context.canvas.clipRect(*selection);
                    draw_text(context.canvas, content, left, baseline_of(index), font, solid_paint(context.palette.selection_foreground));
                    context.canvas.restore();
                }
            }
        }

        // 조합 구간은 밑줄이다. 여러 줄에 걸친 조합은 줄마다 긋는다.
        if (value_.composing && value_.composing_end > value_.composing_begin)
        {
            const std::size_t underline_begin { std::min(value_.composing_begin, text.size()) };
            const std::size_t underline_end { std::min(value_.composing_end, text.size()) };
            for (std::size_t index = first; index < last; ++index)
            {
                const text_line line { lines[index] };
                if (underline_begin > line.end || underline_end <= line.begin)
                    continue;
                const std::u8string_view content { text.substr(line.begin, line.end - line.begin) };
                const float from { left + measure(content.substr(0, std::max(underline_begin, line.begin) - line.begin)) };
                const float to { left + measure(content.substr(0, std::min(underline_end, line.end) - line.begin)) };
                context.canvas.drawRect(SkRect::MakeXYWH(from, baseline_of(index) + 2.0f * scale, to - from, 1.0f * scale), solid_paint(context.palette.accent));
            }
        }

        // caret은 초점을 받은 동안 깜빡인다. 위상은 초점을 받은 시각이다 (한 줄 칸과 같다).
        const bool caret_on { focused && interaction.focus_started_at.has_value() && ((context.now - *interaction.focus_started_at) / caret_blink_interval) % 2 == 0 };
        if (caret_on)
        {
            const std::size_t caret { displayed_caret() };
            const std::size_t index { line_of(lines, caret) };
            const text_line line { lines[index] };
            const float caret_x { left + measure(text.substr(line.begin, caret - line.begin)) };
            const float row_top { top + static_cast<float>(index) * line_height };
            context.canvas.drawRect(SkRect::MakeXYWH(caret_x, row_top + 2.0f * scale, 1.0f * scale, line_height - 4.0f * scale), solid_paint(context.palette.primary_foreground));
        }
        context.canvas.restore();

        // 막대는 글 위에 선다.
        draw_children(context, interaction);
    }

    std::optional<std::chrono::steady_clock::time_point> text_area_element::next_update(const update_context& context, const interaction_snapshot& interaction) const
    {
        if (interaction.focused_input != id() || interaction.focus_started_at.has_value() == false)
            return std::nullopt;
        const auto phase { (context.now - *interaction.focus_started_at) % caret_blink_interval };
        return context.now + (caret_blink_interval - phase);
    }

    access_info text_area_element::accessibility() const
    {
        // 값은 확정 글이다 — 조합 글은 아직 사용자의 글이 아니다 (`text_input_element`와 같은 판단).
        return { .role = access_role::edit, .name = config_.placeholder, .value = value_.text };
    }
} // namespace luil
