#include "luil/ui/slider_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_slider { luil::application_element_kind(0) };

    // 값 변화 메시지의 앱 쪽 정의다.
    struct value_intent
    {
        float delta { 0.0f };
    };

    [[nodiscard]] luil::slider_change_message_factory make_change_factory()
    {
        return [](const float delta) { return luil::input_action { luil::app_message { value_intent { delta } } }; };
    }

    // 폭 112px(물리) 안에서 손잡이 12 × 배율을 뺀 만큼이 도는 거리다.
    [[nodiscard]] std::unique_ptr<luil::slider_element> make_slider(const float value, const float scale, const float width = 112.0f)
    {
        luil::slider_config config {};
        config.value = value;
        config.change = make_change_factory();
        auto slider { std::make_unique<luil::slider_element>(luil::ui_element_id { kind_slider }, config) };
        slider->arrange({ { 100.0f, 40.0f, width, 20.0f * scale }, scale });
        return slider;
    }

    // 절대 값 메시지의 앱 쪽 정의다.
    struct target_intent
    {
        float value { 0.0f };
    };

    // 폭 112px(물리)·배율 1이면 도는 거리가 100이라, 지점 0.5는 트랙 왼쪽 끝에서 50px,
    // 포인터 x로는 156이다.
    [[nodiscard]] std::unique_ptr<luil::slider_element> make_magnet_slider(const float value, std::vector<float> magnets, const bool absolute, const float scale = 1.0f, const float width = 112.0f)
    {
        luil::slider_config config {};
        config.value = value;
        config.change = make_change_factory();
        if (absolute)
            config.change_to = [](const float target) { return luil::input_action { luil::app_message { target_intent { target } } }; };
        config.magnets = std::move(magnets);
        auto slider { std::make_unique<luil::slider_element>(luil::ui_element_id { kind_slider }, config) };
        slider->arrange({ { 100.0f, 40.0f, width, 20.0f * scale }, scale });
        return slider;
    }

    void require_target(const std::vector<luil::input_action>& actions, const float expected)
    {
        REQUIRE(actions.size() == 1);
        const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
        REQUIRE(message != nullptr);
        const auto* const intent { message->get<target_intent>() };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->value == expected);
    }

    void require_delta(const std::vector<luil::input_action>& actions, const float expected)
    {
        REQUIRE(actions.size() == 1);
        const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
        REQUIRE(message != nullptr);
        const auto* const intent { message->get<value_intent>() };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->delta == expected);
    }
}

TEST_CASE("A slider puts the knob centre at the ends of its travel", "[ui][slider]")
{
    // 손잡이의 **중심**이 양 끝에 닿는다. 도는 거리는 폭에서 손잡이 하나를 뺀 것이다.
    const auto lowest { make_slider(0.0f, 1.0f) };
    REQUIRE(lowest->travel() == 100.0f);
    REQUIRE(lowest->knob_center() == 106.0f);

    const auto highest { make_slider(1.0f, 1.0f) };
    REQUIRE(highest->knob_center() == 206.0f);

    const auto middle { make_slider(0.5f, 1.0f) };
    REQUIRE(middle->knob_center() == 156.0f);
}

TEST_CASE("A slider clamps the value it is given", "[ui][slider]")
{
    // 다듬기는 값을 가진 앱의 몫이지만, 그리기가 범위 밖 값에 무너지지는 않는다.
    //  - 설정이 `std::function`을 담아 상수 평가 안에서 지을 수 없다.
    //    그래서 `constexpr` 함수인데도 `STATIC_REQUIRE`가 아니다.
    REQUIRE(luil::slider_element::clamp_value(luil::slider_config { .value = 0.25f }) == 0.25f);
    REQUIRE(luil::slider_element::clamp_value(luil::slider_config { .value = -3.0f }) == 0.0f);
    REQUIRE(luil::slider_element::clamp_value(luil::slider_config { .value = 4.0f }) == 1.0f);
    // 범위가 뒤집혀 있으면 값이 곧 최소다.
    REQUIRE(luil::slider_element::clamp_value(luil::slider_config { .minimum = 5.0f, .maximum = 5.0f, .value = 9.0f }) == 5.0f);
    REQUIRE(luil::slider_element::height_for(luil::slider_config {}) == luil::slider_row_height);
}

