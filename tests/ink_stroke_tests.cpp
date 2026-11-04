#include "luil/ui/ink.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/ink_canvas_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/zoom_view_element.h"

#include "raster_probe.h"
#include "ui/ink_backing.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {
    using Catch::Approx;

    constexpr float raster_scale { 2.0f };

    [[nodiscard]] float distance_to_polyline(const luil::ink_point& point, const std::vector<luil::ink_point>& line)
    {
        float best { std::numeric_limits<float>::infinity() };
        for (std::size_t index { 0 }; index + 1 < line.size(); ++index)
        {
            const luil::ink_point& a { line[index] };
            const luil::ink_point& b { line[index + 1] };
            const float dx { b.x - a.x };
            const float dy { b.y - a.y };
            const float length { dx * dx + dy * dy };
            const float t { length > 0.0f ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length, 0.0f, 1.0f) : 0.0f };
            best = std::min(best, std::hypot(point.x - (a.x + dx * t), point.y - (a.y + dy * t)));
        }
        return best;
    }

    // 캔버스 하나만 담은 tree다. 캔버스는 물리 픽셀의 `slot`에 선다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> canvas_tree(luil::ink_canvas_config config, const luil::rect_f& slot)
    {
        auto canvas { std::make_unique<luil::ink_canvas_element>(std::move(config)) };
        canvas->arrange({ slot, raster_scale });
        return std::make_shared<const luil::ui_tree>(std::move(canvas));
    }
}

TEST_CASE("Ink width follows pressure between the minimum ratio and the full width", "[ink][stroke]")
{
    const luil::ink_style style { .width = 4.0f, .minimum_ratio = 0.25f };
    REQUIRE(luil::ink_width_at(style, 0.0f) == Approx(1.0f));
    REQUIRE(luil::ink_width_at(style, 0.5f) == Approx(2.5f));
    REQUIRE(luil::ink_width_at(style, 1.0f) == Approx(4.0f));
    // 압력은 0~1로 접는다. 모르는 압력(유한하지 않다)은 가장 센 압력과 같다.
    REQUIRE(luil::ink_width_at(style, 3.0f) == Approx(4.0f));
    REQUIRE(luil::ink_width_at(style, std::numeric_limits<float>::quiet_NaN()) == Approx(4.0f));
    REQUIRE(luil::ink_width_at({ .width = -1.0f }, 1.0f) == 0.0f);

    // 압력 1(장치가 압력을 알리지 않은 점)의 점 하나는 `width` 지름의 둥근 점이다.
    const luil::ink_stroke dot { luil::make_ink_stroke(1, { { 10.0f, 20.0f, 1.0f } }, style) };
    REQUIRE(dot.bounds().width == Approx(4.0f).margin(0.01f));
    REQUIRE(dot.bounds().x == Approx(8.0f).margin(0.01f));
    const luil::ink_stroke light { luil::make_ink_stroke(2, { { 10.0f, 20.0f, 0.0f } }, style) };
    REQUIRE(light.bounds().width == Approx(1.0f).margin(0.01f));
}

TEST_CASE("Ink strokes drop non finite points and keep empty and single point strokes", "[ink][stroke]")
{
    const float nan { std::numeric_limits<float>::quiet_NaN() };
    const float infinity { std::numeric_limits<float>::infinity() };
    const luil::ink_stroke stroke { luil::make_ink_stroke(7, { { 0.0f, 0.0f, 0.5f }, { nan, 3.0f, 1.0f }, { 10.0f, infinity, 1.0f }, { 20.0f, 0.0f, 2.0f } }, {}) };
    REQUIRE(stroke.id() == 7u);
    REQUIRE(stroke.points().size() == 2u);
    REQUIRE(stroke.points()[1].x == 20.0f);
    // 압력도 0~1로 접어 저장한다.
    REQUIRE(stroke.points()[1].pressure == 1.0f);
    REQUIRE(stroke.empty() == false);

    const luil::ink_stroke empty { luil::make_ink_stroke(8, {}, {}) };
    REQUIRE(empty.empty());
    REQUIRE(empty.bounds().width == 0.0f);
    const luil::ink_stroke none {};
    REQUIRE(none.empty());
    REQUIRE(none.id() == 0u);
    REQUIRE(none.points().empty());
}

