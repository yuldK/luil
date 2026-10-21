#include "luil/text/grapheme.h"

#include "luil/text/utf8_text.h"
#include "text/grapheme_internal.h"

#include <algorithm>
#include <iterator>

namespace luil::text {
    namespace {
        enum class grapheme_property { other, cr, lf, control, extend, zwj, regional_indicator, prepend, spacing_mark, l, v, t, lv, lvt };
        struct grapheme_range { char32_t begin; char32_t end; grapheme_property property; };
        struct codepoint_range { char32_t begin; char32_t end; };

#include "text/grapheme_data.h"

        template<typename range, std::size_t size>
        const range* find_range(const range (&ranges)[size], const char32_t value) noexcept
        {
            const auto found { std::lower_bound(std::begin(ranges), std::end(ranges), value,
                [](const range& item, const char32_t codepoint) { return item.end < codepoint; })
            };
            return found != std::end(ranges) && found->begin <= value ? found : nullptr;
        }

        grapheme_property property(const char32_t value) noexcept
        {
            if (value >= 0xAC00 && value <= 0xD7A3)
                return (value - 0xAC00) % 28 == 0 ? grapheme_property::lv : grapheme_property::lvt;
            const auto* const found { find_range(grapheme_ranges, value) };
            return found != nullptr ? found->property : grapheme_property::other;
        }

        std::size_t codepoint_start(const std::u8string_view text, std::size_t offset) noexcept
        {
            offset = std::min(offset, text.size());
            while (offset > 0 && offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0u) == 0x80u)
                --offset;
            return offset;
        }

        std::size_t before(const std::u8string_view text, const std::size_t offset) noexcept
        {
            return offset > 0 ? codepoint_start(text, offset - 1) : 0;
        }

        bool is_control(const grapheme_property value) noexcept
        {
            return value == grapheme_property::cr || value == grapheme_property::lf || value == grapheme_property::control;
        }

        bool boundary_at(const std::u8string_view text, const std::size_t position) noexcept
        {
            if (position == 0 || position >= text.size())
                return true;
            const std::size_t left { before(text, position) };
            const auto previous { property(utf8_decode(text, left).codepoint) };
            const auto current { property(utf8_decode(text, position).codepoint) };
            using enum grapheme_property;
            if (previous == cr && current == lf)
                return false;
            if (is_control(previous) || is_control(current))
                return true;
            if (previous == l && (current == l || current == v || current == lv || current == lvt))
                return false;
            if ((previous == lv || previous == v) && (current == v || current == t))
                return false;
            if ((previous == lvt || previous == t) && current == t)
                return false;
            if (current == extend || current == zwj || current == spacing_mark || previous == prepend)
                return false;
            if (previous == zwj && find_range(pictographic_ranges, utf8_decode(text, position).codepoint) != nullptr)
            {
                std::size_t cursor { left };
                while (cursor > 0)
                {
                    cursor = before(text, cursor);
                    const char32_t codepoint { utf8_decode(text, cursor).codepoint };
                    if (property(codepoint) == extend)
                        continue;
                    if (find_range(pictographic_ranges, codepoint) != nullptr)
                        return false;
                    break;
                }
            }
            if (previous == regional_indicator && current == regional_indicator)
            {
                std::size_t count { 0 };
                std::size_t cursor { position };
                while (cursor > 0)
                {
                    cursor = before(text, cursor);
                    if (property(utf8_decode(text, cursor).codepoint) != regional_indicator)
                        break;
                    ++count;
                }
                return count % 2 == 0;
            }
            return true;
        }
    } // namespace

    std::size_t grapheme_previous(const std::u8string_view text, std::size_t offset) noexcept
    {
        offset = std::min(offset, text.size());
#if defined(LUIL_GRAPHEME_SKIA)
        if (const auto boundary { platform_grapheme_boundary(text, offset, false) }; boundary.has_value())
            return *boundary;
#endif
        if (offset == 0)
            return 0;
        offset = before(text, offset);
        while (offset > 0 && boundary_at(text, offset) == false)
            offset = before(text, offset);
        return offset;
    }

    std::size_t grapheme_next(const std::u8string_view text, std::size_t offset) noexcept
    {
        offset = std::min(offset, text.size());
#if defined(LUIL_GRAPHEME_SKIA)
        if (const auto boundary { platform_grapheme_boundary(text, offset, true) }; boundary.has_value())
            return *boundary;
#endif
        offset = codepoint_start(text, offset);
        while (offset < text.size())
        {
            offset = std::min(text.size(), offset + utf8_decode(text, offset).length);
            if (boundary_at(text, offset))
                return offset;
        }
        return text.size();
    }

    std::size_t grapheme_backspace_offset(const std::u8string_view text, const std::size_t offset) noexcept
    {
        const std::size_t end { std::min(offset, text.size()) };
        const std::size_t begin { grapheme_previous(text, end) };
        if (end == 0)
            return 0;
        const std::size_t last { before(text, end) };
        if (find_range(combining_ranges, utf8_decode(text, last).codepoint) == nullptr)
            return begin;
        // 이모지와 한글 자모 시퀀스는 끝이 결합 부호여도 통째로 지운다.
        for (std::size_t cursor { begin }; cursor < end;)
        {
            const auto value { utf8_decode(text, cursor) };
            const auto kind { property(value.codepoint) };
            if (value.codepoint == 0x200D || value.codepoint == 0x20E3
                || (value.codepoint >= 0xFE00 && value.codepoint <= 0xFE0F)
                || (value.codepoint >= 0xE0100 && value.codepoint <= 0xE01EF)
                || (value.codepoint >= 0x1F3FB && value.codepoint <= 0x1F3FF)
                || find_range(pictographic_ranges, value.codepoint) != nullptr
                || kind == grapheme_property::regional_indicator || kind == grapheme_property::l
                || kind == grapheme_property::v || kind == grapheme_property::t)
                return begin;
            cursor += value.length;
        }
        return last;
    }
} // namespace luil::text
