#include "raster_probe.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/image_element.h"
#include "luil/ui/layout_metrics.h"
#include "luil/ui/list_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/modal_host_element.h"
#include "luil/ui/palette_scope_element.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/scroll_area_element.h"
#include "luil/ui/sheet_host_element.h"
#include "luil/ui/slider_element.h"
#include "luil/ui/split_handle_element.h"
#include "luil/ui/tab_bar_element.h"
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
#include <cstdlib>
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
}

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

    SECTION("흘리기 시작한 창의 그림자는 옅게 시작해 흘린 만큼 짙어진다")
    {
        // 반 픽셀을 넘자마자 다 짙은 그림자가 툭 켜지지 않는다. 짙기는 배경에서 멀어진 정도로 잰다.
        const auto distance = [&palette](const luil::ui_color color) {
            const auto channel = [](const luil::ui_color value, const int shift) { return static_cast<int>((value >> shift) & 0xFFu); };
            int sum { 0 };
            for (const int shift : { 0, 8, 16 })
                sum += std::abs(channel(color, shift) - channel(palette.window_background, shift));
            return sum;
        };
        const int early { distance(paint(6.0f, shadows)[0]) };
        const int deeper { distance(paint(12.0f, shadows)[0]) };
        const int full { distance(paint(150.0f, shadows)[0]) };
        REQUIRE(early > 0);
        REQUIRE(early < deeper);
        REQUIRE(deeper < full);
        // 다 짙어진 뒤로는 더 흘려도 같다.
        REQUIRE(distance(paint(luil::scroll_edge_fade_distance, shadows)[0]) == full);
        // 끝에 다가가는 아래쪽도 같은 곡선으로 옅어진다.
        REQUIRE(distance(paint(294.0f, shadows)[1]) < distance(paint(150.0f, shadows)[1]));
    }
}

TEST_CASE("Scroll edge strength rises smoothly over the fade distance", "[ui][scroll]")
{
    REQUIRE(luil::scroll_edge_strength(0.0f) == 0.0f);
    REQUIRE(luil::scroll_edge_strength(0.5f) == 0.0f);
    REQUIRE(luil::scroll_edge_strength(luil::scroll_edge_fade_distance / 2.0f) == 0.5f);
    REQUIRE(luil::scroll_edge_strength(luil::scroll_edge_fade_distance) == 1.0f);
    REQUIRE(luil::scroll_edge_strength(1000.0f) == 1.0f);
    float previous { 0.0f };
    for (float distance { 1.0f }; distance <= luil::scroll_edge_fade_distance; distance += 1.0f)
    {
        const float strength { luil::scroll_edge_strength(distance) };
        REQUIRE(strength > previous);
        previous = strength;
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
    // premultiplied 표면에서 되읽은 반투명 색은 성분이 반올림만큼 어긋난다.
    [[nodiscard]] bool near_color(const luil::ui_color actual, const luil::ui_color expected) noexcept
    {
        for (const int shift : { 24, 16, 8, 0 })
            if (std::abs(static_cast<int>((actual >> shift) & 0xFFu) - static_cast<int>((expected >> shift) & 0xFFu)) > 2)
                return false;
        return true;
    }
}

TEST_CASE("Only a surface whose backdrop stands clears with the backdrop background", "[win32][raster][backdrop]")
{
    // 효과가 실제로 선 표면만 바탕이 옅다 (`frame_state::backdrop`). 서지 않은 표면(설정·OS 사정·popup의 mica)은
    // 지금처럼 불투명한 창 바탕이다 — 그 판단은 표면 종류와 상관없이 이 한 값으로 들어온다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    // 옅은 바탕은 비치되 비지 않는다. 알파가 0이면 아래 견줌이 지운 픽셀과 구별되지 않는다.
    REQUIRE((palette.backdrop_background >> 24) > 0u);
    REQUIRE((palette.backdrop_background >> 24) < 0xFFu);
    SkBitmap pixels {};
    pixels.allocN32Pixels(160, 120);
    SkCanvas canvas { pixels };
    luil::menu_config menu {};
    menu.owner = u8"menu";
    menu.items.push_back(luil::menu_item_config { .key = u8"open", .label = {} });
    menu.items.push_back(luil::menu_item_config { .key = u8"save", .label = {} });
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::menu_element>(std::move(menu)), { 0.0f, 0.0f, 160.0f, 120.0f }, raster_scale) };
    luil::frame_state state {};
    state.width = 160;
    state.height = 120;
    state.dpi_scale = raster_scale;
    state.theme = luil::color_theme::dark;
    const auto pixel_at = [&pixels](const int x, const int y) { return static_cast<luil::ui_color>(pixels.getColor(x, y)); };

    SECTION("효과가 서지 않은 표면은 바탕도 메뉴 몸통도 불투명하다")
    {
        luil::draw_frame(canvas, nullptr, nullptr, state);
        REQUIRE(pixel_at(80, 60) == palette.window_background);
        state.tree = &tree;
        luil::draw_frame(canvas, nullptr, nullptr, state);
        REQUIRE(pixel_at(80, 110) == palette.surface_background);
    }

    SECTION("효과가 선 popup은 바탕이 옅고, 메뉴는 몸통을 칠하지 않아 그 바탕을 비춘다")
    {
        state.backdrop = true;
        state.popup = true;
        luil::draw_frame(canvas, nullptr, nullptr, state);
        REQUIRE(near_color(pixel_at(80, 60), palette.backdrop_background));
        state.tree = &tree;
        luil::draw_frame(canvas, nullptr, nullptr, state);
        REQUIRE(near_color(pixel_at(80, 110), palette.backdrop_background));
    }

    SECTION("효과가 선 창 안에 둔 메뉴는 그대로 불투명한 판이다")
    {
        // 창 바탕만 옅다. 창 안의 메뉴는 popup이 아니라 창 바탕을 대신하지 않는다.
        state.backdrop = true;
        luil::draw_frame(canvas, nullptr, nullptr, state);
        REQUIRE(near_color(pixel_at(80, 60), palette.backdrop_background));
        state.tree = &tree;
        luil::draw_frame(canvas, nullptr, nullptr, state);
        REQUIRE(pixel_at(80, 110) == palette.surface_background);
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
        {
        }

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
}

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

