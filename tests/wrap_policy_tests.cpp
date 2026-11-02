#include "luil/ui/info_bar_element.h"
#include "luil/ui/label_element.h"
#include "luil/ui/markdown_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/text_area_element.h"
#include "luil/ui/text_block.h"
#include "luil/ui/text_layout.h"

#include "luil/text/utf8_text.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// 줄 나눔 정책 스타일 키다 (docs/wrap-policy-key-design.md의 확인). config 칸이 비면 문맥이 든 앱 스타일의
// `text.wrap_policy`를 따르고, 적은 칸은 스타일보다 앞선다.
namespace {
    using luil::text::line_break_policy;

    // 코드포인트마다 글자 크기의 절반 폭이다 (12px 글이면 6px).
    [[nodiscard]] float half_size_width(const std::u8string_view text, const float pixel_size)
    {
        std::size_t count { 0 };
        for (std::size_t offset { 0 }; offset < text.size(); offset += luil::text::utf8_decode(text, offset).length)
            ++count;
        return static_cast<float>(count) * pixel_size / 2.0f;
    }

    const luil::text_measurer half_size_measurer { half_size_width };

    // 네 음절 어절이 이어진다. 45px(12px 글 일곱 글자 반)이면 어절 정책은 어절마다 한 줄이고, 음절 정책은 일곱 글자씩 접는다.
    constexpr std::u8string_view words { u8"가나다라 마바사아 자차카타 파하가나" };
    constexpr float narrow { 45.0f };

    // 내장 스타일에서 정책만 바꾼 스타일이다.
    [[nodiscard]] luil::ui_style style_with(const line_break_policy policy)
    {
        luil::ui_style style { luil::default_ui_style() };
        style.text.wrap_policy = policy;
        return style;
    }

    const luil::ui_style unicode_style { style_with(line_break_policy::unicode) };
    const luil::ui_style words_style { style_with(line_break_policy::hangul_words) };

    [[nodiscard]] luil::measure_context measure_with(const luil::ui_style* const style)
    {
        return luil::measure_context { .scale = 1.0f, .measurer = &half_size_measurer, .style = style };
    }

    [[nodiscard]] luil::label_config wrapping(std::optional<line_break_policy> policy = std::nullopt)
    {
        return luil::label_config { .text = std::u8string { words }, .font_size = 12.0f, .wrap = true, .line_break = policy };
    }

    [[nodiscard]] std::size_t label_lines(const luil::label_config& config, const luil::measure_context& context)
    {
        return luil::label_element::measure_wrap(config, context, narrow).lines.size();
    }
}

TEST_CASE("An empty line break field follows the style the measure context carries", "[ui][wrap-policy]")
{
    // 어절 정책은 어절마다 한 줄이라 넷, 음절 정책은 일곱 글자씩 접어 셋이다.
    REQUIRE(label_lines(wrapping(line_break_policy::hangul_words), measure_with(nullptr)) == 4u);
    REQUIRE(label_lines(wrapping(line_break_policy::unicode), measure_with(nullptr)) == 3u);

    // 칸이 비면 문맥의 스타일이다.
    REQUIRE(label_lines(wrapping(), measure_with(&unicode_style)) == 3u);
    REQUIRE(label_lines(wrapping(), measure_with(&words_style)) == 4u);

    // 적은 칸은 스타일보다 앞선다.
    REQUIRE(label_lines(wrapping(line_break_policy::hangul_words), measure_with(&unicode_style)) == 4u);
    REQUIRE(label_lines(wrapping(line_break_policy::unicode), measure_with(&words_style)) == 3u);

    // 스타일이 빈 측정 문맥은 내장 스타일(LUIL_STYLE)의 정책이다.
    const luil::measure_context empty { measure_with(nullptr) };
    REQUIRE(&empty.app_style() == &luil::default_ui_style());
    REQUIRE(empty.line_break_for(std::nullopt) == luil::default_ui_style().text.wrap_policy);
    REQUIRE(label_lines(wrapping(), empty) == label_lines(wrapping(luil::default_ui_style().text.wrap_policy), empty));

    // 푸는 식 하나가 세 순서를 모두 가린다.
    REQUIRE(measure_with(&unicode_style).line_break_for(std::nullopt) == line_break_policy::unicode);
    REQUIRE(measure_with(&unicode_style).line_break_for(line_break_policy::hangul_words) == line_break_policy::hangul_words);
    REQUIRE(&measure_with(&unicode_style).app_style() == &unicode_style);
}

