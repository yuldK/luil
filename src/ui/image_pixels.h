#pragma once

#include "luil/ui/image_element.h"

#include "include/core/SkBitmap.h"

#include <cstdint>
#include <string>
#include <vector>

class SkData;

namespace luil {
    // 인코더에 넘길 픽셀이다 (`image_encode_*.cpp`가 함께 쓴다).
    //
    // 래스터 그림은 그 픽셀을 복사하고, 벡터 그림(SVG)은 자기 크기로 투명 바탕에 한 번
    // 편다. 결과는 N32 premul sRGB다 — 인코더가 형식에 맞게 다시 옮긴다.
    // 실패하면 거짓과 `error`다.
    [[nodiscard]] bool read_image_pixels(const ui_image& image, SkBitmap& pixels, std::u8string& error);

    // 인코더가 낸 바이트를 공개 API의 값으로 옮긴다. null이면 빈 값과 `error`다.
    [[nodiscard]] std::vector<std::uint8_t> encoded_bytes(const SkData* data, std::u8string& error);
}