TEST_CASE("A modal scrim and a sheet background paint out to the surface", "[ui][raster][sheet]")
{
    // 물리 200×260 그림에서 안전 영역은 y 20~240이다. 위 20(상태 표시줄)과 아래 20(제스처 막대)이 자리 밖이다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::light) };
    const luil::rect_f slot { 0.0f, 20.0f, 200.0f, 220.0f };
    const luil::rect_f surface { 0.0f, 0.0f, 200.0f, 260.0f };
    const auto draw = [&palette](std::unique_ptr<luil::ui_element> host, const luil::rect_f& host_slot, const luil::rect_f& host_surface) {
        host->arrange({ .slot = host_slot, .scale = raster_scale, .surface = host_surface });
        auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 260.0f }, raster_scale });
        root->add(std::move(host));
        const luil::ui_tree tree { std::move(root) };
        luil::testing::raster_frame frame { 200, 260, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});
        return frame;
    };

    SECTION("modal 아래 시트")
    {
        luil::sheet_host_config config {};
        config.owner = u8"sheet";
        const auto edge { draw(std::make_unique<luil::sheet_host_element>(config), slot, surface) };
        // 위 띠는 자리 안의 scrim과 같은 색이다.
        REQUIRE(edge.pixel_at(100, 10) != palette.window_background);
        REQUIRE(edge.pixel_at(100, 10) == edge.pixel_at(100, 60));
        // 시트(반 높이, y 130~240)의 바탕이 아래 띠까지 이어진다.
        REQUIRE(edge.pixel_at(100, 200) == palette.surface_background);
        REQUIRE(edge.pixel_at(100, 250) == palette.surface_background);
        // 표면을 모르면 두 띠는 창 바탕으로 남는다.
        const auto plain { draw(std::make_unique<luil::sheet_host_element>(config), slot, {}) };
        REQUIRE(plain.pixel_at(100, 10) == palette.window_background);
        REQUIRE(plain.pixel_at(100, 250) == palette.window_background);
        REQUIRE(plain.pixel_at(100, 60) == edge.pixel_at(100, 60));
    }

    SECTION("modal dialog")
    {
        luil::modal_host_config config {};
        config.owner = u8"dialog";
        const auto edge { draw(std::make_unique<luil::modal_host_element>(config), slot, surface) };
        REQUIRE(edge.pixel_at(100, 10) != palette.window_background);
        REQUIRE(edge.pixel_at(100, 10) == edge.pixel_at(100, 60));
        REQUIRE(edge.pixel_at(100, 250) == edge.pixel_at(100, 60));
        const auto plain { draw(std::make_unique<luil::modal_host_element>(config), slot, {}) };
        REQUIRE(plain.pixel_at(100, 10) == palette.window_background);
        REQUIRE(plain.pixel_at(100, 60) == edge.pixel_at(100, 60));
    }
}

