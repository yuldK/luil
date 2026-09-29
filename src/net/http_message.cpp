#include "luil/net/http_message.h"

#include <cstddef>

namespace luil::net {
    namespace {
        [[nodiscard]] char8_t lower_ascii(const char8_t byte) noexcept
        {
            return byte >= u8'A' && byte <= u8'Z' ? static_cast<char8_t>(byte - u8'A' + u8'a') : byte;
        }

        // 헤더 이름 비교다. **ASCII만 눕힌다** — 이름은 토큰이라 그것으로 충분하고,
        // 멀티바이트에 손대면 올바른 UTF-8이 깨진다 (`parse_media_type`과 같은 규칙).
        [[nodiscard]] bool same_header_name(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index { 0 }; index < left.size(); ++index)
                if (lower_ascii(left[index]) != lower_ascii(right[index]))
                    return false;
            return true;
        }
    } // namespace

    std::u8string_view http_method_name(const http_method method) noexcept
    {
        switch (method)
        {
        case http_method::get:
            return u8"GET";
        case http_method::head:
            return u8"HEAD";
        case http_method::post:
            return u8"POST";
        case http_method::put:
            return u8"PUT";
        case http_method::patch:
            return u8"PATCH";
        case http_method::remove:
            return u8"DELETE";
        case http_method::options:
            return u8"OPTIONS";
        }
        // default를 두지 않는 것은 값이 는 날 컴파일러가 빠진 갈래를 잡게 하려는
        // 것이다 (`renderer_mode_name`과 같은 모양이다).
        return u8"GET";
    }

    std::u8string_view find_header(const std::span<const http_header> headers, const std::u8string_view name) noexcept
    {
        for (const http_header& header : headers)
            if (same_header_name(header.name, name))
                return header.value;
        return {};
    }

    bool http_header_crosses_origins(const std::u8string_view name) noexcept
    {
        // 비밀이 아니라 요청의 뜻(형식 협상·부분 요청·조건부 요청)을 담는 이름이다.
        // 목록 밖은 모두 지운다 — 새 토큰 이름이 생겨도 기본이 "새지 않음"이다.
        constexpr std::u8string_view kept[] {
            u8"accept",
            u8"accept-language",
            u8"cache-control",
            u8"range",
            u8"if-range",
            u8"if-none-match",
            u8"if-modified-since",
        };
        for (const std::u8string_view safe : kept)
            if (same_header_name(name, safe))
                return true;
        return false;
    }

    std::vector<std::uint8_t> http_text_body(const std::u8string_view text)
    {
        const auto* const first { reinterpret_cast<const std::uint8_t*>(text.data()) };
        return std::vector<std::uint8_t> { first, first + text.size() };
    }

    std::span<const std::uint8_t> http_body::data() const noexcept
    {
        if (bytes == nullptr)
            return {};
        return std::span<const std::uint8_t> { *bytes };
    }

    std::u8string_view http_body::as_text() const noexcept
    {
        if (text == nullptr)
            return {};
        return std::u8string_view { *text };
    }

    bool http_body::parsed() const noexcept
    {
        switch (kind)
        {
        case http_body_kind::json:
            return json != nullptr;
        case http_body_kind::image:
            return image.valid();
        case http_body_kind::text:
            return text != nullptr;
        default:
            // html은 갈래를 알아볼 뿐 푸는 것이 없다 (`http_body_is_parsed`와 같은
            // 답이다). `html_as_text`로 글이 서 있어도 마찬가지다 — 그 글은 눈으로
            // 보라고 딸려 온 것이지 이 갈래의 값이 아니다.
            return false;
        }
    }

    const ui_image& http_body::still_image() const noexcept
    {
        // 범위 검사를 부르는 쪽에 떠넘기지 않는다 — 그림이 없으면 `frame_image`가
        // 빈 이미지를 답하는 계약이 이미 서 있다 (image_element.h).
        return image.frame_image(0);
    }

    bool http_response::ok() const noexcept
    {
        // 전송이 실패했으면 상태 줄이 있어도 "답이 왔다"가 아니다. 몸을 상한에서
        // 끊은 답이 그 자리다 — 200이 적혀 있지만 우리가 든 것은 반쪽이다.
        return error.empty() && http_status_is_success(status_code);
    }

    std::u8string_view http_response::header(const std::u8string_view name) const noexcept
    {
        return find_header(headers, name);
    }
} // namespace luil::net