TEST_CASE("Simplified ink stays within tolerance and keeps pressure changes", "[ink][stroke]")
{
    std::vector<luil::ink_point> points {};
    for (int index { 0 }; index <= 100; ++index)
    {
        const float x { static_cast<float>(index) };
        points.push_back({ x, std::sin(x * 0.05f) * 10.0f + ((index % 2) != 0 ? 0.05f : -0.05f), 0.5f });
    }
    const std::vector<luil::ink_point> simple { luil::simplify_ink_points(points, 0.25f, 0.05f) };
    REQUIRE(simple.size() < points.size() / 2);
    REQUIRE(simple.front() == points.front());
    REQUIRE(simple.back() == points.back());
    for (const luil::ink_point& point : points)
        REQUIRE(distance_to_polyline(point, simple) <= 0.25f + 0.001f);

    // 곧은 선 위에서 압력이 크게 바뀌는 점은 남는다.
    std::vector<luil::ink_point> straight { { 0.0f, 0.0f, 0.2f }, { 10.0f, 0.0f, 0.2f }, { 20.0f, 0.0f, 0.9f }, { 30.0f, 0.0f, 0.2f }, { 40.0f, 0.0f, 0.2f } };
    const std::vector<luil::ink_point> kept { luil::simplify_ink_points(straight, 0.25f, 0.05f) };
    REQUIRE(kept.size() >= 3u);
    REQUIRE(std::find(kept.begin(), kept.end(), straight[2]) != kept.end());
    // 압력을 보지 않으면 곧은 선은 두 점이다. 오차 0은 그대로다.
    REQUIRE(luil::simplify_ink_points(straight, 0.25f, 0.0f).size() == 2u);
    REQUIRE(luil::simplify_ink_points(straight, 0.0f, 0.05f).size() == straight.size());
}

TEST_CASE("Eraser hits use the segment distance after a bounds check", "[ink][stroke][eraser]")
{
    const luil::ink_style style { .width = 4.0f, .minimum_ratio = 1.0f };
    luil::ink_layer layer {};
    layer = layer.with(luil::make_ink_stroke(1, { { 0.0f, 0.0f, 1.0f }, { 100.0f, 0.0f, 1.0f } }, style));
    layer = layer.with(luil::make_ink_stroke(2, { { 0.0f, 50.0f, 1.0f }, { 100.0f, 50.0f, 1.0f } }, style));
    layer = layer.with(luil::make_ink_stroke(3, { { 300.0f, 300.0f, 1.0f } }, style));
    REQUIRE(layer.size() == 3u);

    // 획 1의 선분에서 9만큼 떨어진 점은 반지름 7 + 폭의 절반 2로 닿는다. 10이면 닿지 않는다.
    REQUIRE(luil::ink_strokes_hit(layer, { 50.0f, 9.0f }, { 50.0f, 9.0f }, 7.0f) == std::vector<std::uint64_t> { 1 });
    REQUIRE(luil::ink_strokes_hit(layer, { 50.0f, 10.0f }, { 50.0f, 10.0f }, 7.0f).empty());
    // 지우개가 두 표본 사이를 쓸면 그 선분이 가로지른 획이 모두 닿는다 (묶음의 순서).
    REQUIRE(luil::ink_strokes_hit(layer, { 50.0f, -20.0f }, { 50.0f, 80.0f }, 1.0f) == std::vector<std::uint64_t> { 1, 2 });
    // 점 하나의 획도 닿는다.
    REQUIRE(luil::ink_strokes_hit(layer, { 305.0f, 300.0f }, { 305.0f, 300.0f }, 3.5f) == std::vector<std::uint64_t> { 3 });
    REQUIRE(luil::ink_strokes_hit(layer, { 200.0f, 200.0f }, { 210.0f, 200.0f }, 5.0f).empty());
}

TEST_CASE("Ink layers add and remove strokes as new values and ignore unknown ids", "[ink][stroke]")
{
    const luil::ink_layer empty {};
    const luil::ink_layer one { empty.with(luil::make_ink_stroke(1, { { 0.0f, 0.0f, 1.0f } }, {})) };
    const luil::ink_layer two { one.with(luil::make_ink_stroke(2, { { 5.0f, 0.0f, 1.0f } }, {})) };
    REQUIRE(empty.empty());
    REQUIRE(one.size() == 1u);
    REQUIRE(two.size() == 2u);
    // 빈 획은 더하지 않는다.
    REQUIRE(two.with(luil::ink_stroke {}).size() == 2u);

    const std::vector<std::uint64_t> remove { 1, 99 };
    const luil::ink_layer left { two.without(remove) };
    REQUIRE(left.size() == 1u);
    REQUIRE(left.strokes()[0].id() == 2u);
    // 없는 id만이면 같은 묶음이다. 앞의 묶음은 바뀌지 않는다.
    const std::vector<std::uint64_t> missing { 99 };
    REQUIRE(left.without(missing).size() == 1u);
    REQUIRE(two.size() == 2u);
}