TEST_CASE("A tab badge sits before the close slot and keeps its tone", "[ui][raster][tab]")
{
    // slot은 물리 픽셀이다. 탭 폭 160·높이 32 논리 픽셀, 배율 2다. 닫기가 없는 탭은 오른쪽 10을 비우므로 배지는 물리 x 300에서 끝난다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::tab_bar_config config {};
    config.owner = u8"tabs";
    config.tab_width = 160.0f;
    for (const char8_t* key : { u8"dot", u8"count", u8"error", u8"plain" })
        config.items.push_back(luil::tab_item { .key = key });
    config.items[0].badge = luil::tab_badge {};
    config.items[1].badge = luil::tab_badge { .text = u8"3" };
    config.items[2].badge = luil::tab_badge { .tone = luil::badge_tone::error };
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::tab_bar_element>(std::move(config)), { 0.0f, 0.0f, 1280.0f, 64.0f }, raster_scale) };
    luil::testing::raster_frame frame { 1280, 64, palette, raster_scale };
    frame.draw(tree, luil::interaction_snapshot {});

    // 상태 점: 반지름 3.5라 중심이 물리 x 293이다.
    REQUIRE(frame.pixel_at(293, 32) == palette.accent);
    // 개수 알약: 강조 배지는 채운 바탕이다. 글이 없는 test 글꼴에서도 원보다 좁아지지 않는다 (높이 17 → 34).
    REQUIRE(frame.count_color({ 320.0f + 266.0f, 20.0f, 34.0f, 24.0f }, palette.accent) > 100);
    // 심각도 알약은 오류색 테를 두른다 — 색과 함께 글리프 자리도 잡는다.
    REQUIRE(frame.contains_color({ 640.0f + 200.0f, 0.0f, 120.0f, 64.0f }, palette.error_accent));
    // 배지가 없는 탭의 같은 자리는 비어 있다.
    REQUIRE(frame.pixel_at(960 + 293, 32) == palette.window_background);
}

TEST_CASE("A mixed checkbox is a filled box with a dash, not a tick", "[ui][raster][check]")
{
    // 일부 상태는 켜짐과 같은 채움에 가운데 줄이다. 켜짐의 체크 표시와 갈리는 것은 한가운데 픽셀이다 —
    // 줄은 그 자리를 지나고, 체크의 꺾인 선은 지나지 않는다.
    //  - 배율 2에서 표시는 (8, 10)에서 28×28 물리 픽셀이고 한가운데는 (22, 24)다.
    for (const luil::color_theme theme : { luil::color_theme::dark, luil::color_theme::light, luil::color_theme::high_contrast })
    {
        const luil::ui_color_palette palette { luil::color_palette_for(theme) };
        const auto draw_box = [&palette](const bool checked, const bool mixed) {
            luil::check_config config {};
            config.owner = u8"folder";
            config.checked = checked;
            config.mixed = mixed;
            const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::check_element>(std::move(config)), { 0.0f, 0.0f, 200.0f, 48.0f }, raster_scale) };
            luil::testing::raster_frame frame { 200, 48, palette, raster_scale };
            frame.draw(tree, luil::interaction_snapshot {});
            return frame;
        };

        const luil::testing::raster_frame mixed { draw_box(false, true) };
        REQUIRE(mixed.pixel_at(22, 24) == palette.accent_foreground);
        // 줄 위아래는 채움이다.
        REQUIRE(mixed.pixel_at(22, 15) == palette.accent);

        const luil::testing::raster_frame checked { draw_box(true, false) };
        REQUIRE(checked.pixel_at(22, 15) == palette.accent);
        REQUIRE(checked.pixel_at(22, 24) != palette.accent_foreground);
    }
}

