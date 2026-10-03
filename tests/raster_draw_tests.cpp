#include "raster_probe.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/image_element.h"
#include "luil/ui/layout_metrics.h"
#include "luil/ui/list_element.h"
#include "luil/ui/scroll_area_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/virtual_list_element.h"

#include "host/frame_state.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
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

TEST_CASE("A disabled accent button keeps GrayText readable in high contrast", "[ui][raster][draw]")
{
    // Windows 고대비 Night sky의 짝이다. GrayText(#A6A6A6)를 highlight(#D6B4FD)에 깔면 1.4:1이라 글자가 사라진다.
    luil::high_contrast_colors colors {};
    colors.window_background = luil::make_ui_color(0, 0, 0);
    colors.window_foreground = luil::make_ui_color(255, 255, 255);
    colors.highlight_background = luil::make_ui_color(0xD6, 0xB4, 0xFD);
    colors.highlight_foreground = luil::make_ui_color(0, 0, 0);
    colors.button_background = luil::make_ui_color(0, 0, 0);
    colors.button_foreground = luil::make_ui_color(255, 255, 255);
    colors.disabled_foreground = luil::make_ui_color(0xA6, 0xA6, 0xA6);
    const luil::ui_color_palette palette { luil::high_contrast_palette_for(colors) };
    const luil::ui_element_id enabled_id { luil::application_element_kind(1), u8"enabled" };
    const luil::ui_element_id disabled_id { luil::application_element_kind(1), u8"disabled" };

    auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 400.0f, 120.0f }, raster_scale });
    for (const auto& [id, x] : { std::pair { enabled_id, 20.0f }, std::pair { disabled_id, 220.0f } })
    {
        auto button { std::make_unique<luil::text_button_element>(id, luil::text_button_config { .visual = luil::text_button_visual::accent }) };
        button->arrange({ { x, 20.0f, 160.0f, 40.0f }, raster_scale });
        button->set_enabled(id == enabled_id);
        root->add(std::move(button));
    }
    const luil::ui_tree tree { std::move(root) };
    luil::testing::raster_frame frame { 400, 120, palette, raster_scale };
    frame.draw(tree, luil::interaction_snapshot {});

    // 켜진 단추는 highlight 짝 그대로다 (고른 토글이 읽히는 자리).
    REQUIRE(frame.pixel_at(100, 40) == palette.soft_button_background);
    // 꺼진 단추는 GrayText와 짝인 표면에 깐다.
    REQUIRE(frame.pixel_at(300, 40) == palette.input_background);
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