TEST_CASE("Content coordinates undo the zoom view transform without multiplying zoom twice", "[ink][zoom][rotate]")
{
    // 창 배율 2 × zoom 2라 캔버스가 받는 `context.scale`은 4다. 회전은 90도다.
    const luil::rect_f page { -50.0f, -25.0f, 100.0f, 50.0f };
    luil::zoom_view_config view_config {
        .owner = u8"view",
        .zoom = 2.0f,
        .origin = { 10.0f, -5.0f },
        .content_bounds = luil::rotated_bounds(page, 90.0f),
        .zoom_by = [](float, luil::zoom_point) { return luil::input_action {}; },
        .pan_by = [](luil::zoom_point) { return luil::input_action {}; },
        .rotation = 90.0f,
    };
    auto view { std::make_unique<luil::zoom_view_element>(view_config) };
    auto canvas { std::make_unique<luil::ink_canvas_element>(luil::ink_canvas_config { .owner = u8"ink", .rotation = 90.0f, .content_bounds = page }) };
    const luil::ink_canvas_element* const placed { canvas.get() };
    view->set_content(std::move(canvas));
    view->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 2.0f });
    const luil::zoom_view_state state { view->state() };
    REQUIRE(state.zoom == 2.0f);

    // 내용 점 p의 화면 자리는 `보기 가운데 + origin × scale + scale × zoom × R(rotation) p`다.
    const luil::zoom_point content { 20.0f, 10.0f };
    const luil::zoom_point turned { luil::rotate_point(content, state.rotation) };
    const float screen_x { 200.0f + state.origin.x * 2.0f + 4.0f * turned.x };
    const float screen_y { 200.0f + state.origin.y * 2.0f + 4.0f * turned.y };
    const luil::point_f back { luil::ink_content_point(placed->bounds(), 4.0f, 90.0f, page, screen_x, screen_y) };
    REQUIRE(back.x == Approx(content.x).margin(0.001f));
    REQUIRE(back.y == Approx(content.y).margin(0.001f));

    // rotate-gesture-design.md의 식 `R(-rotation)((점 − 보기 가운데 − origin × scale) / (scale × zoom))`과 같다.
    const luil::zoom_point rotate_formula {
        luil::rotate_point({ (screen_x - 200.0f - state.origin.x * 2.0f) / 4.0f, (screen_y - 200.0f - state.origin.y * 2.0f) / 4.0f }, -state.rotation)
    };
    REQUIRE(back.x == Approx(rotate_formula.x).margin(0.001f));
    REQUIRE(back.y == Approx(rotate_formula.y).margin(0.001f));

    // 내용 경계가 없으면 원점이 slot의 왼쪽 위다.
    const luil::point_f plain { luil::ink_content_point({ 100.0f, 40.0f, 200.0f, 200.0f }, 2.0f, 0.0f, std::nullopt, 120.0f, 50.0f) };
    REQUIRE(plain.x == Approx(10.0f));
    REQUIRE(plain.y == Approx(5.0f));
}

TEST_CASE("Ink layers memo one picture per default color", "[ink][stroke][draw]")
{
    const luil::ink_layer layer { luil::ink_layer {}.with(luil::make_ink_stroke(1, { { 0.0f, 0.0f, 1.0f }, { 20.0f, 0.0f, 1.0f } }, {})) };
    REQUIRE(luil::ink_access::memo(layer) == nullptr);
    SkBitmap pixels {};
    pixels.allocN32Pixels(40, 20);
    SkCanvas canvas { pixels };
    luil::ink_access::draw(canvas, layer, 0xFF112233u);
    const SkPicture* const first { luil::ink_access::memo(layer) };
    REQUIRE(first != nullptr);
    // 복사한 묶음(앱이 frame마다 config에 싣는 값)은 그 그림을 함께 나눈다.
    const luil::ink_layer copy { layer };
    luil::ink_access::draw(canvas, copy, 0xFF112233u);
    REQUIRE(luil::ink_access::memo(layer) == first);
    // 기본색이 바뀌면(테마 전환) 다시 기록한다.
    luil::ink_access::draw(canvas, copy, 0xFF445566u);
    REQUIRE(luil::ink_access::memo(layer) != nullptr);
}

