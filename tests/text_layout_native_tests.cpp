#include "luil/ui/text_layout.h"

#include "host/font_registry.h"
#include "luil/text/grapheme.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/text_area_element.h"
#include "luil/ui/ui_tree.h"
#include "raster_probe.h"
#include "ui/text_layout_internal.h"
#include "ui/text_layout_skia.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkTypeface.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>
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
    const auto usage { luil::text_layout_cache_usage() };
    REQUIRE(usage.entries == 2);
    REQUIRE(usage.hits == 2);
    REQUIRE(usage.misses == 2);
}

TEST_CASE("Native layout cache evicts the least recently used entry", "[ui][text-layout][cache]")
{
    luil::clear_text_layout_cache();
    const auto numbered = [](const int index) {
        const auto number { std::to_string(index) };
        std::u8string text { u8"lru " };
        text.append(reinterpret_cast<const char8_t*>(number.data()), number.size());
        return luil::text_layout_request { .text = std::move(text), .font_size = 18.0f };
    };
    const luil::text_layout_request recent { .text = u8"lru recent", .font_size = 18.0f };
    const luil::text_layout_request oldest { .text = u8"lru oldest", .font_size = 18.0f };
    static_cast<void>(luil::make_text_layout(recent));
    static_cast<void>(luil::make_text_layout(oldest));
    for (int index { 0 }; index < 510; ++index)
        static_cast<void>(luil::make_text_layout(numbered(index)));
    REQUIRE(luil::text_layout_cache_usage().entries == 512);
    // 먼저 넣었어도 다시 쓴 항목은 앞으로 온다. 다음 항목이 밀어내는 것은 그 뒤에 남은 oldest다.
    static_cast<void>(luil::make_text_layout(recent));
    static_cast<void>(luil::make_text_layout(numbered(510)));
    REQUIRE(luil::text_layout_cache_usage().entries == 512);
    const auto before { luil::text_layout_cache_usage() };
    static_cast<void>(luil::make_text_layout(recent));
    REQUIRE(luil::text_layout_cache_usage().hits == before.hits + 1);
    static_cast<void>(luil::make_text_layout(oldest));
    REQUIRE(luil::text_layout_cache_usage().misses == before.misses + 1);
}