TEST_CASE("Scroll edges shade only the side that has more to show", "[ui][raster][scroll]")
{
    // 가장자리 그림자는 "이 너머에 더 있다"의 표시라, 흘린 쪽에만 서야 한다.
    // 맨 위에 선 창의 위와 끝까지 흘린 창의 아래는 깨끗하다 — 이 축은 픽셀에만 있다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    // 창은 100 논리 픽셀이고 내용은 그 네 배다 (최대치 300).
    const auto build = [](const float offset, const luil::scroll_edges edges) {
        luil::scroll_area_config config {};
        config.owner = u8"page";
        config.content_height = 400.0f;
        config.scroll_offset = offset;
        config.bar = luil::scrollbar_visibility::never;
        config.edges = edges;
        config.scroll = [](const float) { return luil::input_action {}; };
        auto area { std::make_unique<luil::scroll_area_element>(std::move(config)) };
        area->set_content(std::make_unique<raster_group>(luil::ui_element_id { luil::application_element_kind(1), u8"content" }));
        return luil::make_arranged_tree(std::move(area), { 0.0f, 0.0f, 200.0f, 200.0f }, raster_scale);
    };
    const auto paint = [&palette, &build](const float offset, const luil::scroll_edges edges) {
        luil::testing::raster_frame frame { 200, 200, palette, raster_scale };
        frame.draw(build(offset, edges), luil::interaction_snapshot {});
        // 맨 윗줄·맨 아랫줄·한가운데다.
        return std::array<luil::ui_color, 3> { frame.pixel_at(50, 0), frame.pixel_at(50, 199), frame.pixel_at(50, 100) };
    };
    const luil::scroll_edges shadows { .shadows = true };

    SECTION("맨 위에 선 창은 아래에만 그림자가 있다")
    {
        const auto [top, bottom, middle] { paint(0.0f, shadows) };
        REQUIRE(top == palette.window_background);
        REQUIRE(bottom != palette.window_background);
        REQUIRE(middle == palette.window_background);
    }

    SECTION("끝까지 흘린 창은 위에만 그림자가 있다")
    {
        const auto [top, bottom, middle] { paint(300.0f, shadows) };
        REQUIRE(top != palette.window_background);
        REQUIRE(bottom == palette.window_background);
        REQUIRE(middle == palette.window_background);
    }

    SECTION("가운데 선 창은 양쪽에 있다")
    {
        const auto [top, bottom, middle] { paint(150.0f, shadows) };
        REQUIRE(top != palette.window_background);
        REQUIRE(bottom != palette.window_background);
        REQUIRE(middle == palette.window_background);
    }

    SECTION("범위 밖 offset은 다듬은 값으로 판정한다")
    {
        // 최대치를 넘긴 값은 끝까지 흘린 것이다 — `arrange`가 다듬은 그 값을 본다.
        const auto [top, bottom, middle] { paint(900.0f, shadows) };
        REQUIRE(top != palette.window_background);
        REQUIRE(bottom == palette.window_background);
    }

    SECTION("기본 설정은 아무것도 그리지 않는다")
    {
        const auto [top, bottom, middle] { paint(150.0f, luil::scroll_edges {}) };
        REQUIRE(top == palette.window_background);
        REQUIRE(bottom == palette.window_background);
    }

    SECTION("구분선은 흘린 양과 무관하게 선다")
    {
        const auto [top, bottom, middle] { paint(0.0f, luil::scroll_edges { .top_rule = true, .bottom_rule = true }) };
        // `divider`는 알파를 지닌 색이라 배경과 섞인 값이지만, 배경 그대로는 아니다.
        REQUIRE(top != palette.window_background);
        REQUIRE(bottom != palette.window_background);
        REQUIRE(middle == palette.window_background);
        // 구분선은 1 논리 픽셀 = 물리 2픽셀이다. 그 아래는 배경이다 (그림자가 꺼져 있다).
        luil::testing::raster_frame frame { 200, 200, palette, raster_scale };
        frame.draw(build(0.0f, luil::scroll_edges { .top_rule = true }), luil::interaction_snapshot {});
        REQUIRE(frame.pixel_at(50, 1) == top);
        REQUIRE(frame.pixel_at(50, 2) == palette.window_background);
    }
}

