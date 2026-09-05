#include "luil/ui/split_handle_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_handle { luil::application_element_kind(0) };

    // 길이 변화 메시지의 앱 쪽 정의다.
    struct length_intent
    {
        float delta { 0.0f };
    };

    [[nodiscard]] luil::split_resize_message_factory make_resize_factory()
    {
        return [](const float delta) { return luil::input_action { luil::app_message { length_intent { delta } } }; };
    }

    // 배치는 담는 쪽의 몫이라 아무 자리나 준다 — 손잡이는 그것을 그대로 쓴다.
    [[nodiscard]] std::unique_ptr<luil::split_handle_element> make_handle(const luil::split_axis axis, const luil::split_grows grows, const float scale)
    {
        luil::split_handle_config config {};
        config.axis = axis;
        config.grows = grows;
        config.resize = make_resize_factory();
        auto handle { std::make_unique<luil::split_handle_element>(luil::ui_element_id { kind_handle }, config) };
        handle->arrange({ { 100.0f, 50.0f, 8.0f, 600.0f }, scale });
        return handle;
    }

    [[nodiscard]] std::vector<luil::input_action> drag(const luil::split_handle_element& handle, const float from_x, const float from_y, const float to_x, const float to_y)
    {
        const luil::ui_action_context from { handle.id(), from_x, from_y };
        const luil::ui_action_context to { handle.id(), to_x, to_y };
        return handle.pointer_drag()->on_move(from, to);
    }

    void require_delta(const std::vector<luil::input_action>& actions, const float expected)
    {
        REQUIRE(actions.size() == 1);
        const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
        REQUIRE(message != nullptr);
        const auto* const intent { message->get<length_intent>() };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->delta == expected);
    }
} // namespace

TEST_CASE("A split handle turns a drag into logical length deltas", "[ui][split]")
{
    // 물리 이동을 배율로 나눈 값이 메시지에 실린다.
    // 배율 1에서는 나누기가 있으나 없으나 같은 값이라 여기서만 보면 놓친다.
    const auto plain { make_handle(luil::split_axis::horizontal, luil::split_grows::toward_end, 1.0f) };
    require_delta(drag(*plain, 480.0f, 300.0f, 490.0f, 300.0f), 10.0f);

    const auto scaled { make_handle(luil::split_axis::horizontal, luil::split_grows::toward_end, 1.25f) };
    require_delta(drag(*scaled, 480.0f, 300.0f, 490.0f, 300.0f), 8.0f);

    const auto doubled { make_handle(luil::split_axis::horizontal, luil::split_grows::toward_end, 2.0f) };
    require_delta(drag(*doubled, 480.0f, 300.0f, 490.0f, 300.0f), 5.0f);
}

TEST_CASE("A split handle that grows toward the start flips the sign", "[ui][split]")
{
    // 임자가 뒤 판이면 앞쪽으로 끌어야 넓어진다.
    // 부호를 element가 맞추므로 앱은 어느 쪽 판인지와 무관하게 "+ = 넓어짐"만 안다.
    const auto handle { make_handle(luil::split_axis::horizontal, luil::split_grows::toward_start, 2.0f) };
    require_delta(drag(*handle, 490.0f, 300.0f, 480.0f, 300.0f), 5.0f);
    require_delta(drag(*handle, 480.0f, 300.0f, 490.0f, 300.0f), -5.0f);
}

TEST_CASE("A vertical split handle reads the drag on its own axis", "[ui][split]")
{
    const auto handle { make_handle(luil::split_axis::vertical, luil::split_grows::toward_end, 2.0f) };
    require_delta(drag(*handle, 480.0f, 300.0f, 480.0f, 310.0f), 5.0f);

    // 가로 이동은 세로 손잡이가 볼 값이 아니다.
    REQUIRE(drag(*handle, 480.0f, 300.0f, 600.0f, 300.0f).empty());
}

TEST_CASE("A split handle stays quiet when the drag does not move it", "[ui][split]")
{
    // 끌기 중에는 메시지가 frame마다 오므로 0을 걸러내지 않으면 아무 일도 하지
    // 않는 메시지가 쏟아진다. 화면은 멀쩡해서 눈으로는 보이지 않는다.
    const auto handle { make_handle(luil::split_axis::horizontal, luil::split_grows::toward_end, 1.0f) };
    REQUIRE(drag(*handle, 480.0f, 300.0f, 480.0f, 300.0f).empty());
}

TEST_CASE("A split handle without a resize factory is not a drag target", "[ui][split]")
{
    // 없는 것은 두지 않는다: 끌 수 없으면 커서도 걸지 않으므로 hit의 임자도 아니다.
    const luil::split_handle_element handle { luil::ui_element_id { kind_handle }, luil::split_handle_config {} };
    REQUIRE(handle.pointer_drag() == nullptr);
    REQUIRE(handle.cursor() == luil::ui_cursor::inherit);
    REQUIRE(handle.interactive() == false);
    REQUIRE(handle.key_step() == nullptr);
    REQUIRE(handle.tab_stop() == false);
}

TEST_CASE("A split handle steps along its own axis and keeps the drag sign", "[ui][split][focus]")
{
    // 커서를 고르는 그 값이 화살표의 축도 고른다.
    const auto horizontal { make_handle(luil::split_axis::horizontal, luil::split_grows::toward_end, 1.0f) };
    REQUIRE(horizontal->key_step() != nullptr);
    REQUIRE(horizontal->key_step()->axis == luil::focus_axis::horizontal);

    const auto vertical { make_handle(luil::split_axis::vertical, luil::split_grows::toward_end, 1.0f) };
    REQUIRE(vertical->key_step()->axis == luil::focus_axis::vertical);

    const auto stepped = [](const luil::split_handle_element& handle, const luil::value_step step) {
        const std::optional<std::vector<luil::input_action>> actions { handle.key_step()->on_step(step) };
        REQUIRE(actions.has_value());
        return *actions;
    };

    require_delta(stepped(*horizontal, luil::value_step::increase), luil::split_handle_key_step);
    require_delta(stepped(*horizontal, luil::value_step::decrease), -luil::split_handle_key_step);
    require_delta(stepped(*horizontal, luil::value_step::increase_page), luil::split_handle_key_page_step);

    // 부호는 끌기와 같은 규약이다 — 앱은 언제나 "+ = 그 판이 넓어진다"만 안다.
    const auto reversed { make_handle(luil::split_axis::horizontal, luil::split_grows::toward_start, 1.0f) };
    require_delta(stepped(*reversed, luil::value_step::increase), -luil::split_handle_key_step);
}

TEST_CASE("A split handle does not answer Home and End", "[ui][split][focus]")
{
    // 손잡이는 길이를 담지 않아 "끝"이 어디인지 모른다.
    // 답할 값이 없으므로 그 키는 내 것이 아니고 그대로 앱으로 흐른다
    // (빈 목록으로 삼키는 것과 갈리는 자리다).
    const auto handle { make_handle(luil::split_axis::horizontal, luil::split_grows::toward_end, 1.0f) };
    REQUIRE(handle->key_step()->on_step(luil::value_step::minimum).has_value() == false);
    REQUIRE(handle->key_step()->on_step(luil::value_step::maximum).has_value() == false);
}
