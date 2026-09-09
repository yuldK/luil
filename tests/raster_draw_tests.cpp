#include "raster_probe.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/image_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {
    constexpr float raster_scale { 2.0f };

    // 자식을 실제로 그리는 컨테이너다.
    // 다른 test 파일의 `test_panel`은 `draw`가 빈 몸통이라 이 축에서는 쓸 수
    // 없다 — 자식이 하나도 그려지지 않아 어떤 픽셀 단언도 배경색만 보게 되고,
    // 그 test는 결함이 있든 없든 똑같이 실패한다.
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

    // 글자는 비워 둔다. 이 축이 보는 것은 도형과 색이라, 글꼴을 싣지 않으면
    // OS에 무엇이 깔려 있든 같은 픽셀이 나온다.
    [[nodiscard]] std::unique_ptr<luil::ui_element> make_button(const luil::ui_element_id& id, const luil::rect_f& slot, const bool default_button)
    {
        auto button { std::make_unique<luil::text_button_element>(id, luil::text_button_config { .default_button = default_button }) };
        button->arrange({ slot, raster_scale });
        return button;
    }

    struct rgb
    {
        std::uint8_t red { 0 };
        std::uint8_t green { 0 };
        std::uint8_t blue { 0 };
    };

    // 사분면 색이 다른 정사각 이미지다 (한 사분면이 `block`×`block` 단색).
    // 윗줄은 빨강·초록, 아랫줄은 파랑·흰색이다.
    //  - 단색 이미지로는 조각을 뽑아 그리는 축을 볼 수 없다. 이웃이 섞여 들어도
    //    같은 색이라 픽셀이 달라지지 않아서다 — 사분면마다 색이 다르면 번져 든
    //    이웃이 곧바로 다른 값으로 드러난다.
    //  - `block`이 1이면 2×2다. 확대해도 경계가 흐려지지 않는지 보는 자리이고,
    //    2면 4×4 시트라 한 사분면이 온전한 스프라이트 하나가 된다.
    [[nodiscard]] luil::ui_image make_quadrant_image(const int block)
    {
        const rgb quadrants[4] { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 255 } };
        const int size { block * 2 };
        std::vector<std::uint8_t> pixels {};
        pixels.reserve(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);
        for (int y { 0 }; y < size; ++y)
            for (int x { 0 }; x < size; ++x)
            {
                const rgb& color { quadrants[(y >= block ? 2 : 0) + (x >= block ? 1 : 0)] };
                pixels.push_back(color.red);
                pixels.push_back(color.green);
                pixels.push_back(color.blue);
                pixels.push_back(255);
            }
        return luil::make_rgba_image(size, size, pixels);
    }
} // namespace