TEST_CASE("A selected row wears the shared selection mark in both lists", "[ui][raster][list]")
{
    // 목록과 가상 목록의 고른 행은 한 함수(`draw_row_selection`)가 그린다. 두 목록이
    // 같은 자리에 같은 표식을 두는지는 픽셀만이 말한다 — 표식이 갈리면 한 화면에
    // 두 모양의 고름이 선다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    // 행은 20 논리 픽셀이다. 둘째 행이 고른 행이라 물리 y 40..80이고, 표식은 x 4..10·y 52..68이다.
    constexpr float row_height { 20.0f };
    const luil::rect_f slot { 0.0f, 0.0f, 400.0f, 200.0f };

    const auto probe = [&palette](const luil::ui_tree& tree) {
        luil::testing::raster_frame frame { 400, 200, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});
        // 고른 행의 표식 · 고른 행의 채움 · 고르지 않은 행의 같은 자리다.
        return std::array<luil::ui_color, 3> { frame.pixel_at(6, 60), frame.pixel_at(200, 60), frame.pixel_at(6, 20) };
    };

    const auto check = [&palette](const std::array<luil::ui_color, 3>& pixels) {
        const auto [mark, fill, other] { pixels };
        REQUIRE(mark == palette.accent);
        // 채움은 `accent_soft`의 낮은 알파라 배경과 섞인 값이다 — 온전한 색이 아니다.
        REQUIRE(fill != palette.window_background);
        REQUIRE(fill != palette.accent_soft);
        REQUIRE(other == palette.window_background);
    };

    SECTION("list_element")
    {
        luil::list_config config {};
        config.owner = u8"list";
        config.row_height = row_height;
        for (const char8_t* key : { u8"a", u8"b", u8"c" })
            config.items.push_back(luil::list_item { .key = key });
        config.selected = u8"b";
        const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::list_element>(std::move(config)), slot, raster_scale) };
        check(probe(tree));
    }

    SECTION("virtual_list_element")
    {
        luil::virtual_list_config config {};
        config.owner = u8"virtual";
        config.row_height = row_height;
        for (const char8_t* key : { u8"a", u8"b", u8"c" })
            config.items.push_back(luil::virtual_list_item { .key = key });
        config.selected = u8"b";
        const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::virtual_list_element>(std::move(config)), slot, raster_scale) };
        check(probe(tree));
    }
}

TEST_CASE("A popup frame strokes its border over the whole surface", "[win32][raster][popup]")
{
    // popup의 테두리는 tree가 아니라 표면이 긋는다 (`frame_state::border`). tree가 없는
    // frame에서도 서야 하고, 획은 창 안에 온전히 들어야 한다 — 밖으로 나간 반 픽셀은
    // 창 밖이라 어디에도 그려지지 않는다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    SkBitmap pixels {};
    pixels.allocN32Pixels(100, 60);
    SkCanvas canvas { pixels };
    luil::frame_state state {};
    state.width = 100;
    state.height = 60;
    state.dpi_scale = raster_scale;
    state.theme = luil::color_theme::dark;
    const auto pixel_at = [&pixels](const int x, const int y) { return static_cast<luil::ui_color>(pixels.getColor(x, y)); };

    SECTION("테두리를 켜면 둘레 1 논리 픽셀이 tooltip_border다")
    {
        state.border = true;
        luil::draw_frame(canvas, nullptr, nullptr, state);
        REQUIRE(pixel_at(0, 30) == palette.tooltip_border);
        REQUIRE(pixel_at(1, 30) == palette.tooltip_border);
        REQUIRE(pixel_at(50, 0) == palette.tooltip_border);
        REQUIRE(pixel_at(99, 30) == palette.tooltip_border);
        REQUIRE(pixel_at(50, 59) == palette.tooltip_border);
        REQUIRE(pixel_at(2, 30) == palette.window_background);
        REQUIRE(pixel_at(50, 30) == palette.window_background);
    }

    SECTION("기본값은 긋지 않는다")
    {
        luil::draw_frame(canvas, nullptr, nullptr, state);
        REQUIRE(pixel_at(0, 30) == palette.window_background);
        REQUIRE(pixel_at(50, 59) == palette.window_background);
    }
}

namespace {
    // 자기 자리를 한 색으로 채우는 element다. 원점이 옮겨졌는지를 그 색의 자리로 본다.
    class fill_element final : public luil::ui_element
    {
    public:
        fill_element(const luil::ui_element_id& id, const luil::ui_color color)
            : ui_element { id }
            , color_ { color }
        {}

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
        {
            static_cast<void>(interaction);
            SkPaint paint {};
            paint.setColor(static_cast<SkColor>(color_));
            const luil::rect_f box { bounds() };
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), paint);
        }

    private:
        luil::ui_color color_;
    };
} // namespace

