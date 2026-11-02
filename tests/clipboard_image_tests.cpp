#include "sample_image_bytes.h"

#include "luil/ui/clipboard_image.h"
#include "luil/ui/image_decode.h"
#include "ui/image_backing.h"

#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

// 클립보드 그림 바이트를 푸는 규칙이다 (image-clipboard-design.md). DIB 머리는 Windows 헤더 없이 바이트로 짓는다 —
// 이 test는 Android 기기에서도 돈다.
namespace {
    struct rgba
    {
        std::uint8_t red { 0 };
        std::uint8_t green { 0 };
        std::uint8_t blue { 0 };
        std::uint8_t alpha { 255 };

        [[nodiscard]] bool operator==(const rgba&) const noexcept = default;
    };

    constexpr std::uint32_t bi_rgb { 0 };
    constexpr std::uint32_t bi_bitfields { 3 };

    // 짓는 DIB의 모양이다. 마스크는 R·G·B·A 순이다.
    struct dib_spec
    {
        std::uint32_t header_size { 40 };
        int width { 0 };
        // 양수면 아래에서 위로 쌓는다.
        int height { 0 };
        std::uint16_t bit_count { 32 };
        std::uint32_t compression { bi_rgb };
        std::array<std::uint32_t, 4> masks {};
        // 8비트 팔레트 그림의 색이다 (그때 `pixels`의 빨강 칸이 색 번호다).
        std::vector<rgba> palette {};
    };

    void put_u16(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint16_t value)
    {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    }

    void put_u32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value)
    {
        for (std::size_t index = 0; index < 4; ++index)
            bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xFFu);
    }

    [[nodiscard]] std::uint32_t place(const std::uint8_t value, const std::uint32_t mask)
    {
        return mask == 0 ? 0u : (static_cast<std::uint32_t>(value) << std::countr_zero(mask)) & mask;
    }

    // 위에서 아래로 적은 픽셀로 DIB 블록을 짓는다. 행은 4바이트로 맞추고, 높이가 양수면 아래 행부터 적는다.
    [[nodiscard]] std::vector<std::uint8_t> make_dib(const dib_spec& spec, const std::vector<rgba>& pixels)
    {
        const bool trailing { spec.compression == bi_bitfields && spec.header_size == 40 };
        const std::size_t masks_bytes { trailing ? 12u : 0u };
        const std::size_t palette_bytes { spec.palette.size() * 4u };
        const std::size_t rows { static_cast<std::size_t>(std::abs(spec.height)) };
        const std::size_t width { static_cast<std::size_t>(spec.width) };
        const std::size_t stride { ((width * spec.bit_count + 31u) / 32u) * 4u };
        const std::size_t offset { spec.header_size + masks_bytes + palette_bytes };
        std::vector<std::uint8_t> bytes(offset + stride * rows, 0u);

        put_u32(bytes, 0, spec.header_size);
        put_u32(bytes, 4, static_cast<std::uint32_t>(spec.width));
        put_u32(bytes, 8, static_cast<std::uint32_t>(spec.height));
        put_u16(bytes, 12, 1);
        put_u16(bytes, 14, spec.bit_count);
        put_u32(bytes, 16, spec.compression);
        put_u32(bytes, 20, static_cast<std::uint32_t>(stride * rows));
        put_u32(bytes, 32, static_cast<std::uint32_t>(spec.palette.size()));
        const std::size_t mask_origin { trailing ? spec.header_size : 40u };
        if (spec.compression == bi_bitfields || spec.header_size >= 56)
            for (std::size_t index = 0; index < (spec.header_size >= 56 ? 4u : 3u); ++index)
                put_u32(bytes, mask_origin + index * 4u, spec.masks[index]);
        for (std::size_t index = 0; index < spec.palette.size(); ++index)
        {
            const std::size_t at { spec.header_size + masks_bytes + index * 4u };
            bytes[at] = spec.palette[index].blue;
            bytes[at + 1] = spec.palette[index].green;
            bytes[at + 2] = spec.palette[index].red;
        }

        for (std::size_t row = 0; row < rows; ++row)
        {
            const std::size_t stored_row { spec.height > 0 ? rows - 1u - row : row };
            for (std::size_t column = 0; column < width; ++column)
            {
                const rgba& pixel { pixels[row * width + column] };
                const std::size_t at { offset + stored_row * stride + column * spec.bit_count / 8u };
                if (spec.bit_count == 8)
                    bytes[at] = pixel.red;
                else if (spec.bit_count == 24)
                {
                    bytes[at] = pixel.blue;
                    bytes[at + 1] = pixel.green;
                    bytes[at + 2] = pixel.red;
                }
                else if (spec.compression == bi_rgb)
                {
                    bytes[at] = pixel.blue;
                    bytes[at + 1] = pixel.green;
                    bytes[at + 2] = pixel.red;
                    bytes[at + 3] = pixel.alpha;
                }
                else
                    put_u32(bytes, at,
                        place(pixel.red, spec.masks[0]) | place(pixel.green, spec.masks[1]) | place(pixel.blue, spec.masks[2]) | place(pixel.alpha, spec.masks[3]));
            }
        }
        return bytes;
    }

    [[nodiscard]] luil::clipboard_data dib_data(std::vector<std::uint8_t> bytes)
    {
        luil::clipboard_data data {};
        data.image_format = luil::clipboard_image_format::dib;
        data.image = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
        return data;
    }

    // 그림의 픽셀을 알파를 곱하지 않은 RGBA로 읽는다 (위에서 아래로).
    [[nodiscard]] std::vector<rgba> pixels_of(const luil::ui_image& image)
    {
        REQUIRE(image.valid());
        REQUIRE(image.backing() != nullptr);
        REQUIRE(image.backing()->image != nullptr);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(image.width()) * static_cast<std::size_t>(image.height()) * 4u);
        const SkImageInfo information { SkImageInfo::Make(image.width(), image.height(), kRGBA_8888_SkColorType, kUnpremul_SkAlphaType) };
        REQUIRE(image.backing()->image->readPixels(nullptr, information, bytes.data(), static_cast<std::size_t>(image.width()) * 4u, 0, 0));
        std::vector<rgba> pixels {};
        for (std::size_t index = 0; index < bytes.size(); index += 4u)
            pixels.push_back(rgba { bytes[index], bytes[index + 1], bytes[index + 2], bytes[index + 3] });
        return pixels;
    }

    [[nodiscard]] luil::ui_image decode(const luil::clipboard_data& data, luil::image_decode_error& error, const luil::image_decode_options& options = {})
    {
        return luil::decode_clipboard_image(data, options, error);
    }

    // 3×2 견본이다. 폭 3이라 24비트 행(9바이트)이 4바이트 정렬로 12바이트가 된다.
    const std::vector<rgba> opaque_sample {
        { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 },
        { 255, 255, 0, 255 }, { 0, 255, 255, 255 }, { 10, 20, 30, 255 },
    };

    // 표준 V5 마스크다 (Win32가 `CF_DIBV5`로 넣는 값).
    constexpr std::array<std::uint32_t, 4> v5_masks { 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u };
}

