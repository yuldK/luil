#include "luil/ui/image_element.h"

#include "raster_probe.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {
    constexpr float raster_scale { 2.0f };

    constexpr luil::ui_element_kind kind_picture { luil::application_element_kind(0) };

    // 자식을 실제로 그리는 컨테이너다 (raster_draw_tests와 같은 이유 — 다른
    // 파일의 test_panel은 draw가 빈 몸통이라 픽셀 단언이 서지 않는다).
    class raster_group final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
        {
            draw_children(context, interaction);
        }
    };

    // 불투명 단색 이미지다. 어느 픽셀을 찍어도 같은 값이라 표본 자리에 매이지 않는다.
    [[nodiscard]] luil::ui_image make_solid_image(const int width, const int height, const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue)
    {
        std::vector<std::uint8_t> pixels {};
        pixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        for (int index = 0; index < width * height; ++index)
        {
            pixels.push_back(red);
            pixels.push_back(green);
            pixels.push_back(blue);
            pixels.push_back(255);
        }
        return luil::make_rgba_image(width, height, pixels);
    }

    // 사분면 색이 다른 2×2 이미지다.
    // 단색으로는 잡히지 않는 상하/좌우 뒤집힘·stride 오류가 색 자리로 드러난다.
    // 윗줄: 빨강·초록, 아랫줄: 파랑·흰색.
    [[nodiscard]] luil::ui_image make_quadrant_image()
    {
        const std::vector<std::uint8_t> pixels {
            255,
            0,
            0,
            255,
            0,
            255,
            0,
            255,
            0,
            0,
            255,
            255,
            255,
            255,
            255,
            255,
        };
        return luil::make_rgba_image(2, 2, pixels);
    }
} // namespace

TEST_CASE("An image is made once from RGBA pixels or not at all", "[ui][image]")
{
    const std::vector<std::uint8_t> pixels(2 * 2 * 4, 128);
    const luil::ui_image image { luil::make_rgba_image(2, 2, pixels) };
    REQUIRE(image.valid());
    REQUIRE(image.width() == 2);
    REQUIRE(image.height() == 2);
    REQUIRE(image.backing() != nullptr);

    // 크기가 어긋난 픽셀은 죽지 않고 빈 이미지다.
    const std::vector<std::uint8_t> short_pixels(15, 0);
    REQUIRE(luil::make_rgba_image(2, 2, short_pixels).valid() == false);
    REQUIRE(luil::make_rgba_image(0, 2, {}).valid() == false);
    REQUIRE(luil::ui_image {}.valid() == false);
    REQUIRE(luil::ui_image {}.backing() == nullptr);
}

TEST_CASE("The image destination follows the fit", "[ui][image]")
{
    const luil::rect_f bounds { 0.0f, 0.0f, 200.0f, 100.0f };

    // contain: 비율을 지키고 전부 들어오며 중앙이다.
    const luil::rect_f contained { luil::image_destination(bounds, 100, 100, luil::image_fit::contain) };
    REQUIRE(contained.x == 50.0f);
    REQUIRE(contained.y == 0.0f);
    REQUIRE(contained.width == 100.0f);
    REQUIRE(contained.height == 100.0f);

    // cover: 비율을 지키고 칸을 덮으며 넘친 만큼 밖으로 나간다.
    const luil::rect_f covered { luil::image_destination(bounds, 100, 100, luil::image_fit::cover) };
    REQUIRE(covered.x == 0.0f);
    REQUIRE(covered.y == -50.0f);
    REQUIRE(covered.width == 200.0f);
    REQUIRE(covered.height == 200.0f);

    // fill: 칸 그대로다.
    const luil::rect_f filled { luil::image_destination(bounds, 100, 100, luil::image_fit::fill) };
    REQUIRE(filled.x == bounds.x);
    REQUIRE(filled.width == bounds.width);
    REQUIRE(filled.height == bounds.height);

    // 넓은 이미지의 contain은 세로 중앙에 선다.
    const luil::rect_f wide { luil::image_destination({ 0.0f, 0.0f, 100.0f, 100.0f }, 200, 50, luil::image_fit::contain) };
    REQUIRE(wide.y == 37.5f);
    REQUIRE(wide.width == 100.0f);
    REQUIRE(wide.height == 25.0f);

    // 빈 칸·빈 이미지는 빈 사각형이다.
    REQUIRE(luil::image_destination({ 0.0f, 0.0f, 0.0f, 100.0f }, 10, 10, luil::image_fit::contain).width == 0.0f);
    REQUIRE(luil::image_destination(bounds, 0, 10, luil::image_fit::contain).width == 0.0f);
}

