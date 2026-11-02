#include "raster_probe.h"

#include "host/frame_state.h"
#include "luil/theme/ui_style.h"
#include "luil/theme/ui_style_json.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/button_element.h"
#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/list_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

// 시험 실행 파일에 실은 assets/style.json이다 (tests/CMakeLists.txt, ui_style_json_tests.cpp와 같은 것).
extern const std::uint8_t luil_test_base_style[];
extern const std::size_t luil_test_base_style_size;

// 파생 역할과 치수가 **실제 픽셀**에 닿는지 보는 축이다.
// 팔레트 합성의 단위 test는 역할이 값을 갖는지만 말한다. element가 그 역할을 읽지 않고
// 옛 숫자를 다시 발명하면 그쪽은 초록인 채로 화면만 옛 색이다 — 그것은 여기서만 드러난다.
namespace {
    constexpr float raster_scale { 2.0f };

    // 파생 역할을 내장값과 확실히 다른 **불투명** 색으로 바꾼 팔레트다.
    // 불투명이라야 배경과 섞이지 않고 픽셀이 그 값 그대로 나온다.
    [[nodiscard]] luil::ui_color_palette marked_palette()
    {
        luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
        palette.row_selection_background = luil::make_ui_color(255, 0, 0);
        palette.drop_target_background = luil::make_ui_color(0, 255, 0);
        palette.control_border = luil::make_ui_color(0, 0, 255);
        palette.accent_pressed = luil::make_ui_color(255, 0, 255);
        palette.active_toggle_background = luil::make_ui_color(0, 255, 255);
        palette.group_border = luil::make_ui_color(255, 255, 0);
        return palette;
    }

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

    [[nodiscard]] luil::ui_tree list_tree(const float row_height)
    {
        luil::list_config config {};
        config.owner = u8"list";
        config.row_height = row_height;
        for (const char8_t* key : { u8"a", u8"b", u8"c" })
            config.items.push_back(luil::list_item { .key = key });
        config.selected = u8"b";
        return luil::make_arranged_tree(std::make_unique<luil::list_element>(std::move(config)), { 0.0f, 0.0f, 400.0f, 200.0f }, raster_scale);
    }
}

TEST_CASE("A selected row is filled with the row selection role", "[ui][raster][style]")
{
    // 행은 20 논리 픽셀이고 둘째 행이 고른 행이라 물리 y 40..80이다.
    const luil::ui_color_palette palette { marked_palette() };
    const luil::ui_tree tree { list_tree(20.0f) };
    luil::testing::raster_frame frame { 400, 200, palette, raster_scale };
    frame.draw(tree, luil::interaction_snapshot {});
    REQUIRE(frame.pixel_at(200, 60) == palette.row_selection_background);
    REQUIRE(frame.pixel_at(6, 60) == palette.accent);
    REQUIRE(frame.pixel_at(200, 20) == palette.window_background);
}

TEST_CASE("High contrast folds the row fill away and keeps the mark", "[ui][raster][style]")
{
    // 채움이 알파 0이라 행의 글자 자리는 배경 그대로이고, 어느 행인지는 표식이 말한다.
    const luil::ui_color_palette palette { luil::high_contrast_palette_for(luil::high_contrast_colors {}) };
    const luil::ui_tree tree { list_tree(20.0f) };
    luil::testing::raster_frame frame { 400, 200, palette, raster_scale };
    frame.draw(tree, luil::interaction_snapshot {});
    REQUIRE(frame.pixel_at(200, 60) == palette.window_background);
    REQUIRE(frame.pixel_at(6, 60) == palette.accent);
}

TEST_CASE("A row under a drag is filled with the drop target role", "[ui][raster][style]")
{
    const luil::ui_color_palette palette { marked_palette() };
    const luil::ui_tree tree { list_tree(20.0f) };
    luil::interaction_snapshot interaction {};
    luil::drag_visual drag {};
    drag.payload.dragged_owner = u8"a";
    drag.hovered_drop_target = luil::ui_element_id { luil::ui_element_kind::list_row, u8"c" };
    // ghost는 포인터에서 10×배율 떨어진 112×24 논리 픽셀 상자다 — 보는 행과 겹치지 않는 자리에 둔다.
    drag.x = 350.0f;
    drag.y = 150.0f;
    interaction.drag = drag;
    luil::testing::raster_frame frame { 400, 200, palette, raster_scale };
    frame.draw(tree, interaction);
    // 셋째 행(물리 y 80..120)이 놓을 자리다.
    REQUIRE(frame.pixel_at(200, 100) == palette.drop_target_background);
    // 고른 행은 여전히 고른 행이다.
    REQUIRE(frame.pixel_at(200, 60) == palette.row_selection_background);
}

