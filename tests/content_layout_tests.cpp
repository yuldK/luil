#include "luil/ui/badge_element.h"
#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/glyph_element.h"
#include "luil/ui/grid_element.h"
#include "luil/ui/label_element.h"
#include "luil/ui/layer_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    // 글자 하나가 글꼴 크기의 절반 폭인 측정기다 (ASCII만 쓴다). 12px 글 네 자가 24px이다.
    const luil::text_measurer half_width { [](const std::u8string_view text, const float pixel_size) { return static_cast<float>(text.size()) * pixel_size * 0.5f; } };

    constexpr float infinite { std::numeric_limits<float>::infinity() };

    [[nodiscard]] luil::measure_context measuring(const float scale = 1.0f, const luil::ui_metrics* const metrics = nullptr)
    {
        return { scale, &half_width, metrics };
    }

    [[nodiscard]] luil::ui_element_id named(const char8_t* const owner)
    {
        return { luil::application_element_kind(60), owner };
    }

    [[nodiscard]] std::unique_ptr<luil::label_element> label(const char8_t* const owner, std::u8string text, const float padding = 0.0f)
    {
        return std::make_unique<luil::label_element>(named(owner), luil::label_config { .text = std::move(text), .font_size = 12.0f, .padding = padding });
    }

    // 누를 수 있는 글자 버튼이다 (액션이 있어야 hit의 답이 된다).
    [[nodiscard]] std::unique_ptr<luil::text_button_element> button(const char8_t* const owner, std::u8string text)
    {
        auto made { std::make_unique<luil::text_button_element>(named(owner), luil::text_button_config { .text = std::move(text) }) };
        made->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; });
        return made;
    }

    // 고정 폭 측정기로 배치한 tree다.
    [[nodiscard]] luil::ui_tree arranged(std::unique_ptr<luil::ui_element> root, const luil::rect_f& slot, const float scale = 1.0f)
    {
        root->arrange({ .slot = slot, .scale = scale, .measurer = &half_width });
        return luil::ui_tree { std::move(root) };
    }

    [[nodiscard]] luil::rect_f bounds_of(const luil::ui_tree& tree, const char8_t* const owner)
    {
        const luil::ui_element* const element { tree.find(named(owner)) };
        REQUIRE(element != nullptr);
        return element->bounds();
    }
}