TEST_CASE("A slider turns a drag into value deltas", "[ui][slider]")
{
    // 물리 이동을 도는 거리로 나눈 비율이 값 변화다.
    // 배율이 커지면 도는 거리도 함께 커지므로 같은 물리 이동이 더 적게 움직인다.
    const auto plain { make_slider(0.5f, 1.0f) };
    const luil::ui_action_context from { plain->id(), 150.0f, 50.0f };
    const luil::ui_action_context to { plain->id(), 175.0f, 50.0f };
    require_delta(plain->pointer_drag()->on_move(from, to), 0.25f);
    require_delta(plain->pointer_drag()->on_move(to, from), -0.25f);

    // 배율 2에서는 폭 224, 손잡이 24 → 도는 거리 200이다.
    const auto scaled { make_slider(0.5f, 2.0f, 224.0f) };
    REQUIRE(scaled->travel() == 200.0f);
    require_delta(scaled->pointer_drag()->on_move(from, to), 0.125f);

    // 움직이지 않으면 아무 말도 하지 않는다.
    REQUIRE(plain->pointer_drag()->on_move(from, from).empty());
}

TEST_CASE("Pressing the track moves the knob to that spot", "[ui][slider]")
{
    // 누른 자리와 손잡이 중심의 거리가 그대로 값 변화가 된다.
    const auto slider { make_slider(0.0f, 1.0f) };
    REQUIRE(slider->knob_center() == 106.0f);
    require_delta(slider->pointer_drag()->on_press({ slider->id(), 156.0f, 50.0f }), 0.5f);
    // 이미 그 자리면 조용하다.
    REQUIRE(slider->pointer_drag()->on_press({ slider->id(), 106.0f, 50.0f }).empty());
}

TEST_CASE("A slider without a change factory is not a drag target", "[ui][slider]")
{
    // 없는 것은 두지 않는다: 값을 바꿀 길이 없으면 끌기 대상도 커서 주인도 아니다.
    const luil::slider_element slider { luil::ui_element_id { kind_slider }, luil::slider_config { .value = 0.5f } };
    REQUIRE(slider.pointer_drag() == nullptr);
    REQUIRE(slider.cursor() == luil::ui_cursor::inherit);
    REQUIRE(slider.interactive() == false);
    // 기본 값과 같은 규칙이다 — 조작할 수 없으면 Tab의 자리도 아니다.
    REQUIRE(slider.key_step() == nullptr);
    REQUIRE(slider.tab_stop() == false);
}

TEST_CASE("A slider walks its range with the arrow, page and end keys", "[ui][slider][focus]")
{
    // 막대는 자기 범위를 알므로 걸음의 크기를 스스로 정한다.
    // 기본 범위는 0..1이라 화살표가 0.01, Page가 0.1이다.
    const auto slider { make_slider(0.5f, 1.0f) };
    const luil::key_step_target* const steps { slider->key_step() };
    REQUIRE(steps != nullptr);
    // 가로 막대라 화살표의 축도 가로다.
    REQUIRE(steps->axis == luil::focus_axis::horizontal);

    const auto stepped = [&steps](const luil::value_step step) {
        const std::optional<std::vector<luil::input_action>> actions { steps->on_step(step) };
        REQUIRE(actions.has_value());
        return *actions;
    };

    require_delta(stepped(luil::value_step::increase), 0.01f);
    require_delta(stepped(luil::value_step::decrease), -0.01f);
    require_delta(stepped(luil::value_step::increase_page), 0.1f);
    require_delta(stepped(luil::value_step::decrease_page), -0.1f);
    // 양 끝은 **변화량**이다 — 값을 직접 주면 끌기와 계약이 갈라진다.
    require_delta(stepped(luil::value_step::minimum), -0.5f);
    require_delta(stepped(luil::value_step::maximum), 0.5f);
}

