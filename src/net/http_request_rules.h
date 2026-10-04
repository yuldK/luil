#pragma once

#include "luil/net/http_message.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luil::net {
    // 백엔드가 함께 쓰는 요청 규칙이다. 선을 건드리기 **전**에 거절할 것을 한 자리에서 정한다.
    //
    // 전송은 OS 백엔드마다 다르지만 무엇을 보내지 않을지는 같아야 한다 — 백엔드마다 따로 두면
    // 한쪽만 헤더 주입을 막는 날이 온다.

    // 우리가 지은 사유다. 시스템 코드는 없다 (`native_error`가 0이다).
    [[nodiscard]] http_error make_http_error(http_error_kind kind, std::u8string_view message);

    // 실제로 실을 요청 헤더다. 앱이 적은 헤더를 차례대로 두고, 몸이 있으면 Content-Type을 더한다.
    //  - 헤더 이름은 토큰 문자만, 값에는 줄바꿈과 NUL이 없어야 한다 (RFC 9110). 어기면
    //    `invalid_header`이고 아무것도 보내지 않는다.
    //  - 몸의 형식은 `content_type`이 말하고, 비어 있으면 앱이 `headers`에 직접 적은 Content-Type을
    //    존중한다. 둘 다 없을 때만 `application/octet-stream`이다.
    [[nodiscard]] bool collect_request_headers(const http_request& request, std::vector<http_header>& headers, http_error& failure);

    // 동사를 본다. 확장 메서드의 이름이 token이 아니거나, `extension`이 아닌데 이름이 있거나, 표준 동사·CONNECT를
    // 이름으로 적었으면 `invalid_method`이고 아무것도 보내지 않는다.
    [[nodiscard]] bool check_request_method(const http_request& request, http_error& failure);

    // 재지정(301·302·303·307·308)을 따라갈 때 다음 요청의 동사다. 따라가지 않을 걸음이면 nullopt다.
    //  - 303은 GET으로 바꾼다 (HEAD는 그대로). 301·302의 POST도 GET이다 (브라우저·WinHTTP와 같다).
    //  - 확장 메서드는 바꾸지 않는다. 303은 GET으로 다시 물으라는 뜻이라 확장 메서드는 그 걸음을 따라가지 않는다.
    //  - 몸 있는 요청은 이 함수와 무관하게 따라가지 않는다 (전송이 몸을 다시 실어 주지 않는다).
    [[nodiscard]] std::optional<http_method> redirected_method(http_method method, int status) noexcept;

    // 되풀이를 그만두게 하는 오류인가 (영원히 같은 답이 올 것들이다).
    [[nodiscard]] bool failure_is_permanent(http_error_kind kind) noexcept;

    // 진단 글에 넣을 십진수다.
    [[nodiscard]] std::u8string count_text(std::size_t value);
} // namespace luil::net
