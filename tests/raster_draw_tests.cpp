#include "raster_probe.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/calendar_element.h"
#include "luil/ui/check_element.h"
#include "luil/ui/color_picker_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/image_element.h"
#include "luil/ui/info_bar_element.h"
#include "luil/ui/layout_metrics.h"
#include "luil/ui/list_element.h"
#include "luil/ui/menu_bar_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/message_box.h"
#include "luil/ui/modal_host_element.h"
#include "luil/ui/palette_scope_element.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/rating_element.h"
#include "luil/ui/scroll_area_element.h"
#include "luil/ui/sheet_host_element.h"
#include "luil/ui/slider_element.h"
#include "luil/ui/split_button_element.h"
#include "luil/ui/split_handle_element.h"
#include "luil/ui/tab_bar_element.h"
#include "luil/ui/teaching_tip_element.h"
#include "luil/ui/text_area_element.h"
#include "luil/ui/text_document.h"
#include "luil/ui/transition.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/virtual_list_element.h"

#include "host/frame_state.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"
#include "include/utils/SkCustomTypeface.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <string_view>
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

    // 분할 버튼 raster 축의 도우미다. 자리는 물리 픽셀이고 화면은 200x80이다 (배율 2).
    //  - 글은 비워 둔다. 도형과 색만 본다.
    //  - 상자는 (20, 20) 160x40이다. 화살표는 오른쪽 논리 28 = 물리 56이라 x가 124~180이고, 본체는 20~124다.
    constexpr luil::rect_f split_box { 20.0f, 20.0f, 160.0f, 40.0f };
    const luil::ui_element_id split_body { luil::ui_element_kind::split_button_body, u8"split" };
    const luil::ui_element_id split_arrow { luil::ui_element_kind::split_button_arrow, u8"split" };

    [[nodiscard]] luil::ui_tree tree_with(std::unique_ptr<luil::ui_element> element)
    {
        auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 80.0f }, raster_scale });
        root->add(std::move(element));
        return luil::ui_tree { std::move(root) };
    }

    [[nodiscard]] luil::split_button_config split_config()
    {
        luil::split_button_config config {};
        config.owner = u8"split";
        config.invoke = [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; };
        config.set_open = [](const bool) { return luil::input_action {}; };
        return config;
    }

    [[nodiscard]] luil::testing::raster_frame draw_split(luil::split_button_config config, const luil::ui_color_palette& palette, const luil::interaction_snapshot& interaction = {})
    {
        auto split { std::make_unique<luil::split_button_element>(std::move(config)) };
        split->arrange({ split_box, raster_scale });
        const luil::ui_tree tree { tree_with(std::move(split)) };
        luil::testing::raster_frame frame { 200, 80, palette, raster_scale };
        frame.draw(tree, interaction);
        return frame;
    }

    // 같은 설정의 글자 버튼 한 상자에서 읽은 바탕색이다. 분할 버튼의 두 절반이 이 색과 같아야 한 dialog에 나란히 서도 어긋나지 않는다.
    [[nodiscard]] luil::ui_color reference_fill(const luil::text_button_config& look, const luil::ui_color_palette& palette, const bool enabled = true, const bool pressed = false, const bool hovered = false)
    {
        const luil::ui_element_id id { luil::application_element_kind(1), u8"reference" };
        auto button { std::make_unique<luil::text_button_element>(id, look) };
        button->arrange({ split_box, raster_scale });
        button->set_enabled(enabled);
        const luil::ui_tree tree { tree_with(std::move(button)) };
        luil::interaction_snapshot interaction {};
        if (pressed)
            interaction.pressed = id;
        if (hovered)
            interaction.hovered = id;
        luil::testing::raster_frame frame { 200, 80, palette, raster_scale };
        frame.draw(tree, interaction);
        return frame.pixel_at(70, 40);
    }

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

TEST_CASE("A split button is one rounded box cut into a body and an arrow with one divider line", "[ui][raster][split-button]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::testing::raster_frame frame { draw_split(split_config(), palette) };

    const luil::ui_color body_fill { frame.pixel_at(70, 40) };
    const luil::ui_color arrow_fill { frame.pixel_at(152, 40) };
    // 두 절반은 글자 버튼과 같은 보통 바탕이다. 무엇인가 그려졌고 배경과 다르다.
    REQUIRE(body_fill == reference_fill({}, palette));
    REQUIRE(arrow_fill == body_fill);
    REQUIRE(body_fill != palette.window_background);

    // 구분선은 화살표 왼쪽 가장자리의 1 논리 픽셀(물리 2) 세로선 한 줄이다.
    REQUIRE(frame.pixel_at(123, 40) == body_fill);
    REQUIRE(frame.pixel_at(124, 40) != body_fill);
    REQUIRE(frame.pixel_at(124, 40) == frame.pixel_at(125, 40));
    REQUIRE(frame.pixel_at(126, 40) == arrow_fill);

    // 바깥 모서리만 둥글다. 안쪽 가장자리는 곧아서 위아래 끝까지 칠한다.
    REQUIRE(frame.pixel_at(20, 20) == palette.window_background);
    REQUIRE(frame.pixel_at(179, 20) == palette.window_background);
    REQUIRE(frame.pixel_at(20, 59) == palette.window_background);
    REQUIRE(frame.pixel_at(179, 59) == palette.window_background);
    REQUIRE(frame.pixel_at(123, 20) == body_fill);
    REQUIRE(frame.pixel_at(123, 59) == body_fill);
    REQUIRE(frame.pixel_at(124, 20) != palette.window_background);
    REQUIRE(frame.pixel_at(124, 59) != palette.window_background);
}

TEST_CASE("A split button without a body is one rounded box with no divider", "[ui][raster][split-button]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::split_button_config config { split_config() };
    config.invoke = nullptr;
    const luil::testing::raster_frame frame { draw_split(std::move(config), palette) };
    const luil::ui_color fill { frame.pixel_at(70, 40) };
    REQUIRE(fill == reference_fill({}, palette));
    // 선 없이 바탕 하나다.
    REQUIRE(frame.pixel_at(124, 40) == fill);
    REQUIRE(frame.pixel_at(152, 40) == fill);
    // 네 모서리가 모두 둥글다.
    for (const auto& [x, y] : { std::pair { 20, 20 }, std::pair { 179, 20 }, std::pair { 20, 59 }, std::pair { 179, 59 } })
    {
        CAPTURE(x, y);
        REQUIRE(frame.pixel_at(x, y) == palette.window_background);
    }
}

TEST_CASE("The two halves of a split button hover and press on their own", "[ui][raster][split-button]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_color plain { reference_fill({}, palette) };
    const luil::ui_color hovered { reference_fill({}, palette, true, false, true) };
    const luil::ui_color pressed { reference_fill({}, palette, true, true, false) };
    REQUIRE(hovered != plain);
    REQUIRE(pressed != plain);

    luil::interaction_snapshot over_body {};
    over_body.hovered = split_body;
    const luil::testing::raster_frame body_hover { draw_split(split_config(), palette, over_body) };
    REQUIRE(body_hover.pixel_at(70, 40) == hovered);
    REQUIRE(body_hover.pixel_at(152, 40) == plain);

    luil::interaction_snapshot down_arrow {};
    down_arrow.pressed = split_arrow;
    const luil::testing::raster_frame arrow_press { draw_split(split_config(), palette, down_arrow) };
    REQUIRE(arrow_press.pixel_at(152, 40) == pressed);
    REQUIRE(arrow_press.pixel_at(70, 40) == plain);
}

TEST_CASE("While the menu is open the arrow wears the pressed fill even with the pointer on the body", "[ui][raster][split-button]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::split_button_config config { split_config() };
    config.open = true;
    luil::interaction_snapshot over_body {};
    over_body.hovered = split_body;
    const luil::testing::raster_frame frame { draw_split(std::move(config), palette, over_body) };
    // 열린 동안 화살표는 눌림 바탕이고, 포인터가 본체 위여도 두 절반은 따로 선다.
    REQUIRE(frame.pixel_at(152, 40) == reference_fill({}, palette, true, true, false));
    REQUIRE(frame.pixel_at(70, 40) == reference_fill({}, palette, true, false, true));

    // 닫힌 뒤에는 보통 바탕이다.
    const luil::testing::raster_frame closed { draw_split(split_config(), palette) };
    REQUIRE(closed.pixel_at(152, 40) == reference_fill({}, palette));
}

