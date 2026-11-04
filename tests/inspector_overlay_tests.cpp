#include "host/frame_state.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/accessibility.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_inspect.h"
#include "luil/ui/ui_tree.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// 검사기 overlay의 래스터 축이다 (inspector-design.md). `draw_frame`을 `frame_state`로 불러 픽셀을 읽는다.
//  - 글은 단언하지 않는다. 래스터 축에는 글꼴이 없을 수도 있고 기기에는 있어서, 글자가 닿는 자리를 피해 선과 상자만 본다.
//  - 배율은 2다. 1 논리 픽셀 획이 물리 2픽셀을 온전히 덮어 팔레트 색이 그대로 남는다.
namespace {
    constexpr float raster_scale { 2.0f };
    constexpr int surface_width { 600 };
    constexpr int surface_height { 400 };
    constexpr luil::ui_element_kind kind_box { luil::application_element_kind(1) };

    using palette_member = luil::ui_color luil::ui_color_palette::*;

    // 자기 자리를 팔레트 색 하나로 채우고 자식을 이어 그리는 요소다.
    class fill_node final : public luil::ui_element
    {
    public:
        fill_node(luil::ui_element_id id, const luil::rect_f& box, const palette_member color)
            : ui_element { std::move(id) }
            , color_ { color }
        {
            set_bounds(box);
        }

        fill_node* add(std::unique_ptr<fill_node> child)
        {
            fill_node* const raw { child.get() };
            add_child(std::move(child));
            return raw;
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
        {
            if (color_ != nullptr)
            {
                SkPaint paint {};
                paint.setColor(context.palette.*color_);
                context.canvas.drawRect(SkRect::MakeXYWH(bounds().x, bounds().y, bounds().width, bounds().height), paint);
            }
            draw_children(context, interaction);
        }

    private:
        palette_member color_ { nullptr };
    };

    [[nodiscard]] luil::ui_element_id box_id(const std::u8string& owner)
    {
        return { kind_box, owner };
    }

    // root(칠하지 않는다) > backdrop(surface_background) > target(raised_background)다.
    // 후광이 서는 자리 아래에 다른 색이 깔리도록 target을 backdrop 안에 둔다. target의 부모는 backdrop이다.
    [[nodiscard]] luil::ui_tree make_scene(const luil::rect_f& root_box, const luil::rect_f& backdrop_box, const luil::rect_f& target_box)
    {
        auto root { std::make_unique<fill_node>(luil::ui_element_id { luil::ui_element_kind::root, {} }, root_box, nullptr) };
        fill_node* const backdrop { root->add(std::make_unique<fill_node>(box_id(u8"backdrop"), backdrop_box, &luil::ui_color_palette::surface_background)) };
        backdrop->add(std::make_unique<fill_node>(box_id(u8"target"), target_box, &luil::ui_color_palette::raised_background));
        return luil::ui_tree { std::move(root) };
    }

    constexpr luil::rect_f default_root { 0.0f, 0.0f, 600.0f, 400.0f };
    constexpr luil::rect_f default_backdrop { 100.0f, 150.0f, 300.0f, 200.0f };
    constexpr luil::rect_f default_target { 200.0f, 200.0f, 100.0f, 60.0f };

    [[nodiscard]] luil::ui_tree default_scene()
    {
        return make_scene(default_root, default_backdrop, default_target);
    }

    struct frame_options
    {
        int width { surface_width };
        int height { surface_height };
        int origin_x { 0 };
        int origin_y { 0 };
        luil::color_theme theme { luil::color_theme::dark };
        luil::interaction_snapshot interaction {};
        std::span<const luil::overlay_layer> overlays {};
    };

    [[nodiscard]] SkBitmap render(const luil::ui_tree* const tree, const luil::inspector_config* const inspector, const frame_options& options = {})
    {
        SkBitmap pixels {};
        pixels.allocN32Pixels(options.width, options.height);
        SkCanvas canvas { pixels };
        luil::frame_state state {};
        state.width = options.width;
        state.height = options.height;
        state.origin_x = options.origin_x;
        state.origin_y = options.origin_y;
        state.dpi_scale = raster_scale;
        state.theme = options.theme;
        state.interaction = options.interaction;
        state.tree = tree;
        state.overlays = options.overlays;
        state.inspector = inspector;
        luil::draw_frame(canvas, nullptr, nullptr, state);
        return pixels;
    }

