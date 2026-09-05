#include "luil/ui/scrollbar_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <utility>
#include <variant>
#include <vector>

// 스크롤 막대는 값(내용·창·스크롤)을 논리 픽셀 설정으로 받아 arrange가 배율을
// 곱한다. thumb 배치, 최소 thumb 크기, thumb 밖 클릭의 점프, 끌기의 픽셀 →
// 논리 변환, 그리고 "factory가 없으면 끌기도 없다"를 고정한다.

namespace {
    constexpr luil::ui_element_kind kind_bar { luil::application_element_kind(0) };

    struct scroll_intent
    {
        float delta { 0.0f };
    };

    [[nodiscard]] luil::scrollbar_config scrolling_config(const float content, const float viewport, const float offset)
    {
        luil::scrollbar_config config {};
        config.scroll = [](const float delta) { return luil::input_action { luil::app_message { scroll_intent { delta } } }; };
        config.content_height = content;
        config.viewport_height = viewport;
        config.scroll_offset = offset;
        return config;
    }

    // 액션이 담은 스크롤 변화량이다 (없으면 nullopt).
    [[nodiscard]] std::optional<float> delta_of(const std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1u)
            return std::nullopt;
        const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
        if (message == nullptr)
            return std::nullopt;
        const auto* const intent { message->get<scroll_intent>() };
        if (intent == nullptr)
            return std::nullopt;
        return intent->delta;
    }
} // namespace

TEST_CASE("A scrollbar arranges its thumb from logical values and the scale", "[ui][scrollbar]")
{
    // 내용 400, 창 100 → thumb는 track의 1/4이고, 스크롤 150/300 = 절반 위치다.
    luil::scrollbar_element bar { { kind_bar }, scrolling_config(400.0f, 100.0f, 150.0f) };
    bar.arrange({ { 0.0f, 0.0f, 16.0f, 400.0f }, 2.0f });

    REQUIRE(bar.thumb_height() == 100.0f);
    // 이동 가능 300px의 절반 = 150px.
    REQUIRE(bar.thumb_top() == 150.0f);
    REQUIRE(bar.draggable());
}

TEST_CASE("A scrollbar clamps the thumb to its minimum size and the track", "[ui][scrollbar]")
{
    // 내용이 아주 길면 비율대로는 4px지만 최소 크기(24 × 배율)로 잡힌다.
    luil::scrollbar_element tiny { { kind_bar }, scrolling_config(10000.0f, 100.0f, 0.0f) };
    tiny.arrange({ { 0.0f, 0.0f, 16.0f, 400.0f }, 2.0f });
    REQUIRE(tiny.thumb_height() == luil::scrollbar_minimum_thumb * 2.0f);

    // 내용이 창보다 짧으면 thumb가 track을 다 채우고 끌 것이 없다.
    luil::scrollbar_element full { { kind_bar }, scrolling_config(50.0f, 100.0f, 0.0f) };
    full.arrange({ { 0.0f, 0.0f, 16.0f, 400.0f }, 1.0f });
    REQUIRE(full.thumb_height() == 400.0f);
    REQUIRE(full.draggable() == false);
    // 끌어도 메시지가 나오지 않는다.
    REQUIRE(full.pointer_drag() != nullptr);
    const auto actions { full.pointer_drag()->on_move({ full.id(), 0.0f, 0.0f, false }, { full.id(), 0.0f, 50.0f, false }) };
    REQUIRE(actions.empty());
}

TEST_CASE("Dragging the thumb converts physical pixels back to logical scroll deltas", "[ui][scrollbar]")
{
    // 배율 2: track 400px(물리), thumb 100px, 이동 가능 300px(물리),
    // 스크롤 가능 (400-100)*2 = 600px(물리) → 물리 150px 끌기 = 논리 150px.
    luil::scrollbar_element bar { { kind_bar }, scrolling_config(400.0f, 100.0f, 0.0f) };
    bar.arrange({ { 0.0f, 0.0f, 16.0f, 400.0f }, 2.0f });

    const auto delta { delta_of(bar.pointer_drag()->on_move({ bar.id(), 0.0f, 0.0f, false }, { bar.id(), 0.0f, 150.0f, false })) };
    REQUIRE(delta.has_value());
    REQUIRE(*delta == 150.0f);
}