TEST_CASE("The default button is painted as a fill and the focus ring as a ring", "[ui][raster][draw]")
{
    // 기본 버튼이 강조색 채움으로 표시되는지 픽셀 면적을 검사한다.
    // 같은 색의 테만 그리는 결과는 키보드 초점 표시와 구별되지 않으므로 실패해야 한다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_element_id primary { luil::application_element_kind(1), u8"primary" };
    const luil::ui_element_id secondary { luil::application_element_kind(1), u8"secondary" };
    // 자리는 전부 물리 픽셀이다 (raster_probe.h의 규약).
    const luil::rect_f primary_box { 20.0f, 20.0f, 160.0f, 40.0f };
    const luil::rect_f secondary_box { 220.0f, 20.0f, 160.0f, 40.0f };

    const auto build = [&primary, &secondary, &primary_box, &secondary_box] {
        auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 120.0f }, raster_scale });
        root->add(make_button(primary, primary_box, true));
        root->add(make_button(secondary, secondary_box, false));
        return luil::ui_tree { std::move(root) };
    };

    SECTION("기본 버튼은 상자를 채우고 보통 버튼은 채우지 않는다")
    {
        const luil::ui_tree tree { build() };
        luil::testing::raster_frame frame { 400, 120, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        // 한가운데가 강조색이면 테가 아니라 채움이다.
        REQUIRE(frame.pixel_at(100, 40) == palette.accent);
        // 보통 버튼은 강조색으로 채우지 않는다.
        //  - 옅은 바탕(`input_background`)은 반투명이라 배경과 섞인 값이 나온다.
        //    그 합성값을 적어 두면 팔레트를 손볼 때마다 test가 깨지므로, 여기서는
        //    "강조색이 아니다"와 "그래도 무엇인가 그려졌다"만 묻는다. 뒤엣것이
        //    없으면 자식이 아예 그려지지 않는 test도 초록으로 지나간다.
        REQUIRE(frame.pixel_at(300, 40) != palette.accent);
        REQUIRE(frame.pixel_at(300, 40) != palette.window_background);
        // 상자의 절반을 훨씬 넘게 덮는다 — 테는 결코 그럴 수 없다.
        REQUIRE(frame.count_color(primary_box, palette.accent) > 160 * 40 / 2);
    }

    // 테는 몸 밖에 선다 — 여백 1×배율 + 굵기 2×배율이라 몸에서 4물리픽셀까지
    // 나간다. 테를 몽땅 담는 질의 영역은 상자를 그만큼 두 배 여유로 넓힌 것이다.
    const luil::rect_f secondary_ring_box { 212.0f, 12.0f, 176.0f, 56.0f };

    SECTION("초점 테는 두를 뿐 채우지 않는다")
    {
        const luil::ui_tree tree { build() };
        luil::interaction_snapshot interaction {};
        interaction.focused = secondary;
        interaction.focus_visible = true;

        luil::testing::raster_frame frame { 400, 120, palette, raster_scale };
        frame.draw(tree, interaction);

        // 테는 몸 **밖**에 선다 (여백 1×배율, 굵기 2×배율 — 획 중심이 몸에서 2×배율).
        REQUIRE(frame.pixel_at(216, 40) == palette.accent);
        // 몸의 가장자리 안쪽은 테가 아니다 — 몸과 테 사이가 띄어져 띠로 읽힌다.
        REQUIRE(frame.pixel_at(221, 40) != palette.accent);
        // 그리고 안쪽은 그대로다. 이 한 줄이 "채움과 테가 갈렸다"의 전부다.
        REQUIRE(frame.pixel_at(300, 40) != palette.accent);

        // 넓이로도 갈린다. 같은 색이어도 덮는 양이 다르다.
        const int filled { frame.count_color(primary_box, palette.accent) };
        const int ringed { frame.count_color(secondary_ring_box, palette.accent) };
        REQUIRE(ringed > 0);
        REQUIRE(filled > ringed * 3);
    }

    SECTION("눌러서 잡은 초점에는 테가 서지 않는다")
    {
        const luil::ui_tree tree { build() };
        luil::interaction_snapshot interaction {};
        interaction.focused = secondary;
        interaction.focus_visible = false;

        luil::testing::raster_frame frame { 400, 120, palette, raster_scale };
        frame.draw(tree, interaction);
        REQUIRE(frame.count_color(secondary_ring_box, palette.accent) == 0);
    }
}

