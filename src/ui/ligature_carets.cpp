#include "ui/ligature_carets.h"

#include <optional>

namespace luil {
    namespace {
        std::optional<std::uint16_t> word(const std::span<const std::uint8_t> table, const std::size_t at)
        {
            if (at > table.size() || table.size() - at < 2)
                return std::nullopt;
            return static_cast<std::uint16_t>((static_cast<unsigned>(table[at]) << 8) | table[at + 1]);
        }

        std::optional<std::size_t> coverage_index(const std::span<const std::uint8_t> table, const std::size_t at, const std::uint16_t glyph)
        {
            const auto format { word(table, at) };
            const auto count { word(table, at + 2) };
            if (count.has_value() == false)
                return std::nullopt;
            for (std::size_t index { 0 }; index < *count; ++index)
            {
                if (format == 1)
                {
                    const auto covered { word(table, at + 4 + index * 2) };
                    if (covered == glyph)
                        return index;
                    if (covered.has_value() == false)
                        return std::nullopt;
                }
                else if (format == 2)
                {
                    const auto first { word(table, at + 4 + index * 6) };
                    const auto last { word(table, at + 6 + index * 6) };
                    const auto start { word(table, at + 8 + index * 6) };
                    if (first.has_value() == false || last.has_value() == false || start.has_value() == false)
                        return std::nullopt;
                    if (glyph >= *first && glyph <= *last)
                        return *start + static_cast<std::size_t>(glyph - *first);
                }
                else
                    return std::nullopt;
            }
            return std::nullopt;
        }
    }

    std::vector<std::int16_t> ligature_caret_coordinates(const std::span<const std::uint8_t> table, const std::uint16_t glyph)
    {
        // GDEF 1.0·1.2·1.3은 LigCaretList의 위치가 같다.
        if (word(table, 0) != 1)
            return {};
        const auto offset { word(table, 8) };
        if (offset.has_value() == false || *offset == 0)
            return {};
        const std::size_t list { *offset };
        const auto coverage { word(table, list) };
        const auto count { word(table, list + 2) };
        if (coverage.has_value() == false || *coverage == 0 || count.has_value() == false)
            return {};
        const auto index { coverage_index(table, list + *coverage, glyph) };
        if (index.has_value() == false || *index >= *count)
            return {};
        const auto glyph_offset { word(table, list + 4 + *index * 2) };
        if (glyph_offset.has_value() == false || *glyph_offset == 0)
            return {};
        const std::size_t ligature { list + *glyph_offset };
        const auto carets { word(table, ligature) };
        if (carets.has_value() == false || *carets > table.size() / 2)
            return {};
        std::vector<std::int16_t> result {};
        for (std::size_t caret { 0 }; caret < *carets; ++caret)
        {
            const auto caret_offset { word(table, ligature + 2 + caret * 2) };
            if (caret_offset.has_value() == false || *caret_offset == 0)
                return {};
            const std::size_t value { ligature + *caret_offset };
            const auto format { word(table, value) };
            const auto coordinate { word(table, value + 2) };
            // Format 2는 contour 점 번호다. Format 3의 device/variation 보정은 좌표로 오독하지 않는다.
            if ((format != 1 && (format != 3 || word(table, value + 4) != 0)) || coordinate.has_value() == false)
                return {};
            const int signed_value { *coordinate < 0x8000 ? static_cast<int>(*coordinate) : static_cast<int>(*coordinate) - 0x10000 };
            result.push_back(static_cast<std::int16_t>(signed_value));
        }
        return result;
    }
}
