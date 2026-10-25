#include "luil/text/case_fold.h"

#include "luil/text/utf8_text.h"

#include <array>
#include <cstdint>

namespace luil::text {
    namespace {
        // 짝이 이웃한 범위다: `first`..`last` 안에서 `upper_parity`와 짝수·홀수가 같은 코드포인트가 대문자이고
        // 바로 다음 것이 소문자다.
        struct paired_range
        {
            char32_t first { 0 };
            char32_t last { 0 };
            bool upper_odd { false };
        };

        // 대문자에 일정한 값을 더하면 소문자인 범위다.
        struct offset_range
        {
            char32_t first { 0 };
            char32_t last { 0 };
            std::int32_t offset { 0 };
        };

        // 한 글자만의 접기다.
        struct single_fold
        {
            char32_t from { 0 };
            char32_t to { 0 };
        };

        constexpr std::array offset_ranges {
            offset_range { U'A', U'Z', 0x20 },
            offset_range { 0x00C0, 0x00D6, 0x20 },
            offset_range { 0x00D8, 0x00DE, 0x20 },
            offset_range { 0x0388, 0x038A, 0x25 },
            offset_range { 0x038E, 0x038F, 0x3F },
            offset_range { 0x0391, 0x03A1, 0x20 },
            offset_range { 0x03A3, 0x03AB, 0x20 },
            offset_range { 0x0400, 0x040F, 0x50 },
            offset_range { 0x0410, 0x042F, 0x20 },
            offset_range { 0x0531, 0x0556, 0x30 },
            offset_range { 0x2160, 0x216F, 0x10 },
            offset_range { 0x24B6, 0x24CF, 0x1A },
            offset_range { 0xFF21, 0xFF3A, 0x20 },
        };

        constexpr std::array paired_ranges {
            // 라틴 확장 A다. U+0130(İ)·U+0131(ı)·U+0138(ĸ)·U+0149(ŉ)은 단순 접기가 없어 빈다.
            paired_range { 0x0100, 0x012F, false },
            paired_range { 0x0132, 0x0137, false },
            paired_range { 0x0139, 0x0148, true },
            paired_range { 0x014A, 0x0177, false },
            paired_range { 0x0179, 0x017E, true },
            // 라틴 확장 B의 짝 범위다.
            paired_range { 0x01CD, 0x01DC, true },
            paired_range { 0x01DE, 0x01EF, false },
            paired_range { 0x01F8, 0x021F, false },
            paired_range { 0x0222, 0x0233, false },
            paired_range { 0x0246, 0x024F, false },
            // 그리스 고대 문자·콥트 문자다.
            paired_range { 0x03D8, 0x03EF, false },
            // 키릴 확장이다.
            paired_range { 0x0460, 0x0481, false },
            paired_range { 0x048A, 0x04BF, false },
            paired_range { 0x04C1, 0x04CE, true },
            paired_range { 0x04D0, 0x052F, false },
            // 라틴 추가 확장이다.
            paired_range { 0x1E00, 0x1E95, false },
            paired_range { 0x1EA0, 0x1EFF, false },
        };

        constexpr std::array single_folds {
            single_fold { 0x00B5, 0x03BC },
            single_fold { 0x0178, 0x00FF },
            single_fold { 0x017F, U's' },
            single_fold { 0x01C4, 0x01C6 },
            single_fold { 0x01C5, 0x01C6 },
            single_fold { 0x01C7, 0x01C9 },
            single_fold { 0x01C8, 0x01C9 },
            single_fold { 0x01CA, 0x01CC },
            single_fold { 0x01CB, 0x01CC },
            single_fold { 0x01F1, 0x01F3 },
            single_fold { 0x01F2, 0x01F3 },
            single_fold { 0x01F4, 0x01F5 },
            single_fold { 0x0386, 0x03AC },
            single_fold { 0x038C, 0x03CC },
            single_fold { 0x03C2, 0x03C3 },
            single_fold { 0x03CF, 0x03D7 },
            single_fold { 0x03D0, 0x03B2 },
            single_fold { 0x03D1, 0x03B8 },
            single_fold { 0x03D5, 0x03C6 },
            single_fold { 0x03D6, 0x03C0 },
            single_fold { 0x03F0, 0x03BA },
            single_fold { 0x03F1, 0x03C1 },
            single_fold { 0x03F5, 0x03B5 },
            single_fold { 0x04C0, 0x04CF },
            single_fold { 0x1E9E, 0x00DF },
            single_fold { 0x2126, 0x03C9 },
            single_fold { 0x212A, U'k' },
            single_fold { 0x212B, 0x00E5 },
        };