TEST_CASE("A custom-visual drag still paints the drop target highlight", "[ui][raster][drag]")
{
    // custom_visual이 있어도 외부 파일 드롭 대상의 강조가 그려져야 한다.
    // 사용자 정의 끌기 그림과 놓을 자리 표시는 독립적으로 검사한다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_element_id target { luil::application_element_kind(0), u8"drop" };
    const luil::rect_f target_box { 20.0f, 20.0f, 120.0f, 60.0f };
    // ghost는 포인터에서 10×배율 떨어진 112×24 논리 픽셀 상자다.
    const luil::rect_f ghost_box { 170.0f, 140.0f, 224.0f, 48.0f };

    const auto build = [&target, &target_box] {
        auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 220.0f }, raster_scale });
        // 대상은 아무것도 그리지 않는다 — 강조는 element가 아니라 tree가 얹는다.
        auto drop { std::make_unique<raster_group>(target) };
        drop->arrange({ target_box, raster_scale });
        root->add(std::move(drop));
        return luil::ui_tree { std::move(root) };
    };

    const auto dragging = [&target](const bool custom_visual) {
        luil::interaction_snapshot interaction {};
        luil::drag_visual drag {};
        drag.payload.dragged_owner = u8"row";
        drag.payload.custom_visual = custom_visual;
        drag.x = 150.0f;
        drag.y = 120.0f;
        drag.hovered_drop_target = target;
        interaction.drag = drag;
        return interaction;
    };

    SECTION("스스로 그리는 끌기도 놓을 자리를 강조한다")
    {
        const luil::ui_tree tree { build() };
        luil::testing::raster_frame frame { 400, 220, palette, raster_scale };
        frame.draw(tree, dragging(true));

        // 강조는 대상의 테두리에 선다 (굵기 1×배율이라 경계 안팎 한 칸씩).
        REQUIRE(frame.pixel_at(20, 50) == palette.accent);
        REQUIRE(frame.contains_color(target_box, palette.accent));
        // 누르는 것은 ghost 하나다 — 그 자리에는 아무것도 서지 않는다.
        REQUIRE(frame.count_color(ghost_box, palette.accent) == 0);
        // 강조는 테이므로 대상 한가운데는 배경 그대로다.
        REQUIRE(frame.pixel_at(80, 50) == palette.window_background);
    }

    SECTION("강조를 꺼도 ghost와 drop 대상은 유지한다")
    {
        const luil::ui_tree tree { build() };
        auto interaction { dragging(false) };
        interaction.drag->payload.suppress_drop_highlight = true;
        luil::testing::raster_frame frame { 400, 220, palette, raster_scale };
        frame.draw(tree, interaction);
        REQUIRE(frame.count_color(target_box, palette.accent) == 0);
        REQUIRE(frame.pixel_at(170, 164) == palette.accent);
        REQUIRE(interaction.drag->hovered_drop_target == target);
        interaction.drag->payload.custom_visual = true;
        REQUIRE(luil::plan_drag_overlay(interaction.drag->payload).ghost == false);
        REQUIRE(luil::plan_drag_overlay(interaction.drag->payload).highlight_target == false);
    }

    SECTION("통상 끌기는 강조와 ghost를 함께 그린다")
    {
        const luil::ui_tree tree { build() };
        luil::testing::raster_frame frame { 400, 220, palette, raster_scale };
        frame.draw(tree, dragging(false));

        REQUIRE(frame.pixel_at(20, 50) == palette.accent);
        REQUIRE(frame.pixel_at(170, 164) == palette.accent);
    }
}

