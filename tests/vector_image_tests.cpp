#include "luil/ui/image_decode.h"

#include "raster_probe.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/image_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

// SVG·Lottie 진입점이다 (docs/vector-image-design.md).
//
// 켠 구성은 실제 Skia 모듈로 읽어 그린 픽셀을 보고, 끈 구성은 "지원 안 함"으로 답하는지를 본다.
// 재생 시각·칸 맞추기는 장면을 무엇이 그리든 같은 계약이라 `image_animation_tests`가 Skottie
// 없이 잠그고, 여기는 **파일이 그 값이 되는가**만 본다.
namespace {
    using namespace std::chrono_literals;

    [[nodiscard]] std::span<const std::uint8_t> bytes_of(const std::string_view text) noexcept
    {
        return { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() };
    }

    // 왼쪽 절반이 빨강, 오른쪽 절반이 파랑인 SVG다. 크기는 `viewBox`만 적었다 — 아이콘 SVG의
    // 흔한 모양이고, 고유 크기를 `viewBox`에서 채우는 갈래를 함께 본다.
    constexpr std::string_view split_svg {
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 2 2">)"
        R"(<rect width="1" height="2" fill="#ff0000"/>)"
        R"(<rect x="1" width="1" height="2" fill="#0000ff"/>)"
        R"(</svg>)",
    };

    // 1초짜리, 초당 10장, 4×4 Lottie다. 앞 절반(0–5장)은 빨강, 뒤 절반(5–10장)은 초록인
    // 단색 레이어 둘이 차례로 선다.
    constexpr std::string_view half_and_half_lottie {
        R"({"v":"5.7.0","fr":10,"ip":0,"op":10,"w":4,"h":4,"layers":[)"
        R"({"ty":1,"ind":1,"sc":"#ff0000","sw":4,"sh":4,"ip":0,"op":5,"st":0,)"
        R"("ks":{"o":{"a":0,"k":100},"p":{"a":0,"k":[0,0,0]},"a":{"a":0,"k":[0,0,0]},"s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0}}},)"
        R"({"ty":1,"ind":2,"sc":"#00ff00","sw":4,"sh":4,"ip":5,"op":10,"st":0,)"
        R"("ks":{"o":{"a":0,"k":100},"p":{"a":0,"k":[0,0,0]},"a":{"a":0,"k":[0,0,0]},"s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0}}})"
        R"(]})",
    };

#if LUIL_TESTS_SVG || LUIL_TESTS_LOTTIE
    [[maybe_unused]] constexpr luil::ui_color colour_red { 0xFFFF0000 };
    [[maybe_unused]] constexpr luil::ui_color colour_green { 0xFF00FF00 };
    [[maybe_unused]] constexpr luil::ui_color colour_blue { 0xFF0000FF };

    // 이미지 하나가 tree의 root다 (image_decode_tests와 같은 그리기).
    [[nodiscard]] luil::testing::raster_frame draw_config(const luil::image_config& config, const int size, const std::chrono::steady_clock::time_point now = {})
    {
        const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
        auto picture { std::make_unique<luil::image_element>(luil::ui_element_id { luil::ui_element_kind::root }, config) };
        picture->arrange({ { 0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size) }, 1.0f });
        const luil::ui_tree tree { std::move(picture) };
        luil::testing::raster_frame frame { size, size, palette, 1.0f };
        frame.draw(tree, luil::interaction_snapshot {}, now);
        return frame;
    }
#endif
} // namespace

#if LUIL_TESTS_SVG
TEST_CASE("An svg decodes into a vector image drawn sharply at any size", "[ui][image][svg][raster]")
{
    luil::image_decode_error error {};
    const luil::ui_image image { luil::decode_svg_bytes(bytes_of(split_svg), error) };
    REQUIRE(image.valid());
    REQUIRE(error.empty());
    // `viewBox`의 크기다.
    REQUIRE(image.width() == 2);
    REQUIRE(image.height() == 2);

    // 2×2 그림을 32×32 칸에 채운다. 벡터라 경계(x = 16)가 섞이지 않는다.
    luil::image_config config {};
    config.image = image;
    config.fit = luil::image_fit::fill;
    const luil::testing::raster_frame frame { draw_config(config, 32) };
    REQUIRE(frame.count_color({ 0.0f, 0.0f, 16.0f, 32.0f }, colour_red) == 16 * 32);
    REQUIRE(frame.count_color({ 16.0f, 0.0f, 16.0f, 32.0f }, colour_blue) == 16 * 32);
}

