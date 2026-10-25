#include "luil/ui/virtual_list_element.h"

#include "luil/ui/app_message.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {
    struct moved_intent
    {
        std::u8string key {};
    };

    [[nodiscard]] std::u8string key_of(const std::size_t index)
    {
        const std::string text { std::to_string(index) };
        return std::u8string { text.begin(), text.end() };
    }

    // 항목 `count`개, 칸 폭 100 이상, 줄 높이 50의 격자다. 커서는 `cursor`번이다.
    [[nodiscard]] luil::virtual_list_config make_grid(const std::size_t count, const std::size_t cursor)
    {
        luil::virtual_list_config config {};
        config.owner = u8"tiles";
        config.row_height = 50.0f;
        config.minimum_column_width = 100.0f;
        config.overscan = 0;
        for (std::size_t index { 0 }; index < count; ++index)
            config.items.push_back(luil::virtual_list_item { .key = key_of(index), .label = key_of(index) });
        config.cursor = key_of(cursor);
        config.move = [](const std::u8string& key) { return luil::input_action { luil::app_message { moved_intent { key } } }; };
        config.scroll = [](const float) { return luil::input_action {}; };
        return config;
    }

    // 높이 100(2줄) 창에 배치한다. 넘치면 막대(16px)가 서므로 폭 316이면 내용 폭 300(3열)이다.
    [[nodiscard]] std::unique_ptr<luil::virtual_list_element> arrange_grid(luil::virtual_list_config config)
    {
        auto list { std::make_unique<luil::virtual_list_element>(std::move(config)) };
        list->arrange({ { 0.0f, 0.0f, 300.0f + luil::scroll_area_bar_width, 100.0f }, 1.0f });
        return list;
    }

    [[nodiscard]] std::u8string moved(const std::vector<luil::input_action>& actions)
    {
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const intent { message->get<moved_intent>() }; intent != nullptr)
                    return intent->key;
        return {};
    }

    [[nodiscard]] std::u8string step(const luil::virtual_list_element& list, const luil::value_step value)
    {
        const std::optional<std::vector<luil::input_action>> actions { list.key_step()->on_step(value) };
        return actions.has_value() ? moved(*actions) : std::u8string {};
    }

    [[nodiscard]] std::u8string sideways(const luil::virtual_list_element& list, const luil::key_code key)
    {
        const std::optional<std::vector<luil::input_action>> actions { (*list.key_handler_of())(luil::key_pressed_event { key }) };
        return actions.has_value() ? moved(*actions) : std::u8string {};
    }
} // namespace

TEST_CASE("A grid takes its column count from the width or the fixed count", "[ui][list][grid]")
{
    luil::virtual_list_config config { make_grid(10, 0) };
    REQUIRE(luil::virtual_list_grid_columns(config, 350.0f) == 3u);
    REQUIRE(luil::virtual_list_grid_columns(config, 50.0f) == 1u);
    config.minimum_column_width = 0.0f;
    config.columns = 4;
    REQUIRE(luil::virtual_list_grid_columns(config, 350.0f) == 4u);
    // 격자가 아니면 1이다.
    config.columns = 0;
    REQUIRE(luil::virtual_list_grid_columns(config, 350.0f) == 1u);
}

TEST_CASE("A grid lays items out row by row and realises only the rows in view", "[ui][list][grid]")
{
    const auto list { arrange_grid(make_grid(10, 0)) };
    REQUIRE(list->columns() == 3u);
    // 10개면 4줄이고 창(2줄)에 드는 6개만 짓는다.
    REQUIRE(list->content_height() == 200.0f);
    REQUIRE(list->realized().begin == 0u);
    REQUIRE(list->realized().end == 6u);
    // 4번은 둘째 줄 가운데 칸이다.
    const luil::ui_element* const lane { list->children()[0]->children()[0]->children()[0].get() };
    const luil::rect_f fifth { lane->children()[4]->bounds() };
    REQUIRE(fifth.x == 100.0f);
    REQUIRE(fifth.y == 50.0f);
    REQUIRE(fifth.width == 100.0f);
    REQUIRE(fifth.height == 50.0f);
}