TEST_CASE("Content starts at the frame origin while the background fills the surface", "[ui][raster][frame]")
{
    // 모바일 창은 화면 끝까지 그려져 시스템 막대가 가장자리를 덮는다. 앱 host는 그 몫을
    // 원점으로 비키고(`frame_state::origin_x`·`origin_y`), 배경만 표면 전체에 칠한다.
    constexpr luil::ui_color fill { 0xFF2080C0u };
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    SkBitmap pixels {};
    pixels.allocN32Pixels(100, 60);
    SkCanvas canvas { pixels };
    auto root { std::make_unique<fill_element>(luil::ui_element_id { luil::ui_element_kind::root }, fill) };
    root->arrange({ { 0.0f, 0.0f, 80.0f, 40.0f }, 1.0f });
    const luil::ui_tree tree { std::move(root) };
    luil::frame_state state {};
    state.width = 80;
    state.height = 40;
    state.origin_x = 10;
    state.origin_y = 15;
    state.theme = luil::color_theme::dark;
    state.tree = &tree;
    const auto pixel_at = [&pixels](const int x, const int y) { return static_cast<luil::ui_color>(pixels.getColor(x, y)); };

    luil::draw_frame(canvas, nullptr, nullptr, state);

    // 비킨 가장자리는 배경이다.
    REQUIRE(pixel_at(5, 30) == palette.window_background);
    REQUIRE(pixel_at(50, 5) == palette.window_background);
    // 내용은 원점부터 크기만큼이다.
    REQUIRE(pixel_at(10, 15) == fill);
    REQUIRE(pixel_at(89, 54) == fill);
    REQUIRE(pixel_at(90, 30) == palette.window_background);
    REQUIRE(pixel_at(50, 55) == palette.window_background);
}

TEST_CASE("An overlay popup paints over the main tree inside its bounds", "[ui][raster][popup]")
{
    // 창이 하나뿐인 플랫폼은 popup을 주 tree 위의 layer로 그린다 (`frame_state::overlays`).
    // layer 자리 밖은 주 tree 그대로이고, 안은 popup tree가 layer 원점에서 시작하며, 둘레에
    // 테두리가 선다.
    constexpr luil::ui_color main_fill { 0xFF2080C0u };
    constexpr luil::ui_color popup_fill { 0xFFC08020u };
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    SkBitmap pixels {};
    pixels.allocN32Pixels(100, 100);
    SkCanvas canvas { pixels };
    auto main_root { std::make_unique<fill_element>(luil::ui_element_id { luil::ui_element_kind::root }, main_fill) };
    main_root->arrange({ { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    const luil::ui_tree main_tree { std::move(main_root) };
    // popup tree는 왼쪽 위 10x10만 칠한다. 나머지는 layer 배경이다.
    auto popup_root { std::make_unique<fill_element>(luil::ui_element_id { luil::ui_element_kind::root }, popup_fill) };
    popup_root->arrange({ { 0.0f, 0.0f, 10.0f, 10.0f }, 1.0f });
    const luil::ui_tree popup_tree { std::move(popup_root) };
    const luil::overlay_layer layer { &popup_tree, { 40, 30, 40, 40 }, true, {} };
    luil::frame_state state {};
    state.width = 100;
    state.height = 100;
    state.theme = luil::color_theme::dark;
    state.tree = &main_tree;
    state.overlays = std::span<const luil::overlay_layer> { &layer, 1 };
    const auto pixel_at = [&pixels](const int x, const int y) { return static_cast<luil::ui_color>(pixels.getColor(x, y)); };

    luil::draw_frame(canvas, nullptr, nullptr, state);

    REQUIRE(pixel_at(10, 10) == main_fill);
    REQUIRE(pixel_at(45, 35) == popup_fill);
    REQUIRE(pixel_at(60, 50) == palette.window_background);
    REQUIRE(pixel_at(40, 50) == palette.tooltip_border);
    REQUIRE(pixel_at(79, 50) == palette.tooltip_border);
}