TEST_CASE("A split button takes the fill of the look it is given", "[ui][raster][split-button]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };

    for (const luil::text_button_visual visual : { luil::text_button_visual::accent, luil::text_button_visual::danger })
    {
        CAPTURE(visual);
        luil::split_button_config config { split_config() };
        config.look.visual = visual;
        const luil::testing::raster_frame frame { draw_split(config, palette) };
        const luil::ui_color expected { reference_fill({ .visual = visual }, palette) };
        REQUIRE(expected != reference_fill({}, palette));
        REQUIRE(frame.pixel_at(70, 40) == expected);
        REQUIRE(frame.pixel_at(152, 40) == expected);
    }

    // 기본 버튼은 두 절반이 함께 강조색으로 채워지고 구분선은 그 위의 글자 색이다.
    luil::split_button_config primary { split_config() };
    primary.look.default_button = true;
    const luil::testing::raster_frame filled { draw_split(std::move(primary), palette) };
    REQUIRE(filled.pixel_at(70, 40) == palette.accent);
    REQUIRE(filled.pixel_at(152, 40) == palette.accent);
    REQUIRE(filled.pixel_at(124, 40) == palette.accent_foreground);

    // 상자가 없는 link는 normal로 읽는다.
    luil::split_button_config link { split_config() };
    link.look.visual = luil::text_button_visual::link;
    const luil::testing::raster_frame linked { draw_split(std::move(link), palette) };
    REQUIRE(linked.pixel_at(70, 40) == reference_fill({}, palette));
}

TEST_CASE("A fill selector paints both halves the same and stays through hover", "[ui][raster][split-button]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_color custom { luil::make_ui_color(10, 200, 30) };
    luil::split_button_config config { split_config() };
    config.look.fill = [](const luil::ui_color_palette&) { return luil::make_ui_color(10, 200, 30); };
    luil::interaction_snapshot over_arrow {};
    over_arrow.hovered = split_arrow;
    const luil::testing::raster_frame frame { draw_split(std::move(config), palette, over_arrow) };
    REQUIRE(frame.pixel_at(70, 40) == custom);
    REQUIRE(frame.pixel_at(152, 40) == custom);
}

TEST_CASE("A disabled split button and a disabled body fill like a disabled text button", "[ui][raster][split-button]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_color disabled { reference_fill({}, palette, false, true, true) };

    luil::interaction_snapshot over_both {};
    over_both.hovered = split_arrow;
    over_both.pressed = split_body;
    luil::split_button_config off { split_config() };
    off.enabled = false;
    const luil::testing::raster_frame all_off { draw_split(std::move(off), palette, over_both) };
    // 꺼진 절반은 hover·눌림 표시를 받지 않는다.
    REQUIRE(all_off.pixel_at(70, 40) == disabled);
    REQUIRE(all_off.pixel_at(152, 40) == disabled);

    // 본체만 꺼진 버튼은 화살표만 살아 hover가 선다.
    luil::split_button_config body_off { split_config() };
    body_off.invoke_enabled = false;
    const luil::testing::raster_frame half_off { draw_split(std::move(body_off), palette, over_both) };
    REQUIRE(half_off.pixel_at(70, 40) == disabled);
    REQUIRE(half_off.pixel_at(152, 40) == reference_fill({}, palette, true, false, true));
}

TEST_CASE("A split button whose body is off does not claim the Enter fill", "[ui][raster][split-button]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::split_button_config config { split_config() };
    config.look.default_button = true;
    config.invoke_enabled = false;
    const luil::testing::raster_frame frame { draw_split(std::move(config), palette) };
    // Enter가 갈 곳이 꺼졌으니 어느 절반도 강조색으로 약속하지 않는다.
    REQUIRE(frame.pixel_at(70, 40) != palette.accent);
    REQUIRE(frame.pixel_at(152, 40) != palette.accent);
    REQUIRE(frame.pixel_at(152, 40) == reference_fill({}, palette));
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

namespace {
    // 정보 막대 raster 축의 도우미다. 화면은 840x200이고 배율은 2다 (자리는 물리 픽셀).
    //  - 글은 비워 둔다. 글꼴을 싣지 않으면 글리프도 그려지지 않으므로 이 축이 보는 것은 바탕·테·단추의 색이다.
    //    글리프가 네 심각도마다 다르다는 것은 `severity_paint_tests.cpp`가 값으로 잠근다.
    //  - 막대 하나는 논리 400x40이고 (20, 20)에 선다. 오른쪽 가장자리는 물리 x 820, 첫 줄 가운데는 물리 y 60이다.
    constexpr luil::rect_f info_bar_box { 20.0f, 20.0f, 800.0f, 80.0f };
    const luil::ui_element_id info_bar_close_id { luil::ui_element_kind::info_bar_close, u8"bar" };

    [[nodiscard]] luil::testing::raster_frame draw_info_bar(luil::info_bar_config config, const luil::ui_color_palette& palette, const luil::rect_f& slot = info_bar_box, const luil::interaction_snapshot& interaction = {})
    {
        config.owner = u8"bar";
        auto bar { std::make_unique<luil::info_bar_element>(std::move(config)) };
        bar->arrange({ slot, raster_scale });
        auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 840.0f, 200.0f }, raster_scale });
        root->add(std::move(bar));
        const luil::ui_tree tree { std::move(root) };
        luil::testing::raster_frame frame { 840, 200, palette, raster_scale };
        frame.draw(tree, interaction);
        return frame;
    }

    // 심각도가 나르는 팔레트 역할이다. 라이브러리의 helper를 거치지 않고 다시 적어 둔 값이라 helper가 어긋나면 드러난다.
    [[nodiscard]] luil::ui_color severity_role(const luil::ui_color_palette& palette, const luil::ui_severity severity)
    {
        switch (severity)
        {
        case luil::ui_severity::success:
            return palette.accent;
        case luil::ui_severity::warning:
            return palette.warning_accent;
        case luil::ui_severity::error:
            return palette.error_accent;
        case luil::ui_severity::info:
            break;
        }
        return palette.secondary_foreground;
    }

    // 반투명한 색을 불투명한 바탕 위에 얹은 색이다. 보조 글자색은 알파가 있어 테가 바탕과 섞여 그려진다.
    [[nodiscard]] luil::ui_color over(const luil::ui_color foreground, const luil::ui_color background) noexcept
    {
        const std::uint32_t alpha { foreground >> 24U };
        const auto channel = [&](const std::uint32_t shift) {
            const std::uint32_t top { (foreground >> shift) & 0xFFU };
            const std::uint32_t bottom { (background >> shift) & 0xFFU };
            return (top * alpha + bottom * (255U - alpha) + 127U) / 255U;
        };
        return 0xFF000000U | (channel(16U) << 16U) | (channel(8U) << 8U) | channel(0U);
    }

    // 반올림 오차(채널당 둘)까지는 같은 색이다.
    [[nodiscard]] bool same_color(const luil::ui_color actual, const luil::ui_color expected) noexcept
    {
        for (const std::uint32_t shift : { 0U, 8U, 16U })
        {
            const int difference { static_cast<int>((actual >> shift) & 0xFFU) - static_cast<int>((expected >> shift) & 0xFFU) };
            if (std::abs(difference) > 2)
                return false;
        }
        return true;
    }
}

TEST_CASE("An info bar is a neutral raised body with a one pixel border in the severity color", "[ui][raster][info-bar]")
{
    for (const luil::color_theme theme : { luil::color_theme::dark, luil::color_theme::light })
    {
        const luil::ui_color_palette palette { luil::color_palette_for(theme) };
        for (const luil::ui_severity severity : { luil::ui_severity::info, luil::ui_severity::success, luil::ui_severity::warning, luil::ui_severity::error })
        {
            CAPTURE(theme, severity);
            const luil::testing::raster_frame frame { draw_info_bar(luil::info_bar_config { .severity = severity }, palette) };
            // 테는 바탕 위에 칠해진다. 보조 글자색(info)은 알파가 있어 바탕과 섞인다.
            const luil::ui_color border { over(severity_role(palette, severity), palette.raised_background) };
            // 몸은 심각도와 무관한 올라온 중립 바탕이다.
            REQUIRE(frame.pixel_at(400, 60) == palette.raised_background);
            REQUIRE(border != palette.raised_background);
            // 테는 막대 안쪽에 온전히 들어온 1 논리 픽셀(물리 2)이다. 네 변 모두다.
            const std::array samples { frame.pixel_at(20, 60), frame.pixel_at(21, 60), frame.pixel_at(819, 60), frame.pixel_at(400, 20), frame.pixel_at(400, 99) };
            for (const luil::ui_color sample : samples)
            {
                CAPTURE(sample, border);
                REQUIRE(same_color(sample, border));
            }
            REQUIRE(frame.pixel_at(22, 60) == palette.raised_background);
            // 막대 밖은 창 바탕이다.
            REQUIRE(frame.pixel_at(10, 60) == palette.window_background);
            REQUIRE(frame.pixel_at(400, 110) == palette.window_background);
        }
    }
}

