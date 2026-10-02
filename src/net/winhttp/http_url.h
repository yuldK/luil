#pragma once

#include "luil/net/http_message.h"

#include <optional>
#include <string>
#include <string_view>

namespace luil::net {
    // URL 하나를 전송이 받는 조각으로 가른 값이다.
    //
    // 조각이 넷인 것은 WinHTTP가 묻는 것이 넷이기 때문이다 — `WinHttpConnect`가
    // host와 port를, `WinHttpOpenRequest`가 나머지를 받는다. 사용자·비밀번호 칸은
    // 버린다 (자격 증명은 이 client의 범위 밖이다 — http-client-design.md).
    struct parsed_url
    {
        bool secure { false };
        std::wstring host {};
        unsigned short port { 0 };
        // 경로 + 질의다 (`/api/x?y=1`). 아무것도 없으면 `/`다.
        std::wstring target {};
    };

    // 절대 URL을 가른다. 실패는 값이고 **선을 건드리기 전**이다.
    //  - http·https가 아니면 `unsupported_scheme`, 못 읽으면 `invalid_url`이다.
    //  - 가르는 일을 `WinHttpCrackUrl`에게 맡기는 이유는 전송이 실제로 읽는 규칙이
    //    그것이기 때문이다. 우리가 따로 가르면 파서가 둘이 되어 언젠가 어긋난다.
    [[nodiscard]] std::optional<parsed_url> parse_http_url(std::u8string_view url, http_error& error);
} // namespace luil::net