TEST_CASE("Text elements answer the size their content wants", "[ui][layout][measure]")
{
    const luil::label_element plain { named(u8"plain"), { .text = u8"abcd", .font_size = 12.0f } };
    REQUIRE(plain.preferred_size(measuring(), infinite) == luil::size_f { 24.0f, luil::label_element::height_for({ .font_size = 12.0f }) });
    const luil::label_element padded { named(u8"padded"), { .text = u8"abcd", .font_size = 12.0f, .padding = 6.0f } };
    REQUIRE(padded.preferred_size(measuring(), infinite)->width == 36.0f);
    // 배율이 달라도 논리 크기는 같다 (물리 크기로 재서 되돌린다).
    REQUIRE(plain.preferred_size(measuring(2.0f), infinite)->width == 24.0f);

    // 버튼은 양옆 여백을 더하고 짧은 글에서도 최소 폭이다.
    const luil::text_button_element short_button { named(u8"short"), { .text = u8"OK" } };
    REQUIRE(short_button.preferred_size(measuring(), infinite) == luil::size_f { luil::text_button_minimum_width, luil::text_button_height });
    const luil::text_button_element long_button { named(u8"long"), { .text = u8"abcdefghijkl" } };
    REQUIRE(long_button.preferred_size(measuring(), infinite)->width == 72.0f + 2.0f * luil::text_button_padding);
    const luil::text_button_element icon_button { named(u8"icon"), { .text = u8"abcdefghijkl", .glyph = U'x' } };
    REQUIRE(icon_button.preferred_size(measuring(), infinite)->width == 72.0f + luil::text_button_element::icon_width_for({ .glyph = U'x' }) + 2.0f * luil::text_button_padding);
    // link는 상자가 없어 여백도 최소 폭도 없다.
    const luil::text_button_element link { named(u8"link"), { .text = u8"OK", .visual = luil::text_button_visual::link } };
    REQUIRE(link.preferred_size(measuring(), infinite)->width == 12.0f);
    // 버튼 글은 스타일의 본문 글자로 잰다 — 앱이 글자를 키우면 버튼도 넓어진다.
    luil::ui_metrics large {};
    large.body_font_size = 20.0f;
    REQUIRE(long_button.preferred_size(measuring(1.0f, &large), infinite)->width == 120.0f + 2.0f * luil::text_button_padding);

    // 체크는 표시 부분과 라벨을 그리는 그 자리 그대로다.
    const luil::check_element check { { .owner = u8"agree", .label = u8"abcd" } };
    const std::optional<luil::size_f> checked { check.preferred_size(measuring(), infinite) };
    REQUIRE(checked.has_value());
    REQUIRE(checked->width == 4.0f + luil::check_box_size + 8.0f + 24.0f + 4.0f);
    REQUIRE(checked->height == luil::check_row_height);

    // 배지는 그리기와 같은 알약 식이다 — 양옆 6, 심각도는 글리프와 간격 4가 더해지고, 원보다 좁지 않다.
    const luil::badge_element badge { named(u8"badge"), { .text = u8"new" } };
    REQUIRE(badge.preferred_size(measuring(), infinite) == luil::size_f { 12.0f + 17.0f, luil::badge_element::height_for({}) });
    const luil::badge_element warning { named(u8"warning"), { .text = u8"new", .tone = luil::badge_tone::warning } };
    REQUIRE(warning.preferred_size(measuring(), infinite)->width == 12.0f + 11.0f + 4.0f + 17.0f);
    const luil::badge_element dot { named(u8"dot"), {} };
    REQUIRE(dot.preferred_size(measuring(), infinite)->width == luil::badge_element::height_for({}));

    const luil::glyph_element glyph { named(u8"glyph"), { .glyph = U'x', .font_size = 16.0f } };
    REQUIRE(glyph.preferred_size(measuring(), infinite) == luil::size_f { 16.0f, 16.0f });
}

TEST_CASE("A stack fits children to the size they want", "[ui][layout][measure]")
{
    auto row { std::make_unique<luil::stack_element>(named(u8"row"), luil::stack_config { .direction = luil::stack_direction::row, .spacing = 8.0f, .cross_alignment = luil::stack_alignment::center }) };
    row->add_fit(button(u8"save", u8"abcdefghijkl"));
    row->add_flexible(label(u8"rest", u8"x"));
    row->add_fit(button(u8"cancel", u8"OK"));
    const luil::ui_tree tree { arranged(std::move(row), { 0.0f, 0.0f, 400.0f, 40.0f }, 2.0f) };

    const luil::rect_f save { bounds_of(tree, u8"save") };
    const luil::rect_f rest { bounds_of(tree, u8"rest") };
    const luil::rect_f cancel { bounds_of(tree, u8"cancel") };
    REQUIRE(save.width == Catch::Approx(96.0f * 2.0f));
    // 맞춤 항목은 교차축 정렬이 stretch가 아니면 원하는 높이에 선다.
    REQUIRE(save.height == Catch::Approx(luil::text_button_height * 2.0f));
    REQUIRE(save.y == Catch::Approx((40.0f - luil::text_button_height * 2.0f) / 2.0f));
    REQUIRE(cancel.width == Catch::Approx(luil::text_button_minimum_width * 2.0f));
    REQUIRE(cancel.x + cancel.width == Catch::Approx(400.0f));
    REQUIRE(rest.width == Catch::Approx(400.0f - save.width - cancel.width - 2.0f * 8.0f * 2.0f));
}

