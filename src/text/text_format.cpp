#include "luil/text/text_format.h"

#include "luil/text/grapheme.h"
#include "text/text_format_internal.h"

#include <algorithm>
#include <utility>

namespace luil::text {
    namespace {
        [[nodiscard]] constexpr text_mark without(const text_mark marks, const text_mark mark) noexcept
        {
            return static_cast<text_mark>(static_cast<std::uint8_t>(marks) & static_cast<std::uint8_t>(~static_cast<std::uint8_t>(mark)));
        }

        // `at` 바이트를 품은 구간이나 그 뒤의 첫 구간이다.
        template<typename ranges_type>
        [[nodiscard]] auto first_reaching(ranges_type& ranges, const std::size_t at) noexcept
        {
            return std::partition_point(ranges.begin(), ranges.end(), [at](const auto& item) { return item.end <= at; });
        }

        void normalize_runs(std::vector<text_mark_run>& runs)
        {
            if (std::ranges::is_sorted(runs, {}, &text_mark_run::begin) == false)
                std::ranges::stable_sort(runs, {}, &text_mark_run::begin);
            std::size_t kept { 0 };
            std::size_t cursor { 0 };
            for (std::size_t index { 0 }; index < runs.size(); ++index)
            {
                text_mark_run run { runs[index] };
                run.begin = std::max(run.begin, cursor);
                if (run.begin >= run.end
                    || run.marks == text_mark::none)
                    continue;
                cursor = run.end;
                if (kept > 0
                    && runs[kept - 1].end == run.begin
                    && runs[kept - 1].marks == run.marks)
                {
                    runs[kept - 1].end = run.end;
                    continue;
                }
                runs[kept] = run;
                ++kept;
            }
            runs.resize(kept);
        }

        void normalize_links(std::vector<text_format_link>& links)
        {
            if (std::ranges::is_sorted(links, {}, &text_format_link::begin) == false)
                std::ranges::stable_sort(links, {}, &text_format_link::begin);
            std::size_t kept { 0 };
            std::size_t cursor { 0 };
            for (std::size_t index { 0 }; index < links.size(); ++index)
            {
                text_format_link& link { links[index] };
                link.begin = std::max(link.begin, cursor);
                if (link.begin >= link.end
                    || link.destination.empty())
                    continue;
                cursor = link.end;
                if (kept > 0
                    && links[kept - 1].end == link.begin
                    && links[kept - 1].destination == link.destination)
                {
                    links[kept - 1].end = link.end;
                    continue;
                }
                if (kept != index)
                    links[kept] = std::move(link);
                ++kept;
            }
            links.resize(kept);
        }

        // [begin, end)의 글자 서식을 `change`로 바꾼다. 구간이 없는 글자는 `none`에서 바꾼다.
        template<typename change_function>
        [[nodiscard]] std::vector<text_mark_run> runs_changed(const std::vector<text_mark_run>& runs, const std::size_t begin, const std::size_t end, change_function&& change)
        {
            std::vector<text_mark_run> result {};
            result.reserve(runs.size() + 3);
            std::size_t cursor { begin };
            for (const text_mark_run& run : runs)
            {
                if (run.end <= begin)
                {
                    result.push_back(run);
                    continue;
                }
                if (run.begin >= end)
                {
                    if (cursor < end)
                    {
                        result.push_back({ cursor, end, change(text_mark::none) });
                        cursor = end;
                    }
                    result.push_back(run);
                    continue;
                }
                if (run.begin < begin)
                    result.push_back({ run.begin, begin, run.marks });
                const std::size_t from { std::max(run.begin, begin) };
                if (from > cursor)
                    result.push_back({ cursor, from, change(text_mark::none) });
                const std::size_t to { std::min(run.end, end) };
                result.push_back({ from, to, change(run.marks) });
                cursor = to;
                if (run.end > end)
                    result.push_back({ end, run.end, run.marks });
            }
            if (cursor < end)
                result.push_back({ cursor, end, change(text_mark::none) });
            return result;
        }

        // 지운 범위를 뺀 자리다. 지운 범위 안의 자리는 그 처음으로 모인다.
        [[nodiscard]] std::size_t after_removal(const std::size_t at, const text_change& change) noexcept
        {
            if (at <= change.offset)
                return at;
            if (at >= change.offset + change.removed)
                return at - change.removed;
            return change.offset;
        }

        // 글자소 경계로 올린 자리다. 경계면 그대로다.
        [[nodiscard]] std::size_t grapheme_ceiling(const std::u8string_view text, const std::size_t at) noexcept
        {
            if (at == 0
                || at >= text.size())
                return std::min(at, text.size());
            const char8_t before { text[at - 1] };
            const char8_t after { text[at] };
            // ASCII 둘 사이는 CR LF만 한 글자소다. 대부분의 경계가 여기서 끝나 줄을 훑지 않는다.
            if (before < 0x80u
                && after < 0x80u
                && (before != u8'\r' || after != u8'\n'))
                return at;
            return grapheme_next(text, grapheme_previous(text, at));
        }