TEST_CASE("Ink canvases draw strokes by pressure, clip to their bounds and paint overlaps once", "[ink][draw][raster]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    constexpr luil::ui_color red { 0xFFFF0000u };
    const luil::rect_f slot { 20.0f, 20.0f, 160.0f, 120.0f };
    const auto frame_of = [&](luil::ink_canvas_config config) {
        luil::testing::raster_frame frame { 200, 160, palette, raster_scale };
        frame.draw(*canvas_tree(std::move(config), slot), {});
        return frame;
    };

    // 압력 1의 획은 압력 0의 획보다 굵다 (폭 8과 2, 논리 픽셀).
    const luil::ink_style style { .color = red, .width = 8.0f, .minimum_ratio = 0.25f };
    luil::ink_layer layer {};
    layer = layer.with(luil::make_ink_stroke(1, { { 10.0f, 10.0f, 1.0f }, { 70.0f, 10.0f, 1.0f } }, style));
    layer = layer.with(luil::make_ink_stroke(2, { { 10.0f, 40.0f, 0.0f }, { 70.0f, 40.0f, 0.0f } }, style));
    const luil::testing::raster_frame drawn { frame_of({ .owner = u8"ink", .strokes = layer }) };
    // 내용 (40, 10)은 화면 (20 + 80, 20 + 20)이다.
    const int heavy { drawn.count_color({ 90.0f, 20.0f, 20.0f, 40.0f }, red) };
    const int thin { drawn.count_color({ 90.0f, 80.0f, 20.0f, 40.0f }, red) };
    REQUIRE(heavy > 0);
    REQUIRE(thin > 0);
    REQUIRE(heavy > thin * 2);

    // 캔버스 밖으로 나간 점은 잘린다.
    const luil::ink_layer outside { luil::ink_layer {}.with(luil::make_ink_stroke(3, { { -30.0f, 30.0f, 1.0f }, { 30.0f, 30.0f, 1.0f } }, style)) };
    const luil::testing::raster_frame clipped { frame_of({ .owner = u8"ink", .strokes = outside }) };
    REQUIRE(clipped.contains_color({ 0.0f, 0.0f, 19.0f, 160.0f }, red) == false);
    REQUIRE(clipped.contains_color({ 20.0f, 60.0f, 40.0f, 40.0f }, red));

    // 반투명 획이 자기와 겹친 곳도 한 번만 칠해진다 (점 사이의 원과 사다리꼴이 한 path다).
    constexpr luil::ui_color translucent { 0x80FF0000u };
    const luil::ink_style soft { .color = translucent, .width = 12.0f, .minimum_ratio = 1.0f };
    const luil::ink_layer loop { luil::ink_layer {}.with(luil::make_ink_stroke(4, { { 10.0f, 30.0f, 1.0f }, { 40.0f, 30.0f, 1.0f }, { 41.0f, 30.0f, 1.0f }, { 70.0f, 30.0f, 1.0f } }, soft)) };
    const luil::testing::raster_frame overlap { frame_of({ .owner = u8"ink", .strokes = loop }) };
    // 획 한가운데(여러 원이 겹친 자리)와 이음매 자리의 색이 같다.
    REQUIRE(overlap.pixel_at(100, 80) == overlap.pixel_at(102, 80));
    REQUIRE(overlap.pixel_at(60, 80) == overlap.pixel_at(140, 80));
    REQUIRE(overlap.pixel_at(60, 80) != palette.window_background);

    // 기본색 획은 그리는 시점의 팔레트를 따른다.
    const luil::ink_layer plain { luil::ink_layer {}.with(luil::make_ink_stroke(5, { { 10.0f, 30.0f, 1.0f }, { 70.0f, 30.0f, 1.0f } }, { .width = 8.0f })) };
    luil::testing::raster_frame dark { 200, 160, palette, raster_scale };
    dark.draw(*canvas_tree({ .owner = u8"ink", .strokes = plain }, slot), {});
    REQUIRE(dark.contains_color({ 60.0f, 70.0f, 60.0f, 40.0f }, palette.primary_foreground));
    const luil::ui_color_palette light_palette { luil::color_palette_for(luil::color_theme::light) };
    luil::testing::raster_frame light { 200, 160, light_palette, raster_scale };
    light.draw(*canvas_tree({ .owner = u8"ink", .strokes = plain }, slot), {});
    REQUIRE(light.contains_color({ 60.0f, 70.0f, 60.0f, 40.0f }, light_palette.primary_foreground));
    REQUIRE(light_palette.primary_foreground != palette.primary_foreground);
}

TEST_CASE("Rendering an ink layer gives a transparent image of the asked size", "[ink][draw]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::light) };
    const luil::ink_layer layer { luil::ink_layer {}.with(luil::make_ink_stroke(1, { { 5.0f, 5.0f, 1.0f }, { 35.0f, 5.0f, 1.0f } }, { .width = 4.0f })) };
    const luil::ui_image image { luil::render_ink_image(layer, { 40.0f, 10.0f }, 2.0f, palette) };
    REQUIRE(image.valid());
    REQUIRE(image.width() == 80);
    REQUIRE(image.height() == 20);
    REQUIRE(luil::render_ink_image(layer, { 0.0f, 10.0f }, 2.0f, palette).valid() == false);
    REQUIRE(luil::render_ink_image(layer, { 40.0f, 10.0f }, std::numeric_limits<float>::quiet_NaN(), palette).valid() == false);
}
