#pragma once

#include <string_view>

namespace luil::net {
    // utf-16 갈래의 자리표다. Windows의 코드 페이지 번호이기는 하지만
    // `MultiByteToWideChar`가 받지 않는 둘이라 (`ERROR_INVALID_PARAMETER`)
    // `codepage_text_transcoder`가 손으로 옮긴다.
    inline constexpr unsigned int utf16le_code_page { 1200u };
    inline constexpr unsigned int utf16be_code_page { 1201u };

    // charset 이름 하나를 코드 페이지 번호로 옮긴다. 모르는 이름은 0이다.
    //
    // 표가 여기 있는 이유는 이것이 **표의 전부**이기 때문이다 — 실제 변환은 OS가
    // 하고 우리는 이름만 안다 (http-client-design.md). 이름은 밖에서 오는
    // 값이라 대소문자와 앞뒤 공백을 가리지 않는다.
    //  - 헤더로 내놓는 것은 test가 표만 따로 물을 수 있게 하려는 것이다. 변환
    //    자체를 물으려면 서버도 몸도 필요 없는 `codepage_text_transcoder()`가 있다.
    [[nodiscard]] unsigned int charset_code_page(std::u8string_view charset) noexcept;
} // namespace luil::net
