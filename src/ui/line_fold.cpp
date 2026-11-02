#include "ui/line_fold.h"

#include "luil/text/utf8_text.h"

#include <algorithm>
#include <iterator>

namespace luil {
    namespace {
        // `at`에서 시작하는 글자소가 매달리는 공백 하나뿐인가. 공백 뒤 결합 부호는 그 공백과 한 글자소이고 보이는
        // 글자라 매달지 않는다.
        bool hangs_at(const std::u8string_view content, const std::span<const std::size_t> boundaries, const std::size_t at) noexcept
        {
            if (at >= content.size())
                return false;
            const auto next { std::upper_bound(boundaries.begin(), boundaries.end(), at) };
            const std::size_t end { next != boundaries.end() ? *next : content.size() };
            const auto decoded { text::utf8_decode(content, at) };
            return decoded.length == end - at && text::line_break_hangs(decoded.codepoint);
        }

        bool on_boundary(const std::span<const std::size_t> boundaries, const std::size_t at) noexcept
        {
            return std::binary_search(boundaries.begin(), boundaries.end(), at);
        }

        bool is_opportunity(const std::span<const text::line_break_opportunity> opportunities, const std::size_t at) noexcept
        {
            const auto found { std::lower_bound(opportunities.begin(), opportunities.end(), at,
                [](const text::line_break_opportunity& item, const std::size_t offset) { return item.offset < offset; })
            };
            return found != opportunities.end() && found->offset == at;
        }

        // `start` 뒤, `limit` 이하(`inclusive`가 거짓이면 미만)의 마지막 후보 가운데 글자소 경계인 것이다. 없으면 0이다.
        std::size_t last_opportunity(
            const std::span<const std::size_t> boundaries, const std::span<const text::line_break_opportunity> opportunities, const std::size_t start, const std::size_t limit, const bool inclusive) noexcept
        {
            auto at { inclusive
                ? std::upper_bound(opportunities.begin(), opportunities.end(), limit, [](const std::size_t offset, const text::line_break_opportunity& item) { return offset < item.offset; })
                : std::lower_bound(opportunities.begin(), opportunities.end(), limit, [](const text::line_break_opportunity& item, const std::size_t offset) { return item.offset < offset; })
            };
            while (at != opportunities.begin())
            {
                --at;
                if (at->offset <= start)
                    break;
                if (on_boundary(boundaries, at->offset))
                    return at->offset;
            }
            return 0;
        }

        line_fold with_visible(const std::u8string_view content, const std::span<const std::size_t> boundaries, const std::size_t start, const std::size_t cut) noexcept
        {
            const auto previous_of = [boundaries](const std::size_t at) { return *std::prev(std::lower_bound(boundaries.begin(), boundaries.end(), at)); };
            const bool hanging { cut > start && hangs_at(content, boundaries, previous_of(cut)) };
            // 접지 않은 마지막 줄의 공백은 입력 칸의 caret 자리라 폭에 남긴다.
            std::size_t visible { cut };
            while (cut < content.size() && visible > start && hangs_at(content, boundaries, previous_of(visible)))
                visible = previous_of(visible);
            return { cut, visible, hanging };
        }
    }

    line_fold choose_line_fold(
        const std::u8string_view content, const std::span<const std::size_t> boundaries, const std::span<const text::line_break_opportunity> opportunities, const std::size_t start, const std::size_t fit) noexcept
    {
        std::size_t cut { fit };
        while (hangs_at(content, boundaries, cut))
            cut = *std::upper_bound(boundaries.begin(), boundaries.end(), cut);
        if (cut >= content.size() || is_opportunity(opportunities, cut))
            return with_visible(content, boundaries, start, cut);
        if (const std::size_t found { last_opportunity(boundaries, opportunities, start, fit, true) }; found != 0)
            return with_visible(content, boundaries, start, found);
        return with_visible(content, boundaries, start, fit);
    }

    line_fold previous_line_fold(
        const std::u8string_view content, const std::span<const std::size_t> boundaries, const std::span<const text::line_break_opportunity> opportunities, const std::size_t start, const std::size_t cut) noexcept
    {
        if (const std::size_t found { last_opportunity(boundaries, opportunities, start, cut, false) }; found != 0)
            return with_visible(content, boundaries, start, found);
        const std::size_t previous { *std::prev(std::lower_bound(boundaries.begin(), boundaries.end(), cut)) };
        return with_visible(content, boundaries, start, std::max(previous, start));
    }
}