TEST_CASE("A source rectangle is clamped into the image or means the whole image", "[ui][image]")
{
    // 좌표는 전부 **이미지 자신의 픽셀**이다 (칸과 목적 사각형의 물리 픽셀과 다른
    // 단위다 — 둘이 같은 `rect_f`를 나눠 쓴다).
    SECTION("비어 있으면 이미지 전체다")
    {
        const luil::rect_f whole { luil::image_source_rect({}, 64, 32) };
        REQUIRE(whole.x == 0.0f);
        REQUIRE(whole.y == 0.0f);
        REQUIRE(whole.width == 64.0f);
        REQUIRE(whole.height == 32.0f);
    }

    SECTION("안에 온전히 든 조각은 그대로다")
    {
        const luil::rect_f piece { luil::image_source_rect({ 16.0f, 8.0f, 16.0f, 16.0f }, 64, 32) };
        REQUIRE(piece.x == 16.0f);
        REQUIRE(piece.y == 8.0f);
        REQUIRE(piece.width == 16.0f);
        REQUIRE(piece.height == 16.0f);
    }

    SECTION("음수로 적힌 조각도 전체다")
    {
        // 뒤집힌 사각형을 그대로 넘기면 그리기가 뜻 모를 자리를 읽는다. 앱이 표에서
        // 읽어 오는 값이라 어긋난 값이 오는 것이 정상이고, 죽는 대신 전체로 모은다.
        const luil::rect_f flipped { luil::image_source_rect({ 20.0f, 10.0f, -8.0f, 10.0f }, 64, 32) };
        REQUIRE(flipped.width == 64.0f);
        REQUIRE(flipped.height == 32.0f);
    }

    SECTION("걸친 조각은 이미지 안으로 잘린다")
    {
        // 왼쪽과 아래로 함께 걸친 조각이다 — 남는 것은 24..32 줄의 0..20 칸이다.
        const luil::rect_f clamped { luil::image_source_rect({ -10.0f, 24.0f, 30.0f, 40.0f }, 64, 32) };
        REQUIRE(clamped.x == 0.0f);
        REQUIRE(clamped.y == 24.0f);
        REQUIRE(clamped.width == 20.0f);
        REQUIRE(clamped.height == 8.0f);
    }

    SECTION("통째로 밖이면 전체다")
    {
        // 아무것도 그리지 않으면 앱은 이미지가 없는 것인지 조각이 어긋난 것인지
        // 화면에서 가릴 수 없다.
        const luil::rect_f outside { luil::image_source_rect({ 100.0f, 100.0f, 10.0f, 10.0f }, 64, 32) };
        REQUIRE(outside.width == 64.0f);
        REQUIRE(outside.height == 32.0f);
    }

    SECTION("빈 이미지는 빈 사각형이다")
    {
        // 그릴 것이 없다는 답을 "전체"로 적을 수는 없다.
        REQUIRE(luil::image_source_rect({ 0.0f, 0.0f, 4.0f, 4.0f }, 0, 32).width == 0.0f);
        REQUIRE(luil::image_source_rect({}, 64, -1).height == 0.0f);
    }
}

