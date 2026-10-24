#include "text/grapheme_internal.h"

#include "modules/skshaper/utils/FactoryHelpers.h"
#include "modules/skunicode/include/SkUnicode.h"

#include <algorithm>
#include <limits>

namespace luil::text {
    std::optional<std::vector<std::size_t>> platform_grapheme_boundaries(const std::u8string_view text)
    {
        if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return std::nullopt;
        static const sk_sp<SkUnicode> unicode { SkShapers::BestAvailableUnicode() };
        if (unicode == nullptr)
            return std::nullopt;
        auto iterator { unicode->makeBreakIterator(SkUnicode::BreakType::kGraphemes) };
        if (iterator == nullptr || iterator->setText(reinterpret_cast<const char*>(text.data()), static_cast<int>(text.size())) == false)
            return std::nullopt;
        std::vector<std::size_t> boundaries { 0 };
        for (int position { iterator->first() }; iterator->isDone() == false; position = iterator->next())
            if (position > 0)
                boundaries.push_back(static_cast<std::size_t>(position));
        if (boundaries.back() != text.size())
            boundaries.push_back(text.size());
        return boundaries;
    }

    std::optional<std::size_t> platform_grapheme_boundary(const std::u8string_view text, std::size_t offset, const bool forward) noexcept
    {
        offset = std::min(offset, text.size());
        // 글자소는 CR·LF를 넘지 않는다 (GB4·GB5, CRLF는 한 덩어리). 그 둘레는 바로 답하고, 나머지는 offset을 감싼
        // 줄 하나만 iterator에 건다 — 큰 문서에서 키 하나가 문서 처음부터 걷지 않는다.
        const auto is_break = [](const char8_t value) { return value == u8'\r' || value == u8'\n'; };
        if (forward)
        {
            if (offset >= text.size())
                return text.size();
            if (text[offset] == u8'\r')
                return offset + 1 < text.size() && text[offset + 1] == u8'\n' ? offset + 2 : offset + 1;
            if (text[offset] == u8'\n')
                return offset + 1;
        }
        else
        {
            if (offset == 0)
                return std::size_t { 0 };
            if (text[offset - 1] == u8'\n')
                return offset >= 2 && text[offset - 2] == u8'\r' ? offset - 2 : offset - 1;
            if (text[offset - 1] == u8'\r')
                return offset - 1;
        }
        std::size_t begin { offset };
        while (begin > 0 && is_break(text[begin - 1]) == false)
            --begin;
        std::size_t end { offset };
        while (end < text.size() && is_break(text[end]) == false)
            ++end;
        const std::u8string_view line { text.substr(begin, end - begin) };
        const std::size_t local { offset - begin };
        if (line.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return std::nullopt;
        try
        {
            static const sk_sp<SkUnicode> unicode { SkShapers::BestAvailableUnicode() };
            if (unicode == nullptr)
                return std::nullopt;
            // Iterator는 호출마다 따로 둔다. 게시된 문서를 여러 thread가 읽는다.
            auto iterator { unicode->makeBreakIterator(SkUnicode::BreakType::kGraphemes) };
            if (iterator == nullptr || iterator->setText(reinterpret_cast<const char*>(line.data()), static_cast<int>(line.size())) == false)
                return std::nullopt;
            std::size_t previous { 0 };
            for (int position { iterator->first() }; iterator->isDone() == false; position = iterator->next())
            {
                const auto boundary { static_cast<std::size_t>(position) };
                if (forward && boundary > local)
                    return begin + boundary;
                if (forward == false && boundary >= local)
                    return begin + previous;
                previous = boundary;
            }
            return begin + (forward ? line.size() : previous);
        }
        catch (...)
        {
            // 편집 경계 질의는 실패를 내보내지 않고 내장 표로 물러선다.
            return std::nullopt;
        }
    }
} // namespace luil::text
