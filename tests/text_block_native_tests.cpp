#include "luil/ui/text_block.h"

#include "host/font_registry.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkTypeface.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <utility>

namespace {
    // 실제 UI 글꼴로 그리는 바탕이다. 배율 1이라 상자·돌려받은 자리가 그대로 픽셀이다.
    struct text_canvas
    {
        static constexpr SkColor background { SK_ColorWHITE };

        // canvas는 만들 때의 픽셀에 붙으므로 bitmap을 먼저 채운다.
        [[nodiscard]] static SkBitmap allocated()
        {
            SkBitmap bitmap {};
            bitmap.allocN32Pixels(400, 160);
            bitmap.eraseColor(background);
            return bitmap;
        }

        [[nodiscard]] luil::draw_context context()
        {
            return luil::draw_context { .canvas = canvas, .ui_typeface = typeface.get(), .code_typeface = typeface.get(), .palette = palette };
        }

        // `area` 안에서 바탕과 다른 픽셀이 선 가장 왼쪽·오른쪽 열이다.
        [[nodiscard]] std::optional<std::pair<int, int>> ink_columns(const luil::rect_f& area) const
        {
            std::optional<std::pair<int, int>> columns {};
            for (int y { static_cast<int>(area.y) }; y < static_cast<int>(area.y + area.height); ++y)
                for (int x { static_cast<int>(area.x) }; x < static_cast<int>(area.x + area.width); ++x)
                {
                    if (bitmap.getColor(x, y) == background)
                        continue;
                    if (columns.has_value() == false)
                        columns = std::pair { x, x };
                    columns->first = std::min(columns->first, x);
                    columns->second = std::max(columns->second, x);
                }
            return columns;
        }

        [[nodiscard]] int count(const SkColor color) const
        {
            int found { 0 };
            for (int y { 0 }; y < bitmap.height(); ++y)
                for (int x { 0 }; x < bitmap.width(); ++x)
                    found += bitmap.getColor(x, y) == color ? 1 : 0;
            return found;
        }

        sk_sp<SkTypeface> typeface { luil::configured_ui_typeface() };
        luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::light) };
        SkBitmap bitmap { allocated() };
        SkCanvas canvas { bitmap };
    };
}

TEST_CASE("A text block aligns its drawn width inside the box", "[ui][text-block][native]")
{
    const luil::rect_f box { 20.0f, 20.0f, 300.0f, 40.0f };
    const SkPaint ink { luil::solid_paint(SK_ColorBLACK) };
    for (const luil::text_align align : { luil::text_align::start, luil::text_align::center, luil::text_align::end })
    {
        text_canvas surface {};
        auto context { surface.context() };
        luil::text_block_style style { .font = { .size = 16.0f }, .align = align };
        const luil::rect_f drawn { luil::draw_text_block(context, u8"Hello 글자", box, style, ink) };
        const float width { luil::measure_text(u8"Hello 글자", luil::make_text_font(context, style.font)) };
        REQUIRE(width > 0.0f);
        REQUIRE(drawn.width == Catch::Approx(width));
        if (align == luil::text_align::start)
            REQUIRE(drawn.x == box.x);
        else if (align == luil::text_align::end)
            REQUIRE(drawn.x + drawn.width == Catch::Approx(box.x + box.width));
        else
            REQUIRE(drawn.x + drawn.width * 0.5f == Catch::Approx(box.x + box.width * 0.5f));

        // 글자는 돌려준 자리 안에 선다 (안티에일리어싱 한 픽셀을 둔다).
        const auto columns { surface.ink_columns({ 0.0f, 0.0f, 400.0f, 160.0f }) };
        REQUIRE(columns.has_value());
        REQUIRE(static_cast<float>(columns->first) >= drawn.x - 2.0f);
        REQUIRE(static_cast<float>(columns->second) <= drawn.x + drawn.width + 2.0f);
    }
}

TEST_CASE("A text block shortens a line that does not fit and stacks wrapped lines from the chosen edge", "[ui][text-block][native]")
{
    text_canvas surface {};
    auto context { surface.context() };
    const SkPaint ink { luil::solid_paint(SK_ColorBLACK) };
    const luil::text_block_style one_line { .font = { .size = 16.0f } };
    const luil::rect_f narrow { luil::draw_text_block(context, u8"a long sentence that cannot fit", { 10.0f, 10.0f, 80.0f, 30.0f }, one_line, ink) };
    REQUIRE(narrow.width <= 80.0f);
    REQUIRE(narrow.width > 0.0f);

    const luil::text_block_style wrapped { .font = { .size = 14.0f }, .vertical = luil::text_vertical_align::bottom, .wrap = true, .max_lines = 2 };
    const luil::rect_f box { 10.0f, 40.0f, 120.0f, 110.0f };
    const luil::rect_f drawn { luil::draw_text_block(context, u8"alpha beta gamma delta epsilon zeta eta theta", box, wrapped, ink) };
    REQUIRE(drawn.height == Catch::Approx(2.0f * luil::text_block_line_height(wrapped)));
    REQUIRE(drawn.y + drawn.height == Catch::Approx(box.y + box.height));
    REQUIRE(drawn.width <= box.width + 0.5f);
}

TEST_CASE("Measuring a text block agrees with drawing it, weights included", "[ui][text-block][native]")
{
    text_canvas surface {};
    auto context { surface.context() };
    for (const luil::font_weight weight : { luil::font_weight::light, luil::font_weight::regular, luil::font_weight::semibold, luil::font_weight::bold })
    {
        const luil::text_block_style style { .font = { .size = 15.0f, .weight = weight, .slant = weight == luil::font_weight::bold ? luil::font_slant::italic : luil::font_slant::upright } };
        // 배치는 문맥에 측정기가 없어 설정된 UI 글꼴로 잰다 — 그리기가 쓰는 글꼴과 같다.
        const luil::text_block_layout measured { luil::measure_text_block(luil::measure_context {}, u8"Measure 측정 Wide", style, 1000.0f) };
        const luil::rect_f drawn { luil::draw_text_block(context, u8"Measure 측정 Wide", { 0.0f, 0.0f, 1000.0f, 40.0f }, style, luil::solid_paint(SK_ColorBLACK)) };
        REQUIRE(measured.truncated == false);
        REQUIRE(measured.width == Catch::Approx(drawn.width));
    }
}

TEST_CASE("A text block halo rings the letters for text over a picture", "[ui][text-block][native]")
{
    const luil::rect_f box { 10.0f, 10.0f, 380.0f, 60.0f };
    const SkPaint ink { luil::solid_paint(SK_ColorWHITE) };
    luil::text_block_style style { .font = { .size = 28.0f, .weight = luil::font_weight::bold } };

    text_canvas plain {};
    plain.bitmap.eraseColor(SK_ColorGRAY);
    auto plain_context { plain.context() };
    static_cast<void>(luil::draw_text_block(plain_context, u8"Over a picture", box, style, ink));
    REQUIRE(plain.count(SK_ColorBLACK) == 0);

    text_canvas haloed {};
    haloed.bitmap.eraseColor(SK_ColorGRAY);
    auto haloed_context { haloed.context() };
    style.halo_color = luil::make_ui_color(0, 0, 0);
    style.halo_width = 2.0f;
    static_cast<void>(luil::draw_text_block(haloed_context, u8"Over a picture", box, style, ink));
    REQUIRE(haloed.count(SK_ColorBLACK) > 0);
    // 글자는 테두리 위에 얹힌다.
    REQUIRE(haloed.count(SK_ColorWHITE) > 0);
}
