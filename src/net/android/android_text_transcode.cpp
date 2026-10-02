#include "luil/net/http_body.h"

#include "luil/text/utf8_text.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace luil::net {
    namespace {
        enum class utf16_order
        {
            none,
            little,
            big,
        };

        struct utf16_name
        {
            std::u8string_view name {};
            utf16_order order { utf16_order::none };
        };

        // Windows 백엔드의 표와 같은 이름이다 (net/winhttp/http_text_transcode.cpp). 바이트 차례를
        // 말하지 않은 이름은 little endian으로 받는다.
        constexpr std::array<utf16_name, 6> utf16_names {
            utf16_name { u8"utf-16", utf16_order::little },
            utf16_name { u8"utf-16le", utf16_order::little },
            utf16_name { u8"ucs-2", utf16_order::little },
            utf16_name { u8"unicode", utf16_order::little },
            utf16_name { u8"utf-16be", utf16_order::big },
            utf16_name { u8"unicodefffe", utf16_order::big },
        };

        [[nodiscard]] char8_t lower_ascii(const char8_t byte) noexcept
        {
            return byte >= u8'A' && byte <= u8'Z' ? static_cast<char8_t>(byte - u8'A' + u8'a') : byte;
        }

        [[nodiscard]] std::u8string_view trimmed(std::u8string_view charset) noexcept
        {
            while (charset.empty() == false && (charset.front() == u8' ' || charset.front() == u8'\t'))
                charset.remove_prefix(1);
            while (charset.empty() == false && (charset.back() == u8' ' || charset.back() == u8'\t'))
                charset.remove_suffix(1);
            return charset;
        }

        [[nodiscard]] bool same_ascii_ci(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index { 0 }; index < left.size(); ++index)
                if (lower_ascii(left[index]) != lower_ascii(right[index]))
                    return false;
            return true;
        }

        [[nodiscard]] utf16_order find_utf16_order(const std::u8string_view charset) noexcept
        {
            const std::u8string_view name { trimmed(charset) };
            for (const utf16_name& entry : utf16_names)
                if (same_ascii_ci(name, entry.name))
                    return entry.order;
            return utf16_order::none;
        }

        // 오류 글에 charset 이름을 싣는다. ASCII만 옮기고 나머지는 `?`다 — 헤더는 밖에서 오는
        // 값이라 이 글이 올바른 UTF-8이려면 그래야 한다.
        void append_charset(std::u8string& output, const std::u8string_view charset)
        {
            for (const char8_t byte : charset)
                output.push_back(byte >= 0x20u && byte < 0x7Fu ? byte : u8'?');
        }

        [[nodiscard]] std::u8string charset_error(const std::u8string_view prefix, const std::u8string_view charset, const std::u8string_view suffix)
        {
            std::u8string message { prefix };
            append_charset(message, charset);
            message += suffix;
            return message;
        }

        // utf-16 바이트를 손으로 모은다. 규칙은 Windows 백엔드와 같다.
        //  - 길이가 홀수면 거절한다. 마지막 반쪽을 버리면 잘린 것이 온전한 답으로 보인다.
        //  - BOM이 이름을 이기고, 글에서는 떼어 낸다.
        //  - 짝 없는 surrogate는 거절한다 (Windows의 엄격한 변환과 같다).
        [[nodiscard]] std::u8string transcode_utf16(const std::span<const std::uint8_t> bytes, utf16_order order, const std::u8string_view charset, std::u8string& error)
        {
            if (bytes.size() % 2u != 0u)
            {
                error = charset_error(u8"The body has an odd byte count for ", charset, u8".");
                return {};
            }

            std::size_t index { 0 };
            if (bytes.size() >= 2u && bytes[0] == 0xFFu && bytes[1] == 0xFEu)
            {
                order = utf16_order::little;
                index = 2u;
            }
            else if (bytes.size() >= 2u && bytes[0] == 0xFEu && bytes[1] == 0xFFu)
            {
                order = utf16_order::big;
                index = 2u;
            }

            const auto unit_at = [&bytes, order](const std::size_t at) noexcept {
                const auto first { static_cast<char16_t>(bytes[at]) };
                const auto second { static_cast<char16_t>(bytes[at + 1u]) };
                return order == utf16_order::big ? static_cast<char16_t>((first << 8u) | second) : static_cast<char16_t>((second << 8u) | first);
            };

            std::u8string text {};
            text.reserve(bytes.size());
            while (index < bytes.size())
            {
                const char16_t unit { unit_at(index) };
                index += 2u;
                char32_t codepoint { unit };
                if (unit >= 0xD800u && unit <= 0xDBFFu)
                {
                    const char16_t trail { index < bytes.size() ? unit_at(index) : char16_t { 0 } };
                    if (trail < 0xDC00u || trail > 0xDFFFu)
                    {
                        error = charset_error(u8"Failed to convert the body from ", charset, u8": an unpaired surrogate.");
                        return {};
                    }
                    index += 2u;
                    codepoint = 0x10000u + ((static_cast<char32_t>(unit) - 0xD800u) << 10u) + (static_cast<char32_t>(trail) - 0xDC00u);
                }
                else if (unit >= 0xDC00u && unit <= 0xDFFFu)
                {
                    error = charset_error(u8"Failed to convert the body from ", charset, u8": an unpaired surrogate.");
                    return {};
                }
                text += text::utf8_encode(codepoint);
            }
            return text;
        }
    } // namespace

    // Android는 utf-8(몸 판정이 직접 읽는다)과 utf-16만 옮기고 그 밖의 charset은 거절한다
    // (docs/android-port-plan.md 결정 3). bionic의 `iconv`는 API 28부터라 minSdk 26에서 쓸 수 없다.
    http_text_transcoder codepage_text_transcoder()
    {
        return [](const std::span<const std::uint8_t> bytes, const std::u8string_view charset, std::u8string& error) -> std::u8string {
            const utf16_order order { find_utf16_order(charset) };
            if (order == utf16_order::none)
            {
                error = charset_error(u8"Unsupported charset '", charset, u8"'.");
                return {};
            }
            if (bytes.empty())
                return {};
            return transcode_utf16(bytes, order, charset, error);
        };
    }
} // namespace luil::net