TEST_CASE("24-bit DIBs read the same pixels bottom-up and top-down with padded rows", "[clipboard][image]")
{
    for (const int height : { 2, -2 })
    {
        luil::image_decode_error error {};
        const luil::ui_image image { decode(dib_data(make_dib(dib_spec { .width = 3, .height = height, .bit_count = 24 }, opaque_sample)), error) };
        REQUIRE(error.empty());
        REQUIRE(image.width() == 3);
        REQUIRE(image.height() == 2);
        REQUIRE(pixels_of(image) == opaque_sample);
    }
}

TEST_CASE("32-bit BI_RGB and V5 bitfield DIBs read the same straight alpha pixels", "[clipboard][image]")
{
    // 투명한 자리는 색을 0으로 둔다 — 곱한 값으로 들고 있는 그림이 투명한 색을 되돌려 주지 않는다.
    std::vector<rgba> sample { opaque_sample };
    sample[1] = rgba { 0, 0, 0, 0 };
    const std::vector<std::vector<std::uint8_t>> blocks {
        make_dib(dib_spec { .width = 3, .height = 2 }, sample),
        make_dib(dib_spec { .width = 3, .height = -2 }, sample),
        make_dib(dib_spec { .header_size = 124, .width = 3, .height = 2, .compression = bi_bitfields, .masks = v5_masks }, sample),
        make_dib(dib_spec { .header_size = 124, .width = 3, .height = -2, .compression = bi_bitfields, .masks = v5_masks }, sample),
    };
    for (const std::vector<std::uint8_t>& block : blocks)
    {
        luil::image_decode_error error {};
        const luil::ui_image image { decode(dib_data(block), error) };
        REQUIRE(error.empty());
        REQUIRE(pixels_of(image) == sample);
    }
}