TEST_CASE("The arrange context hands its style to measuring down the tree", "[ui][wrap-policy]")
{
    const luil::arrange_context context { .slot = { 0.0f, 0.0f, narrow, 400.0f }, .scale = 1.0f, .measurer = &half_size_measurer, .style = &unicode_style };
    const luil::measure_context measured { context.measure() };
    REQUIRE(measured.style == &unicode_style);
    REQUIRE(measured.measurer == &half_size_measurer);
    REQUIRE(measured.scale == 1.0f);

    // 두 계층 아래의 맞춤 label이 배치 문맥의 스타일로 잰 높이에 선다 (`for_child`가 스타일을 잇는다).
    const auto arranged_height = [](const luil::ui_style* const style) {
        auto label { std::make_unique<luil::label_element>(luil::ui_element_id { luil::application_element_kind(0), u8"wrap" }, wrapping()) };
        const luil::label_element* const placed { label.get() };
        auto inner { std::make_unique<luil::stack_element>(luil::ui_element_id { luil::application_element_kind(1), u8"inner" }, luil::stack_config {}) };
        inner->add_fit(std::move(label));
        luil::stack_element root { luil::ui_element_id { luil::application_element_kind(1), u8"root" }, luil::stack_config {} };
        root.add(std::move(inner), 400.0f);
        root.arrange({ .slot = { 0.0f, 0.0f, narrow, 400.0f }, .scale = 1.0f, .measurer = &half_size_measurer, .style = style });
        return placed->bounds().height;
    };
    const float line { luil::label_element::height_for(wrapping()) };
    REQUIRE(arranged_height(&unicode_style) == 3.0f * line);
    REQUIRE(arranged_height(&words_style) == 4.0f * line);
}

TEST_CASE("Markdown blocks wrap with the style when their field is empty", "[ui][wrap-policy][markdown]")
{
    const auto height = [](const std::optional<line_break_policy> policy, const luil::ui_style* const style) {
        luil::markdown_config config { .text = std::u8string { words }, .line_height = 18.0f, .block_spacing = 0.0f };
        config.line_break = policy;
        const luil::markdown_element markdown { { luil::ui_element_kind::root, u8"body" }, std::move(config) };
        return markdown.preferred_size(measure_with(style), narrow)->height;
    };
    const float words_height { height(line_break_policy::hangul_words, nullptr) };
    const float unicode_height { height(line_break_policy::unicode, nullptr) };
    REQUIRE(words_height == 4.0f * 18.0f);
    REQUIRE(unicode_height == 3.0f * 18.0f);
    REQUIRE(height(std::nullopt, &unicode_style) == unicode_height);
    REQUIRE(height(std::nullopt, &words_style) == words_height);
    REQUIRE(height(line_break_policy::hangul_words, &unicode_style) == words_height);
}