TEST_CASE("An info bar keeps its border when high contrast folds the body into the window", "[ui][raster][info-bar]")
{
    luil::high_contrast_colors colors {};
    colors.window_background = luil::make_ui_color(0, 0, 0);
    colors.window_foreground = luil::make_ui_color(255, 255, 255);
    colors.emphasis = luil::make_ui_color(0x37, 0xDC, 0xFF);
    const luil::ui_color_palette palette { luil::high_contrast_palette_for(colors) };

    // 올라온 면은 창 바탕으로 접힌다. 테가 없으면 막대가 표면에서 사라진다.
    REQUIRE(palette.raised_background == palette.window_background);
    for (const luil::ui_severity severity : { luil::ui_severity::info, luil::ui_severity::success, luil::ui_severity::warning, luil::ui_severity::error })
    {
        CAPTURE(severity);
        const luil::testing::raster_frame frame { draw_info_bar(luil::info_bar_config { .severity = severity }, palette) };
        const luil::ui_color border { severity_role(palette, severity) };
        REQUIRE(border != palette.window_background);
        REQUIRE(frame.pixel_at(20, 60) == border);
        REQUIRE(frame.pixel_at(400, 20) == border);
        REQUIRE(frame.pixel_at(400, 60) == palette.window_background);
        // info·warning·error는 같은 전경색이다. 둘을 가르는 것은 글리프의 몫이다.
        if (severity != luil::ui_severity::success)
            REQUIRE(border == colors.window_foreground);
    }
}

TEST_CASE("An info bar draws its close button and its action button as ordinary buttons", "[ui][raster][info-bar]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::info_bar_config config {};
    config.action_label = u8"Retry";
    config.action = [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; };
    config.close = [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; };

    // 닫기는 쉬는 동안 바탕이 없고(글리프만 선다) hover에서 도구 막대의 hover 바탕이 깔린다.
    const luil::testing::raster_frame resting { draw_info_bar(config, palette) };
    REQUIRE(resting.pixel_at(780, 60) == palette.raised_background);
    luil::interaction_snapshot over_close {};
    over_close.hovered = info_bar_close_id;
    const luil::testing::raster_frame hovered { draw_info_bar(config, palette, info_bar_box, over_close) };
    const luil::ui_color hover_fill { over(palette.button_hover_background, palette.raised_background) };
    REQUIRE(hover_fill != palette.raised_background);
    REQUIRE(same_color(hovered.pixel_at(780, 60), hover_fill));

    // 동작 단추는 옆에 서고 보통 글자 버튼의 바탕(입력 표면)을 쓴다 (논리 폭 64, 닫기 왼쪽 8).
    const luil::ui_color plain { over(palette.input_background, palette.raised_background) };
    REQUIRE(plain != palette.raised_background);
    REQUIRE(same_color(resting.pixel_at(630, 60), plain));
    REQUIRE(resting.pixel_at(600, 60) == palette.raised_background);
    // 단추 높이는 논리 28(물리 56)이라 첫 줄 가운데 위아래로 서고, 막대 안쪽에 머문다.
    REQUIRE(same_color(resting.pixel_at(630, 34), plain));
    REQUIRE(resting.pixel_at(630, 30) == palette.raised_background);
}

TEST_CASE("An info bar draws a narrow bar's action button on its own line under the text", "[ui][raster][info-bar]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::info_bar_config config {};
    config.action_label = u8"Retry";
    config.action = [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; };
    config.close = [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; };

    // 논리 300 폭이면 글 폭이 200 아래라 단추가 아래 줄로 간다. 글이 비어도 첫 줄은 있어 단추는 그 아래 8에 선다.
    // 막대는 논리 높이 76(위 10.5 + 첫 줄 19 + 8 + 단추 28 + 아래 10.5)이다.
    const luil::rect_f slot { 20.0f, 20.0f, 600.0f, 152.0f };
    const luil::testing::raster_frame frame { draw_info_bar(config, palette, slot) };
    const luil::ui_color plain { over(palette.input_background, palette.raised_background) };
    // 단추는 글의 왼쪽(물리 x 20 + 76)에서 시작해 물리 128 폭이다. 위쪽은 y 20 + 2 * 37.5 = 95다.
    REQUIRE(same_color(frame.pixel_at(130, 120), plain));
    REQUIRE(frame.pixel_at(130, 92) == palette.raised_background);
    REQUIRE(frame.pixel_at(90, 120) == palette.raised_background);
    // 옆에 서던 자리는 비어 있다.
    REQUIRE(frame.pixel_at(450, 60) == palette.raised_background);
}

namespace {
    // 확인 대화 상자를 그린다. 글은 비워 둔다 (이 축이 보는 것은 도형과 색이다). 화면은 800x400 물리 픽셀이고 배율은 2다.
    //  - 상자는 논리 360 x 88(위 20 + 아래 20 + 단추 28 + 20 + 20)이라 물리 (40, 112)에서 720 x 176이다.
    //  - 단추는 논리 88 폭이고 오른쪽 끝(논리 360)에 붙는다. 마지막 단추는 물리 x 544~720, 그 앞 단추는 352~528이다.
    constexpr luil::rect_f message_box_last_button { 544.0f, 192.0f, 176.0f, 56.0f };
    constexpr luil::rect_f message_box_previous_button { 352.0f, 192.0f, 176.0f, 56.0f };

    [[nodiscard]] luil::testing::raster_frame draw_message_box(std::vector<luil::message_box_button> buttons, const luil::ui_color_palette& palette)
    {
        luil::message_box_config config {};
        config.owner = u8"raster";
        config.buttons = std::move(buttons);
        config.result = [](const std::u8string_view) { return luil::input_action {}; };
        std::unique_ptr<luil::modal_host_element> host { luil::make_message_box(std::move(config)) };
        host->arrange({ { 0.0f, 0.0f, 800.0f, 400.0f }, raster_scale });
        const luil::ui_tree tree { std::move(host) };
        luil::testing::raster_frame frame { 800, 400, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});
        return frame;
    }
}

TEST_CASE("A message box stands on the surface color inside a one pixel divider border over the scrim", "[ui][raster][message-box]")
{
    for (const luil::color_theme theme : { luil::color_theme::dark, luil::color_theme::light })
    {
        CAPTURE(theme);
        const luil::ui_color_palette palette { luil::color_palette_for(theme) };
        const luil::testing::raster_frame frame { draw_message_box({ luil::message_box_cancel(), luil::message_box_ok() }, palette) };

        // 몸은 표면 색이다. 단추가 서지 않는 왼쪽 위 안쪽이다.
        REQUIRE(frame.pixel_at(120, 140) == palette.surface_background);
        // 테는 몸 안쪽의 1 논리 픽셀(물리 2)이고 네 변 모두다. 테 색은 표면 위에 얹힌 `divider`다.
        const luil::ui_color border { over(palette.divider, palette.surface_background) };
        REQUIRE(border != palette.surface_background);
        const std::array samples { frame.pixel_at(40, 200), frame.pixel_at(41, 200), frame.pixel_at(758, 200), frame.pixel_at(759, 200), frame.pixel_at(400, 112), frame.pixel_at(400, 113), frame.pixel_at(400, 286), frame.pixel_at(400, 287) };
        for (const luil::ui_color sample : samples)
        {
            CAPTURE(sample, border);
            REQUIRE(same_color(sample, border));
        }
        // 상자 밖은 scrim이다. 표면과도 창 바탕과도 다르다.
        const luil::ui_color scrim { frame.pixel_at(10, 10) };
        REQUIRE(scrim != palette.surface_background);
        REQUIRE(scrim != palette.window_background);
        REQUIRE(frame.pixel_at(30, 200) == scrim);
    }
}

TEST_CASE("Only the last accept button is filled with the accent and a destructive one wears the danger fill", "[ui][raster][message-box]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };

    SECTION("마지막 긍정 단추가 강조색 채움이고 거절은 채우지 않는다")
    {
        const luil::testing::raster_frame frame { draw_message_box({ luil::message_box_cancel(), luil::message_box_ok() }, palette) };
        REQUIRE(frame.pixel_at(632, 220) == palette.accent);
        REQUIRE(frame.count_color(message_box_last_button, palette.accent) > 176 * 56 / 2);
        REQUIRE(frame.count_color(message_box_previous_button, palette.accent) == 0);
        REQUIRE(frame.pixel_at(440, 220) != palette.surface_background);
    }

    SECTION("긍정이 둘이면 마지막만 채우고 앞의 것은 옅은 강조다")
    {
        const luil::testing::raster_frame frame { draw_message_box({ luil::message_box_yes(), luil::message_box_ok() }, palette) };
        REQUIRE(frame.count_color(message_box_last_button, palette.accent) > 176 * 56 / 2);
        REQUIRE(frame.count_color(message_box_previous_button, palette.accent) == 0);
        REQUIRE(frame.pixel_at(440, 220) != palette.surface_background);
    }

    SECTION("위험한 단추는 강조색으로 채우지 않는다")
    {
        const luil::testing::raster_frame frame { draw_message_box({ luil::message_box_cancel(), luil::message_box_button { .key = u8"erase", .role = luil::message_box_role::destructive } }, palette) };
        REQUIRE(frame.count_color(message_box_last_button, palette.accent) == 0);
        REQUIRE(frame.count_color(message_box_previous_button, palette.accent) == 0);
        // 옅은 오류색 바탕이 깔려 있다. 표면 색 그대로가 아니다.
        REQUIRE(frame.pixel_at(632, 220) != palette.surface_background);
        // 거절 단추의 보통 바탕과도 다르다.
        REQUIRE(frame.pixel_at(632, 220) != frame.pixel_at(440, 220));
    }
}