TEST_CASE("A half transparent V5 pixel keeps its straight color", "[clipboard][image]")
{
    const std::vector<rgba> sample { { 200, 100, 50, 128 } };
    luil::image_decode_error error {};
    const luil::ui_image image {
        decode(dib_data(make_dib(dib_spec { .header_size = 124, .width = 1, .height = 1, .compression = bi_bitfields, .masks = v5_masks }, sample)), error)
    };
    REQUIRE(error.empty());
    const rgba pixel { pixels_of(image).front() };
    // 그림은 곱한 값으로 들므로 되읽은 색이 한두 단계 어긋날 수 있다. 곱한 값(100·50·25)으로 읽었다면 크게 어긋난다.
    REQUIRE(std::abs(pixel.red - 200) <= 2);
    REQUIRE(std::abs(pixel.green - 100) <= 2);
    REQUIRE(std::abs(pixel.blue - 50) <= 2);
    REQUIRE(pixel.alpha == 128);
}

TEST_CASE("A 32-bit DIB whose alpha bytes are all zero is opaque", "[clipboard][image]")
{
    std::vector<rgba> sample { opaque_sample };
    for (rgba& pixel : sample)
        pixel.alpha = 0;
    for (const std::vector<std::uint8_t>& block : {
             make_dib(dib_spec { .width = 3, .height = 2 }, sample),
             make_dib(dib_spec { .header_size = 124, .width = 3, .height = 2, .compression = bi_bitfields, .masks = v5_masks }, sample),
         })
    {
        luil::image_decode_error error {};
        const luil::ui_image image { decode(dib_data(block), error) };
        REQUIRE(error.empty());
        REQUIRE(pixels_of(image) == opaque_sample);
    }
}

TEST_CASE("Bitfield masks after a 40-byte header pick the channels they name", "[clipboard][image]")
{
    // R·G·B를 높은 바이트부터 둔 순서다. 알파 마스크가 없으니 불투명이다.
    const dib_spec spec { .width = 3, .height = 2, .compression = bi_bitfields, .masks = { 0xFF000000u, 0x00FF0000u, 0x0000FF00u, 0u } };
    luil::image_decode_error error {};
    const luil::ui_image image { decode(dib_data(make_dib(spec, opaque_sample)), error) };
    REQUIRE(error.empty());
    REQUIRE(pixels_of(image) == opaque_sample);
}

TEST_CASE("A DIB shorter than its header says is refused", "[clipboard][image]")
{
    std::vector<std::uint8_t> block { make_dib(dib_spec { .width = 3, .height = 2, .bit_count = 24 }, opaque_sample) };
    block.pop_back();
    luil::image_decode_error error {};
    REQUIRE(decode(dib_data(block), error).valid() == false);
    REQUIRE(error.kind == luil::image_decode_error_kind::incomplete_input);

    // 머리보다 짧은 블록과 머리 크기를 부풀린 블록도 같다.
    luil::image_decode_error header_error {};
    REQUIRE(decode(dib_data(std::vector<std::uint8_t>(20, 0u)), header_error).valid() == false);
    REQUIRE(header_error.kind == luil::image_decode_error_kind::incomplete_input);
    std::vector<std::uint8_t> inflated { make_dib(dib_spec { .width = 3, .height = 2, .bit_count = 24 }, opaque_sample) };
    put_u32(inflated, 0, 4096u);
    luil::image_decode_error inflated_error {};
    REQUIRE(decode(dib_data(inflated), inflated_error).valid() == false);
    REQUIRE(inflated_error.kind == luil::image_decode_error_kind::incomplete_input);
}

TEST_CASE("A DIB with a zero or negative side is refused", "[clipboard][image]")
{
    for (const auto& [width, height] : { std::pair { 0, 2 }, std::pair { -3, 2 }, std::pair { 3, 0 } })
    {
        std::vector<std::uint8_t> block { make_dib(dib_spec { .width = 3, .height = 2, .bit_count = 24 }, opaque_sample) };
        put_u32(block, 4, static_cast<std::uint32_t>(width));
        put_u32(block, 8, static_cast<std::uint32_t>(height));
        luil::image_decode_error error {};
        REQUIRE(decode(dib_data(block), error).valid() == false);
        REQUIRE(error.kind == luil::image_decode_error_kind::no_pixels);
    }
}