    [[nodiscard]] luil::ui_color pixel(const SkBitmap& pixels, const int x, const int y)
    {
        return static_cast<luil::ui_color>(pixels.getColor(x, y));
    }

    [[nodiscard]] luil::rect_f inflate(const luil::rect_f& box, const float amount)
    {
        return { box.x - amount, box.y - amount, box.width + amount * 2.0f, box.height + amount * 2.0f };
    }

    [[nodiscard]] luil::rect_f shifted(const luil::rect_f& box, const float dx, const float dy)
    {
        return { box.x + dx, box.y + dy, box.width, box.height };
    }

    // 상자 안쪽의 띠 네 조각이다.
    [[nodiscard]] std::vector<luil::rect_f> ring(const luil::rect_f& box, const float thickness)
    {
        return {
            { box.x, box.y, box.width, thickness },
            { box.x, box.y + box.height - thickness, box.width, thickness },
            { box.x, box.y, thickness, box.height },
            { box.x + box.width - thickness, box.y, thickness, box.height },
        };
    }

    void append(std::vector<luil::rect_f>& allowed, const std::vector<luil::rect_f>& more)
    {
        allowed.insert(allowed.end(), more.begin(), more.end());
    }

    // 허용한 자리 밖에서 두 그림이 다른 픽셀 수다.
    [[nodiscard]] int differences_outside(const SkBitmap& first, const SkBitmap& second, const std::vector<luil::rect_f>& allowed)
    {
        REQUIRE(first.width() == second.width());
        REQUIRE(first.height() == second.height());
        int count { 0 };
        for (int y { 0 }; y < first.height(); ++y)
        {
            for (int x { 0 }; x < first.width(); ++x)
            {
                if (first.getColor(x, y) == second.getColor(x, y))
                    continue;
                bool permitted { false };
                for (const luil::rect_f& box : allowed)
                {
                    if (box.contains(static_cast<float>(x), static_cast<float>(y)))
                    {
                        permitted = true;
                        break;
                    }
                }
                if (permitted == false)
                    ++count;
            }
        }
        return count;
    }

    [[nodiscard]] luil::inspector_config pick_selected(const std::u8string& owner)
    {
        luil::inspector_config config {};
        config.enabled = true;
        config.pick = luil::inspector_pick::selected;
        config.selected = box_id(owner);
        return config;
    }