TEST_CASE("A slider at the end swallows the key instead of leaking it", "[ui][slider][focus]")
{
    // 이미 최소인 막대의 Home이 앱 단축키로 새면 화면이 함께 맨 위로 뛴다.
    const auto slider { make_slider(0.0f, 1.0f) };
    const std::optional<std::vector<luil::input_action>> home { slider->key_step()->on_step(luil::value_step::minimum) };
    REQUIRE(home.has_value());
    REQUIRE(home->empty());

    // 범위가 없으면 어느 걸음도 값을 내지 않는다.
    luil::slider_config flat {};
    flat.minimum = 1.0f;
    flat.maximum = 1.0f;
    flat.value = 1.0f;
    flat.change = make_change_factory();
    const luil::slider_element pinned { luil::ui_element_id { kind_slider }, flat };
    const std::optional<std::vector<luil::input_action>> forward { pinned.key_step()->on_step(luil::value_step::increase) };
    REQUIRE(forward.has_value());
    REQUIRE(forward->empty());
}

TEST_CASE("A magnet holds the knob, then lets it catch up with the pointer", "[ui][slider][magnet]")
{
    // 붙는 거리(6) 안은 지점 그대로, 풀리는 거리(12)까지는 따라잡고, 그 밖은 포인터다.
    const auto slider { make_magnet_slider(0.0f, { 0.5f }, false) };
    std::optional<std::size_t> magnet {};
    REQUIRE(slider->knob_offset_for(156.0f, false, &magnet) == 50.0f);
    REQUIRE(magnet == std::size_t { 0 });
    REQUIRE(slider->knob_offset_for(162.0f, false, &magnet) == 50.0f);
    REQUIRE(magnet == std::size_t { 0 });
    REQUIRE(slider->knob_offset_for(150.0f, false) == 50.0f);
    // 따라잡는 구간은 붙는 거리에서 지점, 풀리는 거리에서 포인터에 닿는 직선이다.
    REQUIRE(slider->knob_offset_for(165.0f, false, &magnet) == 56.0f);
    REQUIRE(magnet.has_value() == false);
    REQUIRE(slider->knob_offset_for(147.0f, false) == 44.0f);
    REQUIRE(slider->knob_offset_for(168.0f, false) == 62.0f);
    REQUIRE(slider->knob_offset_for(176.0f, false) == 70.0f);
    // 붙는 거리 바로 밖도 지점에서 거의 떨어지지 않는다 — 손잡이가 튀지 않는다.
    REQUIRE(slider->knob_offset_for(162.01f, false) < 50.1f);
    // Ctrl은 자석 없이 포인터 그대로다.
    REQUIRE(slider->knob_offset_for(159.0f, true, &magnet) == 53.0f);
    REQUIRE(magnet.has_value() == false);
}

TEST_CASE("A magnet scales with the layout", "[ui][slider][magnet]")
{
    // 배율 2에서는 폭 224, 손잡이 24 → 도는 거리 200, 붙는 거리 12다.
    const auto slider { make_magnet_slider(0.0f, { 0.5f }, false, 2.0f, 224.0f) };
    REQUIRE(slider->knob_offset_for(100.0f + 12.0f + 111.0f, false) == 100.0f);
    REQUIRE(slider->knob_offset_for(100.0f + 12.0f + 130.0f, false) == 130.0f);
}

TEST_CASE("Neighbouring magnets split the space between them", "[ui][slider][magnet]")
{
    // 50과 56.25px의 두 지점은 간격의 절반(3.125)씩만 당긴다.
    const auto slider { make_magnet_slider(0.0f, { 0.5625f, 0.5f }, false) };
    std::optional<std::size_t> magnet {};
    REQUIRE(slider->knob_offset_for(106.0f + 53.0f, false, &magnet) == 50.0f);
    REQUIRE(magnet == std::size_t { 0 });
    REQUIRE(slider->knob_offset_for(106.0f + 54.0f, false, &magnet) == 56.25f);
    REQUIRE(magnet == std::size_t { 1 });
}

TEST_CASE("Magnets outside the range are dropped", "[ui][slider][magnet]")
{
    // 범위 밖·겹친 지점은 버리고 남은 것을 정렬한다. 0.5 하나만 남는다.
    const auto slider { make_magnet_slider(0.0f, { 2.0f, 0.5f, -1.0f, 0.5f }, false) };
    std::optional<std::size_t> magnet {};
    REQUIRE(slider->knob_offset_for(156.0f, false, &magnet) == 50.0f);
    REQUIRE(magnet == std::size_t { 0 });
    REQUIRE(slider->knob_offset_for(206.0f, false) == 100.0f);
}