TEST_CASE("Pressing outside the thumb jumps toward that spot, inside only grabs", "[ui][scrollbar]")
{
    // 스크롤 0이라 thumb(높이 100)는 track 맨 위에 있다.
    luil::scrollbar_element bar { { kind_bar }, scrolling_config(400.0f, 100.0f, 0.0f) };
    bar.arrange({ { 0.0f, 0.0f, 16.0f, 400.0f }, 1.0f });

    // thumb 안(중앙 50)을 누르면 잡기만 한다.
    REQUIRE(bar.pointer_drag()->on_press({ bar.id(), 8.0f, 50.0f, false }).empty());

    // thumb 밖(350)을 누르면 그 자리로 이동한다: thumb 중앙 50 → 350,
    // 물리 300px = 논리 300px (스크롤 가능 300 / 이동 가능 300).
    const auto delta { delta_of(bar.pointer_drag()->on_press({ bar.id(), 8.0f, 350.0f, false })) };
    REQUIRE(delta.has_value());
    REQUIRE(*delta == 300.0f);
}

TEST_CASE("A scrollbar without a factory builds no drag handling at all", "[ui][scrollbar]")
{
    luil::scrollbar_config config {};
    config.content_height = 400.0f;
    config.viewport_height = 100.0f;
    const luil::scrollbar_element bar { { kind_bar }, std::move(config) };
    // "없는 것은 두지 않는다" — 잡히는데 아무 일도 없는 조합이 생기지 않는다.
    REQUIRE(bar.pointer_drag() == nullptr);
    REQUIRE(bar.interactive() == false);
    REQUIRE(bar.key_step() == nullptr);
    REQUIRE(bar.tab_stop() == false);
}

TEST_CASE("A scrollbar walks by line, page and end with the keys", "[ui][scrollbar][focus]")
{
    // 걸음의 크기는 설정에서 나온다 — 창 높이를 아는 것이 이 element뿐이다.
    luil::scrollbar_element bar { { kind_bar }, scrolling_config(400.0f, 100.0f, 120.0f) };
    bar.arrange({ { 0.0f, 0.0f, 8.0f, 100.0f }, 1.0f });

    const luil::key_step_target* const steps { bar.key_step() };
    REQUIRE(steps != nullptr);
    // 세로 막대라 ↑/↓만 가져간다.
    REQUIRE(steps->axis == luil::focus_axis::vertical);

    const auto stepped = [&steps](const luil::value_step step) {
        const std::optional<std::vector<luil::input_action>> actions { steps->on_step(step) };
        REQUIRE(actions.has_value());
        return delta_of(*actions);
    };

    // 한 줄은 휠 한 눈금과 같은 값이다.
    REQUIRE(stepped(luil::value_step::increase) == luil::scrollbar_key_line_step);
    REQUIRE(stepped(luil::value_step::decrease) == -luil::scrollbar_key_line_step);
    // 한 화면은 창 높이 하나다.
    REQUIRE(stepped(luil::value_step::increase_page) == 100.0f);
    REQUIRE(stepped(luil::value_step::decrease_page) == -100.0f);
    // 처음과 끝도 변화량이다 (지금 120에서 0으로, 그리고 300으로).
    REQUIRE(stepped(luil::value_step::minimum) == -120.0f);
    REQUIRE(stepped(luil::value_step::maximum) == 180.0f);
}

TEST_CASE("A scrollbar with nothing to scroll swallows the keys", "[ui][scrollbar][focus]")
{
    // 내용이 창보다 짧으면 끌기가 아무 일도 하지 않는다. 키도 같은 규칙이다.
    luil::scrollbar_element bar { { kind_bar }, scrolling_config(80.0f, 100.0f, 0.0f) };
    bar.arrange({ { 0.0f, 0.0f, 8.0f, 100.0f }, 1.0f });
    const std::optional<std::vector<luil::input_action>> stepped { bar.key_step()->on_step(luil::value_step::increase) };
    REQUIRE(stepped.has_value());
    REQUIRE(stepped->empty());
}
