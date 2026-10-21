#include "text/grapheme_internal.h"

#include "modules/skshaper/utils/FactoryHelpers.h"
#include "modules/skunicode/include/SkUnicode.h"

#include <limits>

namespace luil::text {
    std::optional<std::size_t> platform_grapheme_boundary(const std::u8string_view text, const std::size_t offset, const bool forward) noexcept
    {
        if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return std::nullopt;
        try
        {
            static const sk_sp<SkUnicode> unicode { SkShapers::BestAvailableUnicode() };
            if (unicode == nullptr)
                return std::nullopt;
            // Iterator는 호출마다 따로 둔다. 게시된 문서를 여러 thread가 읽는다.
            auto iterator { unicode->makeBreakIterator(SkUnicode::BreakType::kGraphemes) };
            if (iterator == nullptr || iterator->setText(reinterpret_cast<const char*>(text.data()), static_cast<int>(text.size())) == false)
                return std::nullopt;
            std::size_t previous { 0 };
            for (int position { iterator->first() }; iterator->isDone() == false; position = iterator->next())
            {
                const auto boundary { static_cast<std::size_t>(position) };
                if (forward && boundary > offset)
                    return boundary;
                if (forward == false && boundary >= offset)
                    return previous;
                previous = boundary;
            }
            return forward ? text.size() : previous;
        }
        catch (...)
        {
            // 편집 경계 질의는 실패를 내보내지 않고 내장 표로 물러선다.
            return std::nullopt;
        }
    }
} // namespace luil::text