    // 정보 상자가 설 수 있는 넓은 자리다 (왼쪽 위, 논리 4 여백 안쪽).
    constexpr luil::rect_f top_box_area { 8.0f, 8.0f, 584.0f, 174.0f };
}

TEST_CASE("A disabled inspector draws the same pixels as no inspector", "[inspector][raster]")
{
    const luil::ui_tree tree { default_scene() };
    const SkBitmap baseline { render(&tree, nullptr) };

    luil::inspector_config off { pick_selected(u8"target") };
    off.enabled = false;
    off.outline_all = true;
    off.show_palette = true;
    const SkBitmap with_off { render(&tree, &off) };
    REQUIRE(differences_outside(baseline, with_off, {}) == 0);

    // 켰지만 집을 것이 없다 (hover가 비어 있다). 정보 상자도 선도 없다.
    luil::inspector_config idle {};
    idle.enabled = true;
    const SkBitmap with_idle { render(&tree, &idle) };
    REQUIRE(differences_outside(baseline, with_idle, {}) == 0);

    // 켰지만 id가 tree에 없다.
    const luil::inspector_config missing { pick_selected(u8"nobody") };
    const SkBitmap with_missing { render(&tree, &missing) };
    REQUIRE(differences_outside(baseline, with_missing, {}) == 0);
}

TEST_CASE("An inspector without a tree changes nothing", "[inspector][raster]")
{
    luil::inspector_config config { pick_selected(u8"target") };
    config.outline_all = true;
    config.show_palette = true;
    const SkBitmap baseline { render(nullptr, nullptr) };
    const SkBitmap drawn { render(nullptr, &config) };
    REQUIRE(differences_outside(baseline, drawn, {}) == 0);
}

TEST_CASE("The picked element gets a solid accent line with a window colored halo and an info box", "[inspector][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    REQUIRE(palette.window_background != palette.surface_background);
    REQUIRE(palette.raised_background != palette.accent);
    REQUIRE((palette.tooltip_background >> 24) == 0xFFu);
    const luil::ui_tree tree { default_scene() };
    const SkBitmap baseline { render(&tree, nullptr) };
    const luil::inspector_config config { pick_selected(u8"target") };
    const SkBitmap drawn { render(&tree, &config) };

    // 2 논리 픽셀 = 물리 4픽셀 실선이 target(200,200 100x60)의 안쪽 변을 따라 선다.
    REQUIRE(pixel(drawn, 200, 230) == palette.accent);
    REQUIRE(pixel(drawn, 203, 230) == palette.accent);
    REQUIRE(pixel(drawn, 250, 200) == palette.accent);
    REQUIRE(pixel(drawn, 250, 203) == palette.accent);
    REQUIRE(pixel(drawn, 250, 256) == palette.accent);
    REQUIRE(pixel(drawn, 296, 230) == palette.accent);
    REQUIRE(pixel(drawn, 299, 230) == palette.accent);
    // 선 안쪽은 요소의 색 그대로다.
    REQUIRE(pixel(drawn, 250, 230) == palette.raised_background);
    // 1 논리 픽셀 후광이 선 바깥에 선다. 아래에 깔린 backdrop과 갈리는 창 바탕색이다.
    REQUIRE(pixel(baseline, 199, 230) == palette.surface_background);
    REQUIRE(pixel(drawn, 199, 230) == palette.window_background);
    REQUIRE(pixel(drawn, 198, 230) == palette.window_background);
    REQUIRE(pixel(drawn, 250, 199) == palette.window_background);
    REQUIRE(pixel(drawn, 300, 230) == palette.window_background);
    REQUIRE(pixel(drawn, 250, 261) == palette.window_background);
    // 후광 바깥은 그대로다.
    REQUIRE(pixel(drawn, 197, 230) == pixel(baseline, 197, 230));

    // 정보 상자는 왼쪽 위(논리 4 여백)에 서고 테와 바탕을 palette에서 가져온다.
    REQUIRE(pixel(baseline, 9, 9) == palette.window_background);
    REQUIRE(pixel(drawn, 9, 9) == palette.tooltip_border);
    REQUIRE(pixel(drawn, 14, 14) == palette.tooltip_background);
    REQUIRE(pixel(drawn, 12, 100) == palette.tooltip_background);

    // 선, 부모(backdrop)의 점선, 정보 상자 밖은 그대로다.
    std::vector<luil::rect_f> allowed { ring(inflate(default_target, 2.0f), 6.0f) };
    append(allowed, ring(default_backdrop, 2.0f));
    allowed.push_back(top_box_area);
    REQUIRE(differences_outside(baseline, drawn, allowed) == 0);
}

TEST_CASE("The parent of the picked element is outlined with a dashed line", "[inspector][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_tree tree { default_scene() };
    const SkBitmap baseline { render(&tree, nullptr) };
    // 정보 상자가 이 윗변을 덮지 않게 끈다.
    luil::inspector_config config { pick_selected(u8"target") };
    config.show_info = false;
    const SkBitmap drawn { render(&tree, &config) };
    // backdrop(100,150)의 윗변이다. 마디 8픽셀, 틈 6픽셀이 번갈아 선다.
    REQUIRE(pixel(drawn, 104, 150) == palette.primary_foreground);
    REQUIRE(pixel(drawn, 104, 151) == palette.primary_foreground);
    REQUIRE(pixel(drawn, 111, 150) == pixel(baseline, 111, 150));
    REQUIRE(pixel(drawn, 118, 150) == palette.primary_foreground);
    // 두께는 1 논리 픽셀이다.
    REQUIRE(pixel(drawn, 104, 152) == pixel(baseline, 104, 152));
}

TEST_CASE("The info box moves to the lower left when it would cover the picked element", "[inspector][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    // target을 윗줄로 올려 왼쪽 위 상자(y 8..182)와 겹치게 한다.
    const luil::ui_tree tree { make_scene(default_root, default_backdrop, { 150.0f, 160.0f, 100.0f, 60.0f }) };
    const SkBitmap baseline { render(&tree, nullptr) };
    const luil::inspector_config config { pick_selected(u8"target") };
    const SkBitmap drawn { render(&tree, &config) };
    // 윗줄은 비고 아래 왼쪽에 상자가 선다 (높이 다섯 줄, 물리 174).
    REQUIRE(pixel(drawn, 9, 9) == pixel(baseline, 9, 9));
    REQUIRE(pixel(drawn, 14, 14) == pixel(baseline, 14, 14));
    REQUIRE(pixel(drawn, 9, 219) == palette.tooltip_border);
    REQUIRE(pixel(drawn, 14, 226) == palette.tooltip_background);
}

TEST_CASE("The inner padding band is filled and the children stay untouched", "[inspector][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    REQUIRE((palette.accent_soft >> 24) != 0u);
    // panel(100,100 300x200) 안에 child(140,140 100x60)가 서고 나머지는 안쪽 여백이다.
    auto root { std::make_unique<fill_node>(luil::ui_element_id { luil::ui_element_kind::root, {} }, default_root, nullptr) };
    fill_node* const panel { root->add(std::make_unique<fill_node>(box_id(u8"panel"), luil::rect_f { 100.0f, 100.0f, 300.0f, 200.0f }, &luil::ui_color_palette::surface_background)) };
    panel->add(std::make_unique<fill_node>(box_id(u8"child"), luil::rect_f { 140.0f, 140.0f, 100.0f, 60.0f }, &luil::ui_color_palette::raised_background));
    const luil::ui_tree tree { std::move(root) };
    const SkBitmap baseline { render(&tree, nullptr) };
    luil::inspector_config config { pick_selected(u8"panel") };
    config.show_info = false;
    const SkBitmap drawn { render(&tree, &config) };

    // 왼쪽과 오른쪽 여백에 옅은 accent가 깔리고
    REQUIRE(pixel(drawn, 115, 250) != pixel(baseline, 115, 250));
    REQUIRE(pixel(drawn, 320, 170) != pixel(baseline, 320, 170));
    // 자식이 선 자리는 그대로다.
    REQUIRE(pixel(drawn, 190, 170) == pixel(baseline, 190, 170));
    REQUIRE(pixel(drawn, 141, 141) == pixel(baseline, 141, 141));
}

TEST_CASE("High contrast draws the accent line but fills no padding band", "[inspector][raster]")
{
    const luil::ui_color_palette palette { luil::high_contrast_palette_for(luil::high_contrast_colors {}) };
    auto root { std::make_unique<fill_node>(luil::ui_element_id { luil::ui_element_kind::root, {} }, default_root, nullptr) };
    fill_node* const panel { root->add(std::make_unique<fill_node>(box_id(u8"panel"), luil::rect_f { 100.0f, 100.0f, 300.0f, 200.0f }, &luil::ui_color_palette::surface_background)) };
    panel->add(std::make_unique<fill_node>(box_id(u8"child"), luil::rect_f { 140.0f, 140.0f, 100.0f, 60.0f }, &luil::ui_color_palette::raised_background));
    const luil::ui_tree tree { std::move(root) };
    frame_options options {};
    options.theme = luil::color_theme::high_contrast;
    const SkBitmap baseline { render(&tree, nullptr, options) };
    luil::inspector_config config { pick_selected(u8"panel") };
    config.show_info = false;
    const SkBitmap drawn { render(&tree, &config, options) };
    REQUIRE(pixel(drawn, 101, 200) == palette.accent);
    // 여백은 칠하지 않는다.
    REQUIRE(pixel(drawn, 115, 250) == pixel(baseline, 115, 250));
    REQUIRE(pixel(drawn, 320, 170) == pixel(baseline, 320, 170));
}

TEST_CASE("The outline stands apart from the background in the light and dark themes", "[inspector][raster]")
{
    const luil::ui_tree tree { default_scene() };
    for (const luil::color_theme theme : { luil::color_theme::dark, luil::color_theme::light })
    {
        const char* const theme_name { theme == luil::color_theme::dark ? "dark" : "light" };
        INFO(theme_name);
        const luil::ui_color_palette palette { luil::color_palette_for(theme) };
        frame_options options {};
        options.theme = theme;
        const luil::inspector_config config { pick_selected(u8"target") };
        const SkBitmap baseline { render(&tree, nullptr, options) };
        const SkBitmap drawn { render(&tree, &config, options) };
        REQUIRE(pixel(drawn, 201, 230) == palette.accent);
        REQUIRE(pixel(drawn, 201, 230) != pixel(baseline, 201, 230));
        REQUIRE(pixel(drawn, 199, 230) == palette.window_background);
        REQUIRE(pixel(drawn, 199, 230) != pixel(baseline, 199, 230));
    }
}

TEST_CASE("A frame with an origin draws the outline on the moved element and keeps the box inside the root", "[inspector][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_tree tree { default_scene() };
    frame_options options {};
    options.width = surface_width + 60;
    options.height = surface_height + 40;
    options.origin_x = 20;
    options.origin_y = 10;
    const SkBitmap baseline { render(&tree, nullptr, options) };
    const luil::inspector_config config { pick_selected(u8"target") };
    const SkBitmap drawn { render(&tree, &config, options) };
    REQUIRE(pixel(drawn, 201 + 20, 230 + 10) == palette.accent);
    REQUIRE(pixel(drawn, 201, 230) == pixel(baseline, 201, 230));
    REQUIRE(pixel(drawn, 9 + 20, 9 + 10) == palette.tooltip_border);

    // 달라진 픽셀은 모두 옮겨진 root 안에 든다 (안전 영역 밖은 건드리지 않는다).
    const luil::rect_f root_area { 20.0f, 10.0f, 600.0f, 400.0f };
    std::vector<luil::rect_f> allowed { root_area };
    REQUIRE(differences_outside(baseline, drawn, allowed) == 0);
    // 그리고 안쪽에서는 실제로 달라졌다.
    REQUIRE(differences_outside(baseline, drawn, {}) > 0);
}

TEST_CASE("A surface smaller than the box draws only the outline", "[inspector][raster]")
{
    // 논리 80x50이라 상자가 설 수 없다 (메뉴 popup 크기).
    const luil::rect_f small_root { 0.0f, 0.0f, 160.0f, 100.0f };
    const luil::rect_f small_backdrop { 10.0f, 10.0f, 140.0f, 80.0f };
    const luil::rect_f small_target { 40.0f, 30.0f, 40.0f, 30.0f };
    const luil::ui_tree tree { make_scene(small_root, small_backdrop, small_target) };
    frame_options options {};
    options.width = 200;
    options.height = 120;
    const SkBitmap baseline { render(&tree, nullptr, options) };
    luil::inspector_config config { pick_selected(u8"target") };
    config.show_palette = true;
    const SkBitmap drawn { render(&tree, &config, options) };
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    REQUIRE(pixel(drawn, 41, 45) == palette.accent);
    std::vector<luil::rect_f> allowed { ring(inflate(small_target, 2.0f), 6.0f) };
    append(allowed, ring(small_backdrop, 2.0f));
    REQUIRE(differences_outside(baseline, drawn, allowed) == 0);
}

TEST_CASE("Each overlay layer is inspected with its own tree and interaction around its own origin", "[inspector][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::ui_tree main_tree { default_scene() };
    const luil::rect_f layer_root { 0.0f, 0.0f, 160.0f, 100.0f };
    const luil::rect_f layer_backdrop { 10.0f, 10.0f, 140.0f, 80.0f };
    const luil::rect_f layer_target { 40.0f, 30.0f, 40.0f, 30.0f };
    const luil::ui_tree layer_tree { make_scene(layer_root, layer_backdrop, layer_target) };
    luil::overlay_layer layer {};
    layer.tree = &layer_tree;
    layer.bounds = { 100, 50, 160, 100 };
    layer.border = false;
    layer.interaction.hovered = box_id(u8"target");
    const std::array<luil::overlay_layer, 1> layers { layer };
    frame_options options {};
    options.overlays = layers;

    const SkBitmap baseline { render(&main_tree, nullptr, options) };
    // 켠 상태에서 주 표면의 hover는 비어 있다. layer만 자기 hover를 집는다.
    luil::inspector_config config {};
    config.enabled = true;
    const SkBitmap drawn { render(&main_tree, &config, options) };
    REQUIRE(pixel(drawn, 100 + 41, 50 + 45) == palette.accent);
    const luil::rect_f origin_target { shifted(layer_target, 100.0f, 50.0f) };
    std::vector<luil::rect_f> allowed { ring(inflate(origin_target, 2.0f), 6.0f) };
    append(allowed, ring(shifted(layer_backdrop, 100.0f, 50.0f), 2.0f));
    REQUIRE(differences_outside(baseline, drawn, allowed) == 0);
}

TEST_CASE("Outlining everything draws every visible element and skips hidden ones", "[inspector][raster]")
{
    auto root { std::make_unique<fill_node>(luil::ui_element_id { luil::ui_element_kind::root, {} }, default_root, nullptr) };
    root->add(std::make_unique<fill_node>(box_id(u8"shown"), luil::rect_f { 50.0f, 50.0f, 100.0f, 60.0f }, &luil::ui_color_palette::raised_background));
    fill_node* const hidden { root->add(std::make_unique<fill_node>(box_id(u8"hidden"), luil::rect_f { 300.0f, 50.0f, 100.0f, 60.0f }, &luil::ui_color_palette::raised_background)) };
    hidden->set_visible(false);
    const luil::ui_tree tree { std::move(root) };
    const SkBitmap baseline { render(&tree, nullptr) };
    luil::inspector_config config {};
    config.enabled = true;
    config.outline_all = true;
    const SkBitmap drawn { render(&tree, &config) };
    // 보이는 요소의 변에 옅은 선이 선다. 숨은 요소의 변에는 서지 않는다.
    REQUIRE(pixel(drawn, 51, 80) != pixel(baseline, 51, 80));
    REQUIRE(pixel(drawn, 100, 51) != pixel(baseline, 100, 51));
    REQUIRE(pixel(drawn, 301, 80) == pixel(baseline, 301, 80));
    REQUIRE(pixel(drawn, 350, 51) == pixel(baseline, 350, 51));
    // 선은 1 논리 픽셀이다. 안쪽 깊은 곳은 그대로다.
    REQUIRE(pixel(drawn, 100, 80) == pixel(baseline, 100, 80));
}

namespace {
    // 오른쪽 위 견본 판의 왼쪽 테다. 글 폭을 모르므로 오른쪽 변에서 왼쪽으로 찾는다. 첫 줄 위 여백(y 14)에는 글도 견본도 없어
    // 테만 걸린다. 테는 물리 2이고 먼저 만나는 쪽이 안쪽 픽셀이다. 배율 2의 값이다.
    [[nodiscard]] int palette_left_border(const SkBitmap& drawn, const luil::ui_color_palette& palette, const int surface_right)
    {
        for (int x { surface_right - 14 }; x > 0; --x)
            if (pixel(drawn, x, 14) == palette.tooltip_border)
                return x;
        return -1;
    }

