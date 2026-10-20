#include "luil/ui/image_encode.h"

#include "raster_probe.h"
#include "ui/image_backing.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/image_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPictureRecorder.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 인코더는 **다시 읽어 보는 것**으로 확인한다 (docs/image-encode-design.md).
// 바이트를 견본과 견주면 인코더의 판번 하나에 test가 깨지고, 그것은 우리 계약이 아니다.
// 같은 빌드의 디코더로 읽어 그린 픽셀이 처음 그림과 같은지(무손실), 가까운지(손실)를 본다.
namespace {
    // 사분면 색이 다른 2×2 그림이다. 오른쪽 아래는 반투명 흰색이라 알파가 살아남는지도 본다.
    [[nodiscard]] luil::ui_image make_quadrant_image()
    {
        const std::vector<std::uint8_t> pixels { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128 };
        return luil::make_rgba_image(2, 2, pixels);
    }

    // 단색 8×8 그림이다 (손실 형식의 견본 — 8×8 블록 하나라 번지는 이웃이 없다).
    [[nodiscard]] luil::ui_image make_solid_image(const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue)
    {
        std::vector<std::uint8_t> pixels {};
        for (int index = 0; index < 64; ++index)
        {
            pixels.push_back(red);
            pixels.push_back(green);
            pixels.push_back(blue);
            pixels.push_back(255);
        }
        return luil::make_rgba_image(8, 8, pixels);
    }

    // 그림을 제 크기 그대로 그려 픽셀을 답한다 (1:1이라 표본이 이웃을 섞지 않는다).
    // 바탕은 팔레트의 창 배경이라, 반투명 픽셀은 그 위에 합성된 값으로 온다 — 그래서 비교는
    // 언제나 두 그림을 **같은 바탕에** 그린 것끼리 한다.
    [[nodiscard]] std::vector<luil::ui_color> raster_pixels(const luil::ui_image& image)
    {
        const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
        auto picture { std::make_unique<luil::image_element>(luil::ui_element_id { luil::ui_element_kind::root }, luil::image_config { .image = image, .fit = luil::image_fit::fill }) };
        picture->arrange({ { 0.0f, 0.0f, static_cast<float>(image.width()), static_cast<float>(image.height()) }, 1.0f });
        const luil::ui_tree tree { std::move(picture) };
        luil::testing::raster_frame frame { image.width(), image.height(), palette, 1.0f };
        frame.draw(tree, luil::interaction_snapshot {});
        std::vector<luil::ui_color> pixels {};
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                pixels.push_back(frame.pixel_at(x, y));
        return pixels;
    }

    [[nodiscard]] luil::ui_image decode(const std::vector<std::uint8_t>& bytes)
    {
        luil::image_decode_error error {};
        return luil::decode_image_bytes(bytes, error);
    }

    [[nodiscard]] std::uint32_t channel_of(const luil::ui_color color, const int shift) noexcept
    {
        return (static_cast<std::uint32_t>(color) >> shift) & 0xFFu;
    }

    [[nodiscard]] bool is_red(const luil::ui_color color) noexcept
    {
        return channel_of(color, 16) > 200u && channel_of(color, 8) < 60u && channel_of(color, 0) < 60u;
    }

    [[nodiscard]] bool starts_with(const std::vector<std::uint8_t>& bytes, const std::vector<std::uint8_t>& signature)
    {
        return bytes.size() >= signature.size() && std::equal(signature.begin(), signature.end(), bytes.begin());
    }
} // namespace

TEST_CASE("A png keeps every pixel and the alpha", "[ui][image][encode]")
{
    const luil::ui_image source { make_quadrant_image() };
    std::u8string error { u8"untouched" };
    const std::vector<std::uint8_t> bytes { luil::encode_png(source, error) };
    REQUIRE(bytes.empty() == false);
    // 성공하면 오류를 건드리지 않는다.
    REQUIRE(error == u8"untouched");
    REQUIRE(starts_with(bytes, { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A }));

    const luil::ui_image decoded { decode(bytes) };
    REQUIRE(decoded.valid());
    REQUIRE(decoded.width() == 2);
    REQUIRE(decoded.height() == 2);
    REQUIRE(raster_pixels(decoded) == raster_pixels(source));
}

TEST_CASE("A lossless webp keeps every pixel", "[ui][image][encode]")
{
    const luil::ui_image source { make_quadrant_image() };
    std::u8string error {};
    const std::vector<std::uint8_t> bytes { luil::encode_webp_lossless(source, error) };
    REQUIRE(bytes.empty() == false);
    REQUIRE(starts_with(bytes, { 'R', 'I', 'F', 'F' }));
    REQUIRE(raster_pixels(decode(bytes)) == raster_pixels(source));
}

TEST_CASE("Lossy formats keep the picture close", "[ui][image][encode]")
{
    const luil::ui_image source { make_solid_image(255, 0, 0) };
    std::u8string error {};

    const std::vector<std::uint8_t> jpeg { luil::encode_jpeg(source, 90, error) };
    REQUIRE(starts_with(jpeg, { 0xFF, 0xD8, 0xFF }));
    for (const luil::ui_color pixel : raster_pixels(decode(jpeg)))
        REQUIRE(is_red(pixel));

    const std::vector<std::uint8_t> webp { luil::encode_webp(source, 80, error) };
    REQUIRE(starts_with(webp, { 'R', 'I', 'F', 'F' }));
    for (const luil::ui_color pixel : raster_pixels(decode(webp)))
        REQUIRE(is_red(pixel));

    // 범위 밖의 품질은 끝으로 자른다 — 실패가 아니다.
    REQUIRE(luil::encode_jpeg(source, 400, error).empty() == false);
    REQUIRE(luil::encode_webp(source, -3, error).empty() == false);
}

TEST_CASE("A vector image is encoded at its own size", "[ui][image][encode][vector]")
{
    // SVG가 내는 값과 같은 모양이다 — 왼쪽 절반 빨강, 오른쪽 절반 파랑의 4×2 기록.
    SkPictureRecorder recorder {};
    SkCanvas* const canvas { recorder.beginRecording(SkRect::MakeWH(4.0f, 2.0f)) };
    SkPaint paint {};
    paint.setColor(SK_ColorRED);
    canvas->drawRect(SkRect::MakeWH(2.0f, 2.0f), paint);
    paint.setColor(SK_ColorBLUE);
    canvas->drawRect(SkRect::MakeXYWH(2.0f, 0.0f, 2.0f, 2.0f), paint);
    const luil::ui_image vector { luil::image_access::picture_image(recorder.finishRecordingAsPicture(), 4, 2) };

    std::u8string error {};
    const luil::ui_image decoded { decode(luil::encode_png(vector, error)) };
    REQUIRE(decoded.valid());
    REQUIRE(decoded.width() == 4);
    REQUIRE(decoded.height() == 2);
    const std::vector<luil::ui_color> pixels { raster_pixels(decoded) };
    REQUIRE(pixels[0] == luil::ui_color { 0xFFFF0000 });
    REQUIRE(pixels[3] == luil::ui_color { 0xFF0000FF });
}

TEST_CASE("An empty image is not encoded", "[ui][image][encode]")
{
    std::u8string error {};
    REQUIRE(luil::encode_png(luil::ui_image {}, error).empty());
    REQUIRE(error.empty() == false);
    error.clear();
    REQUIRE(luil::encode_jpeg(luil::ui_image {}, 90, error).empty());
    REQUIRE(error.empty() == false);
}