TEST_CASE("A column stack fits children at its own width", "[ui][layout][measure]")
{
    auto column { std::make_unique<luil::stack_element>(named(u8"column"), luil::stack_config {}) };
    column->add_fit(label(u8"title", u8"abcd"));
    column->add_fit(std::make_unique<luil::check_element>(luil::check_config { .owner = u8"agree", .label = u8"ab" }));
    const luil::ui_tree tree { arranged(std::move(column), { 0.0f, 0.0f, 200.0f, 200.0f }) };
    REQUIRE(bounds_of(tree, u8"title").height == luil::label_element::height_for({ .font_size = 12.0f }));
    // stretch는 교차축을 채운다.
    REQUIRE(bounds_of(tree, u8"title").width == 200.0f);
}

TEST_CASE("Maximum caps a flexible item and the others take what it leaves", "[ui][layout][stack]")
{
    SECTION("One capped item gives the rest to the other")
    {
        auto row { std::make_unique<luil::stack_element>(named(u8"row"), luil::stack_config { .direction = luil::stack_direction::row }) };
        row->add(label(u8"a", u8""), luil::stack_item { .weight = 1.0f, .maximum = 50.0f });
        row->add(label(u8"b", u8""), luil::stack_item { .weight = 1.0f });
        const luil::ui_tree tree { arranged(std::move(row), { 0.0f, 0.0f, 300.0f, 20.0f }) };
        REQUIRE(bounds_of(tree, u8"a").width == 50.0f);
        REQUIRE(bounds_of(tree, u8"b").width == 250.0f);
    }

    SECTION("When every item is capped the free space follows the main alignment")
    {
        auto row { std::make_unique<luil::stack_element>(named(u8"row"), luil::stack_config { .direction = luil::stack_direction::row, .main_alignment = luil::stack_main_alignment::center }) };
        row->add(label(u8"a", u8""), luil::stack_item { .weight = 1.0f, .maximum = 50.0f });
        row->add(label(u8"b", u8""), luil::stack_item { .weight = 1.0f, .maximum = 100.0f });
        const luil::ui_tree tree { arranged(std::move(row), { 0.0f, 0.0f, 300.0f, 20.0f }) };
        REQUIRE(bounds_of(tree, u8"a").x == 75.0f);
        REQUIRE(bounds_of(tree, u8"a").width == 50.0f);
        REQUIRE(bounds_of(tree, u8"b").width == 100.0f);
    }

    SECTION("A cap that frees space can push another item over its cap")
    {
        // 300을 셋이 나누면 100씩이다. a가 40에 걸리면 b·c가 130씩이 되어 b가 다시 120에 걸리고, c가 140이다.
        auto row { std::make_unique<luil::stack_element>(named(u8"row"), luil::stack_config { .direction = luil::stack_direction::row }) };
        row->add(label(u8"a", u8""), luil::stack_item { .weight = 1.0f, .maximum = 40.0f });
        row->add(label(u8"b", u8""), luil::stack_item { .weight = 1.0f, .maximum = 120.0f });
        row->add(label(u8"c", u8""), luil::stack_item { .weight = 1.0f });
        const luil::ui_tree tree { arranged(std::move(row), { 0.0f, 0.0f, 300.0f, 20.0f }) };
        REQUIRE(bounds_of(tree, u8"a").width == 40.0f);
        REQUIRE(bounds_of(tree, u8"b").width == 120.0f);
        REQUIRE(bounds_of(tree, u8"c").width == Catch::Approx(140.0f));
    }

    SECTION("Minimum wins over maximum")
    {
        auto row { std::make_unique<luil::stack_element>(named(u8"row"), luil::stack_config { .direction = luil::stack_direction::row }) };
        row->add(label(u8"a", u8""), luil::stack_item { .weight = 1.0f, .minimum = 80.0f, .maximum = 50.0f });
        row->add(label(u8"b", u8""), luil::stack_item { .weight = 1.0f });
        const luil::ui_tree tree { arranged(std::move(row), { 0.0f, 0.0f, 300.0f, 20.0f }) };
        REQUIRE(bounds_of(tree, u8"a").width == 80.0f);
    }
}