TEST_CASE("Slider magnets paint a mark and a ring while the knob holds on", "[ui][raster][slider]")
{
    // 배율 2에서 폭 224·높이 40이면 손잡이 24, 도는 거리 200이다. 트랙은 y 16..24, 표식은
    // 4 × 20(y 10..30), 고리는 지름 44다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_element_id id { luil::application_element_kind(1), u8"slider" };
    const luil::rect_f slot { 0.0f, 0.0f, 224.0f, 40.0f };

    const auto make_tree = [&](const float value) {
        luil::slider_config config {};
        config.value = value;
        config.change = [](const float) { return luil::input_action {}; };
        // 0.1은 채운 쪽(x 32), 0.75는 빈 쪽(x 162)이다.
        config.magnets = { 0.1f, 0.75f };
        return luil::make_arranged_tree(std::make_unique<luil::slider_element>(id, std::move(config)), slot, raster_scale);
    };

    SECTION("marks")
    {
        const luil::ui_tree tree { make_tree(0.25f) };
        luil::testing::raster_frame frame { 224, 40, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});
        // 채운 쪽 표식은 면 색의 홈이다 — 채운 accent 위에서도 온전한 색이다.
        REQUIRE(frame.pixel_at(32, 20) == palette.surface_background);
        REQUIRE(frame.pixel_at(32, 12) == palette.surface_background);
        // 빈 쪽 표식은 `control_border`의 알파라 바탕과 섞인다. 옆자리는 바탕 그대로다.
        REQUIRE(frame.pixel_at(162, 12) != palette.window_background);
        REQUIRE(frame.pixel_at(170, 12) == palette.window_background);
    }

    SECTION("ring")
    {
        const luil::ui_tree tree { make_tree(0.75f) };
        // 손잡이 중심은 x 162, y 20이다. y 4는 손잡이(반지름 12) 밖이고 고리(반지름 22) 안이다.
        luil::testing::raster_frame idle { 224, 40, palette, raster_scale };
        idle.draw(tree, luil::interaction_snapshot {});
        REQUIRE(idle.pixel_at(162, 4) == palette.window_background);

        luil::interaction_snapshot pressing {};
        pressing.pressed = id;
        luil::testing::raster_frame held { 224, 40, palette, raster_scale };
        held.draw(tree, pressing);
        REQUIRE(held.pixel_at(162, 4) != palette.window_background);
    }
}

TEST_CASE("A split handle can keep a thin divider while resting", "[ui][raster][split]")
{
    // 배율 2에서 높이 32의 가로 손잡이다. 쉬는 동안의 선은 1 논리 px(물리 2 px)로 y 15..17, 강조는 2 논리 px로 y 14..18이다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_element_id id { luil::application_element_kind(1), u8"split" };
    const auto draw = [&](const bool resting, const luil::interaction_snapshot& interaction) {
        luil::split_handle_config config {};
        config.axis = luil::split_axis::vertical;
        config.resize = [](const float) { return luil::input_action {}; };
        config.resting_divider = resting;
        const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::split_handle_element>(id, config), { 0.0f, 0.0f, 100.0f, 32.0f }, raster_scale) };
        luil::testing::raster_frame frame { 100, 32, palette, raster_scale };
        frame.draw(tree, interaction);
        return std::array<luil::ui_color, 2> { frame.pixel_at(50, 16), frame.pixel_at(50, 14) };
    };

    // 기본은 쉬는 동안 아무것도 그리지 않는다.
    REQUIRE(draw(false, {})[0] == palette.window_background);
    // 켜면 가운데에 `divider`가 선다. 알파 색이라 바탕과 섞인 값이고, 강조 두께까지 번지지 않는다.
    const auto resting { draw(true, {}) };
    REQUIRE(resting[0] != palette.window_background);
    REQUIRE(resting[0] != palette.accent);
    REQUIRE(resting[1] == palette.window_background);
    // hover 동안은 켜든 끄든 강조다.
    luil::interaction_snapshot hovering {};
    hovering.hovered = id;
    REQUIRE(draw(true, hovering)[0] == palette.accent);
    REQUIRE(draw(true, hovering)[1] == palette.accent);
}

