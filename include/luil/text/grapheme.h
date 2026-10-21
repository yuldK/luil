#pragma once

#include <cstddef>
#include <string_view>

namespace luil::text {
    // UTF-8 byte offset의 앞·뒤 글자소 경계다. 범위 밖은 문자열 범위로 다듬는다.
    // 조합 중의 caret은 IME가 소유하므로 이 함수로 다시 맞추지 않는다.
    [[nodiscard]] std::size_t grapheme_previous(std::u8string_view text, std::size_t offset) noexcept;
    [[nodiscard]] std::size_t grapheme_next(std::u8string_view text, std::size_t offset) noexcept;
} // namespace luil::text