TEST_CASE("Grid keys move a row with up and down and a cell with left and right", "[ui][list][grid]")
{
    const auto list { arrange_grid(make_grid(10, 4)) };
    REQUIRE(step(*list, luil::value_step::increase) == u8"7");
    REQUIRE(step(*list, luil::value_step::decrease) == u8"1");
    REQUIRE(sideways(*list, luil::key_code::arrow_right) == u8"5");
    REQUIRE(sideways(*list, luil::key_code::arrow_left) == u8"3");
    REQUIRE(step(*list, luil::value_step::minimum) == u8"0");
    REQUIRE(step(*list, luil::value_step::maximum) == u8"9");
    // 창은 두 줄이라 Page는 여섯 칸이다. 4번에서 여섯 칸 아래(10번)는 없고 아래 줄이 있어 마지막 항목이다.
    REQUIRE(step(*list, luil::value_step::increase_page) == u8"9");
    const auto bottom { arrange_grid(make_grid(10, 8)) };
    REQUIRE(step(*bottom, luil::value_step::decrease_page) == u8"2");
}

TEST_CASE("Grid keys stop at the edges and fall to the last item below a short row", "[ui][list][grid]")
{
    // 첫 줄의 ↑는 제자리다 (키는 삼킨다 — 옮길 것이 없어 메시지가 없다).
    const auto top { arrange_grid(make_grid(10, 1)) };
    REQUIRE(step(*top, luil::value_step::decrease).empty());
    // 7번(셋째 줄) 아래 칸(10번)은 없지만 넷째 줄이 있다 — 마지막 항목(9번)으로 간다.
    const auto seventh { arrange_grid(make_grid(10, 7)) };
    REQUIRE(step(*seventh, luil::value_step::increase) == u8"9");
    // 마지막 줄의 ↓는 제자리다.
    const auto last { arrange_grid(make_grid(10, 9)) };
    REQUIRE(step(*last, luil::value_step::increase).empty());
    REQUIRE(sideways(*last, luil::key_code::arrow_right).empty());
}

TEST_CASE("Changing the column count keeps the top item in the top row", "[ui][list][grid]")
{
    // 3열의 셋째 줄(100px) 첫 항목은 6번이다. 2열이면 6번은 넷째 줄(150px)이다.
    REQUIRE(luil::virtual_list_grid_offset_keeping(100.0f, 3, 50.0f, 2, 50.0f) == 150.0f);
    // 줄 안에서 흘러간 만큼은 줄 높이의 비율로 옮긴다.
    REQUIRE(luil::virtual_list_grid_offset_keeping(110.0f, 3, 50.0f, 2, 50.0f) == 160.0f);
    // 목록(1열·24px) ↔ 격자(3열·48px): 열째 줄(10번)이 격자에서는 넷째 줄(144px)이다.
    REQUIRE(luil::virtual_list_grid_offset_keeping(240.0f, 1, 24.0f, 3, 48.0f) == 144.0f);
    // 되돌아가면 넷째 줄 첫 항목(9번)이 맨 위다.
    REQUIRE(luil::virtual_list_grid_offset_keeping(144.0f, 3, 48.0f, 1, 24.0f) == 216.0f);
}

TEST_CASE("Reveal can centre the item instead of sticking to the nearest edge", "[ui][list][grid]")
{
    // 창 밖(500..550)의 항목을 200px 창에 들인다.
    REQUIRE(luil::virtual_list_reveal_offset(500.0f, 50.0f, 0.0f, 200.0f, luil::virtual_list_reveal::nearest) == 350.0f);
    REQUIRE(luil::virtual_list_reveal_offset(500.0f, 50.0f, 0.0f, 200.0f, luil::virtual_list_reveal::center) == 425.0f);
    // 이미 다 보이면 그대로다.
    REQUIRE(luil::virtual_list_reveal_offset(50.0f, 50.0f, 0.0f, 200.0f, luil::virtual_list_reveal::center) == 0.0f);
    // 맨 앞은 0 아래로 가지 않는다.
    REQUIRE(luil::virtual_list_reveal_offset(10.0f, 20.0f, 300.0f, 200.0f, luil::virtual_list_reveal::center) == 0.0f);
}

TEST_CASE("A million tile grid still realises only one screen", "[ui][list][grid]")
{
    luil::virtual_list_config config { make_grid(1'000'000, 500'000) };
    config.scroll_offset = 50.0f * 100'000.0f;
    const auto list { arrange_grid(std::move(config)) };
    // 창의 두 줄(6칸)과 창 밖의 커서 칸 하나다.
    const luil::ui_element* const lane { list->children()[0]->children()[0]->children()[0].get() };
    REQUIRE(lane->children().size() <= 7u);
    REQUIRE(list->realized().end - list->realized().begin == 6u);
}