namespace {
    // 별 다섯짜리 별점이다. 배율 2에서 칸이 56이라 폭 280, 슬롯 300 x 60 안에서 왼쪽에 붙는다.
    // 별 `n`(1부터)의 칸은 x 56(n-1)..56n이고 별의 가로 폭은 칸보다 16 작은 40이다. 가운데는 (56n-28, 30)이다.
    [[nodiscard]] luil::ui_tree make_rating_tree(const float value, const luil::rating_precision precision = luil::rating_precision::whole, const bool display_only = false)
    {
        luil::rating_config config {};
        config.owner = u8"stars";
        config.value = value;
        config.precision = precision;
        if (display_only == false)
            config.change_to = [](const float) { return luil::input_action {}; };
        return luil::make_arranged_tree(std::make_unique<luil::rating_element>(std::move(config)), { 0.0f, 0.0f, 300.0f, 60.0f }, raster_scale);
    }

    [[nodiscard]] luil::ui_element_id rating_cell(const char8_t digit)
    {
        std::u8string owner { u8"stars" };
        owner += u8'\u001f';
        owner += digit;
        return { luil::ui_element_kind::rating_cell, std::move(owner) };
    }

    [[nodiscard]] int star_center_x(const int number)
    {
        return 56 * number - 28;
    }

    // 영역 안에서 기대한 색에 가까운 픽셀 수다. 보조 글자색은 알파가 있어 선이 바탕과 섞여 그려지므로 `over`로 섞은 색을 준다.
    [[nodiscard]] int count_near(const luil::testing::raster_frame& frame, const luil::rect_f& area, const luil::ui_color expected)
    {
        int count { 0 };
        for (int y = static_cast<int>(area.y); y < static_cast<int>(area.y + area.height); ++y)
        {
            for (int x = static_cast<int>(area.x); x < static_cast<int>(area.x + area.width); ++x)
            {
                if (same_color(frame.pixel_at(x, y), expected))
                    ++count;
            }
        }
        return count;
    }
}

TEST_CASE("Filled stars are the accent face and empty stars are a line", "[ui][raster][rating]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_tree tree { make_rating_tree(2.0f) };
    luil::testing::raster_frame frame { 300, 60, palette, raster_scale };
    frame.draw(tree, luil::interaction_snapshot {});

    // 채운 별은 면이다. 가운데가 accent이고 선 색은 없다.
    REQUIRE(frame.pixel_at(star_center_x(1), 30) == palette.accent);
    REQUIRE(frame.pixel_at(star_center_x(2), 30) == palette.accent);
    const luil::ui_color outline { over(palette.secondary_foreground, palette.window_background) };
    REQUIRE(outline != palette.window_background);
    REQUIRE(count_near(frame, { 0.0f, 0.0f, 112.0f, 60.0f }, outline) == 0);
    // 빈 별은 선이다. 가운데가 바탕이고 둘레에 secondary_foreground 선이 선다.
    REQUIRE(frame.pixel_at(star_center_x(3), 30) == palette.window_background);
    REQUIRE(frame.pixel_at(star_center_x(5), 30) == palette.window_background);
    REQUIRE(count_near(frame, { 112.0f, 0.0f, 56.0f, 60.0f }, outline) > 0);
    REQUIRE(frame.count_color({ 112.0f, 0.0f, 56.0f, 60.0f }, palette.accent) == 0);
    // 슬롯의 남는 자리(별 줄 밖)에는 아무것도 그리지 않는다.
    REQUIRE(frame.pixel_at(290, 30) == palette.window_background);
}

TEST_CASE("A fractional value fills only part of the last star", "[ui][raster][rating]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    // 4.3이면 별 넷과 다섯째의 30%다. 다섯째 별의 가로 폭은 x 232..272이고 30% 지점이 244다.
    const luil::ui_tree tree { make_rating_tree(4.3f) };
    luil::testing::raster_frame frame { 300, 60, palette, raster_scale };
    frame.draw(tree, luil::interaction_snapshot {});
    REQUIRE(frame.pixel_at(star_center_x(4), 30) == palette.accent);
    REQUIRE(frame.count_color({ 232.0f, 0.0f, 12.0f, 60.0f }, palette.accent) > 0);
    REQUIRE(frame.count_color({ 248.0f, 0.0f, 32.0f, 60.0f }, palette.accent) == 0);
    // 나머지 70%는 빈 별의 선이다.
    REQUIRE(count_near(frame, { 248.0f, 0.0f, 32.0f, 60.0f }, over(palette.secondary_foreground, palette.window_background)) > 0);
    REQUIRE(frame.pixel_at(star_center_x(5), 30) == palette.window_background);

    // 반 별은 별을 정확히 절반으로 가른다. 셋째 별의 폭은 x 120..160이고 가운데가 140이다.
    const luil::ui_tree half_tree { make_rating_tree(2.5f, luil::rating_precision::half) };
    luil::testing::raster_frame half { 300, 60, palette, raster_scale };
    half.draw(half_tree, luil::interaction_snapshot {});
    REQUIRE(half.count_color({ 120.0f, 0.0f, 18.0f, 60.0f }, palette.accent) > 0);
    REQUIRE(half.count_color({ 142.0f, 0.0f, 20.0f, 60.0f }, palette.accent) == 0);
}

TEST_CASE("A star the pointer is over previews its value in the hover accent", "[ui][raster][rating]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    REQUIRE(palette.accent_hover != palette.accent);
    const luil::ui_tree tree { make_rating_tree(1.0f) };

    // 칸 3 위에서는 별 셋이 accent_hover로 채워진다. 지금 값(1)의 accent는 남지 않는다.
    luil::interaction_snapshot hovering {};
    hovering.hovered = rating_cell(u8'3');
    luil::testing::raster_frame preview { 300, 60, palette, raster_scale };
    preview.draw(tree, hovering);
    REQUIRE(preview.pixel_at(star_center_x(1), 30) == palette.accent_hover);
    REQUIRE(preview.pixel_at(star_center_x(3), 30) == palette.accent_hover);
    REQUIRE(preview.pixel_at(star_center_x(4), 30) == palette.window_background);
    REQUIRE(preview.count_color({ 0.0f, 0.0f, 300.0f, 60.0f }, palette.accent) == 0);

    // 칸의 값이 현재 값보다 작으면 사이의 별이 빈 별로 돌아간다.
    const luil::ui_tree high { make_rating_tree(4.0f) };
    luil::testing::raster_frame lower { 300, 60, palette, raster_scale };
    lower.draw(high, hovering);
    REQUIRE(lower.pixel_at(star_center_x(3), 30) == palette.accent_hover);
    REQUIRE(lower.pixel_at(star_center_x(4), 30) == palette.window_background);

    // 반 별의 칸 하나는 반 별이다. 칸 5는 2.5다.
    const luil::ui_tree half_tree { make_rating_tree(0.0f, luil::rating_precision::half) };
    luil::interaction_snapshot half_hover {};
    half_hover.hovered = rating_cell(u8'5');
    luil::testing::raster_frame half { 300, 60, palette, raster_scale };
    half.draw(half_tree, half_hover);
    REQUIRE(half.pixel_at(star_center_x(2), 30) == palette.accent_hover);
    REQUIRE(half.count_color({ 120.0f, 0.0f, 18.0f, 60.0f }, palette.accent_hover) > 0);
    REQUIRE(half.count_color({ 142.0f, 0.0f, 20.0f, 60.0f }, palette.accent_hover) == 0);

    // 누른 동안은 미리 보기를 끄고 값을 그린다. 끌기가 서면 hover가 누른 칸에 머물기 때문이다.
    luil::interaction_snapshot pressing {};
    pressing.hovered = rating_cell(u8'3');
    pressing.pressed = rating_cell(u8'3');
    luil::testing::raster_frame held { 300, 60, palette, raster_scale };
    held.draw(tree, pressing);
    REQUIRE(held.pixel_at(star_center_x(1), 30) == palette.accent);
    REQUIRE(held.pixel_at(star_center_x(3), 30) == palette.window_background);
}