TEST_CASE("A panel can draw its border on chosen sides only", "[ui][raster][panel]")
{
    // 배율 2에서 100 × 60 판이다. 테두리는 1 논리 px(물리 2 px)라 위 변은 y 0..2, 왼 변은 x 0..2다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const auto draw = [&](const luil::panel_border_sides sides) {
        luil::panel_config config {};
        config.background = [](const luil::ui_color_palette& colors) { return colors.surface_background; };
        config.border = [](const luil::ui_color_palette& colors) { return colors.accent; };
        config.border_sides = sides;
        auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { luil::application_element_kind(1), u8"panel" }, config) };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(panel), { 0.0f, 0.0f, 100.0f, 60.0f }, raster_scale) };
        luil::testing::raster_frame frame { 100, 60, palette, raster_scale };
        frame.draw(tree, {});
        // 위 변 · 아래 변 · 왼 변 · 오른 변의 가운데다.
        return std::array<luil::ui_color, 4> { frame.pixel_at(50, 0), frame.pixel_at(50, 59), frame.pixel_at(0, 30), frame.pixel_at(99, 30) };
    };

    // 기본은 네 변 전부다.
    const auto all { draw({}) };
    REQUIRE(all[0] == palette.accent);
    REQUIRE(all[2] == palette.accent);
    // 위·아래만이면 좌우 변은 바탕이다.
    const auto rows { draw({ .top = true, .right = false, .bottom = true, .left = false }) };
    REQUIRE(rows[0] == palette.accent);
    REQUIRE(rows[1] == palette.accent);
    REQUIRE(rows[2] == palette.surface_background);
    REQUIRE(rows[3] == palette.surface_background);
    // 오른쪽만이다.
    const auto right { draw({ .top = false, .right = true, .bottom = false, .left = false }) };
    REQUIRE(right[0] == palette.surface_background);
    REQUIRE(right[3] == palette.accent);
}

TEST_CASE("A palette scope redraws its branch with the adjusted palette", "[ui][raster][palette]")
{
    // 기본 버튼은 `accent` 채움이다. 가지가 accent를 바꾸면 그 안의 버튼만 바뀐 색으로 칠해진다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    // 상수는 static이라 람다가 잡지 않고 읽는다 (MSVC와 clang이 함께 받는 꼴).
    static constexpr luil::ui_color red { luil::make_ui_color(255, 0, 0) };
    static constexpr luil::ui_color blue { luil::make_ui_color(0, 0, 255) };
    const auto to_red = [](luil::ui_color_palette colors, luil::color_theme) {
        colors.accent = red;
        return colors;
    };
    const auto build = [&](const luil::palette_scope_config& outer, const std::optional<luil::palette_scope_config>& inner) {
        auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 120.0f }, raster_scale });
        auto scope { std::make_unique<luil::palette_scope_element>(luil::ui_element_id { luil::application_element_kind(2), u8"scope" }, outer) };
        scope->arrange({ { 0.0f, 0.0f, 200.0f, 120.0f }, raster_scale });
        std::unique_ptr<luil::ui_element> button { make_button({ luil::application_element_kind(1), u8"inside" }, { 20.0f, 20.0f, 160.0f, 56.0f }, true) };
        if (inner.has_value())
        {
            auto nested { std::make_unique<luil::palette_scope_element>(luil::ui_element_id { luil::application_element_kind(2), u8"nested" }, *inner) };
            nested->arrange({ { 0.0f, 0.0f, 200.0f, 120.0f }, raster_scale });
            nested->set_content(std::move(button));
            scope->set_content(std::move(nested));
        }
        else
            scope->set_content(std::move(button));
        root->add(std::move(scope));
        root->add(make_button({ luil::application_element_kind(1), u8"outside" }, { 220.0f, 20.0f, 160.0f, 56.0f }, true));
        return luil::ui_tree { std::move(root) };
    };

    SECTION("inside and outside")
    {
        const luil::ui_tree tree { build({ .palette = to_red }, std::nullopt) };
        luil::testing::raster_frame frame { 400, 120, palette, raster_scale };
        frame.draw(tree, {});
        REQUIRE(frame.pixel_at(100, 48) == red);
        REQUIRE(frame.pixel_at(300, 48) == palette.accent);
    }

    SECTION("nested scopes see the outer change")
    {
        // 안쪽 함수는 바깥이 바꾼 팔레트를 받는다 — 빨강을 받았으면 파랑으로 바꾼다.
        const auto red_to_blue = [](luil::ui_color_palette colors, luil::color_theme) {
            if (colors.accent == red)
                colors.accent = blue;
            return colors;
        };
        const luil::ui_tree tree { build({ .palette = to_red }, luil::palette_scope_config { .palette = red_to_blue }) };
        luil::testing::raster_frame frame { 400, 120, palette, raster_scale };
        frame.draw(tree, {});
        REQUIRE(frame.pixel_at(100, 48) == blue);
    }

    SECTION("high contrast keeps the system colours unless asked")
    {
        const luil::ui_color_palette system { luil::color_palette_for(luil::color_theme::high_contrast) };
        const luil::ui_tree kept { build({ .palette = to_red }, std::nullopt) };
        luil::testing::raster_frame frame { 400, 120, system, raster_scale };
        frame.set_theme(luil::color_theme::high_contrast);
        frame.draw(kept, {});
        REQUIRE(frame.pixel_at(100, 48) == system.accent);
        const luil::ui_tree forced { build({ .palette = to_red, .in_high_contrast = true }, std::nullopt) };
        frame.draw(forced, {});
        REQUIRE(frame.pixel_at(100, 48) == red);
    }
}

