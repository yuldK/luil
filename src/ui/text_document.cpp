#include "luil/ui/text_document.h"

#include <algorithm>

namespace luil {
    namespace {
        template<typename range>
        std::span<const range> visible_ranges(const std::span<const range> spans, const std::size_t begin, const std::size_t end) noexcept
        {
            if (begin >= end)
                return {};
            const auto first { std::lower_bound(spans.begin(), spans.end(), begin, [](const range& item, const auto offset) { return item.end <= offset; }) };
            const auto last { std::lower_bound(first, spans.end(), end, [](const range& item, const auto offset) { return item.begin < offset; }) };
            return { first, last };
        }
    } // namespace

    std::span<const text_span> text_spans_in_range(const std::span<const text_span> spans, const std::size_t begin, const std::size_t end) noexcept
    {
        return visible_ranges(spans, begin, end);
    }

    std::span<const text_highlight> text_highlights_in_range(const std::span<const text_highlight> spans, const std::size_t begin, const std::size_t end) noexcept
    {
        return visible_ranges(spans, begin, end);
    }
} // namespace luil
