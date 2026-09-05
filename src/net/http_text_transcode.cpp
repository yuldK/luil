#include "net/http_text_transcode.h"

#include "luil/net/http_body.h"
#include "win32/utf8.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace luil::net {
    namespace {
        constexpr char32_t byte_order_mark { 0xFEFFu };

        struct charset_entry
        {
            std::u8string_view name {};
            unsigned int code_page { 0 };
        };

        // 이름표다. **여기 없는 이름은 실패로 답한다** — 모르는 것을 latin-1로
        // 짐작해 옮기면 깨진 글이 성공으로 보이고, 바이트가 온전한데도 앱은 그것을
        // 알 길이 없다.
        //  - `iso-8859-1`은 28591이 아니라 **1252**로 보낸다. 이름은 ISO지만 그렇게
        //    적고 실제로는 windows-1252를 보내는 서버가 절대다수이고(0x80~0x9F에
        //    따옴표·대시가 든다), 브라우저들이 전부 그렇게 읽는다. 28591로 읽으면
        //    그 자리가 C1 제어 문자가 되어 **올바른 UTF-8인 채로 못 읽을 글**이
        //    나온다 — 실패보다 나쁜 답이다.
        //  - utf-16 둘은 코드 페이지 번호만 빌린다 (`utf16le_code_page`의 주석).
        constexpr std::array<charset_entry, 48> charset_table {
            charset_entry { u8"utf-8", CP_UTF8 },
            charset_entry { u8"utf8", CP_UTF8 },
            charset_entry { u8"unicode-1-1-utf-8", CP_UTF8 },
            charset_entry { u8"us-ascii", 20127u },
            charset_entry { u8"ascii", 20127u },
            charset_entry { u8"iso-ir-6", 20127u },
            charset_entry { u8"ansi_x3.4-1968", 20127u },
            charset_entry { u8"utf-16", utf16le_code_page },
            charset_entry { u8"utf-16le", utf16le_code_page },
            charset_entry { u8"ucs-2", utf16le_code_page },
            charset_entry { u8"unicode", utf16le_code_page },
            charset_entry { u8"utf-16be", utf16be_code_page },
            charset_entry { u8"unicodefffe", utf16be_code_page },
            charset_entry { u8"euc-kr", 949u },
            charset_entry { u8"ks_c_5601-1987", 949u },
            charset_entry { u8"ks_c_5601-1989", 949u },
            charset_entry { u8"ksc5601", 949u },
            charset_entry { u8"ksc_5601", 949u },
            charset_entry { u8"korean", 949u },
            charset_entry { u8"cp949", 949u },
            charset_entry { u8"windows-949", 949u },
            charset_entry { u8"uhc", 949u },
            charset_entry { u8"shift_jis", 932u },
            charset_entry { u8"shift-jis", 932u },
            charset_entry { u8"sjis", 932u },
            charset_entry { u8"x-sjis", 932u },
            charset_entry { u8"ms_kanji", 932u },
            charset_entry { u8"cp932", 932u },
            charset_entry { u8"windows-31j", 932u },
            charset_entry { u8"euc-jp", 51932u },
            charset_entry { u8"iso-2022-jp", 50220u },
            charset_entry { u8"gb2312", 936u },
            charset_entry { u8"gbk", 936u },
            charset_entry { u8"cp936", 936u },
            charset_entry { u8"windows-936", 936u },
            charset_entry { u8"csgb2312", 936u },
            charset_entry { u8"chinese", 936u },
            charset_entry { u8"gb18030", 54936u },
            charset_entry { u8"big5", 950u },
            charset_entry { u8"big5-hkscs", 950u },
            charset_entry { u8"cp950", 950u },
            charset_entry { u8"windows-950", 950u },
            charset_entry { u8"koi8-r", 20866u },
            charset_entry { u8"koi8-u", 21866u },
            charset_entry { u8"tis-620", 874u },
            // 번호가 줄에서 벗어나는 둘이라 아래 접두사 규칙이 잡지 못한다.
            charset_entry { u8"iso-8859-13", 28603u },
            charset_entry { u8"iso-8859-15", 28605u },
            charset_entry { u8"iso8859-15", 28605u },
        };

        // `iso-8859-n`·`windows-125n`은 줄이 많아 접두사로 받는다. 표에 열다섯 줄을
        // 더 쓰는 것보다 이 넷이 읽기 쉽다.
        struct code_page_range
        {
            std::u8string_view prefix {};
            unsigned int first_suffix { 0 };
            unsigned int last_suffix { 0 };
            unsigned int base { 0 };
        };

        constexpr std::array<code_page_range, 4> charset_ranges {
            // iso-8859-2..9 → 28592..28599. 1은 위에서 1252로 가로챈다.
            code_page_range { u8"iso-8859-", 2u, 9u, 28590u },
            code_page_range { u8"iso8859-", 2u, 9u, 28590u },
            // windows-1250..1258은 번호가 그대로다.
            code_page_range { u8"windows-125", 0u, 8u, 1250u },
            code_page_range { u8"cp125", 0u, 8u, 1250u },
        };

        [[nodiscard]] char8_t lower_ascii(const char8_t byte) noexcept
        {
            return byte >= u8'A' && byte <= u8'Z' ? static_cast<char8_t>(byte - u8'A' + u8'a') : byte;
        }

        // 표를 볼 때 이름을 눕힌 사본을 짓지 않는다 — `charset_code_page`가
        // `noexcept`라 그 안에서 할당하면 실패가 갈 곳이 없다.
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

        [[nodiscard]] bool starts_with_ascii_ci(const std::u8string_view text, const std::u8string_view prefix) noexcept
        {
            return text.size() >= prefix.size() && same_ascii_ci(text.substr(0, prefix.size()), prefix);
        }

        // 오류 글에 charset 이름을 싣는 자리다. **ASCII만 옮기고 나머지는 `?`다** —
        // 이 글이 `parse_error`를 거쳐 그리기까지 가므로 올바른 UTF-8이어야 하는데,
        // 헤더는 밖에서 오는 값이라 무엇이 들었는지 알 수 없다 (text/utf8_text.h).
        void append_charset(std::u8string& output, const std::u8string_view charset)
        {
            for (const char8_t byte : charset)
                output.push_back(byte >= 0x20u && byte < 0x7Fu ? byte : u8'?');
        }

        [[nodiscard]] std::u8string unsupported_charset_error(const std::u8string_view charset)
        {
            std::u8string message { u8"Unsupported charset '" };
            append_charset(message, charset);
            message += u8"'.";
            return message;
        }

        [[nodiscard]] std::u8string lossy_charset_note(const std::u8string_view charset)
        {
            std::u8string message { u8"Some bytes are not valid " };
            append_charset(message, charset);
            message += u8"; they were replaced.";
            return message;
        }

        // 진단 글의 꼬리는 `make_hresult_error`와 같은 모양이다 — 앞은 사람이 읽을
        // 문장이고 뒤는 코드다 (win32/win32_error.h).
        [[nodiscard]] std::u8string system_charset_error(const std::u8string_view charset, const unsigned long code)
        {
            std::u8string message { u8"Failed to convert the body from " };
            append_charset(message, charset);
            message += u8". (Win32=0x";
            for (int shift { 28 }; shift >= 0; shift -= 4)
            {
                const auto digit { static_cast<unsigned int>((code >> shift) & 0xFu) };
                message.push_back(digit < 10u ? static_cast<char8_t>(u8'0' + digit) : static_cast<char8_t>(u8'A' + (digit - 10u)));
            }
            message += u8")";
            return message;
        }

        [[nodiscard]] std::u8string from_utf16(const std::wstring_view text, const std::u8string_view charset, std::u8string& error)
        {
            win32::utf_conversion_result<std::u8string> converted { win32::utf16_to_utf8(text) };
            if (converted.value.has_value() == false)
            {
                error = system_charset_error(charset, converted.error.has_value() ? converted.error->native_error : 0ul);
                return {};
            }
            return std::move(*converted.value);
        }

        // utf-16 바이트를 손으로 모은다. `MultiByteToWideChar`가 1200·1201을 받지
        // 않아서다.
        //  - 길이가 홀수면 **거절한다.** 마지막 반쪽을 버리고 넘어가면 잘린 것이
        //    "원래 그런 답"으로 보인다 (`decode_animated_image_bytes`가 장 하나
        //    실패에 통째로 거절하는 그 판단과 같다).
        //  - **BOM이 이름을 이긴다.** `charset=utf-16`은 바이트 차례를 말하지 않은
        //    것이라 표가 le로 받아 두는데, 앞에 `FE FF`가 있으면 서버가 be라고 적어
        //    둔 셈이다. 떼어 내는 것도 여기서 한다 — 글에 남으면 보이지 않는 한
        //    글자가 앞에 붙고 그것을 앱이 잘라 내야 한다.
        [[nodiscard]] std::u8string transcode_utf16(const std::span<const std::uint8_t> bytes, bool big_endian, const std::u8string_view charset, std::u8string& error)
        {
            if (bytes.size() % 2u != 0u)
            {
                std::u8string message { u8"The body has an odd byte count for " };
                append_charset(message, charset);
                message += u8".";
                error = std::move(message);
                return {};
            }
            if (bytes.size() >= 2u && bytes[0] == 0xFFu && bytes[1] == 0xFEu)
                big_endian = false;
            else if (bytes.size() >= 2u && bytes[0] == 0xFEu && bytes[1] == 0xFFu)
                big_endian = true;

            std::wstring text {};
            text.reserve(bytes.size() / 2u);
            for (std::size_t index { 0 }; index + 1u < bytes.size(); index += 2u)
            {
                const auto low { static_cast<unsigned int>(bytes[big_endian ? index + 1u : index]) };
                const auto high { static_cast<unsigned int>(bytes[big_endian ? index : index + 1u]) };
                text.push_back(static_cast<wchar_t>((high << 8u) | low));
            }
            if (text.empty() == false && text.front() == static_cast<wchar_t>(byte_order_mark))
                text.erase(text.begin());
            return from_utf16(text, charset, error);
        }

        // 코드 페이지 하나로 옮긴다. **먼저 엄격하게 시도하고, 거절당하면 느슨하게
        // 다시 옮기며 그 사실을 적는다.**
        //  - 빈 글을 돌려주는 것보다 낫다: 바이트 하나가 깨졌다고 나머지 2 MB를
        //    버리면 화면에서 "빈 응답"과 구별되지 않는다 (깨진 UTF-8을 U+FFFD로
        //    담는 `decode_http_body`의 규칙과 같은 자리다). 글은 서고 원문과 다르다는
        //    사실만 `error`에 남으므로, 부르는 쪽이 그것을 `parse_error`로 옮긴다.
        //  - `MB_ERR_INVALID_CHARS`를 받지 않는 코드 페이지가 있다
        //    (`ERROR_INVALID_FLAGS`). 그때는 손실 여부를 알 길이 없으므로 적지
        //    않고 그냥 옮긴다.
        [[nodiscard]] std::u8string transcode_code_page(const unsigned int code_page, const std::span<const std::uint8_t> bytes, const std::u8string_view charset, std::u8string& error)
        {
            if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                error = system_charset_error(charset, static_cast<unsigned long>(ERROR_ARITHMETIC_OVERFLOW));
                return {};
            }

            const auto* const first { reinterpret_cast<const char*>(bytes.data()) };
            const auto length { static_cast<int>(bytes.size()) };

            DWORD flags { MB_ERR_INVALID_CHARS };
            bool lossy { false };
            int required { MultiByteToWideChar(code_page, flags, first, length, nullptr, 0) };
            if (required <= 0)
            {
                const unsigned long failure { GetLastError() };
                if (failure != ERROR_NO_UNICODE_TRANSLATION && failure != ERROR_INVALID_FLAGS)
                {
                    error = system_charset_error(charset, failure);
                    return {};
                }
                lossy = failure == ERROR_NO_UNICODE_TRANSLATION;
                flags = 0;
                required = MultiByteToWideChar(code_page, flags, first, length, nullptr, 0);
                if (required <= 0)
                {
                    error = system_charset_error(charset, GetLastError());
                    return {};
                }
            }

            std::wstring wide {};
            wide.resize(static_cast<std::size_t>(required));
            const int written { MultiByteToWideChar(code_page, flags, first, length, wide.data(), required) };
            if (written <= 0)
            {
                error = system_charset_error(charset, GetLastError());
                return {};
            }
            wide.resize(static_cast<std::size_t>(written));
            if (wide.empty() == false && wide.front() == static_cast<wchar_t>(byte_order_mark))
                wide.erase(wide.begin());

            std::u8string failure {};
            std::u8string text { from_utf16(wide, charset, failure) };
            if (failure.empty() == false)
            {
                error = std::move(failure);
                return {};
            }
            if (lossy)
                error = lossy_charset_note(charset);
            return text;
        }
    } // namespace

    unsigned int charset_code_page(const std::u8string_view charset) noexcept
    {
        const std::u8string_view name { trimmed(charset) };
        if (name.empty())
            return 0u;

        for (const charset_entry& entry : charset_table)
            if (same_ascii_ci(name, entry.name))
                return entry.code_page;

        // `iso-8859-1`·`latin1`은 표 밖에서 1252로 받는다 (위 주석의 이유다).
        if (same_ascii_ci(name, u8"iso-8859-1") || same_ascii_ci(name, u8"iso8859-1") || same_ascii_ci(name, u8"iso_8859-1") || same_ascii_ci(name, u8"latin1") || same_ascii_ci(name, u8"l1")
            || same_ascii_ci(name, u8"cp819") || same_ascii_ci(name, u8"csisolatin1"))
            return 1252u;

        for (const code_page_range& range : charset_ranges)
        {
            if (name.size() != range.prefix.size() + 1u || starts_with_ascii_ci(name, range.prefix) == false)
                continue;
            const char8_t digit { name.back() };
            if (digit < u8'0' || digit > u8'9')
                continue;
            const auto suffix { static_cast<unsigned int>(digit - u8'0') };
            if (suffix < range.first_suffix || suffix > range.last_suffix)
                continue;
            return range.base + suffix;
        }
        return 0u;
    }

    http_text_transcoder codepage_text_transcoder()
    {
        return [](const std::span<const std::uint8_t> bytes, const std::u8string_view charset, std::u8string& error) -> std::u8string {
            const unsigned int code_page { charset_code_page(charset) };
            if (code_page == 0u)
            {
                error = unsupported_charset_error(charset);
                return {};
            }
            if (bytes.empty())
                return {};
            if (code_page == utf16le_code_page || code_page == utf16be_code_page)
                return transcode_utf16(bytes, code_page == utf16be_code_page, charset, error);
            return transcode_code_page(code_page, bytes, charset, error);
        };
    }
} // namespace luil::net
