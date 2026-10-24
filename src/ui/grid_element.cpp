#include "luil/ui/grid_element.h"

#include "ui/flexible_lengths.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace luil {
    namespace {
        [[nodiscard]] std::size_t span_of(const std::size_t span) noexcept
        {
            return span > 0u ? span : 1u;
        }

        // 놓인 자식까지 덮는 줄 수만큼 규칙을 편다. 모자란 줄은 맞춤이다.
        [[nodiscard]] std::vector<grid_track> tracks_for(const std::vector<grid_track>& declared, const std::size_t used)
        {
            std::vector<grid_track> tracks { declared };
            if (tracks.size() < used)
                tracks.resize(used, grid_track::fit());
            return tracks;
        }

        // 줄 길이를 정한다. 고정은 그 값, 맞춤은 잰 값(하한 이상), 유연은 남는 자리를 비율로 나눈 값(하한 이상)이다.
        //  - `available`이 무한대면 남는 자리가 없다. 유연 줄도 잰 값이다 (원하는 크기를 물을 때).
        [[nodiscard]] std::vector<float> resolve_tracks(const std::vector<grid_track>& tracks, const std::vector<float>& wanted, const float available, const float spacing)
        {
            std::vector<float> lengths(tracks.size(), 0.0f);
            float fixed { tracks.empty() ? 0.0f : spacing * static_cast<float>(tracks.size() - 1u) };
            float weights { 0.0f };
            const bool bounded { std::isfinite(available) };
            for (std::size_t index = 0; index < tracks.size(); ++index)
            {
                const grid_track& track { tracks[index] };
                if (track.kind == grid_track_kind::fixed)
                    lengths[index] = track.length;
                else if (track.kind == grid_track_kind::fit || bounded == false)
                    lengths[index] = std::max(track.minimum, wanted[index]);
                else
                {
                    weights += track.weight > 0.0f ? track.weight : 1.0f;
                    continue;
                }
                fixed += lengths[index];
            }
            if (bounded == false || weights <= 0.0f)
                return lengths;
            // 유연 줄은 stack의 유연 항목과 같은 규칙으로 나눈다. 하한에 걸린 줄의 부족분은 다른 유연 줄의 몫에서 나오고,
            // 하한들의 합이 남은 자리를 넘을 때만 넘친다 (`distribute_flexible`).
            const float remaining { std::max(0.0f, available - fixed) };
            std::vector<flexible_share> shares {};
            std::vector<std::size_t> owners {};
            for (std::size_t index = 0; index < tracks.size(); ++index)
            {
                const grid_track& track { tracks[index] };
                if (track.kind != grid_track_kind::flexible)
                    continue;
                shares.push_back({ .weight = track.weight > 0.0f ? track.weight : 1.0f, .minimum = track.minimum });
                owners.push_back(index);
            }
            const std::vector<float> resolved { distribute_flexible(shares, remaining) };
            for (std::size_t share = 0; share < owners.size(); ++share)
                lengths[owners[share]] = resolved[share];
            return lengths;
        }

        // `first`부터 `span`줄이 차지하는 길이다 (사이 간격 포함).
        [[nodiscard]] float spanned(const std::vector<float>& lengths, const std::size_t first, const std::size_t span, const float spacing) noexcept
        {
            float total { 0.0f };
            for (std::size_t index = first; index < first + span && index < lengths.size(); ++index)
                total += lengths[index];
            return total + spacing * static_cast<float>(span - 1u);
        }

        [[nodiscard]] float sum_with_spacing(const std::vector<float>& lengths, const float spacing) noexcept
        {
            return lengths.empty() ? 0.0f : spanned(lengths, 0u, lengths.size(), spacing);
        }
    } // namespace

    grid_element::grid_element(ui_element_id id, grid_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
    {}

    void grid_element::add(std::unique_ptr<ui_element> child, const grid_cell cell)
    {
        if (child == nullptr)
            return;
        entries_.push_back({ child.get(), cell });
        add_child(std::move(child));
    }

    std::vector<float> grid_element::measure_columns(const measure_context& context, const float inner_width) const
    {
        std::size_t used { config_.columns.size() };
        for (const entry& current : entries_)
            used = std::max(used, current.cell.column + span_of(current.cell.column_span));
        const std::vector<grid_track> tracks { tracks_for(config_.columns, used) };
        // 열의 맞춤 폭은 그 열 하나에 놓인 자식이 제한 없는 폭에서 원하는 폭이다.
        std::vector<float> wanted(tracks.size(), 0.0f);
        for (const entry& current : entries_)
        {
            if (span_of(current.cell.column_span) != 1u)
                continue;
            if (const std::optional<size_f> size { current.child->preferred_size(context, std::numeric_limits<float>::infinity()) }; size.has_value())
                wanted[current.cell.column] = std::max(wanted[current.cell.column], size->width);
        }
        return resolve_tracks(tracks, wanted, inner_width, config_.column_spacing);
    }

    std::vector<float> grid_element::measure_rows(const measure_context& context, const std::vector<float>& columns, const float inner_height) const
    {
        std::size_t used { config_.rows.size() };
        for (const entry& current : entries_)
            used = std::max(used, current.cell.row + span_of(current.cell.row_span));
        const std::vector<grid_track> tracks { tracks_for(config_.rows, used) };
        // 행의 맞춤 높이는 그 행 하나에 놓인 자식이 **자기 칸 폭에서** 원하는 높이다.
        std::vector<float> wanted(tracks.size(), 0.0f);
        for (const entry& current : entries_)
        {
            if (span_of(current.cell.row_span) != 1u)
                continue;
            const float width { spanned(columns, current.cell.column, span_of(current.cell.column_span), config_.column_spacing) };
            if (const std::optional<size_f> size { current.child->preferred_size(context, width) }; size.has_value())
                wanted[current.cell.row] = std::max(wanted[current.cell.row], size->height);
        }
        return resolve_tracks(tracks, wanted, inner_height, config_.row_spacing);
    }

    void grid_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const measure_context measure { context.measure() };
        const float inner_width { context.slot.width / scale - config_.padding.left - config_.padding.right };
        const float inner_height { context.slot.height / scale - config_.padding.top - config_.padding.bottom };
        column_widths_ = measure_columns(measure, std::max(0.0f, inner_width));
        row_heights_ = measure_rows(measure, column_widths_, std::max(0.0f, inner_height));

        // 줄의 시작점이다 (논리 픽셀, 안쪽 영역 기준).
        const auto starts = [](const std::vector<float>& lengths, const float spacing) {
            std::vector<float> positions(lengths.size(), 0.0f);
            float position { 0.0f };
            for (std::size_t index = 0; index < lengths.size(); ++index)
            {
                positions[index] = position;
                position += lengths[index] + spacing;
            }
            return positions;
        };
        const std::vector<float> column_starts { starts(column_widths_, config_.column_spacing) };
        const std::vector<float> row_starts { starts(row_heights_, config_.row_spacing) };

        for (const entry& current : entries_)
        {
            const grid_cell& cell { current.cell };
            const float cell_width { spanned(column_widths_, cell.column, span_of(cell.column_span), config_.column_spacing) };
            const float cell_height { spanned(row_heights_, cell.row, span_of(cell.row_span), config_.row_spacing) };
            float width { cell_width };
            float height { cell_height };
            // 칸을 채우지 않는 자식은 원하는 크기에 서고 칸보다 크지 않다.
            if (cell.horizontal != layout_alignment::stretch || cell.vertical != layout_alignment::stretch)
            {
                if (const std::optional<size_f> size { current.child->preferred_size(measure, cell_width) }; size.has_value())
                {
                    if (cell.horizontal != layout_alignment::stretch)
                        width = std::min(cell_width, size->width);
                    if (cell.vertical != layout_alignment::stretch)
                        height = std::min(cell_height, size->height);
                }
            }
            const float x { align_within(config_.padding.left + column_starts[cell.column], cell_width, width, cell.horizontal) };
            const float y { align_within(config_.padding.top + row_starts[cell.row], cell_height, height, cell.vertical) };
            current.child->arrange(context.for_child({ context.slot.x + x * scale, context.slot.y + y * scale, width * scale, height * scale }));
        }
    }

    void grid_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }

    std::optional<size_f> grid_element::preferred_size(const measure_context& context, const float available_width) const
    {
        if (entries_.empty())
            return std::nullopt;
        // 폭이 주어져도 유연 열은 남는 자리를 나누지 않고 잰 폭이다 — 원하는 크기는 "얼마면 다 들어가는가"다.
        static_cast<void>(available_width);
        const std::vector<float> columns { measure_columns(context, std::numeric_limits<float>::infinity()) };
        const std::vector<float> rows { measure_rows(context, columns, std::numeric_limits<float>::infinity()) };
        return size_f {
            sum_with_spacing(columns, config_.column_spacing) + config_.padding.left + config_.padding.right,
            sum_with_spacing(rows, config_.row_spacing) + config_.padding.top + config_.padding.bottom,
        };
    }

    const std::vector<float>& grid_element::column_widths() const noexcept
    {
        return column_widths_;
    }

    const std::vector<float>& grid_element::row_heights() const noexcept
    {
        return row_heights_;
    }
} // namespace luil