TEST_CASE("An unchecked box is outlined with the control border role", "[ui][raster][style]")
{
    const luil::ui_color_palette palette { marked_palette() };
    luil::check_config config {};
    config.owner = u8"check";
    auto control { std::make_unique<luil::check_element>(config) };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(control), { 0.0f, 0.0f, 200.0f, luil::check_row_height }, raster_scale) };
    luil::testing::raster_frame frame { 400, 48, palette, raster_scale };
    frame.draw(tree, luil::interaction_snapshot {});
    // 상자는 x 8 (check_inset 4 × 배율)에서 시작하고 획은 1.5 논리 픽셀이라 그 자리가 온전히 덮인다.
    const luil::rect_f box { 6.0f, 8.0f, 32.0f, 32.0f };
    REQUIRE(frame.contains_color(box, palette.control_border));
    REQUIRE(frame.count_color(box, palette.accent) == 0);
}

TEST_CASE("A pressed solid button wears the accent pressed role", "[ui][raster][style]")
{
    const luil::ui_color_palette palette { marked_palette() };
    const luil::ui_element_id id { luil::application_element_kind(1), u8"ok" };
    auto button { std::make_unique<luil::text_button_element>(id, luil::text_button_config { .default_button = true }) };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(button), { 20.0f, 20.0f, 160.0f, 40.0f }, raster_scale) };

    luil::interaction_snapshot interaction {};
    luil::testing::raster_frame frame { 400, 120, palette, raster_scale };
    frame.draw(tree, interaction);
    REQUIRE(frame.pixel_at(100, 40) == palette.accent);

    interaction.pressed = id;
    interaction.hovered = id;
    frame.draw(tree, interaction);
    REQUIRE(frame.pixel_at(100, 40) == palette.accent_pressed);
}

TEST_CASE("A toggled-on icon button rests on the active toggle role", "[ui][raster][style]")
{
    const luil::ui_color_palette palette { marked_palette() };
    const luil::ui_element_id id { luil::application_element_kind(1), u8"toggle" };
    auto button { std::make_unique<luil::button_element>(id, luil::button_config { .active = true }) };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(button), { 20.0f, 20.0f, 40.0f, 40.0f }, raster_scale) };
    luil::testing::raster_frame frame { 120, 120, palette, raster_scale };
    frame.draw(tree, luil::interaction_snapshot {});
    REQUIRE(frame.pixel_at(40, 40) == palette.active_toggle_background);
}

TEST_CASE("The control corner radius from the style reaches the button's corners", "[ui][raster][style]")
{
    // 기본 버튼은 강조색 채움이다. 모서리가 둥글면 상자의 꼭짓점은 배경이고,
    // 반지름 0이면 꼭짓점까지 채움이다 — 치수가 context를 지나 element에 닿는 증거다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_element_id id { luil::application_element_kind(1), u8"ok" };
    const auto build = [&id] {
        auto button { std::make_unique<luil::text_button_element>(id, luil::text_button_config { .default_button = true }) };
        return luil::make_arranged_tree(std::move(button), { 20.0f, 20.0f, 160.0f, 40.0f }, raster_scale);
    };

    SECTION("내장 치수(3 논리 픽셀)는 꼭짓점을 비운다")
    {
        const luil::ui_tree tree { build() };
        luil::testing::raster_frame frame { 400, 120, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});
        REQUIRE(frame.pixel_at(20, 20) == palette.window_background);
        REQUIRE(frame.pixel_at(100, 40) == palette.accent);
    }

    SECTION("반지름 0은 꼭짓점까지 채운다")
    {
        const luil::ui_tree tree { build() };
        luil::testing::raster_frame frame { 400, 120, palette, raster_scale };
        luil::ui_metrics metrics {};
        metrics.control_corner_radius = 0.0f;
        frame.set_metrics(metrics);
        frame.draw(tree, luil::interaction_snapshot {});
        REQUIRE(frame.pixel_at(20, 20) == palette.accent);
    }
}