TEST_CASE("A derived draw context keeps every field but the palette and scale", "[ui][palette]")
{
    // 문맥에 필드가 늘면 이 test와 `derive_draw_context`를 함께 고친다.
    SkBitmap bitmap {};
    bitmap.allocN32Pixels(1, 1);
    SkCanvas canvas { bitmap };
    const luil::ui_color_palette outer { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_color_palette inner { luil::color_palette_for(luil::color_theme::light) };
    luil::ui_metrics metrics {};
    metrics.body_font_size = 17.0f;
    const luil::draw_context context {
        .canvas = canvas,
        .palette = outer,
        .scale = 1.5f,
        .now = std::chrono::steady_clock::time_point {} + std::chrono::seconds { 3 },
        .maximized = true,
        .fullscreen = false,
        .metrics = metrics,
        .reduce_motion = true,
        .theme = luil::color_theme::light,
    };
    const luil::draw_context derived { luil::derive_draw_context(context, inner, 3.0f) };
    REQUIRE(&derived.canvas == &canvas);
    REQUIRE(&derived.palette == &inner);
    REQUIRE(derived.scale == 3.0f);
    REQUIRE(derived.now == context.now);
    REQUIRE(derived.maximized);
    REQUIRE(derived.metrics.body_font_size == 17.0f);
    REQUIRE(derived.reduce_motion);
    REQUIRE(derived.theme == luil::color_theme::light);
}

TEST_CASE("An invalid text field keeps the error colour on its border even while focused", "[ui][raster][number]")
{
    // 오류 줄은 상자 아래다. 테는 상자에만 긋고 오류 줄까지 내려가지 않는다 (글꼴을 싣지 않아 글자는 보지 않는다).
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_element_id field { luil::application_element_kind(3), u8"seconds" };
    const auto build = [&field](std::u8string error) {
        auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 240.0f, 120.0f }, raster_scale });
        luil::text_input_config config {};
        config.error = std::move(error);
        const float height { (28.0f + luil::text_input_element::error_height_for(config)) * raster_scale };
        auto input { std::make_unique<luil::text_input_element>(field, luil::text_input_view {}, std::move(config)) };
        input->arrange({ { 0.0f, 0.0f, 200.0f, height }, raster_scale });
        root->add(std::move(input));
        return luil::ui_tree { std::move(root) };
    };
    const float box_bottom { 28.0f * raster_scale };

    luil::testing::raster_frame frame { 240, 120, palette, raster_scale };
    frame.draw(build(u8""), {});
    // 보통 테는 반투명한 테두리색이라 바탕과 섞인다. 오류색이 아닌 것만 본다.
    REQUIRE(frame.pixel_at(100, 0) != palette.error_accent);

    const luil::ui_tree invalid { build(u8"범위 밖") };
    frame.draw(invalid, {});
    REQUIRE(frame.pixel_at(100, 0) == palette.error_accent);
    // 상자의 아래 테는 오류 줄 위에 선다. 그 아래 오류 줄의 바탕은 칸 바탕이 아니다.
    REQUIRE(frame.pixel_at(100, static_cast<int>(box_bottom) - 1) == palette.error_accent);
    REQUIRE(frame.pixel_at(100, static_cast<int>(box_bottom) + 8) == palette.window_background);

    luil::interaction_snapshot focused {};
    focused.focused_input = field;
    frame.draw(invalid, focused);
    REQUIRE(frame.pixel_at(100, 0) == palette.error_accent);
    REQUIRE(frame.pixel_at(100, 1) == palette.error_accent);
}
