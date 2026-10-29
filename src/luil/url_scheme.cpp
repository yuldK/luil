#include "luil/app/platform_services.h"

#include <algorithm>

// `open_url`이 여는 주소의 검사다. 플랫폼마다 여는 길만 다르고 검사는 같아 따로 둔다 (platform-services-design.md).
namespace luil {
    namespace {
        [[nodiscard]] constexpr bool ascii_alpha(const char8_t character) noexcept
        {
            return (character >= u8'a' && character <= u8'z') || (character >= u8'A' && character <= u8'Z');
        }

        [[nodiscard]] constexpr bool scheme_character(const char8_t character) noexcept
        {
            return ascii_alpha(character) || (character >= u8'0' && character <= u8'9') || character == u8'+' || character == u8'-' || character == u8'.';
        }

        [[nodiscard]] constexpr char8_t ascii_lower(const char8_t character) noexcept
        {
            return character >= u8'A' && character <= u8'Z' ? static_cast<char8_t>(character - u8'A' + u8'a') : character;
        }

        [[nodiscard]] bool same_scheme(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            return std::ranges::equal(left, right, [](const char8_t first, const char8_t second) { return ascii_lower(first) == ascii_lower(second); });
        }

        [[nodiscard]] bool has_control(const std::u8string_view url) noexcept
        {
            for (std::size_t index { 0 }; index < url.size(); ++index)
            {
                const char8_t byte { url[index] };
                if (byte < 0x20 || byte == 0x7F)
                    return true;
                // C1 제어 문자(U+0080~U+009F)는 UTF-8로 C2 80~C2 9F다.
                if (byte == 0xC2 && index + 1 < url.size() && url[index + 1] >= 0x80 && url[index + 1] <= 0x9F)
                    return true;
            }
            return false;
        }
    } // namespace

    bool url_scheme_allowed(const std::u8string_view url, const std::span<const std::u8string> schemes)
    {
        // OS나 브라우저가 공백·제어 문자를 걸러 읽으면 여기서 본 scheme과 열리는 scheme이 달라진다
        // (` javascript:`, `java\tscript:`). 고쳐 읽지 않고 막는다.
        if (url.empty() || url.front() == u8' ' || url.back() == u8' ' || has_control(url))
            return false;
        if (ascii_alpha(url.front()) == false)
            return false;
        std::size_t end { 1 };
        while (end < url.size() && scheme_character(url[end]))
            ++end;
        if (end >= url.size() || url[end] != u8':' || end + 1 == url.size())
            return false;

        const std::u8string_view scheme { url.substr(0, end) };
        return std::ranges::any_of(schemes, [scheme](const std::u8string& allowed) { return same_scheme(scheme, allowed); });
    }
} // namespace luil
