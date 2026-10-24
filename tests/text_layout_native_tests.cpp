#include "luil/ui/text_layout.h"

#include "host/font_registry.h"
#include "luil/text/grapheme.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/text_area_element.h"
#include "ui/text_layout_internal.h"
#include "ui/text_layout_skia.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkTypeface.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <limits>
#include <string>
#include <thread>
#include <vector>

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
    {
        // cache를 비워 같은 값을 꺼내 비교하지 않고 측정 경로가 따로 지은 값과 견준다.
        luil::clear_text_layout_cache();
        REQUIRE(luil::measure_text(request.text, font) == Catch::Approx(layout.size().width));
    }
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

    // 항목 수 상한보다 훨씬 적은 수의 큰 항목으로 byte 예산을 넘긴다.
    constexpr std::size_t limit { 16 * 1024 * 1024 };
    luil::clear_text_layout_cache();
    const std::u8string bulk(40000, u8'x');
    const auto each { luil::make_text_layout({ .text = bulk, .font_size = 18.0f }).storage_bytes() };
    REQUIRE(each > 0);
    REQUIRE(each < limit / 2);
    const std::size_t count { limit / each + 2 };
    REQUIRE(count < 512);
    for (std::size_t index { 0 }; index < count; ++index)
    {
        const auto number { std::to_string(index) };
        std::u8string text { bulk };
        text.append(reinterpret_cast<const char8_t*>(number.data()), number.size());
        static_cast<void>(luil::make_text_layout({ .text = std::move(text), .font_size = 18.0f }));
    }
    const auto budget { luil::text_layout_cache_usage() };
    REQUIRE(budget.bytes <= limit);
    REQUIRE(budget.bytes > limit / 2);
    REQUIRE(budget.entries < count);
}

TEST_CASE("Native layout cache keys normalize geometry-free fields", "[ui][text-layout][cache]")
{
    const auto face { luil::configured_ui_typeface() };
    REQUIRE(face != nullptr);
    const SkFont font { face, 18.0f };
    luil::clear_text_layout_cache();
    // NaN 폭은 접지 않는 요청과 같다. key에 NaN이 남으면 같은 요청이 영영 맞지 않는다.
    const luil::text_layout_request unmeasured { .text = u8"normalized key", .width = std::numeric_limits<float>::quiet_NaN(), .wrap = true, .line_height = std::numeric_limits<float>::quiet_NaN() };
    const auto first { luil::make_font_text_layout(unmeasured, font) };
    const auto second { luil::make_font_text_layout(unmeasured, font) };
    REQUIRE(first.caret_stops().data() == second.caret_stops().data());
    // 접지 않으면 폭은 기하에 쓰이지 않고, face는 글꼴이 이미 정했다.
    const auto narrow { luil::make_font_text_layout({ .text = u8"unwrapped", .width = 10.0f }, font) };
    const auto wide { luil::make_font_text_layout({ .text = u8"unwrapped", .face = luil::text_layout_face::code, .width = 500.0f }, font) };
    REQUIRE(narrow.caret_stops().data() == wide.caret_stops().data());
    REQUIRE(luil::text_layout_cache_usage().entries == 2);
}

TEST_CASE("Native layout cache drops stale revisions and keeps one entry per request", "[ui][text-layout][cache]")
{
    luil::clear_text_layout_cache();
    const luil::text_layout_request request { .text = u8"stale revision", .font_size = 18.0f };
    static_cast<void>(luil::make_text_layout(request));
    luil::invalidate_text_layouts();
    static_cast<void>(luil::make_text_layout(request));
    REQUIRE(luil::text_layout_cache_usage().entries == 1);

    luil::clear_text_layout_cache();
    const luil::text_layout_request shared { .text = u8"concurrent miss", .font_size = 18.0f };
    std::vector<luil::text_layout> results(4);
    std::vector<std::thread> workers {};
    for (std::size_t index { 0 }; index < results.size(); ++index)
        workers.emplace_back([&results, &shared, index] { results[index] = luil::make_text_layout(shared); });
    for (auto& worker : workers)
        worker.join();
    REQUIRE(luil::text_layout_cache_usage().entries == 1);
    for (const auto& result : results)
        REQUIRE(result.caret_stops().data() == results.front().caret_stops().data());
}

TEST_CASE("Native layout reports an empty typeface as unavailable", "[ui][text-layout][native]")
{
    // SkFont는 nullptr typeface를 빈 typeface로 바꾼다. 폭이 모두 0이니 호출자가 자기 측정기로 돌아가야 한다.
    const auto layout { luil::make_font_text_layout({ .text = u8"abc" }, SkFont { sk_sp<SkTypeface> {}, 12.0f }) };
    REQUIRE(layout.backend() == luil::text_layout_backend::unavailable);
}

TEST_CASE("Relayout honors the requested font size", "[ui][text-layout][native]")
{
    const auto face { luil::configured_ui_typeface() };
    REQUIRE(face != nullptr);
    const std::u8string text { u8"remake size" };
    const auto small { luil::make_font_text_layout({ .text = text, .font_size = 12.0f }, SkFont { face, 12.0f }) };
    const auto large { luil::remake_text_layout(small, { .text = text, .font_size = 24.0f }) };
    const auto direct { luil::make_font_text_layout({ .text = text, .font_size = 24.0f }, SkFont { face, 24.0f }) };
    REQUIRE(large.size().width == Catch::Approx(direct.size().width));
    REQUIRE(large.size().width > small.size().width * 1.5f);
}

TEST_CASE("Invalid UTF-8 measures like its layout without changing byte offsets", "[ui][text-layout][native]")
{
    const auto face { luil::configured_ui_typeface() };
    REQUIRE(face != nullptr);
    const SkFont font { face, 24.0f };
    std::u8string text { u8"AVA" };
    text.push_back(static_cast<char8_t>(0xFF));
    text.append(u8"To office");
    const auto layout { luil::make_font_text_layout({ .text = text, .font_size = 24.0f }, font) };
    // 고친 사본으로 shape하면 U+FFFD가 3 byte라 원문 offset이 어긋난다. 원문 그대로 끈 구성으로 잰다.
    REQUIRE(layout.backend() == luil::text_layout_backend::primitive);
    REQUIRE(layout.text() == text);
    REQUIRE(layout.lines().back().end == text.size());
    if (luil::ui_text_shaping_enabled())
    {
        luil::clear_text_layout_cache();
        REQUIRE(luil::measure_text(text, font) == Catch::Approx(layout.size().width));
    }
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
        std::u8string ui {};
        std::u8string code {};
        ~restore_fonts() { luil::set_configured_fonts(ui, code); }
    } restore { luil::configured_ui_family(), luil::configured_code_family() };
    luil::set_configured_fonts(u8"Courier New", {});
    const std::u8string alternate { u8"wide WW illi" };
    const auto bounds { input.text_span_bounds({ alternate, 0, 0, alternate.size() }, luil::default_text_measurer()) };
    const auto original { luil::make_font_text_layout({ .text = alternate }, SkFont { original_face, 11.0f }) };
    REQUIRE(bounds->width == Catch::Approx(original.size().width));

}