        template<typename range>
        void snap_ranges(std::vector<range>& ranges, const std::u8string_view text, const std::size_t from, const std::size_t to)
        {
            // 끝이 `from`과 같은 구간(경계가 들어온 자리 바로 앞)도 그 끝을 맞춰야 해서 한 바이트 앞부터 찾는다.
            for (auto item { first_reaching(ranges, from > 0 ? from - 1 : 0) }; item != ranges.end() && item->begin <= to; ++item)
            {
                if (item->begin >= from)
                    item->begin = grapheme_ceiling(text, item->begin);
                if (item->end >= from
                    && item->end <= to)
                    item->end = grapheme_ceiling(text, item->end);
            }
        }
    }

    void normalize_text_format(text_format& format)
    {
        normalize_runs(format.runs);
        normalize_links(format.links);
    }

    void snap_text_format(text_format& format, const std::u8string_view text, const std::size_t from, const std::size_t to)
    {
        snap_ranges(format.runs, text, from, to);
        snap_ranges(format.links, text, from, to);
        normalize_text_format(format);
    }

    text_mark text_marks_at(const text_format& format, const std::size_t offset) noexcept
    {
        if (offset == 0)
            return text_mark::none;
        const auto run { first_reaching(format.runs, offset - 1) };
        return run != format.runs.end() && run->begin < offset ? run->marks : text_mark::none;
    }

    bool text_marks_cover(const text_format& format, const std::size_t begin, const std::size_t end, const text_mark mark) noexcept
    {
        if (begin >= end)
            return false;
        std::size_t cursor { begin };
        for (auto run { first_reaching(format.runs, begin) }; run != format.runs.end(); ++run)
        {
            if (run->begin > cursor
                || has_mark(run->marks, mark) == false)
                return false;
            cursor = run->end;
            if (cursor >= end)
                return true;
        }
        return false;
    }

    text_format text_format_with_mark(const text_format& format, const std::size_t begin, const std::size_t end, const text_mark mark, const bool on)
    {
        if (begin >= end)
            return format;
        text_format result { runs_changed(format.runs, begin, end, [mark, on](const text_mark marks) { return on ? marks | mark : without(marks, mark); }), format.links };
        normalize_runs(result.runs);
        return result;
    }

    text_format text_format_with_link(const text_format& format, const std::size_t begin, const std::size_t end, const std::u8string_view destination)
    {
        if (begin >= end)
            return format;
        text_format result { format.runs, {} };
        result.links.reserve(format.links.size() + 2);
        for (const text_format_link& link : format.links)
        {
            if (link.end <= begin
                || link.begin >= end)
            {
                result.links.push_back(link);
                continue;
            }
            if (link.begin < begin)
                result.links.push_back({ link.begin, begin, link.destination });
            if (link.end > end)
                result.links.push_back({ end, link.end, link.destination });
        }
        if (destination.empty() == false)
            result.links.push_back({ begin, end, std::u8string { destination } });
        normalize_links(result.links);
        return result;
    }

    text_format text_format_shifted(const text_format& format, const text_change& change, const text_mark inserted)
    {
        if (change.empty())
            return format;
        const std::size_t at { change.offset };
        const std::size_t length { change.inserted };
        text_format result {};
        result.runs.reserve(format.runs.size() + 2);
        for (const text_mark_run& run : format.runs)
        {
            const std::size_t begin { after_removal(run.begin, change) };
            const std::size_t end { after_removal(run.end, change) };
            if (begin >= end)
                continue;
            if (end <= at)
                result.runs.push_back({ begin, end, run.marks });
            else if (begin >= at)
                result.runs.push_back({ begin + length, end + length, run.marks });
            else
            {
                // 넣은 자리가 구간 한가운데면 둘로 나눈다. 넣은 글은 따로 받은 서식을 갖는다.
                result.runs.push_back({ begin, at, run.marks });
                result.runs.push_back({ at + length, end + length, run.marks });
            }
        }
        if (length > 0)
            result.runs.push_back({ at, at + length, inserted });
        normalize_runs(result.runs);

        result.links.reserve(format.links.size());
        for (const text_format_link& link : format.links)
        {
            const std::size_t begin { after_removal(link.begin, change) };
            const std::size_t end { after_removal(link.end, change) };
            if (begin >= end)
                continue;
            if (end <= at)
                result.links.push_back({ begin, end, link.destination });
            else if (begin >= at)
                result.links.push_back({ begin + length, end + length, link.destination });
            else
                result.links.push_back({ begin, end + length, link.destination });
        }
        normalize_links(result.links);
        return result;
    }

    text_format text_format_slice(const text_format& format, const std::size_t begin, const std::size_t end)
    {
        text_format result {};
        if (begin >= end)
            return result;
        for (auto run { first_reaching(format.runs, begin) }; run != format.runs.end() && run->begin < end; ++run)
            result.runs.push_back({ std::max(run->begin, begin) - begin, std::min(run->end, end) - begin, run->marks });
        for (auto link { first_reaching(format.links, begin) }; link != format.links.end() && link->begin < end; ++link)
            result.links.push_back({ std::max(link->begin, begin) - begin, std::min(link->end, end) - begin, link->destination });
        normalize_text_format(result);
        return result;
    }

    std::size_t text_format_bytes(const text_format& format) noexcept
    {
        std::size_t bytes { sizeof(text_format) + format.runs.size() * sizeof(text_mark_run) };
        for (const text_format_link& link : format.links)
            bytes += sizeof(text_format_link) + link.destination.size();
        return bytes;
    }
}
