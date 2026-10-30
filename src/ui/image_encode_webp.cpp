#include "luil/ui/image_encode.h"

#include "ui/image_backing.h"
#include "ui/image_pixels.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkData.h"
#include "include/encode/SkWebpEncoder.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

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

        // 품질 표를 0–100으로 자른다. 비면 기본 표다.
        [[nodiscard]] std::vector<int> quality_steps(const std::vector<int>& requested)
        {
            std::vector<int> steps {};
            for (const int quality : requested)
                steps.push_back(std::clamp(quality, 0, 100));
            if (steps.empty())
                steps = webp_size_limit_options {}.qualities;
            return steps;
        }

        void append_number(std::u8string& text, const std::size_t value)
        {
            const std::string digits { std::to_string(value) };
            text.append(digits.begin(), digits.end());
        }

        [[nodiscard]] webp_size_limit_result cancelled_result(webp_size_limit_result result, std::u8string& error)
        {
            result.bytes.clear();
            result.cancelled = true;
            error = u8"The size-limited WebP encode was cancelled.";
            return result;
        }
    }

    std::vector<std::uint8_t> encode_webp(const ui_image& image, const int quality, std::u8string& error)
    {
        SkWebpEncoder::Options options {};
        options.fCompression = SkWebpEncoder::Compression::kLossy;
        options.fQuality = static_cast<float>(std::clamp(quality, 0, 100));
        return encode_webp_with(image, options, error);
    }

    webp_size_limit_result encode_webp_within(const ui_image& image, const webp_size_limit_options& options, std::u8string& error)
    {
        webp_size_limit_result result {};
        // 벡터 그림은 다른 인코더처럼 제 크기로 한 번 편 픽셀에서 시작한다 — 축소는 픽셀이 있어야 한다.
        ui_image source { image };
        if (image.valid() == false || image.backing()->image == nullptr)
        {
            SkBitmap pixels {};
            if (read_image_pixels(image, pixels, error) == false)
                return result;
            pixels.setImmutable();
            source = image_access::raster_image(pixels.asImage());
        }
        const std::vector<int> qualities { quality_steps(options.qualities) };
        const float shrink { options.shrink > 0.0f && options.shrink < 1.0f ? options.shrink : 0.75f };
        const auto asked_to_stop = [&options] { return options.cancelled != nullptr && options.cancelled(); };

        ui_image current { source };
        float factor { 1.0f };
        std::size_t last_bytes { 0 };
        for (;;)
        {
            for (const int quality : qualities)
            {
                if (asked_to_stop())
                    return cancelled_result(std::move(result), error);
                std::vector<std::uint8_t> bytes { encode_webp(current, quality, error) };
                ++result.attempts;
                if (bytes.empty())
                    return result;
                if (options.maximum_bytes == 0 || bytes.size() <= options.maximum_bytes)
                {
                    result.bytes = std::move(bytes);
                    result.quality = quality;
                    result.size = { current.width(), current.height() };
                    return result;
                }
                last_bytes = bytes.size();
            }

            // 표 끝으로도 넘친다. 원본에서 새로 줄여 표를 처음부터 다시 돈다 — 줄인 것을 다시 줄이면 흐림이 쌓이고,
            // 줄인 그림은 높은 품질로도 드는 일이 흔하다.
            factor *= shrink;
            const int width { std::max(1, static_cast<int>(std::lround(static_cast<float>(source.width()) * factor))) };
            const int height { std::max(1, static_cast<int>(std::lround(static_cast<float>(source.height()) * factor))) };
            if (std::max(width, height) < options.minimum_side)
            {
                error = u8"The image does not fit in ";
                append_number(error, options.maximum_bytes);
                error += u8" bytes even at ";
                append_number(error, static_cast<std::size_t>(current.width()));
                error += u8"x";
                append_number(error, static_cast<std::size_t>(current.height()));
                error += u8" and quality ";
                append_number(error, static_cast<std::size_t>(qualities.back()));
                error += u8" (";
                append_number(error, last_bytes);
                error += u8" bytes).";
                return result;
            }
            image_decode_options smaller { .max_width = width, .max_height = height };
            smaller.cancelled = options.cancelled;
            image_decode_error downscale_error {};
            current = downscale_image(source, smaller, downscale_error);
            if (downscale_error.kind == image_decode_error_kind::cancelled)
                return cancelled_result(std::move(result), error);
            if (current.valid() == false)
            {
                error = downscale_error.message;
                return result;
            }
        }
    }

    std::vector<std::uint8_t> encode_webp_lossless(const ui_image& image, std::u8string& error)
    {
        // 무손실에서 품질은 "얼마나 애써 줄이는가"다. 기본값(100)이 가장 작게 담는다.
        SkWebpEncoder::Options options {};
        options.fCompression = SkWebpEncoder::Compression::kLossless;
        return encode_webp_with(image, options, error);
    }
}
