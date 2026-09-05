#include "luil/net/http_media_type.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace luil::net {
    namespace {
        // 헤더는 밖에서 오는 줄이라 규칙을 넉넉히 잡되, 판정에 쓰는 글은 전부
        // ASCII로 눕혀 둔다. 소문자화가 ASCII에만 걸리는 것이 중요하다 — 멀티바이트
        // 이어짐 바이트에 손대면 올바른 UTF-8이 깨지고, 그 글이 `parse_error`를
        // 거쳐 그리기까지 간다 (text/utf8_text.h가 세운 관문의 앞자리다).
        [[nodiscard]] char8_t lower_ascii(const char8_t byte) noexcept
        {
            return byte >= u8'A' && byte <= u8'Z' ? static_cast<char8_t>(byte - u8'A' + u8'a') : byte;
        }

        [[nodiscard]] bool is_space(const char8_t byte) noexcept
        {
            return byte == u8' ' || byte == u8'\t';
        }

        [[nodiscard]] std::u8string_view trim(std::u8string_view text) noexcept
        {
            while (text.empty() == false && is_space(text.front()))
                text.remove_prefix(1);
            while (text.empty() == false && is_space(text.back()))
                text.remove_suffix(1);
            return text;
        }

        [[nodiscard]] std::u8string lowered(const std::u8string_view text)
        {
            std::u8string output {};
            output.reserve(text.size());
            for (const char8_t byte : text)
                output.push_back(lower_ascii(byte));
            return output;
        }

        [[nodiscard]] bool ends_with(const std::u8string_view text, const std::u8string_view suffix) noexcept
        {
            return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        [[nodiscard]] bool starts_with(const std::u8string_view text, const std::u8string_view prefix) noexcept
        {
            return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
        }

        // "type/subtype" 꼴인가. 양쪽이 다 있어야 참이다.
        //  - 여기서 참이 아니면 `essence`를 아예 비운다. 반쪽만 담아 두면
        //    `classify_media_type`이 쓰레기 글을 표에 넣고 보는 셈이고, 어차피
        //    `media_type_invites_sniffing`이 그 자리에서 참을 답한다.
        [[nodiscard]] bool is_essence(const std::u8string_view essence) noexcept
        {
            const std::size_t slash { essence.find(u8'/') };
            return slash != std::u8string_view::npos && slash != 0 && slash + 1 < essence.size() && essence.find(u8'/', slash + 1) == std::u8string_view::npos;
        }

        // `;`로 매개변수 하나를 뗀다. **따옴표 안의 `;`는 구분자가 아니다** —
        // `filename="a;b"`가 흔하고, 그것을 자르면 뒤엣것이 이름 없는 매개변수로
        // 보여 charset이 엉뚱한 자리에서 걸린다.
        [[nodiscard]] std::size_t find_parameter_end(const std::u8string_view text) noexcept
        {
            bool quoted { false };
            for (std::size_t index { 0 }; index < text.size(); ++index)
            {
                const char8_t byte { text[index] };
                if (quoted)
                {
                    if (byte == u8'\\' && index + 1 < text.size())
                        ++index;
                    else if (byte == u8'"')
                        quoted = false;
                    continue;
                }
                if (byte == u8'"')
                    quoted = true;
                else if (byte == u8';')
                    return index;
            }
            return text.size();
        }

        // 따옴표를 벗기고 `\` 이스케이프를 푼다. 따옴표가 없으면 그대로다.
        [[nodiscard]] std::u8string unquote(const std::u8string_view text)
        {
            if (text.size() < 2 || text.front() != u8'"')
                return std::u8string { text };

            std::u8string output {};
            output.reserve(text.size());
            for (std::size_t index { 1 }; index < text.size(); ++index)
            {
                const char8_t byte { text[index] };
                if (byte == u8'"')
                    break;
                if (byte == u8'\\' && index + 1 < text.size())
                    ++index;
                output.push_back(text[index]);
            }
            return output;
        }

        // 우리 코덱이 실제로 여는 서명 다섯이다 (http_media_type.h의 목록 그대로).
        // ico가 빠진 이유도 거기 적혀 있다 — 빌드 구성에 따라 답이 달라지는 냄새는
        // 함정이다.
        constexpr std::array<std::uint8_t, 8> png_signature { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au };
        constexpr std::array<std::uint8_t, 3> jpeg_signature { 0xFFu, 0xD8u, 0xFFu };
        constexpr std::array<std::uint8_t, 6> gif87a_signature { 'G', 'I', 'F', '8', '7', 'a' };
        constexpr std::array<std::uint8_t, 6> gif89a_signature { 'G', 'I', 'F', '8', '9', 'a' };
        constexpr std::array<std::uint8_t, 2> bmp_signature { 'B', 'M' };
        constexpr std::array<std::uint8_t, 4> riff_signature { 'R', 'I', 'F', 'F' };
        constexpr std::array<std::uint8_t, 4> webp_signature { 'W', 'E', 'B', 'P' };

        [[nodiscard]] bool has_signature(const std::span<const std::uint8_t> bytes, const std::span<const std::uint8_t> signature, const std::size_t offset = 0) noexcept
        {
            if (bytes.size() < offset + signature.size())
                return false;
            return std::equal(signature.begin(), signature.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    } // namespace

    http_media_type parse_media_type(const std::u8string_view value)
    {
        const std::u8string_view head { value.substr(0, find_parameter_end(value)) };
        const std::u8string essence { lowered(trim(head)) };
        if (is_essence(essence) == false)
            return {};

        http_media_type parsed {};
        parsed.essence = essence;
        parsed.valid = true;

        std::u8string_view rest { value.substr(head.size()) };
        bool charset_seen { false };
        while (rest.empty() == false)
        {
            rest.remove_prefix(1); // `;`
            const std::size_t end { find_parameter_end(rest) };
            const std::u8string_view parameter { trim(rest.substr(0, end)) };
            rest.remove_prefix(end);

            const std::size_t equals { parameter.find(u8'=') };
            if (equals == std::u8string_view::npos)
                continue; // 값 없는 `;`와 `;;`는 그냥 넘긴다.
            if (lowered(trim(parameter.substr(0, equals))) != u8"charset")
                continue;

            // 같은 매개변수가 두 번 오면 **앞엣것**이 이긴다. 뒤가 이기면 서버가
            // 덧붙인 한 줄에 charset이 흔들린다 (http_media_type.h의 계약이다).
            //  - 앞엣것이 `charset=`처럼 비어 있어도 앞엣것이다. 값이 비었다고 자리를
            //    비워 두면 뒤에 붙은 줄이 그 자리를 차지한다.
            if (charset_seen)
                continue;
            charset_seen = true;
            parsed.charset = lowered(trim(unquote(trim(parameter.substr(equals + 1)))));
        }
        return parsed;
    }

    http_body_kind classify_media_type(const http_media_type& media_type) noexcept
    {
        const std::u8string_view essence { media_type.essence };

        // **`+json`을 `+xml`보다 먼저 본다.** 뒤집으면 `application/problem+json`이
        // xml 갈래로 새고, 요즘 API의 오류 응답이 통째로 글이 된다.
        if (essence == u8"application/json" || essence == u8"text/json" || ends_with(essence, u8"+json"))
            return http_body_kind::json;
        // image는 접두사로 걸리므로 svg를 여기서 뺀다 — 아래 `+xml` 줄까지 흘려
        // 보내면 svg가 글이 된다.
        if (starts_with(essence, u8"image/"))
            return essence == u8"image/svg+xml" ? http_body_kind::bytes : http_body_kind::image;
        // html이 `text/*`보다 먼저다. 뒤면 `text/html`이 text로 걸려 갈래 하나가
        // 통째로 사라진다.
        if (essence == u8"text/html" || essence == u8"application/xhtml+xml")
            return http_body_kind::html;
        if (starts_with(essence, u8"text/") || essence == u8"application/xml" || ends_with(essence, u8"+xml") || essence == u8"application/javascript"
            || essence == u8"application/x-www-form-urlencoded")
            return http_body_kind::text;
        return http_body_kind::bytes;
    }

    bool media_type_invites_sniffing(const http_media_type& media_type) noexcept
    {
        return media_type.valid == false || media_type.essence.empty() || media_type.essence == u8"application/octet-stream";
    }

    http_body_kind sniff_body_kind(const std::span<const std::uint8_t> bytes) noexcept
    {
        if (has_signature(bytes, png_signature) || has_signature(bytes, jpeg_signature) || has_signature(bytes, gif87a_signature) || has_signature(bytes, gif89a_signature)
            || has_signature(bytes, bmp_signature))
            return http_body_kind::image;
        // webp는 `RIFF`와 `WEBP` 사이에 길이 넷이 끼는 유일한 서명이라 따로 본다.
        // `RIFF`만 보고 넘기면 wav·avi가 그림이 된다.
        if (has_signature(bytes, riff_signature) && has_signature(bytes, webp_signature, 8))
            return http_body_kind::image;
        return http_body_kind::bytes;
    }
} // namespace luil::net