TEST_CASE("Native layout cache compares the whole key when hashes collide", "[ui][text-layout][cache]")
{
    // 시험이 실패해도 다음 시험에 같은 hash가 남지 않게 되돌린다.
    struct forced_hash
    {
        forced_hash() { luil::set_text_layout_cache_hash_for_test(7); }
        ~forced_hash() { luil::set_text_layout_cache_hash_for_test(std::nullopt); }
    };
    const forced_hash scope {};
    const luil::text_layout_request left { .text = u8"collide left", .font_size = 18.0f };
    const luil::text_layout_request right { .text = u8"collide right", .font_size = 18.0f };
    const auto first { luil::make_text_layout(left) };
    const auto second { luil::make_text_layout(right) };
    REQUIRE(first.text() == left.text);
    REQUIRE(second.text() == right.text);
    REQUIRE(first.caret_stops().data() != second.caret_stops().data());
    REQUIRE(luil::text_layout_cache_usage().entries == 2);
    // 같은 bucket에서도 원문이 같은 항목만 맞는다.
    const auto again_left { luil::make_text_layout(left) };
    const auto again_right { luil::make_text_layout(right) };
    REQUIRE(again_left.caret_stops().data() == first.caret_stops().data());
    REQUIRE(again_right.caret_stops().data() == second.caret_stops().data());
    // 원문이 같고 크기만 다른 요청도 같은 bucket에서 갈라진다.
    auto larger { left };
    larger.font_size = 24.0f;
    const auto large { luil::make_text_layout(larger) };
    REQUIRE(large.caret_stops().data() != first.caret_stops().data());
    auto usage { luil::text_layout_cache_usage() };
    REQUIRE(usage.entries == 3);
    REQUIRE(usage.hits == 2);
    REQUIRE(usage.misses == 3);
    // 지난 revision을 거둘 때 같은 bucket의 색인도 함께 지워야 다시 지은 값이 낡은 항목에 맞지 않는다.
    luil::invalidate_text_layouts();
    const auto rebuilt { luil::make_text_layout(left) };
    REQUIRE(rebuilt.caret_stops().data() != first.caret_stops().data());
    REQUIRE(luil::make_text_layout(left).caret_stops().data() == rebuilt.caret_stops().data());
    usage = luil::text_layout_cache_usage();
    REQUIRE(usage.entries == 1);
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

TEST_CASE("An italic span lays out as an italic style request and only the paragraph whose italic changed is shaped again", "[ui][text-layout][native][syntax]")
{
    if (luil::ui_text_shaping_enabled() == false)
        SKIP("The configured application retains the legacy text input path");

    // 기울임 구간의 배치 폭이 `styles { italic }` 요청의 폭과 같다. 굵기 구간과 같은 길이다.
    const std::u8string text { u8"office text" };
    luil::text_input_view view { .text = text };
    luil::text_area_config config {};
    config.wrap = false;
    config.font_size = 20.0f;
    config.spans = std::make_shared<const std::vector<luil::text_span>>(std::vector<luil::text_span> { { 0, 6, luil::text_color_role::comment, false, luil::text_background_role::none, true } });
    luil::text_area_element document { { luil::application_element_kind(2) }, view, config };
    document.arrange({ { 0.0f, 0.0f, 500.0f, 100.0f } });
    const auto italic { luil::make_text_layout({ .text = text, .font_size = 20.0f, .styles = { { 0, 6, false, true } } }) };
    const auto upright { luil::make_text_layout({ .text = text, .font_size = 20.0f }) };
    const auto start { document.text_span_bounds({ text, 0, 0, 0 }, luil::default_text_measurer()) };
    const auto end { document.text_span_bounds({ text, 0, text.size(), text.size() }, luil::default_text_measurer()) };
    REQUIRE(start.has_value());
    REQUIRE(end.has_value());
    REQUIRE(end->x - start->x == Catch::Approx(italic.size().width));
    // 기울임과 굵기는 따로 쥔다. 굵은 구간의 폭이 기울임 구간의 폭을 대신하지 않는다.
    const auto bold { luil::make_text_layout({ .text = text, .font_size = 20.0f, .styles = { { 0, 6, true, false } } }) };
    REQUIRE(italic.size().width > 0.0f);
    REQUIRE(upright.size().width > 0.0f);
    REQUIRE(bold.size().width > 0.0f);

    // 두 문단에서 둘째 문단의 기울임만 바뀌면 그 문단만 다시 shape한다.
    luil::clear_text_layout_cache();
    const std::u8string paragraphs { u8"alpha beta\ngamma delta" };
    luil::text_input_view two { .text = paragraphs };
    luil::text_area_config shared {};
    shared.wrap = false;
    shared.font_size = 20.0f;
    shared.wrap_cache = std::make_shared<luil::text_area_wrap_cache>();
    const auto misses_after = [&](const std::vector<luil::text_span>& spans) {
        luil::text_area_config next { shared };
        next.spans = std::make_shared<const std::vector<luil::text_span>>(spans);
        const auto before { luil::text_layout_cache_usage().misses };
        luil::text_area_element element { { luil::application_element_kind(3) }, two, std::move(next) };
        element.arrange({ { 0.0f, 0.0f, 500.0f, 100.0f } });
        return luil::text_layout_cache_usage().misses - before;
    };
    const auto cold { misses_after({}) };
    const auto second_italic { misses_after({ { 11, 16, luil::text_color_role::comment, false, luil::text_background_role::none, true } }) };
    REQUIRE(cold > 0u);
    REQUIRE(second_italic > 0u);
    REQUIRE(second_italic < cold);
    // 같은 구간으로 다시 배치하면 아무것도 다시 shape하지 않는다.
    REQUIRE(misses_after({ { 11, 16, luil::text_color_role::comment, false, luil::text_background_role::none, true } }) == 0u);
    // 색만 바뀌면 배치에 걸리지 않는다. 구문 색은 캐시를 무효로 하지 않는다.
    REQUIRE(misses_after({ { 11, 16, luil::text_color_role::keyword, false, luil::text_background_role::none, true } }) == 0u);
}

TEST_CASE("Native layouts keep line break policies apart and cut at break opportunities", "[ui][text-layout][native][line-break]")
{
    const auto face { luil::configured_ui_typeface() };
    REQUIRE(face != nullptr);
    const SkFont font { face, 18.0f };
    luil::clear_text_layout_cache();
    const std::u8string text { u8"설정을 저장했습니다. 東京「大阪」です。 alpha-beta https://example.com/a/b 가나다라마바사아자차카타파하" };
    luil::text_layout_request words { .text = text, .width = 120.0f, .wrap = true };
    luil::text_layout_request syllables { words };
    syllables.line_break = luil::text::line_break_policy::unicode;
    // 정책만 다른 두 요청은 cache에서 따로 miss다. 같은 요청은 맞는다.
    const auto first { luil::make_font_text_layout(words, font) };
    const auto second { luil::make_font_text_layout(syllables, font) };
    REQUIRE(luil::text_layout_cache_usage().misses == 2);
    REQUIRE(luil::text_layout_cache_usage().entries == 2);
    REQUIRE(luil::make_font_text_layout(words, font).caret_stops().data() == first.caret_stops().data());
    REQUIRE(luil::text_layout_cache_usage().hits == 1);
    // 접지 않는 요청은 정책이 달라도 한 항목이다.
    luil::text_layout_request plain { .text = text };
    luil::text_layout_request plain_unicode { plain };
    plain_unicode.line_break = luil::text::line_break_policy::unicode;
    REQUIRE(luil::make_font_text_layout(plain, font).caret_stops().data() == luil::make_font_text_layout(plain_unicode, font).caret_stops().data());

    // 실제 글꼴로 접은 자리는 후보이거나, 후보가 없는 줄의 비상 분할(글자소 경계)이다.
    const auto boundaries { luil::text::grapheme_boundaries(text) };
    for (const auto& [layout, policy] : { std::pair { first, words.line_break }, std::pair { second, syllables.line_break } })
    {
        const auto opportunities { luil::text::line_break_opportunities(text, policy) };
        const auto is_opportunity = [&opportunities](const std::size_t at) {
            return std::any_of(opportunities.begin(), opportunities.end(), [at](const auto& item) { return item.offset == at; });
        };
        REQUIRE(layout.lines().size() > 2);
        for (const auto& row : layout.lines())
        {
            REQUIRE(std::binary_search(boundaries.begin(), boundaries.end(), row.end));
            if (row.wrapped == false || is_opportunity(row.end))
                continue;
            // 비상 분할이면 그 줄 안(처음 뒤)에 후보가 없다.
            REQUIRE(std::none_of(opportunities.begin(), opportunities.end(), [&row](const auto& item) { return item.offset > row.begin && item.offset < row.end; }));
        }
        // 줄 경계 offset의 caret이 제 줄로 돌아온다.
        for (std::size_t index { 0 }; index < layout.lines().size(); ++index)
        {
            const auto& row { layout.lines()[index] };
            REQUIRE(layout.line_at(row.begin) == index);
            if (row.wrapped)
                REQUIRE(layout.line_at(row.end, true) == index);
        }
    }
}

TEST_CASE("A code span lays out in the code face and keeps its own wrap cache entry", "[ui][text-layout][native][rich-text]")
{
    if (luil::ui_text_shaping_enabled() == false)
        SKIP("The configured application retains the legacy text input path");

    // 코드 구간의 배치 폭이 `styles { code }` 요청의 폭과 같고, 보통 face의 폭과 다르다.
    const std::u8string text { u8"office text" };
    luil::text_input_view view { .text = text };
    luil::text_area_config config {};
    config.wrap = false;
    config.font_size = 20.0f;
    luil::text_span code_span { 0, 6 };
    code_span.code = true;
    config.spans = std::make_shared<const std::vector<luil::text_span>>(std::vector<luil::text_span> { code_span });
    luil::text_area_element document { { luil::application_element_kind(2) }, view, config };
    document.arrange({ { 0.0f, 0.0f, 500.0f, 100.0f } });
    const auto code { luil::make_text_layout({ .text = text, .font_size = 20.0f, .styles = { { 0, 6, false, false, true } } }) };
    const auto plain { luil::make_text_layout({ .text = text, .font_size = 20.0f }) };
    const auto start { document.text_span_bounds({ text, 0, 0, 0 }, luil::default_text_measurer()) };
    const auto end { document.text_span_bounds({ text, 0, text.size(), text.size() }, luil::default_text_measurer()) };
    REQUIRE(start.has_value());
    REQUIRE(end.has_value());
    REQUIRE(end->x - start->x == Catch::Approx(code.size().width));
    // 시험 실행의 글꼴 설정이 코드 글꼴을 따로 두지 않으면 같은 face라 폭도 같다. 다른 face일 때만 폭이 갈린다.
    const auto ui_face { luil::configured_ui_typeface() };
    const auto code_face { luil::configured_code_typeface() };
    if (ui_face != nullptr && code_face != nullptr && ui_face->uniqueID() != code_face->uniqueID())
        REQUIRE(code.size().width != Catch::Approx(plain.size().width));

    // 같은 글의 문단이 코드 구간만 다르면 wrap cache가 따로 쥔다. 코드 face는 advance를 바꾼다.
    const std::u8string paragraphs { u8"abc\nabc\nabc" };
    luil::text_area_config repeated {};
    repeated.wrap = false;
    repeated.font_size = 20.0f;
    repeated.wrap_cache = std::make_shared<luil::text_area_wrap_cache>();
    luil::text_span middle { 4, 7 };
    middle.code = true;
    repeated.spans = std::make_shared<const std::vector<luil::text_span>>(std::vector<luil::text_span> { middle });
    luil::text_area_element three { { luil::application_element_kind(3) }, luil::text_input_view { .text = paragraphs }, repeated };
    three.arrange({ { 0.0f, 0.0f, 500.0f, 100.0f } });
    REQUIRE(repeated.wrap_cache->size() == 2u);
    // 밑줄은 폭을 바꾸지 않아 배치에 싣지 않는다. 밑줄만 다른 문단은 한 항목이다.
    luil::text_area_config underlined { repeated };
    underlined.wrap_cache = std::make_shared<luil::text_area_wrap_cache>();
    luil::text_span line { 4, 7 };
    line.underline = true;
    underlined.spans = std::make_shared<const std::vector<luil::text_span>>(std::vector<luil::text_span> { line });
    luil::text_area_element lined { { luil::application_element_kind(3) }, luil::text_input_view { .text = paragraphs }, underlined };
    lined.arrange({ { 0.0f, 0.0f, 500.0f, 100.0f } });
    REQUIRE(underlined.wrap_cache->size() == 1u);
}

TEST_CASE("An underline span draws a line under its glyphs in the span color", "[ui][text-layout][native][rich-text][raster]")
{
    if (luil::ui_text_shaping_enabled() == false)
        SKIP("The configured application retains the legacy text input path");

    // 강조색 구간이라 글자와 밑줄이 강조색이다. 밑줄이 있으면 강조색 화소가 그만큼 는다.
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const auto accent_pixels = [&](const bool underline) {
        luil::text_span span { 0, 6, luil::text_color_role::accent };
        span.underline = underline;
        luil::text_area_config config {};
        config.spans = std::make_shared<const std::vector<luil::text_span>>(std::vector<luil::text_span> { span });
        const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::text_area_element>(luil::ui_element_id { luil::application_element_kind(4) }, luil::text_input_view { .text = u8"office" }, std::move(config)), { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f) };
        luil::testing::raster_frame frame { 400, 200, palette };
        frame.draw(tree, luil::interaction_snapshot {});
        return frame.count_color({ 0.0f, 0.0f, 400.0f, 200.0f }, palette.accent);
    };
    const int plain { accent_pixels(false) };
    const int lined { accent_pixels(true) };
    // 글 폭이 수십 물리 픽셀이고 밑줄은 2 물리 픽셀 두께라 적어도 그 폭만큼 는다.
    REQUIRE(lined > plain + 40);
}