TEST_CASE("An svg takes its size from width and height before the viewBox", "[ui][image][svg]")
{
    luil::image_decode_error error {};
    constexpr std::string_view sized { R"(<svg xmlns="http://www.w3.org/2000/svg" width="40" height="20" viewBox="0 0 2 1"><rect width="2" height="1" fill="#00ff00"/></svg>)" };
    const luil::ui_image image { luil::decode_svg_bytes(bytes_of(sized), error) };
    REQUIRE(image.valid());
    REQUIRE(image.width() == 40);
    REQUIRE(image.height() == 20);

    // 한 변만 적으면 다른 변은 `viewBox`의 비율이다.
    constexpr std::string_view half_sized { R"(<svg xmlns="http://www.w3.org/2000/svg" width="30" viewBox="0 0 3 1"><rect width="3" height="1"/></svg>)" };
    const luil::ui_image derived { luil::decode_svg_bytes(bytes_of(half_sized), error) };
    REQUIRE(derived.valid());
    REQUIRE(derived.width() == 30);
    REQUIRE(derived.height() == 10);

    // 상한은 그 크기를 비율대로 줄인다 (그림 자신의 좌표계가 줄어든다).
    const luil::ui_image limited { luil::decode_svg_bytes(bytes_of(sized), { .max_width = 10 }, error) };
    REQUIRE(limited.valid());
    REQUIRE(limited.width() == 10);
    REQUIRE(limited.height() == 5);
}

TEST_CASE("An svg without a size or that is not svg fails by name", "[ui][image][svg]")
{
    luil::image_decode_error error {};
    constexpr std::string_view sizeless { R"(<svg xmlns="http://www.w3.org/2000/svg"><rect width="1" height="1"/></svg>)" };
    REQUIRE(luil::decode_svg_bytes(bytes_of(sizeless), error).valid() == false);
    REQUIRE(error.kind == luil::image_decode_error_kind::no_pixels);

    error = {};
    REQUIRE(luil::decode_svg_bytes(bytes_of("this is not xml"), error).valid() == false);
    REQUIRE(error.kind == luil::image_decode_error_kind::unsupported_format);

    // 접힌 디코딩은 빈 값과 `cancelled`다 (래스터 진입점과 같은 규칙이다).
    error = {};
    const luil::image_decode_options cancelled { .cancelled = [] { return true; } };
    REQUIRE(luil::decode_svg_bytes(bytes_of(split_svg), cancelled, error).valid() == false);
    REQUIRE(error.kind == luil::image_decode_error_kind::cancelled);
}
#else
TEST_CASE("An svg is refused by name when the build leaves it out", "[ui][image][svg]")
{
    luil::image_decode_error error {};
    REQUIRE(luil::decode_svg_bytes(bytes_of(split_svg), error).valid() == false);
    REQUIRE(error.kind == luil::image_decode_error_kind::unsupported_format);
}
#endif

#if LUIL_TESTS_LOTTIE
TEST_CASE("A Lottie decodes into a scene that changes with the playback clock", "[ui][image][lottie][raster]")
{
    luil::image_decode_error error {};
    const luil::ui_lottie scene { luil::decode_lottie_bytes(bytes_of(half_and_half_lottie), error) };
    REQUIRE(scene.valid());
    REQUIRE(error.empty());
    REQUIRE(scene.width() == 4);
    REQUIRE(scene.height() == 4);
    REQUIRE(scene.duration() == 1s);
    REQUIRE(scene.frame_rate() == 10.0);

    luil::image_config config {};
    config.lottie = scene;
    config.fit = luil::image_fit::fill;
    config.playback.started = std::chrono::steady_clock::time_point {};
    const std::chrono::steady_clock::time_point origin {};
    // 앞 절반은 빨강 레이어, 뒤 절반은 초록 레이어다. 4×4 장면을 16×16 칸에 채운다.
    REQUIRE(draw_config(config, 16, origin + 100ms).count_color({ 0.0f, 0.0f, 16.0f, 16.0f }, colour_red) == 16 * 16);
    REQUIRE(draw_config(config, 16, origin + 700ms).count_color({ 0.0f, 0.0f, 16.0f, 16.0f }, colour_green) == 16 * 16);
}

TEST_CASE("Bytes that are not a Lottie fail by name", "[ui][image][lottie]")
{
    luil::image_decode_error error {};
    REQUIRE(luil::decode_lottie_bytes(bytes_of("{\"not\":\"lottie\"}"), error).valid() == false);
    REQUIRE(error.kind == luil::image_decode_error_kind::unsupported_format);
}
#else
TEST_CASE("A Lottie is refused by name when the build leaves it out", "[ui][image][lottie]")
{
    luil::image_decode_error error {};
    REQUIRE(luil::decode_lottie_bytes(bytes_of(half_and_half_lottie), error).valid() == false);
    REQUIRE(error.kind == luil::image_decode_error_kind::unsupported_format);
}
#endif