        // 원문 `text`의 `offset` 자리 코드포인트를 접은 값과 원문 길이다. 잘못된 바이트는 그 바이트 값 그대로다 —
        // 잘못된 바이트끼리만 같고 어떤 글자와도 같지 않게, 서로게이트 대역 밖의 사적 값으로 옮긴다.
        struct folded_codepoint
        {
            char32_t value { 0 };
            std::size_t length { 1 };
        };

        [[nodiscard]] folded_codepoint fold_at(const std::u8string_view text, const std::size_t offset) noexcept
        {
            const utf8_decoded decoded { utf8_decode(text, offset) };
            const bool real_replacement { decoded.length == 3 };
            if (decoded.codepoint == utf8_replacement_character && real_replacement == false)
                return folded_codepoint { 0x110000u + static_cast<char32_t>(text[offset]), 1 };
            return folded_codepoint { fold_case(decoded.codepoint), decoded.length };
        }

        // `text`의 `offset`에서 `needle` 전체가 접어서 같으면 원문 끝 자리다.
        [[nodiscard]] std::optional<std::size_t> match_at(const std::u8string_view text, std::size_t offset, const std::u8string_view needle) noexcept
        {
            std::size_t at { 0 };
            while (at < needle.size())
            {
                if (offset >= text.size())
                    return std::nullopt;
                const folded_codepoint wanted { fold_at(needle, at) };
                const folded_codepoint got { fold_at(text, offset) };
                if (wanted.value != got.value)
                    return std::nullopt;
                at += wanted.length;
                offset += got.length;
            }
            return offset;
        }
    } // namespace

    char32_t fold_case(const char32_t codepoint) noexcept
    {
        // ASCII 소문자·숫자·기호가 가장 흔하다. 표를 훑지 않고 돌아간다.
        if (codepoint < U'A')
            return codepoint;
        for (const offset_range& range : offset_ranges)
            if (codepoint >= range.first && codepoint <= range.last)
                return static_cast<char32_t>(static_cast<std::int32_t>(codepoint) + range.offset);
        if (codepoint < 0x00B5)
            return codepoint;
        for (const paired_range& range : paired_ranges)
            if (codepoint >= range.first && codepoint <= range.last)
                return ((codepoint & 1u) != 0) == range.upper_odd ? codepoint + 1 : codepoint;
        for (const single_fold& fold : single_folds)
            if (fold.from == codepoint)
                return fold.to;
        return codepoint;
    }

    std::u8string fold_case(const std::u8string_view text)
    {
        std::u8string result {};
        result.reserve(text.size());
        for (std::size_t offset { 0 }; offset < text.size();)
        {
            const utf8_decoded decoded { utf8_decode(text, offset) };
            if (decoded.codepoint == utf8_replacement_character && decoded.length != 3)
                result.push_back(text[offset]);
            else
                result += utf8_encode(fold_case(decoded.codepoint));
            offset += decoded.length;
        }
        return result;
    }

    bool starts_with_folded(const std::u8string_view text, const std::u8string_view prefix) noexcept
    {
        return match_at(text, 0, prefix).has_value();
    }

    std::optional<folded_match> find_folded(const std::u8string_view text, const std::u8string_view needle, const bool backward) noexcept
    {
        if (needle.empty())
            return backward ? folded_match { text.size(), text.size() } : folded_match { 0, 0 };
        std::optional<folded_match> found {};
        for (std::size_t offset { 0 }; offset < text.size(); offset += utf8_decode(text, offset).length)
            if (const std::optional<std::size_t> end { match_at(text, offset, needle) }; end.has_value())
            {
                found = folded_match { offset, *end };
                if (backward == false)
                    return found;
            }
        return found;
    }
} // namespace luil::text
