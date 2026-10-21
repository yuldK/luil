#include "luil/ui/text_layout.h"

#include "host/font_registry.h"
#include "luil/text/grapheme.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/text_area_element.h"
#include "ui/text_layout_skia.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkTypeface.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

TEST_CASE("Native layout drawing and measurement share glyph runs and caret geometry", "[ui][text-layout][native]")
{
    const auto face { luil::configured_ui_typeface() };
    REQUIRE(face != nullptr);
    const SkFont font { face, 24.0f };
    const luil::text_layout_request request { .text = u8"office e\u0301 각 🇰🇷 👩🏽‍💻", .font_size = 24.0f };
    const auto layout { luil::make_font_text_layout(request, font) };
    REQUIRE(layout.backend() != luil::text_layout_backend::unavailable);
    REQUIRE_FALSE(layout.runs().empty());
    if (luil::ui_text_shaping_enabled())
        REQUIRE(luil::measure_text(request.text, font) == Catch::Approx(layout.size().width));
    for (const auto at : luil::text::grapheme_boundaries(request.text))
    {
        const auto caret { layout.caret_rect(at) };
        REQUIRE(layout.hit_test({ caret.x, 1.0f }).offset == at);
    }
    SkBitmap bitmap {};
    bitmap.allocN32Pixels(900, 80);
    bitmap.eraseColor(SK_ColorWHITE);
    SkCanvas canvas { bitmap };
    luil::draw_text_layout(canvas, layout, { 5.0f, 5.0f }, luil::solid_paint(SK_ColorBLACK));
    bool ink { false };
    for (int y { 0 }; y < bitmap.height(); ++y)
        for (int x { 0 }; x < bitmap.width(); ++x)
            ink = ink || bitmap.getColor(x, y) != SK_ColorWHITE;
    REQUIRE(ink);
}

TEST_CASE("Native layout cache reuses immutable results and respects revisions and its byte budget", "[ui][text-layout][cache]")
{
    luil::clear_text_layout_cache();
    const luil::text_layout_request request { .text = u8"ffi cache", .font_size = 18.0f };
    const auto first { luil::make_text_layout(request) };
    const auto second { luil::make_text_layout(request) };
    REQUIRE(first.caret_stops().data() == second.caret_stops().data());
    REQUIRE(luil::text_layout_cache_usage().hits > 0);
    luil::invalidate_text_layouts();
    const auto changed { luil::make_text_layout(request) };
    REQUIRE(first.caret_stops().data() != changed.caret_stops().data());
    auto bold { request };
    bold.styles.push_back({ 0, 3, true });
    const auto styled { luil::make_text_layout(bold) };
    REQUIRE(styled.caret_stops().data() != changed.caret_stops().data());
    for (int index { 0 }; index < 550; ++index)
    {
        const auto number { std::to_string(index) };
        std::u8string text { u8"entry " };
        text.append(reinterpret_cast<const char8_t*>(number.data()), number.size());
        static_cast<void>(luil::make_text_layout({ .text = std::move(text), .font_size = 18.0f }));
    }
    const auto usage { luil::text_layout_cache_usage() };
    REQUIRE(usage.entries <= 512);
    REQUIRE(usage.bytes <= 16 * 1024 * 1024);
    REQUIRE(first.text() == request.text);
}

TEST_CASE("Input and document queries agree with the arranged native text layout", "[ui][text-layout][native]")
{
    if (luil::ui_text_shaping_enabled() == false)
        SKIP("The configured application retains the legacy text input path");
    const std::u8string text { u8"office e\u0301" };
    luil::text_input_view view { .text = text };
    luil::text_input_element input { { luil::application_element_kind(1) }, view, {} };
    input.arrange({ { 0.0f, 0.0f, 500.0f, 30.0f } });
    for (const auto at : luil::text::grapheme_boundaries(text))
    {
        const auto bounds { input.text_span_bounds({ text, 0, at, at }, luil::default_text_measurer()) };
        REQUIRE(bounds.has_value());
        REQUIRE(input.offset_at(bounds->x, luil::default_text_measurer()) == at);
    }
    luil::text_area_config config {};
    config.wrap = false;
    config.font_size = 20.0f;
    config.spans = std::make_shared<const std::vector<luil::text_span>>(std::vector<luil::text_span> { { 0, 6, luil::text_color_role::accent, true } });
    config.wrap_cache = std::make_shared<luil::text_area_wrap_cache>();
    luil::text_area_element document { { luil::application_element_kind(2) }, view, config };
    document.arrange({ { 0.0f, 0.0f, 500.0f, 100.0f } });
    const auto layout { luil::make_text_layout({ .text = text, .font_size = 20.0f, .styles = { { 0, 6, true } } }) };
    const auto start { document.text_span_bounds({ text, 0, 0, 0 }, luil::default_text_measurer()) };
    const auto end { document.text_span_bounds({ text, 0, text.size(), text.size() }, luil::default_text_measurer()) };
    REQUIRE(end->x - start->x == Catch::Approx(layout.size().width));
    const auto misses { luil::text_layout_cache_usage().misses };
    luil::text_area_element unchanged { { luil::application_element_kind(2) }, view, config };
    unchanged.arrange({ { 0.0f, 0.0f, 500.0f, 100.0f } });
    REQUIRE(luil::text_layout_cache_usage().misses == misses);
    const auto original_face { luil::configured_ui_typeface() };
    struct restore_fonts
    {
        ~restore_fonts() { luil::set_configured_fonts({}, {}); }
    } restore {};
    luil::set_configured_fonts(u8"Courier New", {});
    const std::u8string alternate { u8"wide WW illi" };
    const auto bounds { input.text_span_bounds({ alternate, 0, 0, alternate.size() }, luil::default_text_measurer()) };
    const auto original { luil::make_font_text_layout({ .text = alternate }, SkFont { original_face, 11.0f }) };
    REQUIRE(bounds->width == Catch::Approx(original.size().width));

}