TEST_CASE("One sprite of a sheet is painted alone and sharp sampling keeps its edges", "[ui][raster][image]")
{
    // 조각을 뽑아 그리는 축은 순수 함수(`image_source_rect`)만으로 잠기지 않는다.
    // 그 사각형이 정말 표본의 경계가 되는지는 픽셀에만 있다 — 제약이 fast로
    // 남으면 필터가 조각 밖을 함께 읽어 **옆 사분면이 가장자리에 번져 든다.**
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    constexpr luil::ui_color red { 0xFFFF0000 };
    constexpr luil::ui_color green { 0xFF00FF00 };
    constexpr luil::ui_color blue { 0xFF0000FF };
    constexpr luil::ui_color white { 0xFFFFFFFF };
    // 자리는 전부 물리 픽셀이다 (raster_probe.h의 규약). 칸의 한가운데가 160÷2를
    // 더한 100이라, 98과 102가 경계를 사이에 둔 두 픽셀이다.
    const luil::rect_f box { 20.0f, 20.0f, 160.0f, 160.0f };

    const auto build = [&box](const luil::image_config& config) {
        auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, raster_scale });
        auto picture { std::make_unique<luil::image_element>(luil::ui_element_id { luil::application_element_kind(2), u8"sprite" }, config) };
        picture->arrange({ box, raster_scale });
        root->add(std::move(picture));
        return luil::ui_tree { std::move(root) };
    };

    SECTION("왼쪽 위 조각만 그려지고 이웃 색은 한 픽셀도 서지 않는다")
    {
        const luil::image_config config { .image = make_quadrant_image(2), .fit = luil::image_fit::fill, .source = { 0.0f, 0.0f, 2.0f, 2.0f } };
        const luil::ui_tree tree { build(config) };
        luil::testing::raster_frame frame { 200, 200, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        // 조각이 단색이므로 칸 어디를 찍어도 그 색이다.
        REQUIRE(frame.pixel_at(30, 30) == red);
        REQUIRE(frame.pixel_at(100, 100) == red);
        REQUIRE(frame.pixel_at(170, 170) == red);
        // 이웃 사분면은 조각 밖이다 — 한 픽셀이라도 나오면 표본이 조각을 넘었다.
        REQUIRE(frame.count_color(box, green) == 0);
        REQUIRE(frame.count_color(box, blue) == 0);
        REQUIRE(frame.count_color(box, white) == 0);
    }

    SECTION("오른쪽 위 조각은 맞닿은 이웃을 가장자리로 물어 오지 않는다")
    {
        // 이 조각의 **왼쪽 변**이 빨강과 맞닿아 있다. 제약이 느슨하면 그 변에서
        // 빨강이 섞여 나온다.
        const luil::image_config config { .image = make_quadrant_image(2), .fit = luil::image_fit::fill, .source = { 2.0f, 0.0f, 2.0f, 2.0f } };
        const luil::ui_tree tree { build(config) };
        luil::testing::raster_frame frame { 200, 200, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        REQUIRE(frame.pixel_at(24, 100) == green);
        REQUIRE(frame.pixel_at(100, 100) == green);
        REQUIRE(frame.count_color(box, red) == 0);
        REQUIRE(frame.count_color(box, blue) == 0);
    }

    SECTION("sharp는 크게 늘려도 경계를 흐리지 않는다")
    {
        // 2×2를 80배로 늘린다. nearest는 경계를 사이에 둔 두 픽셀이 각각 원본 색
        // 그대로다.
        const luil::image_config config { .image = make_quadrant_image(1), .fit = luil::image_fit::fill, .sampling = luil::image_sampling::sharp };
        const luil::ui_tree tree { build(config) };
        luil::testing::raster_frame frame { 200, 200, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        REQUIRE(frame.pixel_at(98, 60) == red);
        REQUIRE(frame.pixel_at(102, 60) == green);
        REQUIRE(frame.pixel_at(98, 140) == blue);
        REQUIRE(frame.pixel_at(102, 140) == white);
        // 네 색이 칸을 사분한다 — 섞인 값이 끼어들 자리가 없다.
        REQUIRE(frame.count_color(box, red) + frame.count_color(box, green) + frame.count_color(box, blue) + frame.count_color(box, white) > 160 * 160 * 9 / 10);
    }

    SECTION("smooth는 같은 자리를 섞는다 — 두 방식이 정말 갈린다")
    {
        // 기본값으로 같은 그림을 그리면 경계의 두 픽셀이 원본 색이 아니다. 이
        // 단언이 없으면 위 SECTION은 표본이 무엇이든 지나간다.
        const luil::image_config config { .image = make_quadrant_image(1), .fit = luil::image_fit::fill };
        const luil::ui_tree tree { build(config) };
        luil::testing::raster_frame frame { 200, 200, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});

        REQUIRE(frame.pixel_at(98, 60) != red);
        REQUIRE(frame.pixel_at(98, 60) != green);
        // 가장자리는 표본이 이미지 밖으로 나가지 않아 원본 색 그대로다.
        REQUIRE(frame.pixel_at(30, 30) == red);
    }
}
