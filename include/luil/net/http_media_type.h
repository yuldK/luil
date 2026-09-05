#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace luil::net {
    // 답의 몸이 무엇인가.
    //
    // **서버가 뭐라고 했는가**이지 우리가 푸는 데 성공했는가가 아니다. json이라고
    // 적힌 몸이 22째 바이트에서 깨져도 갈래는 `json`이고, 무슨 일이 있었는지는
    // `http_body::parse_error`가 적는다 — 실패했다고 `bytes`로 낮추면 앱이 사람에게
    // 보일 이유가 갈래와 함께 사라진다.
    enum class http_body_kind
    {
        // 몸이 없다 (204·304·HEAD, 길이 0). Content-Type이 무엇이라 적혔든 이것이다.
        empty,
        // `nlohmann::json`으로 푼다.
        json,
        // `ui_animated_image`로 푼다. 한 장짜리 파일도 여기로 온다
        // (`decode_animated_image_bytes`가 그렇게 답하는 자리다).
        image,
        // UTF-8 글로 푼다.
        text,
        // **읽어 두기만 한다.** 나중에 webview가 이 바이트와 charset을 그대로
        // 받는다 — 라이브러리는 HTML을 해석하지 않고, 갈래 하나와 `bytes`·
        // `content_type.charset`이 그때를 위해 비워 둔 자리다
        // (http-client-design.md).
        html,
        // 우리가 풀지 않는 바이트다 (zip·pdf·octet-stream·svg).
        // **실패가 아니다** — 바이트는 온전히 들어 있고, 무엇인지는 앱이 안다.
        bytes,
    };

    // 우리가 값으로 푸는 갈래인가.
    // `html`은 거짓이다 — 알아보기는 하지만 푸는 것은 아직 없다.
    [[nodiscard]] constexpr bool http_body_is_parsed(const http_body_kind kind) noexcept
    {
        return kind == http_body_kind::json || kind == http_body_kind::image || kind == http_body_kind::text;
    }

    // 갈래를 무엇으로 정했는가 (진단이자 test가 잠그는 자리다).
    enum class http_body_kind_source
    {
        // 몸이 없어 정할 것이 없었다.
        absent,
        // Content-Type이 말한 그대로다.
        declared,
        // 말이 없거나 `application/octet-stream`이라 앞 바이트를 보고 정했다.
        sniffed,
        // 앱이 `assume_kind`로 정해 주었다 (서버 말보다 앱이 이긴다).
        assumed,
    };

    // Content-Type 한 줄을 갈라 둔 값이다.
    //
    // 나르는 것이 둘뿐인 이유는 쓰는 것이 둘뿐이어서다 — 갈래를 정하는 essence와,
    // 글로 옮길 때 필요한 charset. `boundary` 같은 나머지 매개변수가 필요한 앱은
    // `http_response::headers`에 남아 있는 원문 줄을 본다.
    struct http_media_type
    {
        // "type/subtype"을 소문자로 눕힌 것이다 (매개변수 없이).
        // 판정표가 이것 하나만 본다.
        std::u8string essence {};
        // charset 매개변수를 소문자로 눕힌 것이다. 따옴표는 벗긴다.
        // 없으면 빈 값이고, 그때의 뜻은 갈래마다 다르다 (`decode_http_body`).
        std::u8string charset {};
        // "/"가 있는 꼴이었는가. 헤더가 아예 없었거나 쓰레기면 거짓이고,
        // 그때 냄새를 맡을지는 `media_type_invites_sniffing`이 답한다.
        bool valid { false };

        [[nodiscard]] bool operator==(const http_media_type&) const noexcept = default;
    };

    // Content-Type 헤더 값을 가른다 (`text/html; charset="EUC-KR"`).
    //
    // 던지지 않고, 못 읽은 것은 `valid == false`로 답한다. 밖에서 오는 줄이라
    // 규칙을 넉넉히 잡는다: 앞뒤 공백·대소문자·따옴표·모르는 매개변수를 넘기고,
    // 같은 매개변수가 두 번 오면 **앞엣것**이 이긴다 (뒤가 이기면 덧붙이기로
    // charset이 흔들린다).
    [[nodiscard]] http_media_type parse_media_type(std::u8string_view value);

    // essence 하나로 갈래를 정한다. 표는 이 함수가 전부다.
    //  - `application/json`·`text/json`과 **`+json`으로 끝나는 모든 것**
    //    (`application/problem+json`·`application/ld+json`)이 json이다. 접미사를
    //    보지 않으면 요즘 API의 오류 응답이 통째로 `bytes`가 된다.
    //  - `image/*`가 image다. **`image/svg+xml`만 뺀다** — 이 빌드에 svg 코덱이
    //    없어 image라 부르면 모든 svg가 디코드 실패로 오고, 그것은 서버 잘못이
    //    아니다.
    //  - `text/html`·`application/xhtml+xml`이 html이다.
    //  - 나머지 `text/*`와 `application/xml`·`+xml`·`application/javascript`·
    //    `application/x-www-form-urlencoded`가 text다.
    //  - 그 밖은 전부 bytes다.
    [[nodiscard]] http_body_kind classify_media_type(const http_media_type& media_type) noexcept;

    // 이 Content-Type이면 바이트를 보아도 되는가.
    //
    // **참이 되는 자리는 셋뿐이다**: 헤더가 없었다(`valid == false`), essence가
    // 비었다, essence가 `application/octet-stream`이다.
    //  - 서버가 구체적인 형식을 말했으면 **절대 맡지 않는다.** png 바이트에
    //    `text/plain`이라 적은 서버는 그렇게 하기로 한 것이고, 그 말을 뒤집는 것이
    //    브라우저들이 십수 년 content-sniffing 취약점으로 값을 치른 바로 그 길이다.
    //    서버가 틀린 것을 아는 앱에게는 `http_body_parse_options::assume_kind`라는
    //    문이 따로 있다 — 라이브러리가 몰래 정하는 것과 앱이 시켜서 하는 것은 다르다.
    [[nodiscard]] bool media_type_invites_sniffing(const http_media_type& media_type) noexcept;

    // 앞 바이트로 갈래를 짐작한다. `image` 아니면 `bytes`다.
    //
    // **이미지 서명만 본다.** 이 빌드의 코덱이 실제로 여는 것 다섯이다:
    // png(`89 50 4E 47 0D 0A 1A 0A`)·jpeg(`FF D8 FF`)·gif(`GIF87a`·`GIF89a`)·
    // webp(`RIFF....WEBP`)·bmp(`BM`).
    //  - **ico는 맡지 않는다.** ico는 libpng 갈래에서만 열린다(image_decode.h) —
    //    답이 빌드 구성에 따라 달라지는 냄새는 함정이지 편의가 아니다. wbmp도
    //    빠진다: 서명이랄 것이 없다.
    //  - **json은 맡지 않는다.** `{`는 서명이 아니다. 그것으로 갈래를 정하면
    //    아무 텍스트 파일이나 json이 되고, Content-Type을 빠뜨린 서버의 잘못이
    //    우리 판정으로 굳는다.
    //  - **text도 맡지 않는다.** "UTF-8로 읽히니 글이겠지"는 zip을 글로 만든다.
    //    모르는 바이트는 `bytes`이고, 그것이 정직한 답이다.
    [[nodiscard]] http_body_kind sniff_body_kind(std::span<const std::uint8_t> bytes) noexcept;
} // namespace luil::net
