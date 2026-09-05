#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace luil::win32 {
    enum class utf_conversion_error_kind
    {
        invalid_input,
        system_error,
    };

    struct utf_conversion_error
    {
        utf_conversion_error_kind kind { utf_conversion_error_kind::system_error };
        unsigned long native_error { 0 };
    };

    template<typename value_type>
    struct utf_conversion_result
    {
        std::optional<value_type> value {};
        std::optional<utf_conversion_error> error {};
    };

    // UTF-16과 UTF-8을 오간다.
    // 실패는 예외가 아니라 값이다 — 잘못된 입력은 `invalid_input`, Win32 변환
    // 실패는 `system_error`로 돌아온다. 출력 버퍼를 잡지 못한 것(OOM)도
    // `system_error`(`ERROR_NOT_ENOUGH_MEMORY`)다. noexcept 밖으로 나가는 예외는 없다.
    [[nodiscard]] utf_conversion_result<std::u8string> utf16_to_utf8(std::wstring_view input) noexcept;
    [[nodiscard]] utf_conversion_result<std::wstring> utf8_to_utf16(std::u8string_view input) noexcept;

    // UTF-16 코드 단위를 하나씩 받아 codepoint로 모은다.
    // `WM_CHAR`가 BMP 밖 문자를 surrogate **쌍**으로 두 번에 나눠 보내기 때문에 필요하다.
    //  - 이모지 패널 (Win+.)이 이 경로다.
    //
    // 앞쪽 surrogate면 `pending`에 남기고 `nullopt`를 돌려준다.
    // 뒤쪽이 오면 합쳐 돌려준다.
    // 짝이 맞지 않는 조각은 **버린다**.
    //  - 잘못된 UTF-8을 만들지 않는 것이 계약이다.
    [[nodiscard]] std::optional<char32_t> merge_utf16_code_unit(std::optional<char16_t>& pending, char16_t code_unit) noexcept;
} // namespace luil::win32