TEST_CASE("A text area resolves its policy in arrange and refills its cache when the style flips", "[ui][wrap-policy][text-area]")
{
    // 칸 200에서 좌우 여백(7)을 뺀 186이 접는 폭이다. 12px 글 서른한 글자다.
    const std::u8string text { u8"가나다라마바사아자차카타파하가나다라마바 사아자차카타파하가나다라마바사아\n끝" };
    std::size_t measured { 0 };
    const luil::text_measurer counting { [&measured](const std::u8string_view value, const float size) {
        ++measured;
        return half_size_width(value, size);
    }
    };
    const auto arrange = [&](const std::optional<line_break_policy> policy, const luil::ui_style* const style, std::shared_ptr<luil::text_area_wrap_cache> cache) {
        luil::text_area_config config {};
        config.font_size = 12.0f;
        config.wrap_cache = std::move(cache);
        config.line_break = policy;
        luil::text_input_view view {};
        view.text = text;
        luil::text_area_element memo { luil::ui_element_id { luil::application_element_kind(0), u8"memo" }, std::move(view), std::move(config) };
        memo.arrange({ .slot = { 0.0f, 0.0f, 200.0f, 100.0f }, .scale = 1.0f, .measurer = &counting, .style = style });
        return memo.lines();
    };

    // 측정기 길이 접은 줄은 같은 정책을 실은 text_layout의 줄과 같다.
    for (const luil::ui_style* const style : { &unicode_style, &words_style })
    {
        const auto lines { arrange(std::nullopt, style, nullptr) };
        const auto layout { luil::make_text_layout(
            { .text = text, .font_size = 12.0f, .width = 186.0f, .wrap = true, .line_height = 18.0f, .line_break = style->text.wrap_policy }, half_size_measurer)
        };
        std::vector<luil::text_area_element::line> expected {};
        for (const auto& row : layout.lines())
            expected.push_back({ row.begin, row.end, row.wrapped });
        REQUIRE(lines == expected);
    }
    const auto syllables { arrange(std::nullopt, &unicode_style, nullptr) };
    const auto words_lines { arrange(std::nullopt, &words_style, nullptr) };
    REQUIRE(syllables != words_lines);
    REQUIRE(arrange(line_break_policy::hangul_words, &unicode_style, nullptr) == words_lines);

    // config는 그대로 두고 스타일만 바꿔 다시 배치하면 cache가 통째로 비워지고, 다시 찬 자리는 cache 없이 접은 것과 같다.
    const auto cache { std::make_shared<luil::text_area_wrap_cache>() };
    REQUIRE(arrange(std::nullopt, &words_style, cache) == words_lines);
    measured = 0;
    REQUIRE(arrange(std::nullopt, &words_style, cache) == words_lines);
    // 같은 정책이면 표본 글 하나만 잰다.
    REQUIRE(measured == 1u);
    measured = 0;
    REQUIRE(arrange(std::nullopt, &unicode_style, cache) == syllables);
    REQUIRE(measured > 1u);
    // 스타일이 같은 정책을 다시 고르면 cache가 다시 맞는다. 적은 칸이 같은 정책이면 스타일과 상관없이 그대로다.
    measured = 0;
    REQUIRE(arrange(line_break_policy::unicode, &words_style, cache) == syllables);
    REQUIRE(measured == 1u);
}

TEST_CASE("A text block measures with the measure context and draws with the draw context", "[ui][wrap-policy][text-block]")
{
    luil::text_block_style style {};
    style.wrap = true;
    REQUIRE(style.line_break.has_value() == false);
    REQUIRE(luil::measure_text_block(measure_with(&unicode_style), words, style, narrow).lines.size() == 3u);
    REQUIRE(luil::measure_text_block(measure_with(&words_style), words, style, narrow).lines.size() == 4u);
    style.line_break = line_break_policy::hangul_words;
    REQUIRE(luil::measure_text_block(measure_with(&unicode_style), words, style, narrow).lines.size() == 4u);

    // 채우지 않은 그리기 문맥은 struct 기본값이다. 글꼴이 없는 core라 그린 줄 수는 native test가 견준다.
    SkBitmap bitmap {};
    bitmap.allocN32Pixels(16, 16);
    SkCanvas canvas { bitmap };
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const luil::draw_context unfilled { .canvas = canvas, .palette = palette };
    REQUIRE(unfilled.wrap_policy == line_break_policy::hangul_words);

    // 팔레트·배율만 바꾼 문맥이 정책을 옮긴다.
    luil::draw_context filled { .canvas = canvas, .palette = palette, .wrap_policy = line_break_policy::unicode };
    const luil::ui_color_palette light { luil::color_palette_for(luil::color_theme::light) };
    const luil::draw_context derived { luil::derive_draw_context(filled, light, 2.0f) };
    REQUIRE(derived.wrap_policy == line_break_policy::unicode);
    REQUIRE(&derived.palette == &light);
    REQUIRE(derived.scale == 2.0f);
}

TEST_CASE("An info bar message follows the style without a field of its own", "[ui][wrap-policy][info-bar]")
{
    const luil::info_bar_config config { .owner = u8"sync", .message = u8"가나다라 마바사아 자차카타 파하가나 다라마바 사아자차 카타파하 가나다라" };
    // 본문 폭이 바뀌는 여러 폭에서 어절 스타일은 스타일 없는 문맥(내장 스타일)과 같고, 음절 스타일은 어딘가에서 더 적은 줄이다.
    bool differs { false };
    for (float width { 100.0f }; width <= 400.0f; width += 5.0f)
    {
        const float words_height { luil::info_bar_element::measure(config, measure_with(&words_style), width).height };
        const float unicode_height { luil::info_bar_element::measure(config, measure_with(&unicode_style), width).height };
        if (luil::default_ui_style().text.wrap_policy == line_break_policy::hangul_words)
            REQUIRE(luil::info_bar_element::measure(config, measure_with(nullptr), width).height == words_height);
        REQUIRE(unicode_height <= words_height);
        differs = differs || unicode_height < words_height;
    }
    REQUIRE(differs);
}