TEST_CASE("The destination follows the source rectangle instead of the sheet", "[ui][image]")
{
    // 아틀라스의 요점이다. 8:1 시트에서 잘라 낸 정사각 스프라이트는 정사각으로
    // 앉아야 하고, 시트의 크기로 계산하면 `contain`이 시트의 비율로 자리를 잡아
    // 조각이 엉뚱한 여백 안에 앉는다.
    const luil::rect_f bounds { 0.0f, 0.0f, 100.0f, 100.0f };

    const luil::rect_f sheet { luil::image_destination(bounds, 256, 32, luil::image_fit::contain) };
    REQUIRE(sheet.width == 100.0f);
    REQUIRE(sheet.height == 12.5f);

    const luil::rect_f sprite_source { luil::image_source_rect({ 64.0f, 0.0f, 32.0f, 32.0f }, 256, 32) };
    const luil::rect_f sprite { luil::image_destination(bounds, static_cast<int>(sprite_source.width), static_cast<int>(sprite_source.height), luil::image_fit::contain) };
    REQUIRE(sprite.width == 100.0f);
    REQUIRE(sprite.height == 100.0f);
}

TEST_CASE("An image is not interactive", "[ui][image]")
{
    // 표시뿐이다. 누르는 이미지는 담는 쪽이 액션을 걸어 만든다 —
    // 그러면 기본 규칙("누를 수 있으면 자리다")을 그대로 탄다.
    const luil::image_element picture { luil::ui_element_id { kind_picture, u8"plain" }, { .image = make_solid_image(1, 1, 255, 0, 0) } };
    REQUIRE(picture.interactive() == false);
    REQUIRE(picture.tab_stop() == false);
    REQUIRE(picture.cursor() == luil::ui_cursor::inherit);
}

