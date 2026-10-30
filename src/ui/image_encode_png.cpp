#include "luil/ui/image_encode.h"

#include "ui/image_pixels.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkData.h"
#include "include/encode/SkPngRustEncoder.h"

// PNG 인코더다. 이 파일만 `SkPngRustEncoder`를 부른다 (image_encode.h — 부른 형식만 링크한다).
namespace luil {
    std::vector<std::uint8_t> encode_png(const ui_image& image, std::u8string& error)
    {
        SkBitmap pixels {};
        if (read_image_pixels(image, pixels, error) == false)
            return {};
        const sk_sp<SkData> data { SkPngRustEncoder::Encode(pixels.pixmap(), SkPngRustEncoder::Options {}) };
        return encoded_bytes(data.get(), error);
    }
}