TEST_CASE("A stack without fit or maximum items arranges as before", "[ui][layout][stack]")
{
    // 고정 100, 유연 1:2 — 남는 200을 66.67:133.33으로 나눈다 (지금까지의 식).
    auto row { std::make_unique<luil::stack_element>(named(u8"row"), luil::stack_config { .direction = luil::stack_direction::row }) };
    row->add(label(u8"a", u8"abcdefgh"), 100.0f);
    row->add_flexible(label(u8"b", u8"abcdefgh"), 1.0f);
    row->add_flexible(label(u8"c", u8"abcdefgh"), 2.0f);
    const luil::ui_tree tree { arranged(std::move(row), { 0.0f, 0.0f, 300.0f, 20.0f }) };
    REQUIRE(bounds_of(tree, u8"a").width == 100.0f);
    REQUIRE(bounds_of(tree, u8"b").width == Catch::Approx(200.0f / 3.0f));
    REQUIRE(bounds_of(tree, u8"c").width == Catch::Approx(400.0f / 3.0f));
}

TEST_CASE("A form grid sizes the label column to the longest label", "[ui][layout][grid]")
{
    luil::grid_config config { .columns = { luil::grid_track::fit(), luil::grid_track::flexible() }, .column_spacing = 8.0f, .row_spacing = 4.0f };
    auto grid { std::make_unique<luil::grid_element>(named(u8"form"), config) };
    grid->add(label(u8"name", u8"Name"), { .row = 0, .column = 0 });
    grid->add(label(u8"address", u8"Address"), { .row = 1, .column = 0 });
    grid->add(label(u8"zip", u8"Zip"), { .row = 2, .column = 0 });
    grid->add(button(u8"pick", u8"OK"), { .row = 2, .column = 1, .horizontal = luil::layout_alignment::end });
    // 두 열에 걸친 자식은 맞춤 열을 넓히지 않는다.
    grid->add(label(u8"note", u8"a very long note that spans"), { .row = 3, .column = 0, .column_span = 2 });
    auto* const form { grid.get() };
    const luil::ui_tree tree { arranged(std::move(grid), { 0.0f, 0.0f, 300.0f, 200.0f }) };

    REQUIRE(form->column_widths().size() == 2u);
    REQUIRE(form->column_widths()[0] == 42.0f);
    REQUIRE(form->column_widths()[1] == 300.0f - 42.0f - 8.0f);
    // 모자란 행은 맞춤 행이다. 버튼이 놓인 행은 버튼 높이다.
    REQUIRE(form->row_heights().size() == 4u);
    REQUIRE(form->row_heights()[0] == luil::label_element::height_for({ .font_size = 12.0f }));
    REQUIRE(form->row_heights()[2] == luil::text_button_height);

    // 칸 안 정렬은 원하는 크기에 선다.
    const luil::rect_f pick { bounds_of(tree, u8"pick") };
    REQUIRE(pick.width == luil::text_button_minimum_width);
    REQUIRE(pick.x + pick.width == 300.0f);
    REQUIRE(bounds_of(tree, u8"zip").y == form->row_heights()[0] + form->row_heights()[1] + 2.0f * 4.0f);
    REQUIRE(bounds_of(tree, u8"note").width == 300.0f);

    // 원하는 크기는 유연 열도 잰 폭이다.
    const std::optional<luil::size_f> wanted { tree.root()->preferred_size(measuring(), infinite) };
    REQUIRE(wanted.has_value());
    REQUIRE(wanted->width == 42.0f + 8.0f + luil::text_button_minimum_width);
}

