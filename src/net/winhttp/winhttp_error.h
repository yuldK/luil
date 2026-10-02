#pragma once

#include "luil/net/http_message.h"

#include <string>
#include <string_view>

namespace luil::net {
    // 우리가 지은 사유에 시스템 코드를 붙인다 (`make_hresult_error`와 같은 자리이되
    // 꼬리가 `(WinHTTP=0x00002EE7)`이다).
    //  - `code`가 0이면 꼬리를 달지 않는다 — 시스템이 말한 것이 없다는 뜻이고,
    //    그때 `native_error`도 0이다.
    //  - 글은 사람이 아니라 개발자가 읽을 진단 영문이다 (`http_error::message`의 규약).
    [[nodiscard]] http_error make_http_error(http_error_kind kind, std::u8string_view message, unsigned long code);

    // WinHTTP·Win32 코드 하나를 오류 값으로 옮긴다 (http-client-design.md절의 표).
    //  - `secure_flags`는 앞서 `SECURE_FAILURE` 알림에서 받아 둔 깃발이다. TLS 실패는
    //    코드 하나로 왜인지 말하지 못해 그 깃발이 유일한 재료다.
    //  - `ERROR_WINHTTP_OPERATION_CANCELLED`는 여기서 `cancelled`로 온다. 걸쇠에 이미
    //    적힌 사유가 이기는 것은 부르는 쪽의 일이다 (`finish_and_close`가 그 걸쇠다).
    [[nodiscard]] http_error make_winhttp_error(unsigned long code, unsigned long secure_flags);

    // 보안 실패 깃발을 사람이 읽을 목록으로 푼다 (빈 깃발이면 빈 글이다).
    [[nodiscard]] std::u8string describe_secure_failure(unsigned long secure_flags);
} // namespace luil::net
