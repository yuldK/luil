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
        // Indic_Conjunct_Break다 (UAX #29 GB9c). 표에 없으면 none이다.
        enum class incb_property { none, consonant, extend, linker };
        struct incb_range { char32_t begin; char32_t end; incb_property property; };

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

        incb_property incb(const char32_t value) noexcept
        {
            const auto* const found { find_range(incb_ranges, value) };
            return found != nullptr ? found->property : incb_property::none;
        }

        // GB9c: 자음 + (Extend·Linker)* 안에 Linker가 하나라도 있으면 다음 자음과 붙는다 ("क्ष"가 한 글자소다).
        //  - 거꾸로 걷는 길이는 그 결합 자리 안이다. 결합 부호가 끝없이 이어지지 않는 한 짧다.
        bool joins_conjunct(const std::u8string_view text, const std::size_t position) noexcept
        {
            if (incb(utf8_decode(text, position).codepoint) != incb_property::consonant)
                return false;
            bool linked { false };
            std::size_t cursor { position };
            while (cursor > 0)
            {
                cursor = before(text, cursor);
                const incb_property value { incb(utf8_decode(text, cursor).codepoint) };
                if (value == incb_property::linker)
                    linked = true;
                else if (value == incb_property::consonant)
                    return linked;
                else if (value != incb_property::extend)
                    return false;
            }
            return false;
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
            if (joins_conjunct(text, position))
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
    }

    std::vector<std::size_t> grapheme_boundaries(const std::u8string_view text)
    {
#if defined(LUIL_GRAPHEME_SKIA)
        if (auto boundaries { platform_grapheme_boundaries(text) }; boundaries.has_value())
            return std::move(*boundaries);
#endif
        return builtin_grapheme_boundaries(text);
    }

    std::vector<std::size_t> builtin_grapheme_boundaries(const std::u8string_view text)
    {
        std::vector<std::size_t> boundaries { 0 };
        std::size_t regional_count { 0 };
        for (std::size_t at { 0 }; at < text.size();)
        {
            const auto decoded { utf8_decode(text, at) };
            regional_count = property(decoded.codepoint) == grapheme_property::regional_indicator ? regional_count + 1 : 0;
            at += decoded.length;
            const bool regional_pair { at < text.size() && regional_count > 0 && property(utf8_decode(text, at).codepoint) == grapheme_property::regional_indicator };
            if (at == text.size() || (regional_pair ? regional_count % 2 == 0 : boundary_at(text, at)))
                boundaries.push_back(at);
        }
        return boundaries;
    }

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
}
