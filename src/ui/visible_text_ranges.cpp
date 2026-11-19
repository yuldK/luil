#include "ui/visible_text_ranges.h"

#include "luil/text/grapheme.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/text_area_element.h"
#include "luil/ui/text_layout.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace luil {
    namespace {
        void append_span(std::vector<visible_text_ranges::span>& ranges, const std::size_t begin, const std::size_t end)
        {
            if (ranges.empty() == false && ranges.back().end >= begin)
                ranges.back().end = std::max(ranges.back().end, end);
            else
                ranges.push_back({ begin, end });
        }

        void append_visual_row(std::vector<visible_text_ranges::span>& ranges, const text_layout& layout
            , const std::size_t start, const std::size_t next, const float left, const float right
        )
        {
            const auto stops { layout.caret_stops() };
            for (const auto& cluster : layout.clusters())
            {
                if (cluster.left >= right || cluster.right <= left)
                    continue;
                const auto following {
                    std::upper_bound(stops.begin(), stops.end(), cluster.begin
                        , [](const std::size_t at, const text_layout_stop& stop) { return at < stop.offset; }
                    )
                };
                if (following == stops.begin())
                    continue;

                // 방향이 바뀌는 경계에는 두 caret 위치가 있다. glyph cluster의 양 끝을 쓰고 합자 내부만 caret에서 잰다.
                const auto position = [&](const text_layout_stop& stop) {
                    if (stop.offset <= cluster.begin)
                        return cluster.rtl ? cluster.right : cluster.left;
                    if (stop.offset >= cluster.end)
                        return cluster.rtl ? cluster.left : cluster.right;
                    return std::clamp(stop.x, cluster.left, cluster.right);
                };
                for (auto from { std::prev(following) }, to { following }; to != stops.end() && from->offset < cluster.end; ++from, ++to)
                {
                    const auto [min_x, max_x] { std::minmax({ position(*from), position(*to) }) };
                    if (max_x > left && min_x < right && min_x < max_x)
                        append_span(ranges, start + from->offset, start + to->offset);
                }
            }

            const std::size_t end { layout.text().size() };
            const float end_x { layout.caret_rect(end).x };
            if (start + end < next && left <= end_x && end_x <= right)
                append_span(ranges, start + end, next);
        }

        // 글자소의 일부만 걸쳐도 그 글자소 전체를 반환한다. CRLF와 접은 줄은 원본 문서의 순서를 보존한다.
        template<typename position_of>
        void append_row(std::vector<visible_text_ranges::span>& ranges, const std::u8string_view content
            , const std::size_t start, const std::size_t next, const float left, const float right, const text_layout& layout, const position_of& position
        )
        {
            if (left >= right)
                return;
            if (layout.lines().size() == 1
                && (layout.lines().front().increasing == false
                    || std::any_of(layout.runs().begin(), layout.runs().end(), [](const text_layout_run& run) { return run.rtl; })))
            {
                append_visual_row(ranges, layout, start, next, left, right);
                return;
            }
            if (right <= 0.0f)
                return;
            const float width { position(content.size()) };
            if (left > width || (left == width && content.empty() == false && next == start + content.size()))
                return;

            std::size_t begin { start };
            std::size_t end { next };
            if (left > 0.0f || right < width)
            {
                const auto boundaries { text::grapheme_boundaries(content) };
                if (left > 0.0f)
                {
                    const auto before = [&](float x, std::size_t offset) { return x < position(offset); };
                    const auto after { std::upper_bound(boundaries.begin(), boundaries.end(), left, before) };
                    begin += after == boundaries.begin() ? 0 : *std::prev(after);
                }
                if (right < width)
                {
                    const auto before = [&](std::size_t offset, float x) { return position(offset) < x; };
                    const auto at { std::lower_bound(boundaries.begin(), boundaries.end(), right, before) };
                    end = start + (at == boundaries.end() ? content.size() : *at);
                }
            }
            append_span(ranges, begin, end);
        }
    }

    std::vector<visible_text_ranges::span> visible_text_ranges::of(const text_input_element& field, const rect_f& clip)
    {
        std::vector<span> ranges {};
        const rect_f box { field.box() };
        if (field.config_.secret || clip.y >= box.y + box.height || clip.y + clip.height <= box.y || field.layout_measurer_ == nullptr)
            return ranges;
        const float origin { field.text_left() };
        const float left { std::max(0.0f, clip.x - origin) };
        const float right { std::min(field.inner_width(), clip.x + clip.width - origin) };
        if (right <= left)
            return ranges;
        const auto content { field.displayed_text() };
        const auto measure = [&](std::u8string_view prefix) { return field.layout_measurer_(prefix, field.font_size()); };
        const auto& layout { field.layout_ };
        const float scroll { layout.valid() ? field.scroll_for(layout, field.displayed_caret()) : field.scroll_for(measure) };
        const auto position = [&](std::size_t at) { return layout.valid() ? layout.caret_rect(at).x : measure(content.substr(0, at)); };
        append_row(ranges, content, 0, content.size(), left + scroll, right + scroll, layout, position);
        return ranges;
    }

    std::vector<visible_text_ranges::span> visible_text_ranges::of(const text_area_element& field, const rect_f& clip)
    {
        std::vector<span> ranges {};
        const rect_f view { field.area().viewport() };
        const float left { std::max(view.x + field.gutter_width_ * field.scale_, clip.x) - field.text_left() };
        const float right { std::min(view.x + view.width, clip.x + clip.width) - field.text_left() };
        const float top { std::max(view.y, clip.y) - field.text_top() };
        const float bottom { std::min(view.y + view.height, clip.y + clip.height) - field.text_top() };
        const float height { field.line_height_logical() * field.scale_ };
        if (left >= right || top >= bottom || bottom <= 0.0f || height <= 0.0f || field.layout_measurer_ == nullptr)
            return ranges;

        const auto& lines { field.lines_ };
        const auto first { static_cast<std::size_t>(std::floor(std::max(0.0f, top) / height)) };
        const auto last { std::min(lines.size(), static_cast<std::size_t>(std::ceil(bottom / height))) };
        const auto text { field.displayed_text() };
        const auto& measure { field.measure_with(field.layout_measurer_) };
        for (std::size_t index { first }; index < last; ++index)
        {
            const auto& row { lines[index] };
            const auto content { text.substr(row.begin, row.end - row.begin) };
            const auto layout { field.row_layout(text, row) };
            const auto position = [&](std::size_t at) {
                return layout.valid() ? layout.caret_rect(at).x : measure(content.substr(0, at), field.font_size());
            };
            const std::size_t next { index + 1 < lines.size() ? lines[index + 1].begin : text.size() };
            append_row(ranges, content, row.begin, next, left, right, layout, position);
        }
        return ranges;
    }
}