TEST_CASE("A display only or disabled rating never previews and a disabled one is gray", "[ui][raster][rating]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::interaction_snapshot hovering {};
    hovering.hovered = rating_cell(u8'3');

    // 표시 전용에는 칸이 없다. 같은 id가 와도 미리 보기가 서지 않는다.
    const luil::ui_tree display { make_rating_tree(2.0f, luil::rating_precision::whole, true) };
    luil::testing::raster_frame shown { 300, 60, palette, raster_scale };
    shown.draw(display, hovering);
    REQUIRE(shown.pixel_at(star_center_x(2), 30) == palette.accent);
    REQUIRE(shown.pixel_at(star_center_x(3), 30) == palette.window_background);

    luil::rating_config config {};
    config.owner = u8"stars";
    config.value = 2.0f;
    config.change_to = [](const float) { return luil::input_action {}; };
    auto rating { std::make_unique<luil::rating_element>(std::move(config)) };
    rating->set_enabled(false);
    const luil::ui_tree disabled { luil::make_arranged_tree(std::move(rating), { 0.0f, 0.0f, 300.0f, 60.0f }, raster_scale) };
    luil::testing::raster_frame gray { 300, 60, palette, raster_scale };
    gray.draw(disabled, hovering);
    // 채움과 선이 모두 비활성 색이다. 비활성 색은 알파가 있어 바탕과 섞인다. 미리 보기는 서지 않는다.
    const luil::ui_color gray_color { over(palette.disabled_foreground, palette.window_background) };
    REQUIRE(same_color(gray.pixel_at(star_center_x(2), 30), gray_color));
    REQUIRE(gray.pixel_at(star_center_x(3), 30) == palette.window_background);
    REQUIRE(count_near(gray, { 112.0f, 0.0f, 56.0f, 60.0f }, gray_color) > 0);
    REQUIRE(count_near(gray, { 112.0f, 0.0f, 56.0f, 60.0f }, over(palette.secondary_foreground, palette.window_background)) == 0);
}

namespace {
    // 알파를 켠 색 고르기다. 배율 2에서 판은 (0, 0, 400, 400), 색상 막대는 y 416..448, 알파 막대는 y 464..496이다.
    [[nodiscard]] luil::ui_tree make_picker_tree(const luil::hsv_color value, const bool enabled = true)
    {
        luil::color_picker_config config {};
        config.owner = u8"paint";
        config.value = value;
        config.alpha = true;
        config.change_to = [](const luil::hsv_color) { return luil::input_action {}; };
        auto picker { std::make_unique<luil::color_picker_element>(std::move(config)) };
        picker->set_enabled(enabled);
        return luil::make_arranged_tree(std::move(picker), { 0.0f, 0.0f, 500.0f, 700.0f }, raster_scale);
    }

    // 채널마다 허용치 안인가다. 그라디언트는 픽셀 가운데에서 샘플하므로 정확한 정지점과 조금 어긋난다.
    [[nodiscard]] bool close_to(const luil::ui_color actual, const luil::ui_color expected, const int tolerance) noexcept
    {
        for (const std::uint32_t shift : { 0U, 8U, 16U })
        {
            const int difference { static_cast<int>((actual >> shift) & 0xFFU) - static_cast<int>((expected >> shift) & 0xFFU) };
            if (std::abs(difference) > tolerance)
                return false;
        }
        return true;
    }
}

TEST_CASE("The color plane is white at the top left, the pure hue at the top right and black at the bottom", "[ui][raster][color-picker]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    // 손잡이는 오른쪽 아래(360, 360)에 있어 표본 자리를 덮지 않는다.
    const luil::ui_tree tree { make_picker_tree({ 200.0f, 0.9f, 0.1f, 0.75f }) };
    luil::testing::raster_frame frame { 500, 700, palette, raster_scale };
    frame.draw(tree, luil::interaction_snapshot {});
    const luil::ui_color white { luil::make_ui_color(255, 255, 255) };
    REQUIRE(close_to(frame.pixel_at(4, 4), white, 8));
    REQUIRE(close_to(frame.pixel_at(395, 4), luil::color_from_hsv({ 200.0f, 1.0f, 1.0f, 1.0f }), 8));
    REQUIRE(close_to(frame.pixel_at(200, 396), luil::make_ui_color(0, 0, 0), 8));
    // 가운데는 HSV 식과 허용치 2로 같다. 흰색 쪽 투명 정지점이 검정이면 미리 곱하지 않은 보간이 이 자리를 회색으로 끈다.
    REQUIRE(same_color(frame.pixel_at(200, 200), luil::color_from_hsv({ 200.0f, 0.5f, 0.5f, 1.0f })));
    REQUIRE(same_color(frame.pixel_at(100, 300), luil::color_from_hsv({ 200.0f, 0.25f, 0.25f, 1.0f })));
}

TEST_CASE("The hue bar runs through the six stops and the alpha bar shows the checker", "[ui][raster][color-picker]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_tree tree { make_picker_tree({ 200.0f, 0.9f, 0.1f, 0.75f }) };
    luil::testing::raster_frame frame { 500, 700, palette, raster_scale };
    frame.draw(tree, luil::interaction_snapshot {});
    // 정지점은 400의 여섯 등분 자리다. 손잡이(200도, x 222)는 표본을 덮지 않는다.
    REQUIRE(close_to(frame.pixel_at(66, 432), luil::make_ui_color(255, 255, 0), 3));
    REQUIRE(close_to(frame.pixel_at(133, 432), luil::make_ui_color(0, 255, 0), 3));
    REQUIRE(close_to(frame.pixel_at(200, 432), luil::make_ui_color(0, 255, 255), 3));
    REQUIRE(close_to(frame.pixel_at(266, 432), luil::make_ui_color(0, 0, 255), 3));
    REQUIRE(close_to(frame.pixel_at(333, 432), luil::make_ui_color(255, 0, 255), 3));
    REQUIRE(close_to(frame.pixel_at(397, 432), luil::make_ui_color(255, 0, 0), 12));
    // 알파 막대의 왼쪽 끝은 거의 투명이라 체커가 보인다. 칸은 물리 8이고 흰색과 #CCCCCC가 엇갈린다. 몇 %의 색이 얹혀 조금 어둡다.
    REQUIRE(close_to(frame.pixel_at(4, 468), luil::make_ui_color(255, 255, 255), 10));
    REQUIRE(close_to(frame.pixel_at(12, 468), luil::make_ui_color(0xCC, 0xCC, 0xCC), 10));
    REQUIRE(close_to(frame.pixel_at(4, 476), luil::make_ui_color(0xCC, 0xCC, 0xCC), 10));
    // 오른쪽 끝은 불투명한 그 색이다.
    REQUIRE(close_to(frame.pixel_at(395, 480), luil::color_from_hsv({ 200.0f, 0.9f, 0.1f, 1.0f }), 6));
}

TEST_CASE("The plane handle ring is white over a dark color and black over a light one", "[ui][raster][color-picker]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_color white { luil::make_ui_color(255, 255, 255) };
    const luil::ui_color black { luil::make_ui_color(0, 0, 0) };
    // 반지름 7(물리 14)의 원이다. 안쪽 테는 반지름 10..14, 바깥 테는 14..16이다.
    const luil::ui_tree dark { make_picker_tree({ 200.0f, 0.9f, 0.1f, 1.0f }) };
    luil::testing::raster_frame on_dark { 500, 700, palette, raster_scale };
    on_dark.draw(dark, luil::interaction_snapshot {});
    REQUIRE(close_to(on_dark.pixel_at(372, 360), white, 8));
    REQUIRE(close_to(on_dark.pixel_at(375, 360), black, 8));

    const luil::ui_tree light { make_picker_tree({ 200.0f, 0.1f, 0.9f, 1.0f }) };
    luil::testing::raster_frame on_light { 500, 700, palette, raster_scale };
    on_light.draw(light, luil::interaction_snapshot {});
    REQUIRE(close_to(on_light.pixel_at(52, 40), black, 8));
    REQUIRE(close_to(on_light.pixel_at(55, 40), white, 8));
}

