#include "luil/ui/text_layout.h"

#include "luil/text/grapheme.h"
#include "ui/text_layout_internal.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace luil {
    namespace {
        std::atomic<std::uint64_t> layout_revision { 1 };

        std::vector<std::size_t> boundaries_of(const std::u8string_view text)
        {
            return text::grapheme_boundaries(text);
        }

        std::vector<text_layout_stop> stops_of(const std::u8string_view text, const text_layout_segment& shaped, bool& approximate)
        {
            std::vector<text_layout_stop> stops {};
            const auto boundaries { boundaries_of(text) };
            // cluster는 원문 순서다. Glyph의 시각 순서와 별개다.
            std::size_t cluster { 0 };
            for (const auto at : boundaries)
            {
                while (cluster + 1 < shaped.clusters.size() && shaped.clusters[cluster].end < at)
                    ++cluster;
                float x { at == 0 ? 0.0f : shaped.width };
                if (cluster < shaped.clusters.size())
                {
                    const auto& item { shaped.clusters[cluster] };
                    const auto first { std::lower_bound(boundaries.begin(), boundaries.end(), item.begin) };
                    const auto last { std::lower_bound(first, boundaries.end(), item.end) };
                    const auto count { last - first };
                    const auto position { std::lower_bound(first, last, at) - first };
                    const float fraction { count > 0 ? static_cast<float>(position) / static_cast<float>(count) : (at >= item.end ? 1.0f : 0.0f) };
                    x = item.rtl ? item.right - fraction * (item.right - item.left) : item.left + fraction * (item.right - item.left);
                    approximate = approximate || count > 1;
                }
                stops.push_back({ at, x });
            }
            return stops;
        }

        float x_at(const std::span<const text_layout_stop> stops, const std::size_t offset) noexcept
        {
            if (stops.empty())
                return 0.0f;
            const auto found { std::upper_bound(stops.begin(), stops.end(), offset,
                [](const auto at, const auto& stop) { return at < stop.offset; })
            };
            return found == stops.begin() ? found->x : std::prev(found)->x;
        }
    }

    std::uint64_t text_layout_revision() noexcept { return layout_revision.load(std::memory_order_relaxed); }
    void invalidate_text_layouts() noexcept { layout_revision.fetch_add(1, std::memory_order_relaxed); }
    bool text_layout::valid() const noexcept { return data_ != nullptr; }
    std::u8string_view text_layout::text() const noexcept { return data_ != nullptr ? data_->text : std::u8string_view {}; }
    text_layout_backend text_layout::backend() const noexcept { return data_ != nullptr ? data_->backend : text_layout_backend::unavailable; }
    text_layout_size text_layout::size() const noexcept { return data_ != nullptr ? data_->size : text_layout_size {}; }
    std::span<const text_layout_line> text_layout::lines() const noexcept { return data_ != nullptr ? std::span<const text_layout_line> { data_->lines } : std::span<const text_layout_line> {}; }
    std::span<const text_layout_cluster> text_layout::clusters() const noexcept { return data_ != nullptr ? std::span<const text_layout_cluster> { data_->clusters } : std::span<const text_layout_cluster> {}; }
    std::span<const text_layout_run> text_layout::runs() const noexcept { return data_ != nullptr ? std::span<const text_layout_run> { data_->runs } : std::span<const text_layout_run> {}; }
    std::span<const text_layout_stop> text_layout::caret_stops() const noexcept { return data_ != nullptr ? std::span<const text_layout_stop> { data_->stops } : std::span<const text_layout_stop> {}; }
    bool text_layout::approximate_ligature_carets() const noexcept { return data_ != nullptr && data_->approximate; }

    std::size_t text_layout::storage_bytes() const noexcept
    {
        if (data_ == nullptr)
            return 0;
        std::size_t size { sizeof(text_layout_data) + data_->text.capacity() };
        size += data_->lines.capacity() * sizeof(text_layout_line) + data_->clusters.capacity() * sizeof(text_layout_cluster);
        size += data_->runs.capacity() * sizeof(text_layout_run) + data_->stops.capacity() * sizeof(text_layout_stop);
        size += data_->drawings.capacity() * sizeof(std::shared_ptr<const text_layout_draw_data>);
        for (const auto& drawing : data_->drawings)
            if (drawing != nullptr)
                size += drawing->storage_bytes();
        return size;
    }

    std::size_t text_layout::line_at(const std::size_t offset, const bool upstream) const noexcept
    {
        if (data_ == nullptr || data_->lines.empty())
            return 0;
        const auto& lines { data_->lines };
        const auto found { std::lower_bound(lines.begin(), lines.end(), std::min(offset, data_->text.size()),
            [](const auto& line, const auto at) { return line.end < at || (line.end == at && line.wrapped); })
        };
        std::size_t index { found == lines.end() ? lines.size() - 1 : static_cast<std::size_t>(found - lines.begin()) };
        if (index > 0 && offset < lines[index].begin)
            --index;
        if (upstream && index > 0 && lines[index - 1].wrapped && lines[index - 1].end == offset)
            --index;
        return index;
    }

    rect_f text_layout::caret_rect(const std::size_t offset, const bool upstream) const noexcept
    {
        if (data_ == nullptr || data_->lines.empty())
            return {};
        const auto& row { data_->lines[line_at(offset, upstream)] };
        const auto stops { std::span<const text_layout_stop> { data_->stops }.subspan(row.stop_begin, row.stop_count) };
        return { x_at(stops, std::clamp(offset, row.begin, row.end)), row.top, 0.0f, row.height };
    }

    text_caret_position text_layout::hit_test(const zoom_point point) const noexcept
    {
        if (data_ == nullptr || data_->lines.empty() || point.y < 0.0f)
            return {};
        if (point.y >= data_->size.height)
            return { data_->text.size(), false };
        const auto found { std::upper_bound(data_->lines.begin(), data_->lines.end(), point.y,
            [](const float y, const auto& row) { return y < row.top; })
        };
        const auto& row { found == data_->lines.begin() ? *found : *std::prev(found) };
        const auto stops { std::span<const text_layout_stop> { data_->stops }.subspan(row.stop_begin, row.stop_count) };
        std::size_t offset { row.begin };
        if (row.increasing && stops.empty() == false)
        {
            const auto after { std::upper_bound(stops.begin(), stops.end(), point.x, [](const float x, const auto& stop) { return x < stop.x; }) };
            auto nearest { after == stops.end() ? std::prev(after) : after };
            if (after != stops.begin() && (after == stops.end() || point.x - std::prev(after)->x < after->x - point.x))
                nearest = std::prev(after);
            const auto last_equal { std::upper_bound(nearest, stops.end(), nearest->x, [](const float x, const auto& stop) { return x < stop.x; }) };
            offset = std::prev(last_equal)->offset;
        }
        else
        {
            float distance { std::numeric_limits<float>::max() };
            for (const auto& stop : stops)
                if (const float candidate { std::abs(stop.x - point.x) }; candidate <= distance)
                {
                    distance = candidate;
                    offset = stop.offset;
                }
        }
        return { offset, row.wrapped && offset == row.end };
    }

    std::vector<rect_f> text_layout::selection_rects(std::size_t begin, std::size_t end) const
    {
        std::vector<rect_f> rects {};
        if (data_ == nullptr)
            return rects;
        if (begin > end)
            std::swap(begin, end);
        begin = std::min(begin, data_->text.size());
        end = std::min(end, data_->text.size());
        if (begin == end)
        {
            rects.push_back(caret_rect(begin));
            return rects;
        }
        for (std::size_t index { line_at(begin) }; index < data_->lines.size(); ++index)
        {
            const auto& row { data_->lines[index] };
            if (row.begin >= end)
                break;
            const auto stops { std::span<const text_layout_stop> { data_->stops }.subspan(row.stop_begin, row.stop_count) };
            const auto from { std::max(begin, row.begin) };
            const auto to { std::min(end, row.end) };
            // LTR 문단이 첫 배포 범위다. 후속 bidi 편집 단계에서 시각 구간을 분리한다.
            float left { x_at(stops, from) };
            float right { x_at(stops, to) };
            if (left > right)
                std::swap(left, right);
            // 고른 줄 바꿈의 표시다. 요청이 물리 픽셀이라 글자 크기를 따른다 — 12px에서 예전의 4px이다.
            if (end > row.end && row.wrapped == false)
                right += data_->font_size / 3.0f;
            rects.push_back({ left, row.top, right - left, row.height });
        }
        return rects;
    }

    void normalize_text_layout_request(text_layout_request& request) noexcept
    {
        request.font_size = std::isfinite(request.font_size) ? std::max(0.0f, request.font_size) : 12.0f;
        // 접지 않으면 폭은 기하에 쓰이지 않는다. 남겨 두면 폭만 다른 같은 배치가 cache를 따로 채운다.
        if (request.wrap == false || std::isfinite(request.width) == false || request.width < 0.0f)
            request.width = 0.0f;
        if (std::isfinite(request.line_height) == false || request.line_height < 0.0f)
            request.line_height = 0.0f;
    }

    text_layout build_text_layout(text_layout_request request, const text_layout_backend backend, const text_layout_shaper& shape)
    {
        normalize_text_layout_request(request);
        auto data { std::make_shared<text_layout_data>() };
        data->text = std::move(request.text);
        data->backend = backend;
        data->font_size = request.font_size;
        const std::u8string_view text { data->text };
        std::size_t logical { 0 };
        const auto append = [&](const std::size_t begin, const std::size_t end, const bool wrapped, text_layout_segment segment, const std::size_t visible) {
            const auto index { data->lines.size() };
            const float height { std::max(1.0f, segment.height) };
            auto stops { stops_of(text.substr(begin, end - begin), segment, data->approximate) };
            const bool increasing { std::is_sorted(stops.begin(), stops.end(), [](const auto& a, const auto& b) { return a.x < b.x; }) };
            // 접은 줄 끝에 매단 공백은 caret 자리로 남기되 폭에는 넣지 않는다. 넣으면 줄이 접은 폭을 넘은 것처럼 보인다.
            float width { segment.width };
            if (visible < end && increasing && stops.empty() == false)
                width = std::min(width, x_at(stops, visible - begin));
            data->lines.push_back({ begin, end, logical, wrapped, width, data->size.height, height, segment.baseline, data->stops.size(), stops.size(), increasing });
            data->lines.back().ascent = segment.ascent;
            data->lines.back().descent = segment.descent;
            data->lines.back().ink_bounds = { segment.ink_bounds.x, data->size.height + segment.baseline + segment.ink_bounds.y,
                segment.ink_bounds.width, segment.ink_bounds.height
            };
            for (auto stop : stops)
            {
                stop.offset += begin;
                data->stops.push_back(stop);
            }
            for (auto cluster : segment.clusters)
            {
                cluster.begin += begin;
                cluster.end += begin;
                cluster.line = index;
                data->clusters.push_back(cluster);
            }
            for (auto run : segment.runs)
            {
                run.begin += begin;
                run.end += begin;
                run.line = index;
                data->runs.push_back(run);
            }
            data->drawings.push_back(std::move(segment.drawing));
            data->size.width = std::max(data->size.width, width);
            data->size.height += height;
        };
        for (std::size_t paragraph { 0 };; ++logical)
        {
            const auto newline { text.find_first_of(u8"\r\n", paragraph) };
            const auto end { newline == std::u8string_view::npos ? text.size() : newline };
            const auto content { text.substr(paragraph, end - paragraph) };
            auto full { shape(content, paragraph) };
            if (request.wrap == false || request.width <= 0.0f || full.width <= request.width || content.empty())
                append(paragraph, end, false, std::move(full), end);
            else
            {
                bool approximate { false };
                const auto stops { stops_of(content, full, approximate) };
                const auto boundaries { boundaries_of(content) };
                std::size_t first { 0 };
                while (first + 1 < boundaries.size())
                {
                    std::size_t fit { first + 1 };
                    while (fit + 1 < boundaries.size() && stops[fit + 1].x - stops[first].x <= request.width)
                        ++fit;
                    std::size_t cut { boundaries[fit] };
                    // 자르는 자리는 언제나 글자소 경계다. 공백 뒤 결합 부호는 공백과 한 글자소라
                    // byte 단위로 건너면 그 부호가 어느 줄에도 속하지 않는다.
                    while (cut < content.size() && content[cut] == u8' ')
                        cut = *std::upper_bound(boundaries.begin(), boundaries.end(), cut);
                    if (cut == boundaries[fit] && cut < content.size())
                    {
                        const auto space { content.substr(boundaries[first], cut - boundaries[first]).find_last_of(u8' ') };
                        if (space != std::u8string_view::npos)
                            cut = *std::lower_bound(boundaries.begin(), boundaries.end(), boundaries[first] + space + 1);
                    }
                    auto row { shape(content.substr(boundaries[first], cut - boundaries[first]), paragraph + boundaries[first]) };
                    // 줄 경계 문맥으로 폭이 늘면 다시 줄인다. 글자소 하나는 넘쳐도 보존한다.
                    while (row.width > request.width && cut > boundaries[first + 1] && content[cut - 1] != u8' ')
                    {
                        cut = *std::prev(std::lower_bound(boundaries.begin(), boundaries.end(), cut));
                        row = shape(content.substr(boundaries[first], cut - boundaries[first]), paragraph + boundaries[first]);
                    }
                    std::size_t visible { cut };
                    while (cut < content.size() && visible > boundaries[first] && content[visible - 1] == u8' ')
                        --visible;
                    append(paragraph + boundaries[first], paragraph + cut, cut < content.size(), std::move(row), paragraph + visible);
                    first = static_cast<std::size_t>(std::lower_bound(boundaries.begin(), boundaries.end(), cut) - boundaries.begin());
                }
            }
            if (newline == std::u8string_view::npos)
                break;
            paragraph = newline + 1;
            if (text[newline] == u8'\r' && paragraph < text.size() && text[paragraph] == u8'\n')
                ++paragraph;
        }
        return text_layout_access::make(std::move(data));
    }

    text_layout remake_text_layout(const text_layout& reference, text_layout_request request)
    {
        const auto* const data { text_layout_access::get(reference) };
        if (data == nullptr || data->drawings.empty() || data->drawings.front() == nullptr)
            return {};
        return data->drawings.front()->relayout(std::move(request));
    }

    text_layout make_text_layout(text_layout_request request, const text_measurer& measurer)
    {
        normalize_text_layout_request(request);
        const float size { request.font_size };
        const float height { request.line_height > 0.0f ? request.line_height : std::max(1.0f, size * 1.5f) };
        return build_text_layout(std::move(request), text_layout_backend::measured, [&](const std::u8string_view text, std::size_t) {
            text_layout_segment segment {};
            segment.height = height;
            segment.baseline = (height - size) * 0.5f + size * 0.8f;
            segment.ascent = size * 0.8f;
            segment.descent = size * 0.2f;
            float previous { 0.0f };
            const auto boundaries { text::grapheme_boundaries(text) };
            for (std::size_t index { 0 }; index + 1 < boundaries.size(); ++index)
            {
                const auto at { boundaries[index] };
                const auto next { boundaries[index + 1] };
                const float width { measurer ? measurer(text.substr(0, next), size) : 0.0f };
                segment.clusters.push_back({ at, next, 0, previous, width, false });
                previous = width;
            }
            segment.width = previous;
            return segment;
        });
    }
}