TEST_CASE("An image is painted into its fitted rectangle", "[ui][image][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    constexpr luil::ui_color red { 0xFFFF0000 };

    // 자리는 전부 물리 픽셀이다 (raster_probe.h의 규약).
    const auto build = [](const luil::image_config& config, const luil::rect_f& slot) {
        auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, raster_scale });
        auto picture { std::make_unique<luil::image_element>(luil::ui_element_id { kind_picture, u8"one" }, config) };
        picture->arrange({ slot, raster_scale });
        root->add(std::move(picture));
        return luil::ui_tree { std::move(root) };
    };

    SECTION("contain은 비율을 지키고 남는 자리를 배경에 맡긴다")
    {
        // 정사각 이미지가 2:1 칸에 들어오면 세로에 맞은 정사각이 중앙에 선다.
        const luil::ui_tree tree { build({ .image = make_solid_image(1, 1, 255, 0, 0) }, { 20.0f, 20.0f, 80.0f, 40.0f }) };
        luil::testing::raster_frame frame { 200, 100, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        // 중앙(40..80 구간)은 이미지 색이다.
        REQUIRE(frame.pixel_at(60, 40) == red);
        // 칸 안이지만 이미지 밖(왼쪽 여백)은 배경 그대로다.
        REQUIRE(frame.pixel_at(25, 40) == palette.window_background);
        // 칸 밖도 배경 그대로다.
        REQUIRE(frame.pixel_at(150, 40) == palette.window_background);
    }

    SECTION("fill은 칸을 그대로 채운다")
    {
        const luil::ui_tree tree { build({ .image = make_solid_image(1, 1, 255, 0, 0), .fit = luil::image_fit::fill }, { 20.0f, 20.0f, 80.0f, 40.0f }) };
        luil::testing::raster_frame frame { 200, 100, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        REQUIRE(frame.pixel_at(25, 25) == red);
        REQUIRE(frame.pixel_at(95, 55) == red);
        REQUIRE(frame.pixel_at(105, 40) == palette.window_background);
    }

    SECTION("cover는 넘친 부분을 자기 칸으로 자른다")
    {
        // 정사각 이미지가 2:1 칸을 덮으면 세로로 넘친다 — 칸 위쪽 밖은 배경이어야 한다.
        const luil::ui_tree tree { build({ .image = make_solid_image(1, 1, 255, 0, 0), .fit = luil::image_fit::cover }, { 20.0f, 20.0f, 80.0f, 40.0f }) };
        luil::testing::raster_frame frame { 200, 100, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        REQUIRE(frame.pixel_at(60, 40) == red);
        // 잘리지 않았다면 이 자리(칸 위, 넘친 dest 안)가 이미지 색이 된다.
        REQUIRE(frame.pixel_at(60, 10) == palette.window_background);
    }

    SECTION("픽셀의 자리가 뒤집히지 않는다 — 사분면 색이 제자리에 선다")
    {
        const luil::ui_tree tree { build({ .image = make_quadrant_image(), .fit = luil::image_fit::fill }, { 20.0f, 20.0f, 80.0f, 40.0f }) };
        luil::testing::raster_frame frame { 200, 100, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        // 모서리 가까이는 linear 보간이 섞이지 않는 자리다.
        REQUIRE(frame.pixel_at(24, 24) == red);
        REQUIRE(frame.pixel_at(96, 24) == luil::ui_color { 0xFF00FF00 });
        REQUIRE(frame.pixel_at(24, 56) == luil::ui_color { 0xFF0000FF });
        REQUIRE(frame.pixel_at(96, 56) == luil::ui_color { 0xFFFFFFFF });
    }

    SECTION("소수점이 든 조각은 잘리지 않고 반올림한 크기로 앉는다")
    {
        // 변을 **자르면** 1.9가 1이 되어 조각이 청한 것보다 좁게 앉는다. 그림은
        // 그대로 뜨므로 눈으로는 "원래 저런 그림"과 갈리지 않는 종류다.
        //  - 1.9×10 조각을 80×40 칸에 `contain`으로 놓으면 세로가 배율을 정해(4배)
        //    목적 사각형이 8×40이고 가로로 56..64에 선다. 잘라 1로 본 구현은 4×40에
        //    58..62라, 아래 자리가 배경으로 남는다.
        const luil::ui_tree tree {
            build({ .image = make_solid_image(16, 16, 255, 0, 0), .source = { 0.0f, 0.0f, 1.9f, 10.0f } }, { 20.0f, 20.0f, 80.0f, 40.0f }),
        };
        luil::testing::raster_frame frame { 200, 100, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        REQUIRE(frame.pixel_at(57, 40) == red);
        // 반올림은 한 픽셀의 일이다 — 칸 전체로 번지지 않는다.
        REQUIRE(frame.pixel_at(30, 40) == palette.window_background);
    }

    SECTION("한 픽셀보다 좁은 조각도 사라지지 않는다")
    {
        // 자르면 0이 되고, 그러면 목적 사각형이 비어 그리기가 **아무 말 없이**
        // 돌아선다 — 화면에서 "빈 이미지"와 구별되지 않아 아무도 실패를 못 본다.
        const luil::ui_tree half {
            build({ .image = make_solid_image(16, 16, 255, 0, 0), .source = { 0.0f, 0.0f, 0.5f, 10.0f } }, { 20.0f, 20.0f, 80.0f, 40.0f }),
        };
        luil::testing::raster_frame half_frame { 200, 100, palette, raster_scale };
        half_frame.draw(half, luil::interaction_snapshot {});
        REQUIRE(half_frame.pixel_at(60, 40) == red);

        // 반올림마저 0으로 떨어지는 조각도 마찬가지다. 어느 변도 1 아래로 내려가지
        // 않는다 — `decoded_image_size`가 축소한 축을 0으로 만들지 않는 것과 같은
        // 자리이자 같은 이유다.
        const luil::ui_tree sliver {
            build({ .image = make_solid_image(16, 16, 255, 0, 0), .source = { 0.0f, 0.0f, 0.25f, 10.0f } }, { 20.0f, 20.0f, 80.0f, 40.0f }),
        };
        luil::testing::raster_frame sliver_frame { 200, 100, palette, raster_scale };
        sliver_frame.draw(sliver, luil::interaction_snapshot {});
        REQUIRE(sliver_frame.pixel_at(60, 40) == red);
    }

    SECTION("빈 이미지는 아무것도 그리지 않는다")
    {
        const luil::ui_tree tree { build({}, { 20.0f, 20.0f, 80.0f, 40.0f }) };
        luil::testing::raster_frame frame { 200, 100, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        REQUIRE(frame.pixel_at(60, 40) == palette.window_background);
    }
}