TEST_CASE("A layer puts children in one place and the last one on top", "[ui][layout][layer]")
{
    auto layer { std::make_unique<luil::layer_element>(named(u8"layer"), luil::edge_insets::all(10.0f)) };
    layer->add(button(u8"base", u8"base"));
    layer->add(button(u8"badge", u8"!"), { .horizontal = luil::layout_alignment::end, .vertical = luil::layout_alignment::start, .width = 16.0f, .height = 16.0f, .margin = luil::edge_insets::all(2.0f) });
    layer->add(label(u8"hint", u8"abcd"), { .horizontal = luil::layout_alignment::center, .vertical = luil::layout_alignment::center });
    const luil::ui_tree tree { arranged(std::move(layer), { 0.0f, 0.0f, 200.0f, 100.0f }) };

    REQUIRE(bounds_of(tree, u8"base").x == 10.0f);
    REQUIRE(bounds_of(tree, u8"base").width == 180.0f);
    const luil::rect_f badge { bounds_of(tree, u8"badge") };
    REQUIRE(badge.x == 200.0f - 10.0f - 2.0f - 16.0f);
    REQUIRE(badge.y == 12.0f);
    const luil::rect_f hint { bounds_of(tree, u8"hint") };
    REQUIRE(hint.width == 24.0f);
    REQUIRE(hint.x == 100.0f - 12.0f);

    // 뒤에 담은 것이 위다.
    REQUIRE(tree.hit_test(badge.x + 4.0f, badge.y + 4.0f)->id() == named(u8"badge"));
    REQUIRE(tree.hit_test(30.0f, 50.0f)->id() == named(u8"base"));
}

TEST_CASE("Containers that only fix lengths do not answer a size", "[ui][layout][measure]")
{
    luil::stack_element fixed { named(u8"fixed"), {} };
    fixed.add_gap(10.0f);
    REQUIRE(fixed.preferred_size(measuring(), infinite).has_value() == false);
    // 맞춤 항목을 담은 stack은 그 크기를 쌓아 답한다 (stack 안의 stack).
    luil::stack_element column { named(u8"column"), { .spacing = 4.0f, .padding = luil::edge_insets::all(2.0f) } };
    column.add_fit(label(u8"one", u8"ab"));
    column.add_fit(label(u8"two", u8"abcdef"));
    const std::optional<luil::size_f> wanted { column.preferred_size(measuring(), infinite) };
    REQUIRE(wanted.has_value());
    REQUIRE(wanted->width == 36.0f + 4.0f);
    REQUIRE(wanted->height == 2.0f * luil::label_element::height_for({ .font_size = 12.0f }) + 4.0f + 4.0f);
}

TEST_CASE("A grid flexible column at its minimum takes room from the other flexible columns", "[ui][layout][grid]")
{
    // 400을 1:3으로 나누면 100·300이다. 첫 열이 하한 200에 서면 둘째 열은 나머지 200을 받는다 — 넘치지 않는다.
    luil::grid_config config { .columns = { luil::grid_track::flexible(1.0f, 200.0f), luil::grid_track::flexible(3.0f) } };
    auto grid { std::make_unique<luil::grid_element>(named(u8"columns"), config) };
    grid->add(label(u8"left", u8""), { .row = 0, .column = 0 });
    grid->add(label(u8"right", u8""), { .row = 0, .column = 1 });
    auto* const columns { grid.get() };
    const luil::ui_tree tree { arranged(std::move(grid), { 0.0f, 0.0f, 400.0f, 40.0f }) };
    REQUIRE(columns->column_widths() == std::vector<float> { 200.0f, 200.0f });
    REQUIRE(bounds_of(tree, u8"right").x + bounds_of(tree, u8"right").width == 400.0f);

    // 하한만으로 모자라면 그때만 넘친다.
    luil::grid_config tight { .columns = { luil::grid_track::flexible(1.0f, 200.0f), luil::grid_track::flexible(1.0f, 300.0f) } };
    auto narrow { std::make_unique<luil::grid_element>(named(u8"tight"), tight) };
    auto* const tight_columns { narrow.get() };
    const luil::ui_tree narrow_tree { arranged(std::move(narrow), { 0.0f, 0.0f, 400.0f, 40.0f }) };
    REQUIRE(tight_columns->column_widths() == std::vector<float> { 200.0f, 300.0f });
}