TEST_CASE("In high contrast the plane keeps the real colors and the outline is the window foreground", "[ui][raster][color-picker]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::high_contrast) };
    REQUIRE(palette.control_border == palette.primary_foreground);
    const luil::ui_tree tree { make_picker_tree({ 200.0f, 0.9f, 0.1f, 0.75f }) };
    luil::testing::raster_frame frame { 500, 700, palette, raster_scale };
    frame.set_theme(luil::color_theme::high_contrast);
    frame.draw(tree, luil::interaction_snapshot {});
    REQUIRE(same_color(frame.pixel_at(200, 200), luil::color_from_hsv({ 200.0f, 0.5f, 0.5f, 1.0f })));
    REQUIRE(close_to(frame.pixel_at(266, 432), luil::make_ui_color(0, 0, 255), 3));
    // 둘레 1 논리 픽셀(물리 2)이 윤곽 색이다.
    REQUIRE(frame.pixel_at(0, 200) == palette.control_border);
    REQUIRE(frame.pixel_at(1, 200) == palette.control_border);
    REQUIRE(frame.pixel_at(399, 200) == palette.control_border);
}

TEST_CASE("A disabled picker draws its content faded and its handle in the disabled color", "[ui][raster][color-picker]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_tree tree { make_picker_tree({ 200.0f, 0.5f, 0.5f, 1.0f }, false) };
    luil::testing::raster_frame frame { 500, 700, palette, raster_scale };
    frame.draw(tree, luil::interaction_snapshot {});
    // 내용은 40% 불투명이라 바탕과 섞인다.
    const luil::ui_color full { luil::color_from_hsv({ 200.0f, 0.9f, 0.9f, 1.0f }) };
    REQUIRE(same_color(frame.pixel_at(360, 40), full) == false);
    REQUIRE(same_color(frame.pixel_at(360, 40), over(luil::with_alpha(luil::color_from_hsv({ 200.0f, 360.5f / 400.0f, 1.0f - 40.5f / 400.0f, 1.0f }), 0.4f), palette.window_background)));
    // 손잡이 테는 비활성 색이다 (알파가 있어 아래와 섞인다).
    REQUIRE(frame.pixel_at(212, 200) != luil::make_ui_color(255, 255, 255));
    REQUIRE(frame.pixel_at(212, 200) != luil::make_ui_color(0, 0, 0));
}

namespace {
    // 바이트마다 글자 크기의 절반인 고정 폭이다. 이 축에는 글꼴이 없어 기본 측정기는 0을 잰다.
    const luil::text_measurer menu_bar_width { [](const std::u8string_view text, const float size) { return static_cast<float>(text.size()) * size * 0.5f; } };

    // "File·Edit·Help"를 (0, 0)부터 세운 막대를 그린다. 항목은 물리 88픽셀씩이다 ((24 + 20) × 2).
    [[nodiscard]] luil::testing::raster_frame draw_menu_bar(const luil::menu_bar_state state, const luil::ui_color_palette& palette, const luil::interaction_snapshot& interaction = {})
    {
        luil::menu_bar_config config {};
        config.owner = u8"bar";
        config.items = { { .key = u8"file", .label = u8"File", .access_key = U'f' }, { .key = u8"edit", .label = u8"Edit" }, { .key = u8"help", .label = u8"Help" } };
        config.state = state;
        config.set_state = [](const luil::menu_bar_state&) { return luil::input_action {}; };
        auto bar { std::make_unique<luil::menu_bar_element>(std::move(config)) };
        bar->arrange({ .slot = { 0.0f, 0.0f, 400.0f, luil::menu_bar_height * raster_scale }, .scale = raster_scale, .measurer = &menu_bar_width });
        auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 80.0f }, raster_scale });
        root->add(std::move(bar));
        const luil::ui_tree tree { std::move(root) };
        luil::testing::raster_frame frame { 400, 80, palette, raster_scale };
        frame.draw(tree, interaction);
        return frame;
    }
}

TEST_CASE("A menu bar paints no row background and shades only hovered, selected and open items", "[ui][raster][menu-bar]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    // hover·눌림 바탕은 반투명이라 창 바탕 위에 섞인 색이다.
    const luil::ui_color hover { over(palette.button_hover_background, palette.window_background) };
    const luil::ui_color pressed { over(palette.button_pressed_background, palette.window_background) };
    REQUIRE(same_color(hover, palette.window_background) == false);
    REQUIRE(same_color(pressed, hover) == false);

    // 쉬는 막대는 줄도 항목도 바탕이 없다.
    const luil::testing::raster_frame resting { draw_menu_bar({}, palette) };
    for (const int x : { 44, 132, 220, 300 })
        REQUIRE(resting.pixel_at(x, 28) == palette.window_background);

    // 키보드로 선 항목은 hover 바탕이다. 이웃은 그대로다.
    const luil::testing::raster_frame selected { draw_menu_bar({ u8"edit", false, true }, palette) };
    REQUIRE(same_color(selected.pixel_at(132, 28), hover));
    REQUIRE(selected.pixel_at(44, 28) == palette.window_background);

    // 열린 항목은 눌림 바탕이고, 포인터가 올라간 다른 항목은 hover 바탕이다.
    luil::interaction_snapshot over_help {};
    over_help.hovered = luil::ui_element_id { luil::ui_element_kind::menu_bar_item, u8"help" };
    const luil::testing::raster_frame open { draw_menu_bar({ u8"file", true, false }, palette, over_help) };
    REQUIRE(same_color(open.pixel_at(44, 28), pressed));
    REQUIRE(same_color(open.pixel_at(220, 28), hover));
    REQUIRE(open.pixel_at(132, 28) == palette.window_background);
    // 막대 오른쪽의 빈 자리도 칠하지 않는다.
    REQUIRE(open.pixel_at(300, 28) == palette.window_background);
}

namespace {
    [[nodiscard]] std::chrono::year_month_day raster_date(const unsigned day)
    {
        return { std::chrono::year { 2026 }, std::chrono::November, std::chrono::day { day } };
    }

    // 2026년 11월 달력이다 (1일이 일요일). 배율 2에서 칸은 물리 64이고 격자는 y 112부터다. n일(1~7)은 x (n - 1) × 64부터다.
    //  - 고른 날 `value`, 오늘 `today`다. 주말은 고를 수 없다. 글꼴이 없어 숫자는 그려지지 않고 채움과 테만 남는다.
    [[nodiscard]] luil::ui_tree make_calendar_tree(const unsigned value, const unsigned today)
    {
        luil::calendar_config config {};
        config.owner = u8"trip";
        config.value = raster_date(value);
        config.cursor = raster_date(value);
        config.today = raster_date(today);
        config.available = [](const std::chrono::year_month_day day) {
            const std::chrono::weekday weekday { std::chrono::sys_days { day } };
            return weekday != std::chrono::Saturday && weekday != std::chrono::Sunday;
        };
        config.select = [](const std::chrono::year_month_day) { return luil::input_action {}; };
        config.move_to = [](const std::chrono::year_month_day) { return luil::input_action {}; };
        return luil::make_arranged_tree(std::make_unique<luil::calendar_element>(std::move(config)), { 0.0f, 0.0f, 500.0f, 700.0f }, raster_scale);
    }

    [[nodiscard]] luil::ui_element_id raster_day(const unsigned day)
    {
        return luil::calendar_element::day_id(u8"trip", raster_date(day));
    }
}

TEST_CASE("The chosen day is an accent fill and today is an accent ring with an inner ring when both", "[ui][raster][calendar]")
{
    for (const luil::color_theme theme : { luil::color_theme::dark, luil::color_theme::light, luil::color_theme::high_contrast })
    {
        const luil::ui_color_palette palette { luil::color_palette_for(theme) };
        const luil::ui_color accent { over(palette.accent, palette.window_background) };
        // 3일을 고르고 오늘은 5일이다. 3일 칸은 x 128..192, 5일 칸은 x 256..320, 줄은 y 112..176이다.
        luil::testing::raster_frame frame { 500, 700, palette, raster_scale };
        frame.set_theme(theme);
        frame.draw(make_calendar_tree(3, 5), luil::interaction_snapshot {});
        REQUIRE(same_color(frame.pixel_at(160, 144), accent));
        // 칸 안쪽 여백(논리 2, 물리 4)은 채우지 않는다.
        REQUIRE(frame.pixel_at(129, 144) == palette.window_background);
        // 오늘은 채움 없이 테 1.5 논리 픽셀(물리 3)이다. 테는 칸 여백 안쪽 x 260..263이다.
        REQUIRE(same_color(frame.pixel_at(261, 144), accent));
        REQUIRE(frame.pixel_at(288, 144) == palette.window_background);
        // 고르지 않은 평일은 아무것도 칠하지 않는다.
        REQUIRE(frame.pixel_at(224, 144) == palette.window_background);

        // 고른 날이 오늘이면 채움 바깥 테는 강조색, 그 안쪽 테는 강조 위 글색이다. 색 하나로만 가르지 않는다.
        luil::testing::raster_frame both { 500, 700, palette, raster_scale };
        both.set_theme(theme);
        both.draw(make_calendar_tree(3, 3), luil::interaction_snapshot {});
        REQUIRE(same_color(both.pixel_at(133, 144), accent));
        REQUIRE(same_color(both.pixel_at(136, 144), over(palette.accent_foreground, accent)));
        REQUIRE(same_color(both.pixel_at(160, 144), accent));
    }
}

