#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace luil {
    // GDEF LigCaretList의 design-unit 좌표다. 표가 없거나 손상됐거나 outline 점/장치 보정이 필요하면 빈 값이다.
    // 폰트 바이트의 길이를 넘는 offset·개수는 읽지 않는다.
    [[nodiscard]] std::vector<std::int16_t> ligature_caret_coordinates(std::span<const std::uint8_t> table, std::uint16_t glyph);
}