TEST_CASE("The pixel limit refuses a DIB before its pixels are read", "[clipboard][image]")
{
    // 앱이 건 상한이다.
    luil::image_decode_options options {};
    options.max_source_pixels = 4;
    luil::image_decode_error error {};
    REQUIRE(decode(dib_data(make_dib(dib_spec { .width = 3, .height = 2, .bit_count = 24 }, opaque_sample)), error, options).valid() == false);
    REQUIRE(error.kind == luil::image_decode_error_kind::too_many_pixels);

    // 머리만 거대한 블록은 자료가 모자란 것보다 상한에 먼저 걸린다 — 크기를 믿고 메모리를 잡지 않는다.
    std::vector<std::uint8_t> block { make_dib(dib_spec { .width = 3, .height = 2, .bit_count = 24 }, opaque_sample) };
    put_u32(block, 4, 100000u);
    put_u32(block, 8, 100000u);
    luil::image_decode_error huge_error {};
    REQUIRE(decode(dib_data(block), huge_error).valid() == false);
    REQUIRE(huge_error.kind == luil::image_decode_error_kind::too_many_pixels);
}

TEST_CASE("An 8-bit palette DIB is read through the BMP codec", "[clipboard][image]")
{
    const dib_spec spec { .width = 3, .height = 2, .bit_count = 8, .palette = { { 255, 0, 0 }, { 0, 0, 255 } } };
    // 빨강 칸이 색 번호다.
    const std::vector<rgba> indices { { 0 }, { 1 }, { 0 }, { 1 }, { 1 }, { 0 } };
    luil::image_decode_error error {};
    const luil::ui_image image { decode(dib_data(make_dib(spec, indices)), error) };
    REQUIRE(error.empty());
    REQUIRE(image.width() == 3);
    REQUIRE(image.height() == 2);
    const rgba red { 255, 0, 0, 255 };
    const rgba blue { 0, 0, 255, 255 };
    REQUIRE(pixels_of(image) == std::vector<rgba> { red, blue, red, blue, blue, red });
}

TEST_CASE("Encoded clipboard bytes decode like image bytes", "[clipboard][image]")
{
    luil::clipboard_data data {};
    data.image_format = luil::clipboard_image_format::encoded;
    data.image = std::make_shared<const std::vector<std::uint8_t>>(luil::testing::sample_quadrant_png.begin(), luil::testing::sample_quadrant_png.end());
    luil::image_decode_error error {};
    const luil::ui_image image { decode(data, error) };
    REQUIRE(error.empty());
    luil::image_decode_error direct_error {};
    const luil::ui_image direct { luil::decode_image_bytes(luil::testing::sample_quadrant_png, direct_error) };
    REQUIRE(direct_error.empty());
    REQUIRE(pixels_of(image) == pixels_of(direct));
}

TEST_CASE("No clipboard image is an empty image and an error", "[clipboard][image]")
{
    luil::image_decode_error error {};
    REQUIRE(decode(luil::clipboard_data {}, error).valid() == false);
    REQUIRE(error.kind == luil::image_decode_error_kind::file_empty);

    luil::clipboard_data large {};
    large.image_format = luil::clipboard_image_format::encoded;
    large.image_too_large = true;
    luil::image_decode_error large_error {};
    REQUIRE(decode(large, large_error).valid() == false);
    REQUIRE(large_error.kind == luil::image_decode_error_kind::input_too_large);
}

TEST_CASE("A clipboard DIB is scaled down to the decode bounds after it is read", "[clipboard][image]")
{
    const std::vector<rgba> solid(16, rgba { 40, 80, 120, 255 });
    luil::image_decode_options options {};
    options.max_width = 2;
    options.max_height = 2;
    luil::image_decode_error error {};
    const luil::ui_image image { decode(dib_data(make_dib(dib_spec { .width = 4, .height = 4, .bit_count = 24 }, solid)), error, options) };
    REQUIRE(error.empty());
    REQUIRE(image.width() == 2);
    REQUIRE(image.height() == 2);
    REQUIRE(pixels_of(image) == std::vector<rgba>(4, rgba { 40, 80, 120, 255 }));
}

TEST_CASE("Clipboard copy requests compare their image by value identity", "[clipboard][image]")
{
    const std::vector<std::uint8_t> pixel { 1, 2, 3, 255 };
    const luil::ui_image image { luil::make_rgba_image(1, 1, pixel) };
    const luil::clipboard_data_copy_request request { u8"name", image, { 1, 2 } };
    REQUIRE(request == luil::clipboard_data_copy_request { u8"name", image, { 1, 2 } });
    // 같은 픽셀이라도 따로 지은 그림은 다른 값이다.
    REQUIRE((request == luil::clipboard_data_copy_request { u8"name", luil::make_rgba_image(1, 1, pixel), { 1, 2 } }) == false);
    REQUIRE((request == luil::clipboard_data_copy_request { u8"other", image, { 1, 2 } }) == false);
}
