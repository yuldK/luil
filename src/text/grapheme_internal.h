#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace luil::text {
    // Backspace만 결합 부호를 한 코드포인트씩 벗기는 예외를 가진다.
    [[nodiscard]] std::size_t grapheme_backspace_offset(std::u8string_view text, std::size_t offset) noexcept;
    // 내장 표(UAX #29, 생성 데이터)로만 모은 경계다. 플랫폼 경계가 있는 구성에서도 내장 표를 시험하는 길이다.
    [[nodiscard]] std::vector<std::size_t> builtin_grapheme_boundaries(std::u8string_view text);
#if defined(LUIL_GRAPHEME_SKIA)
    [[nodiscard]] std::optional<std::vector<std::size_t>> platform_grapheme_boundaries(std::u8string_view text);
    [[nodiscard]] std::optional<std::size_t> platform_grapheme_boundary(std::u8string_view text, std::size_t offset, bool forward) noexcept;
#endif
}