TEST_CASE("Only a day that can be chosen shows the hover fill", "[ui][raster][calendar]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_color hover { over(palette.button_hover_background, palette.window_background) };
    REQUIRE(same_color(hover, palette.window_background) == false);
    // 4일(수요일, x 192..256)은 고를 수 있고 7일(토요일, x 384..448)은 고를 수 없다.
    luil::interaction_snapshot over_wednesday {};
    over_wednesday.hovered = raster_day(4);
    luil::testing::raster_frame frame { 500, 700, palette, raster_scale };
    frame.draw(make_calendar_tree(3, 5), over_wednesday);
    REQUIRE(same_color(frame.pixel_at(224, 144), hover));

    luil::interaction_snapshot over_saturday {};
    over_saturday.hovered = raster_day(7);
    luil::testing::raster_frame blocked { 500, 700, palette, raster_scale };
    blocked.draw(make_calendar_tree(3, 5), over_saturday);
    REQUIRE(blocked.pixel_at(416, 144) == palette.window_background);
}

namespace {
    // 안내 풍선 raster 축이다. 화면은 물리 400x300이고 배율은 2다.
    //  - 대상은 물리 (150, 20) 100x40이고 가운데 x는 200이다. 풍선은 논리 80x48(닫기만 있다)이라 물리 (120, 80) 160x96에 선다.
    //  - 꼬리는 위 변(y 80)에서 위를 가리키고 끝이 y 64다. 밑변은 x 184..216이다.
    [[nodiscard]] luil::ui_tree make_tip_tree(const std::chrono::steady_clock::time_point opened_at = {})
    {
        luil::teaching_tip_config config {};
        config.owner = u8"tip";
        config.width = 80.0f;
        config.target = luil::rect_f { 150.0f, 20.0f, 100.0f, 40.0f };
        config.opened_at = opened_at;
        config.dismiss = [](const luil::teaching_tip_dismiss_reason) { return luil::input_action {}; };
        auto tip { std::make_unique<luil::teaching_tip_element>(std::move(config)) };
        tip->arrange({ { 0.0f, 0.0f, 400.0f, 300.0f }, raster_scale });
        auto root { std::make_unique<raster_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 300.0f }, raster_scale });
        root->add(std::move(tip));
        return luil::ui_tree { std::move(root) };
    }

    // 채널마다 `tolerance` 안이면 같은 색이다. 비스듬한 획은 안티에일리어싱이 덮개를 조금 덜 채운다.
    [[nodiscard]] bool near_color(const luil::ui_color actual, const luil::ui_color expected, const int tolerance) noexcept
    {
        for (const std::uint32_t shift : { 0U, 8U, 16U })
        {
            const int difference { static_cast<int>((actual >> shift) & 0xFFU) - static_cast<int>((expected >> shift) & 0xFFU) };
            if (std::abs(difference) > tolerance)
                return false;
        }
        return true;
    }
}

TEST_CASE("A teaching tip tail shares the body fill and its border runs around the tail, not across its base", "[ui][raster][teaching-tip]")
{
    for (const luil::color_theme theme : { luil::color_theme::dark, luil::color_theme::light, luil::color_theme::high_contrast })
    {
        CAPTURE(static_cast<int>(theme));
        const luil::ui_color_palette palette { luil::color_palette_for(theme) };
        const luil::ui_color border { over(palette.control_border, palette.raised_background) };
        REQUIRE(same_color(border, palette.raised_background) == false);
        luil::testing::raster_frame frame { 400, 300, palette, raster_scale };
        frame.set_theme(theme);
        frame.draw(make_tip_tree(), luil::interaction_snapshot {});

        // 꼬리 안은 몸과 같은 바탕이다. 밑변 자리(y 80)에도 테가 가로지르지 않는다.
        REQUIRE(frame.pixel_at(200, 74) == palette.raised_background);
        REQUIRE(frame.pixel_at(200, 80) == palette.raised_background);
        REQUIRE(frame.pixel_at(200, 130) == palette.raised_background);
        // 꼬리 밖의 위 변은 1 논리 픽셀 테다.
        REQUIRE(same_color(frame.pixel_at(150, 80), border));
        // 꼬리의 비스듬한 변에도 테가 돈다. 끝(y 64) 위는 창 쪽이다.
        bool edged { false };
        for (int x { 186 }; x < 200; ++x)
            edged = edged || near_color(frame.pixel_at(x, 72), border, 24);
        REQUIRE(edged);
        // 덮개는 투명하다. 풍선 밖은 창 바탕 그대로다.
        REQUIRE(frame.pixel_at(20, 280) == palette.window_background);
    }
}

TEST_CASE("A teaching tip fades in from the side away from its tail while it appears", "[ui][raster][teaching-tip]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const std::chrono::steady_clock::time_point opened { std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { 1000 } };

    // 연 시각 전에는 아무것도 그리지 않는다.
    luil::testing::raster_frame before { 400, 300, palette, raster_scale };
    before.draw(make_tip_tree(opened), luil::interaction_snapshot {}, opened - std::chrono::milliseconds { 1 });
    REQUIRE(before.pixel_at(200, 130) == palette.window_background);

    // 길이의 가운데(70 ms)에서 몸 전체가 반쯤 비치고, 꼬리 반대쪽(아래)으로 논리 2(물리 4)만큼 밀려 있다.
    luil::testing::raster_frame middle { 400, 300, palette, raster_scale };
    middle.draw(make_tip_tree(opened), luil::interaction_snapshot {}, opened + std::chrono::milliseconds { 70 });
    const luil::ui_color half { over((palette.raised_background & 0x00FFFFFFU) | 0x80000000U, palette.window_background) };
    REQUIRE(near_color(middle.pixel_at(200, 130), half, 3));
    REQUIRE(middle.pixel_at(200, 82) != palette.raised_background);

    // 끝나면 제자리에 불투명하게 선다.
    luil::testing::raster_frame done { 400, 300, palette, raster_scale };
    done.draw(make_tip_tree(opened), luil::interaction_snapshot {}, opened + luil::transition_duration);
    REQUIRE(done.pixel_at(200, 130) == palette.raised_background);
    REQUIRE(done.pixel_at(200, 82) == palette.raised_background);
}

namespace {
    constexpr int selection_frame_width { 400 };
    constexpr int selection_frame_height { 120 };
    constexpr luil::ui_color white { 0xFFFFFFFFu };
    constexpr luil::ui_color red { 0xFFFF0000u };

    // 글자마다 같은 직사각형인 글꼴이다. core test에는 OS 글꼴이 없어 글자 가장자리 픽셀을 보려면 직접 지어야 한다.
    //  - 한 글자가 0.5 em이라 22 물리 픽셀 글자에서 11 픽셀씩 나아가고, 직사각형의 가로 변은 정수 픽셀에 맞지 않는다.
    //    안티에일리어싱된 가장자리 픽셀이 서야 같은 글을 두 번 그렸을 때 그 픽셀이 달라진다.
    [[nodiscard]] sk_sp<SkTypeface> make_block_typeface()
    {
        SkFontMetrics metrics {};
        metrics.fTop = -0.9f;
        metrics.fAscent = -0.8f;
        metrics.fDescent = 0.2f;
        metrics.fBottom = 0.3f;
        metrics.fXHeight = 0.5f;
        metrics.fCapHeight = 0.7f;
        const SkPath block { SkPath::Rect(SkRect::MakeLTRB(0.1f, -0.613f, 0.45f, 0.0f)) };
        SkCustomTypefaceBuilder builder {};
        builder.setMetrics(metrics);
        for (SkGlyphID glyph { 0 }; glyph < 128; ++glyph)
            builder.setGlyph(glyph, 0.5f, block);
        return builder.detach();
    }

    // 위에서 `rows`줄까지 다른 픽셀의 수다.
    [[nodiscard]] int differing_pixels(const luil::testing::raster_frame& left, const luil::testing::raster_frame& right, const int rows)
    {
        int found { 0 };
        for (int y { 0 }; y < rows; ++y)
            for (int x { 0 }; x < selection_frame_width; ++x)
                found += left.pixel_at(x, y) != right.pixel_at(x, y) ? 1 : 0;
        return found;
    }

    // 칸의 안쪽(테와 모서리 밖)에서 칸 바탕도 고른 바탕도 글자색도 아닌 픽셀의 수다. 안티에일리어싱된 글자 가장자리다.
    [[nodiscard]] int edge_pixels(const luil::testing::raster_frame& frame, const luil::ui_color_palette& palette, const luil::ui_color ink)
    {
        int found { 0 };
        for (int y { 14 }; y < 42; ++y)
            for (int x { 12 }; x < selection_frame_width - 12; ++x)
            {
                const luil::ui_color color { frame.pixel_at(x, y) };
                found += color != palette.input_background && color != palette.selection_background && color != ink ? 1 : 0;
            }
        return found;
    }

