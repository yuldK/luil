#include "luil/net/http_body.h"

#include "luil/text/utf8_text.h"

#include <CoreFoundation/CoreFoundation.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>

// macOS의 기본 charset 변환기다. utf-16은 손으로, 나머지는 CoreFoundation의 표(`CFStringCreateWithBytes`)가 옮긴다.
//  - 이름은 IANA 이름이라 CoreFoundation이 그대로 안다. 다만 서버가 흔히 쓰는 별칭 셋은 Windows의 변환 계약과
//    같은 문자 집합으로 돌린다: EUC-KR은 확장 한글(CP949), Shift-JIS는 CP932, Latin-1은 CP1252다 (Android와 같다).
//  - CoreFoundation은 표에 없는 바이트에서 NULL을 돌려준다. 잘린 바이트를 대체 문자로 숨기지 않는다.
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

        struct charset_alias
        {
            std::u8string_view name {};
            CFStringEncoding encoding { kCFStringEncodingInvalidId };
        };

        // CoreFoundation의 `euc-kr`은 KS X 1001의 2350자만 읽어 확장 한글에서 실패한다. 서버가 euc-kr이라
        // 적고 보내는 바이트는 대개 CP949라 Windows(949)와 같은 표로 읽는다. 나머지 둘도 같은 이유다.
        constexpr std::array<charset_alias, 24> charset_aliases {
            charset_alias { u8"euc-kr", kCFStringEncodingDOSKorean },
            charset_alias { u8"ks_c_5601-1987", kCFStringEncodingDOSKorean },
            charset_alias { u8"ks_c_5601-1989", kCFStringEncodingDOSKorean },
            charset_alias { u8"ksc5601", kCFStringEncodingDOSKorean },
            charset_alias { u8"ksc_5601", kCFStringEncodingDOSKorean },
            charset_alias { u8"korean", kCFStringEncodingDOSKorean },
            charset_alias { u8"cp949", kCFStringEncodingDOSKorean },
            charset_alias { u8"windows-949", kCFStringEncodingDOSKorean },
            charset_alias { u8"uhc", kCFStringEncodingDOSKorean },
            charset_alias { u8"shift_jis", kCFStringEncodingDOSJapanese },
            charset_alias { u8"shift-jis", kCFStringEncodingDOSJapanese },
            charset_alias { u8"sjis", kCFStringEncodingDOSJapanese },
            charset_alias { u8"x-sjis", kCFStringEncodingDOSJapanese },
            charset_alias { u8"ms_kanji", kCFStringEncodingDOSJapanese },
            charset_alias { u8"cp932", kCFStringEncodingDOSJapanese },
            charset_alias { u8"windows-31j", kCFStringEncodingDOSJapanese },
            // 이름은 ISO지만 실제로 오는 바이트는 windows-1252다 (0x80~0x9F에 따옴표가 든다).
            charset_alias { u8"iso-8859-1", kCFStringEncodingWindowsLatin1 },
            charset_alias { u8"iso8859-1", kCFStringEncodingWindowsLatin1 },
            charset_alias { u8"iso_8859-1", kCFStringEncodingWindowsLatin1 },
            charset_alias { u8"latin1", kCFStringEncodingWindowsLatin1 },
            charset_alias { u8"latin-1", kCFStringEncodingWindowsLatin1 },
            charset_alias { u8"l1", kCFStringEncodingWindowsLatin1 },
            charset_alias { u8"cp819", kCFStringEncodingWindowsLatin1 },
            charset_alias { u8"csisolatin1", kCFStringEncodingWindowsLatin1 },
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
        [[nodiscard]] std::u8string charset_error(const std::u8string_view prefix, const std::u8string_view charset, const std::u8string_view suffix)
        {
            std::u8string message { prefix };
            for (const char8_t byte : charset)
                message.push_back(byte >= 0x20u && byte < 0x7Fu ? byte : u8'?');
            message += suffix;
            return message;
        }

        // 이름을 CoreFoundation의 부호화로 옮긴다. 모르는 이름은 `kCFStringEncodingInvalidId`다.
        [[nodiscard]] CFStringEncoding find_encoding(const std::u8string_view charset)
        {
            const std::u8string_view name { trimmed(charset) };
            if (name.empty())
                return kCFStringEncodingInvalidId;
            for (const charset_alias& entry : charset_aliases)
                if (same_ascii_ci(name, entry.name))
                    return entry.encoding;
            // IANA 이름은 인쇄 가능한 ASCII다. 그 밖의 바이트가 든 이름은 묻지 않는다.
            std::string ascii {};
            for (const char8_t byte : name)
            {
                if (byte < 0x21u || byte > 0x7Eu)
                    return kCFStringEncodingInvalidId;
                ascii.push_back(static_cast<char>(lower_ascii(byte)));
            }
            const CFStringRef label { CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(ascii.data()), static_cast<CFIndex>(ascii.size()), kCFStringEncodingASCII, false) };
            if (label == nullptr)
                return kCFStringEncodingInvalidId;
            const CFStringEncoding encoding { CFStringConvertIANACharSetNameToEncoding(label) };
            CFRelease(label);
            return encoding;
        }

        // CoreFoundation의 표로 옮긴다. 표에 없는 바이트는 실패다.
        [[nodiscard]] std::u8string transcode_table(const std::span<const std::uint8_t> bytes, const CFStringEncoding encoding, const std::u8string_view charset, std::u8string& error)
        {
            if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<CFIndex>::max()))
            {
                error = charset_error(u8"The body is too large to convert from ", charset, u8".");
                return {};
            }
            const CFStringRef text { CFStringCreateWithBytes(nullptr, bytes.data(), static_cast<CFIndex>(bytes.size()), encoding, false) };
            if (text == nullptr)
            {
                error = charset_error(u8"Failed to convert the body from ", charset, u8": an invalid byte sequence.");
                return {};
            }
            // `CFStringGetCString`은 NUL에서 멈춘다. 길이를 재고 바이트로 꺼낸다.
            const CFRange range { 0, CFStringGetLength(text) };
            CFIndex needed { 0 };
            const CFIndex measured { CFStringGetBytes(text, range, kCFStringEncodingUTF8, 0, false, nullptr, 0, &needed) };
            std::u8string output(static_cast<std::size_t>(needed), u8'\0');
            const CFIndex converted { measured == range.length ? CFStringGetBytes(text, range, kCFStringEncodingUTF8, 0, false, reinterpret_cast<UInt8*>(output.data()), needed, nullptr) : 0 };
            CFRelease(text);
            // 짝 없는 surrogate는 UTF-8로 옮기지 못한다 (`lossByte`가 0이라 거기서 멈춘다).
            if (converted != range.length || text::utf8_is_valid(output) == false)
            {
                error = charset_error(u8"Failed to convert the body from ", charset, u8": the text is not valid Unicode.");
                return {};
            }
            return output;
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
    }

    http_text_transcoder codepage_text_transcoder()
    {
        return [](const std::span<const std::uint8_t> bytes, const std::u8string_view charset, std::u8string& error) -> std::u8string {
            const utf16_order order { find_utf16_order(charset) };
            if (order != utf16_order::none)
                return bytes.empty() ? std::u8string {} : transcode_utf16(bytes, order, charset, error);
            const CFStringEncoding encoding { find_encoding(charset) };
            if (encoding == kCFStringEncodingInvalidId || CFStringIsEncodingAvailable(encoding) == false)
            {
                error = charset_error(u8"Unsupported charset: ", charset, u8".");
                return {};
            }
            if (bytes.empty())
                return {};
            return transcode_table(bytes, encoding, charset, error);
        };
    }
}
