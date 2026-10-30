#include "luil/ui/image_encode.h"

#include "ui/image_pixels.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkData.h"
#include "include/encode/SkJpegEncoder.h"

#include <algorithm>

// JPEG 인코더다. 이 파일만 `SkJpegEncoder`를 부른다 (image_encode.h — 부른 형식만 링크한다).
namespace luil {
    std::vector<std::uint8_t> encode_jpeg(const ui_image& image, const int quality, std::u8string& error)
    {
        SkBitmap pixels {};
        if (read_image_pixels(image, pixels, error) == false)
            return {};
        SkJpegEncoder::Options options {};
        options.fQuality = std::clamp(quality, 0, 100);
        // 알파가 없는 형식이다. 검정 위에 합성한다고 **적어 둔다** — 기본값(kIgnore)도
        // premul 픽셀에서는 같은 그림이지만, 그것은 우연이지 계약이 아니다.
        options.fAlphaOption = SkJpegEncoder::AlphaOption::kBlendOnBlack;
        const sk_sp<SkData> data { SkJpegEncoder::Encode(pixels.pixmap(), options) };
        return encoded_bytes(data.get(), error);
    }
}