    // 배율 2의 줄 가운데 y다. 줄 높이는 물리 30, 윗 여백은 8 + 12다.
    [[nodiscard]] int palette_row_y(const int row)
    {
        return 8 + 12 + row * 30 + 15;
    }

    // 줄 `row`에서 `from`의 오른쪽으로 `color`가 처음 나오는 x다. 없으면 -1이다.
    //  - 글 폭은 글꼴에 달려 숫자로 적을 수 없으므로 견본 색으로 둘째 열의 자리를 찾는다. core 시험의 글꼴 관리자는 비어 있어 글
    //    폭이 0이고, 그래도 판의 최소 폭 안에 두 열이 선다.
    [[nodiscard]] int find_color(const SkBitmap& drawn, const int from, const int row, const luil::ui_color color)
    {
        for (int x { from }; x < drawn.width(); ++x)
            if (pixel(drawn, x, palette_row_y(row)) == color)
                return x;
        return -1;
    }
}

TEST_CASE("The palette shows every role in two columns in front of its name", "[inspector][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const std::vector<luil::inspector_swatch> swatches { luil::inspector_palette_swatches(palette) };
    REQUIRE(swatches.size() == 56u);
    // 배율 2에서 논리 1000x500이다. 56개가 28줄씩 두 열로 다 든다.
    const luil::rect_f wide_root { 0.0f, 0.0f, 2000.0f, 1000.0f };
    const luil::ui_tree tree { make_scene(wide_root, { 100.0f, 150.0f, 300.0f, 200.0f }, default_target) };
    frame_options options {};
    options.width = 2000;
    options.height = 1000;
    luil::inspector_config config {};
    config.enabled = true;
    config.show_palette = true;
    const SkBitmap baseline { render(&tree, nullptr, options) };
    const SkBitmap drawn { render(&tree, &config, options) };

    REQUIRE(pixel(drawn, 1991, 9) == palette.tooltip_border);
    const int left_edge { palette_left_border(drawn, palette, 2000) };
    REQUIRE(left_edge > 0);
    // 판의 왼쪽 변에서 12 떨어져 첫 열의 견본(물리 24)이 선다. 견본은 줄 안에서 세로로 가운데다.
    const int first_x { left_edge - 1 + 12 + 12 };
    const auto expect_role = [&swatches, &drawn](const int x, const int row, const std::size_t index) {
        CAPTURE(index);
        REQUIRE(pixel(drawn, x, palette_row_y(row)) == swatches[index].color);
    };
    // 불투명한 역할만 그대로 읽는다. 알파가 있는 역할은 판 위에 섞여 보인다.
    expect_role(first_x, 0, 0u);
    expect_role(first_x, 1, 1u);
    expect_role(first_x, 2, 2u);
    expect_role(first_x, 10, 10u);

    // 둘째 열은 스물아홉째 역할부터다. 견본의 왼쪽 테(물리 2) 안쪽에서 색을 처음 만난다.
    const int second_hit { find_color(drawn, first_x + 14, 0, swatches[28].color) };
    REQUIRE(second_hit > first_x);
    const int second_x { second_hit + 10 };
    expect_role(second_x, 0, 28u);
    expect_role(second_x, 4, 32u);
    expect_role(second_x, 8, 36u);
    // 하위 팔레트와 마지막 역할까지 보인다.
    REQUIRE(swatches[39].name == u8"caption.background");
    expect_role(second_x, 11, 39u);
    REQUIRE(swatches[55].name == u8"syntax_function");
    expect_role(second_x, 27, 55u);
    // 판 밖은 그대로다.
    REQUIRE(pixel(drawn, 10, 900) == pixel(baseline, 10, 900));
    REQUIRE(pixel(drawn, 1900, 950) == pixel(baseline, 1900, 950));
}

TEST_CASE("The palette fills the second column from where the first one ran out of height", "[inspector][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const std::vector<luil::inspector_swatch> swatches { luil::inspector_palette_swatches(palette) };
    // 물리 높이 300이면 (300 - 40) / 30 = 8줄이다. 16개까지 쓰고 둘째 열은 아홉째 역할부터다.
    const luil::rect_f low_root { 0.0f, 0.0f, 2000.0f, 300.0f };
    const luil::ui_tree tree { make_scene(low_root, { 100.0f, 50.0f, 300.0f, 200.0f }, { 150.0f, 80.0f, 100.0f, 60.0f }) };
    frame_options options {};
    options.width = 2000;
    options.height = 300;
    luil::inspector_config config {};
    config.enabled = true;
    config.show_palette = true;
    const SkBitmap drawn { render(&tree, &config, options) };

    const int left_edge { palette_left_border(drawn, palette, 2000) };
    REQUIRE(left_edge > 0);
    const int first_x { left_edge - 1 + 12 + 12 };
    REQUIRE(pixel(drawn, first_x, palette_row_y(0)) == swatches[0].color);
    // 열한째 역할(`accent`)은 둘째 열의 셋째 줄이다. 판을 한 열로 잇는 그림이면 이 줄에 그 색이 없다.
    REQUIRE(swatches[10].name == u8"accent");
    REQUIRE(find_color(drawn, first_x + 14, 2, swatches[10].color) > first_x);
}

TEST_CASE("Drawing the inspector leaves the tree, the interaction and hit testing as they were", "[inspector][raster]")
{
    const luil::ui_tree tree { default_scene() };
    luil::interaction_snapshot interaction {};
    interaction.hovered = box_id(u8"target");
    interaction.focused = box_id(u8"backdrop");
    const luil::interaction_snapshot interaction_before { interaction };
    const luil::access_snapshot access_before { luil::make_access_snapshot(tree) };
    std::vector<const luil::ui_element*> hits_before {};
    for (int y { 0 }; y < surface_height; y += 20)
    {
        for (int x { 0 }; x < surface_width; x += 20)
            hits_before.push_back(tree.hit_test(static_cast<float>(x), static_cast<float>(y)));
    }

    luil::inspector_config config { pick_selected(u8"target") };
    config.pick = luil::inspector_pick::hovered;
    config.outline_all = true;
    config.show_palette = true;
    frame_options options {};
    options.interaction = interaction;
    static_cast<void>(render(&tree, &config, options));

    REQUIRE(interaction == interaction_before);
    REQUIRE(luil::make_access_snapshot(tree) == access_before);
    std::vector<const luil::ui_element*> hits_after {};
    for (int y { 0 }; y < surface_height; y += 20)
    {
        for (int x { 0 }; x < surface_width; x += 20)
            hits_after.push_back(tree.hit_test(static_cast<float>(x), static_cast<float>(y)));
    }
    REQUIRE(hits_after == hits_before);
}