TEST_CASE("The row corner radius from the style reaches the selection fill", "[ui][raster][style]")
{
    // 고른 행의 채움은 좌우 2px·위아래 1px 들여 그려진다 (물리 x 4, y 42..78).
    // 둥근 모서리에서는 그 꼭짓점 픽셀이 배경이고, 반지름 0이면 채움이다.
    const luil::ui_color_palette palette { marked_palette() };
    const luil::ui_tree tree { list_tree(20.0f) };

    SECTION("내장 치수(4 논리 픽셀)는 꼭짓점을 비운다")
    {
        luil::testing::raster_frame frame { 400, 200, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});
        REQUIRE(frame.pixel_at(4, 42) == palette.window_background);
    }

    SECTION("반지름 0은 꼭짓점까지 채운다")
    {
        luil::testing::raster_frame frame { 400, 200, palette, raster_scale };
        luil::ui_metrics metrics {};
        metrics.row_corner_radius = 0.0f;
        frame.set_metrics(metrics);
        frame.draw(tree, luil::interaction_snapshot {});
        REQUIRE(frame.pixel_at(4, 42) == palette.row_selection_background);
    }
}

TEST_CASE("A style read at run time reaches the window background and the button corners", "[ui][raster][style][style_json]")
{
    // 앱이 실행 중에 읽은 스타일을 두 자리(frame의 스타일, 배치의 스타일)에 같은 포인터로 준다 (style-sheet-design.md).
    // 어두운 창 바탕과 컨트롤 모서리를 바꾼 글이다. 바탕은 팔레트로, 모서리는 그리기 치수로 픽셀에 닿는다.
    std::string text { reinterpret_cast<const char*>(luil_test_base_style), luil_test_base_style_size };
    const std::string background { "\"windowBackground\": \"#1e1e1e\"" };
    const std::string corner { "\"controlCornerRadius\": 3" };
    REQUIRE(text.find(background) != std::string::npos);
    REQUIRE(text.find(corner) != std::string::npos);
    text.replace(text.find(background), background.size(), "\"windowBackground\": \"#102030\"");
    text.replace(text.find(corner), corner.size(), "\"controlCornerRadius\": 0");
    const luil::style_parse_result parsed { luil::parse_ui_style({ reinterpret_cast<const char8_t*>(text.data()), text.size() }) };
    REQUIRE(parsed.ok());
    const luil::ui_style& style { *parsed.style };

    luil::frame_state state {};
    state.theme = luil::color_theme::dark;
    state.style = &style;
    const luil::ui_color_palette palette { luil::frame_palette(state) };
    REQUIRE(palette.window_background == luil::make_ui_color(0x10, 0x20, 0x30));

    const luil::ui_element_id id { luil::application_element_kind(1), u8"ok" };
    auto button { std::make_unique<luil::text_button_element>(id, luil::text_button_config { .default_button = true }) };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(button), { 20.0f, 20.0f, 160.0f, 40.0f }, raster_scale, {}, nullptr, &style) };

    SECTION("두 자리에 주면 바탕과 모서리가 함께 바뀐다")
    {
        luil::testing::raster_frame frame { 400, 120, palette, raster_scale };
        frame.set_metrics(luil::frame_style(state).metrics);
        frame.draw(tree, luil::interaction_snapshot {});
        REQUIRE(frame.pixel_at(300, 100) == luil::make_ui_color(0x10, 0x20, 0x30));
        REQUIRE(frame.pixel_at(20, 20) == palette.accent);
    }

    SECTION("배치에만 주면 모서리는 그대로다")
    {
        // 모서리는 그리기 치수(`draw_context::metrics`)가 정하고 그것은 frame의 스타일에서 온다. 배치만 바꾸면 둥근 채다.
        luil::testing::raster_frame frame { 400, 120, palette, raster_scale };
        frame.draw(tree, luil::interaction_snapshot {});
        REQUIRE(frame.pixel_at(20, 20) == palette.window_background);
        REQUIRE(frame.pixel_at(100, 40) == palette.accent);
    }
}