TEST_CASE("Dragging through a magnet sends the difference of knob spots", "[ui][slider][magnet]")
{
    const auto slider { make_magnet_slider(0.5f, { 0.5f }, false) };
    const luil::pointer_drag_target* const drag { slider->pointer_drag() };
    const luil::ui_action_context near { slider->id(), 150.0f, 50.0f };
    const luil::ui_action_context closer { slider->id(), 160.0f, 50.0f };
    const luil::ui_action_context away { slider->id(), 170.0f, 50.0f };
    // 둘 다 붙는 거리 안이라 손잡이가 움직이지 않는다.
    REQUIRE(drag->on_move(near, closer).empty());
    // 지점(50)에서 포인터 자리(64)까지다.
    require_delta(drag->on_move(near, away), 0.14f);
    require_delta(drag->on_move(away, near), -0.14f);
}

TEST_CASE("Ctrl frees the knob even in the middle of a drag", "[ui][slider][magnet]")
{
    // 각 문맥의 Ctrl로 함수를 고르므로, 누르고 떼는 사이의 델타를 더하면 포인터 자리의
    // 손잡이와 같다.
    const auto slider { make_magnet_slider(0.5f, { 0.5f }, false) };
    const luil::pointer_drag_target* const drag { slider->pointer_drag() };
    const luil::ui_action_context held { slider->id(), 160.0f, 50.0f };
    luil::ui_action_context freed { held };
    freed.control = true;
    // 붙어 있던 손잡이(50)가 Ctrl로 포인터(54)에게 간다.
    require_delta(drag->on_move(held, freed), 0.04f);
    // Ctrl을 떼면 다시 지점이다.
    require_delta(drag->on_move(freed, held), -0.04f);
}

TEST_CASE("A slider keeps the knob at the end until the pointer returns", "[ui][slider]")
{
    // 포인터 자리는 트랙 안으로 자른다. 끝 너머에서 움직이면 아무 말도 하지 않는다.
    const auto slider { make_slider(0.5f, 1.0f) };
    const luil::pointer_drag_target* const drag { slider->pointer_drag() };
    const luil::ui_action_context inside { slider->id(), 156.0f, 50.0f };
    const luil::ui_action_context beyond { slider->id(), 400.0f, 50.0f };
    const luil::ui_action_context still_beyond { slider->id(), 380.0f, 50.0f };
    require_delta(drag->on_move(inside, beyond), 0.5f);
    REQUIRE(drag->on_move(beyond, still_beyond).empty());
}

TEST_CASE("Snapping sends the magnet value itself when the app takes one", "[ui][slider][magnet]")
{
    // 델타를 이어 더한 값은 지점에서 오차만큼 비켜 선다. 절대 메시지는 지점 그대로다.
    const auto slider { make_magnet_slider(0.0f, { 0.5f }, true) };
    const luil::pointer_drag_target* const drag { slider->pointer_drag() };
    require_target(drag->on_move({ slider->id(), 140.0f, 50.0f }, { slider->id(), 156.0f, 50.0f }), 0.5f);
    // 누른 자리도 같다.
    require_target(drag->on_press({ slider->id(), 160.0f, 50.0f }), 0.5f);
    // 붙지 않는 이동은 지금처럼 델타다.
    require_delta(drag->on_move({ slider->id(), 170.0f, 50.0f }, { slider->id(), 180.0f, 50.0f }), 0.1f);
}

TEST_CASE("Keys walk past magnets without snapping", "[ui][slider][magnet]")
{
    // 방향키는 정확한 값을 고르는 길이라 자석이 없다.
    const auto slider { make_magnet_slider(0.495f, { 0.5f }, true) };
    const std::optional<std::vector<luil::input_action>> step { slider->key_step()->on_step(luil::value_step::increase) };
    REQUIRE(step.has_value());
    require_delta(*step, 0.01f);
}
