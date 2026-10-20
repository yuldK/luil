#include "luil/ui/image_encode.h"

#include "ui/image_pixels.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkData.h"
#include "include/encode/SkWebpEncoder.h"

#include <algorithm>

// WebP 인코더다. 화면 캡처가 이미 같은 인코더를 쓰므로 Windows 앱에서는 늘 링크되어 있다.
namespace luil {
    namespace {
        [[nodiscard]] std::vector<std::uint8_t> encode_webp_with(const ui_image& image, const SkWebpEncoder::Options& options, std::u8string& error)
        {
            SkBitmap pixels {};
            if (read_image_pixels(image, pixels, error) == false)
                return {};
            const sk_sp<SkData> data { SkWebpEncoder::Encode(pixels.pixmap(), options) };
            return encoded_bytes(data.get(), error);
        }
    } // namespace

    std::vector<std::uint8_t> encode_webp(const ui_image& image, const int quality, std::u8string& error)
    {
        SkWebpEncoder::Options options {};
        options.fCompression = SkWebpEncoder::Compression::kLossy;
        options.fQuality = static_cast<float>(std::clamp(quality, 0, 100));
        return encode_webp_with(image, options, error);
    }

    std::vector<std::uint8_t> encode_webp_lossless(const ui_image& image, std::u8string& error)
    {
        // 무손실에서 품질은 "얼마나 애써 줄이는가"다. 기본값(100)이 가장 작게 담는다.
        SkWebpEncoder::Options options {};
        options.fCompression = SkWebpEncoder::Compression::kLossless;
        return encode_webp_with(image, options, error);
    }
} // namespace luil