    // 글자색이 꽉 찬 가장 아래 줄이다. 그 밑은 밑줄이 긋는 자리라 글자 비교에서 뺀다.
    [[nodiscard]] int lowest_ink_row(const luil::testing::raster_frame& frame, const luil::ui_color ink)
    {
        int lowest { -1 };
        for (int y { 0 }; y < selection_frame_height; ++y)
            for (int x { 0 }; x < selection_frame_width; ++x)
                if (frame.pixel_at(x, y) == ink)
                    lowest = y;
        return lowest;
    }
}

TEST_CASE("A selected styled run is painted once in the selection color", "[ui][raster][text-area][selection]")
{
    // 고른 구간의 글자는 구문색·링크색·검색색으로 한 번 그린 위에 다시 그리지 않는다. 두 번 그리면 글자 가장자리의 반투명
    // 픽셀에 먼저 그린 색이 비쳐 글자가 굵고 탁해 보인다.
    //  - 기준은 서식이 없는 같은 칸이다. 고른 글자색이 본문색과 같아 덧그림이 서지 않고 글자를 한 번만 그린다.
    //  - 글자는 모두 고른 구간 안이고 구간의 가로 변이 정수 픽셀이라, 한 번 그린 그림과 픽셀 단위로 같아야 한다.
    const sk_sp<SkTypeface> typeface { make_block_typeface() };
    luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    palette.primary_foreground = white;
    palette.selection_foreground = white;
    palette.syntax_keyword = red;
    palette.accent = red;
    palette.document_search_foreground = red;
    // 검색 결과의 바탕은 비워 둔다. 고른 바탕은 반투명이라 위에 한 번 더 깔면 색이 달라진다.
    palette.document_search_background = 0x00000000u;
    const luil::ui_element_id memo { luil::application_element_kind(2), u8"memo" };
    luil::text_input_view all_selected {};
    all_selected.text = u8"aaaa";
    all_selected.caret = 4;
    all_selected.anchor = 0;
    const auto draw_memo = [&](const luil::text_input_view& view, luil::text_area_config config) {
        const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::text_area_element>(memo, view, std::move(config)), { 0.0f, 0.0f, 200.0f, 60.0f }, raster_scale) };
        luil::testing::raster_frame frame { selection_frame_width, selection_frame_height, palette, raster_scale };
        frame.set_typeface(typeface);
        frame.draw(tree, luil::interaction_snapshot {});
        return frame;
    };

    const luil::testing::raster_frame reference { draw_memo(all_selected, {}) };
    // 기준 그림에 글자가 서 있고 그 가장자리가 반투명 픽셀이어야 두 번 그린 차이가 드러난다.
    REQUIRE(reference.count_color({ 0.0f, 0.0f, 400.0f, 120.0f }, white) > 0);
    REQUIRE(edge_pixels(reference, palette, white) > 0);
    const int ink_rows { lowest_ink_row(reference, white) + 1 };
    REQUIRE(ink_rows > 0);

    SECTION("a syntax colored run")
    {
        luil::text_area_config config {};
        config.spans = std::make_shared<const std::vector<luil::text_span>>(std::vector<luil::text_span> { { 0, 4, luil::text_color_role::keyword } });
        REQUIRE(differing_pixels(draw_memo(all_selected, config), reference, selection_frame_height) == 0);
    }

    SECTION("a search match")
    {
        luil::text_area_config config {};
        config.highlights = std::vector<luil::text_highlight> { { 0, 4, false } };
        REQUIRE(differing_pixels(draw_memo(all_selected, config), reference, selection_frame_height) == 0);
    }

    SECTION("a link keeps its underline and loses only its own color")
    {
        luil::text_area_config config {};
        // 본문색 구간을 하나 걸어 덧그림이 서는 칸으로 만든다. 링크 글자만 있는 칸은 덧그림이 서지 않아 링크색이 남는다.
        config.spans = std::make_shared<const std::vector<luil::text_span>>(std::vector<luil::text_span> { { 0, 4, luil::text_color_role::primary } });
        config.links = std::make_shared<const std::vector<luil::text_link>>(std::vector<luil::text_link> { { 0, 4, u8"https://example.com" } });
        const luil::testing::raster_frame linked { draw_memo(all_selected, config) };
        REQUIRE(differing_pixels(linked, reference, ink_rows) == 0);
        // 밑줄은 고른 구간 안에서도 링크색으로 남는다.
        REQUIRE(linked.count_color({ 0.0f, 0.0f, 400.0f, 120.0f }, red) > 0);
    }

    SECTION("text outside the selection keeps its own color")
    {
        luil::text_area_config config {};
        config.spans = std::make_shared<const std::vector<luil::text_span>>(std::vector<luil::text_span> { { 0, 4, luil::text_color_role::keyword } });
        luil::text_input_view tail_selected { all_selected };
        tail_selected.anchor = 2;
        const luil::testing::raster_frame partly { draw_memo(tail_selected, config) };
        luil::text_input_view none_selected { all_selected };
        none_selected.anchor = 4;
        const luil::testing::raster_frame unselected { draw_memo(none_selected, config) };
        // 고른 바탕이 시작하기 전의 글자 둘은 아무것도 고르지 않은 그림과 같고 구문색으로 남는다.
        //  - 글자는 11 픽셀씩 나아가고 고른 구간은 그 둘 뒤에서 시작한다. 첫 구문색 열에서 18 픽셀까지가 그 앞이다.
        int first_red { 0 };
        while (first_red < selection_frame_width && unselected.count_color({ static_cast<float>(first_red), 0.0f, 1.0f, 120.0f }, red) == 0)
            ++first_red;
        REQUIRE(first_red < selection_frame_width);
        const luil::rect_f leading { static_cast<float>(first_red), 0.0f, 18.0f, 120.0f };
        REQUIRE(unselected.count_color(leading, red) > 0);
        REQUIRE(partly.count_color(leading, red) == unselected.count_color(leading, red));
        int differing { 0 };
        for (int y { 0 }; y < selection_frame_height; ++y)
            for (int x { first_red }; x < first_red + 18; ++x)
                differing += partly.pixel_at(x, y) != unselected.pixel_at(x, y) ? 1 : 0;
        REQUIRE(differing == 0);
        // 고른 글자 둘은 고른 글자색이고 구문색이 남지 않는다.
        const luil::rect_f selected { static_cast<float>(first_red + 22), 0.0f, 22.0f, 120.0f };
        REQUIRE(partly.count_color(selected, white) > 0);
        REQUIRE(partly.count_color(selected, red) == 0);
    }
}

TEST_CASE("A selected single line run is painted once in the selection color", "[ui][raster][text-field][selection]")
{
    // 고대비는 고른 글자색이 본문색과 달라 칸이 고른 구간에 글자를 다시 그린다. 본문색으로 한 번 그린 위에 얹지 않고
    // 구간 밖에만 본문색을 그려, 구간 안은 고른 글자색으로 한 번만 그려야 한다.
    //  - 기준은 본문색이 고른 글자색과 같은 팔레트의 같은 칸이다. 이때 덧그림이 서지 않고 글자를 한 번만 그린다.
    const sk_sp<SkTypeface> typeface { make_block_typeface() };
    luil::ui_color_palette contrast { luil::color_palette_for(luil::color_theme::dark) };
    contrast.primary_foreground = red;
    contrast.selection_foreground = white;
    luil::ui_color_palette single { contrast };
    single.primary_foreground = white;
    const luil::ui_element_id field { luil::application_element_kind(2), u8"field" };
    luil::text_input_view all_selected {};
    all_selected.text = u8"aaaa";
    all_selected.caret = 4;
    all_selected.anchor = 0;
    const auto draw_field = [&](const luil::ui_color_palette& with) {
        const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::text_input_element>(field, all_selected, luil::text_input_config {}), { 0.0f, 0.0f, 200.0f, 28.0f }, raster_scale) };
        luil::testing::raster_frame frame { selection_frame_width, selection_frame_height, with, raster_scale };
        frame.set_typeface(typeface);
        frame.draw(tree, luil::interaction_snapshot {});
        return frame;
    };

    const luil::testing::raster_frame reference { draw_field(single) };
    REQUIRE(reference.count_color({ 0.0f, 0.0f, 400.0f, 120.0f }, white) > 0);
    REQUIRE(edge_pixels(reference, single, white) > 0);
    const luil::testing::raster_frame drawn { draw_field(contrast) };
    REQUIRE(differing_pixels(drawn, reference, selection_frame_height) == 0);
}
