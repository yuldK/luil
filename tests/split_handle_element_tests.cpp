#include "luil/ui/split_handle_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
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

    // 길이 바꾸기가 끝났다는 메시지의 앱 쪽 정의다.
    struct finish_intent
    {
    };

    [[nodiscard]] luil::split_finish_message_factory make_finish_factory()
    {
        return []() { return luil::input_action { luil::app_message { finish_intent {} } }; };
    }

    [[nodiscard]] std::size_t count_finish(const std::vector<luil::input_action>& actions)
    {
        std::size_t count { 0 };
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr && message->get<finish_intent>() != nullptr)
                ++count;
        return count;
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

    constexpr luil::ui_element_kind kind_pane { luil::application_element_kind(1) };

    // 눌러지는 판이다. 손잡이가 떠난 자리를 덮는 것이 무엇인지가 이 test의 쟁점이다.
    class pane final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
    };

    // 앞 판(길이 `width`)·손잡이(두께 8)·뒤 판이 가로로 선 화면이다.
    // 손잡이는 앞 판의 끝에 서므로 길이가 바뀌면 손잡이도 옮겨 간다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_split_tree(const float width, const bool with_finish = false)
    {
        auto root { std::make_unique<pane>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 800.0f, 600.0f }, 1.0f });

        auto front { std::make_unique<pane>(luil::ui_element_id { kind_pane, u8"front" }) };
        front->arrange({ { 0.0f, 0.0f, width, 600.0f }, 1.0f });
        front->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; });
        root->add(std::move(front));

        luil::split_handle_config config {};
        config.resize = make_resize_factory();
        if (with_finish)
            config.finish = make_finish_factory();
        auto handle { std::make_unique<luil::split_handle_element>(luil::ui_element_id { kind_handle }, config) };
        handle->arrange({ { width, 0.0f, 8.0f, 600.0f }, 1.0f });
        root->add(std::move(handle));

        auto back { std::make_unique<pane>(luil::ui_element_id { kind_pane, u8"back" }) };
        back->arrange({ { width + 8.0f, 0.0f, 800.0f - width - 8.0f, 600.0f }, 1.0f });
        back->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; });
        root->add(std::move(back));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    // 액션 중 길이 변화의 합이다. 없으면 0이다.
    [[nodiscard]] float total_delta(const std::vector<luil::input_action>& actions)
    {
        float total { 0.0f };
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const intent { message->get<length_intent>() }; intent != nullptr)
                    total += intent->delta;
        return total;
    }
} // namespace

TEST_CASE("A split handle keeps dragging after the pane it resizes moves it away from the press", "[ui][split][interaction]")
{
    // 앱이 길이를 받아 tree를 다시 지으면 손잡이가 경계를 따라 옮겨 간다. 누른 자리는 곧 앞 판이 덮는다.
    // 누른 자리를 덮은 것으로 끌기를 끊으면 첫 이동 하나만 듣고 끝난다.
    float width { 100.0f };
    luil::interaction_controller controller {};
    controller.set_tree(make_split_tree(width));

    static_cast<void>(controller.process(luil::pointer_pressed_event { 104.0f, 300.0f, luil::pointer_button::left, at(0) }));

    const float first { total_delta(controller.process(luil::pointer_moved_event { 124.0f, 300.0f, at(16) })) };
    REQUIRE(first == 20.0f);
    width += first;
    controller.set_tree(make_split_tree(width));

    const float second { total_delta(controller.process(luil::pointer_moved_event { 144.0f, 300.0f, at(32) })) };
    REQUIRE(second == 20.0f);
    width += second;
    controller.set_tree(make_split_tree(width));

    // 되돌아와도 같은 손잡이가 듣는다.
    const float back { total_delta(controller.process(luil::pointer_moved_event { 134.0f, 300.0f, at(48) })) };
    REQUIRE(back == -10.0f);

    static_cast<void>(controller.process(luil::pointer_released_event { 134.0f, 300.0f, luil::pointer_button::left, at(64) }));
}

TEST_CASE("A split handle announces the end of a drag once when the pointer lets go", "[ui][split][interaction]")
{
    // 끄는 동안은 변화량만 온다. 끝난 길이를 저장할 때를 앱이 타이머로 짐작하지 않게, 뗄 때 한 번 알린다.
    float width { 100.0f };
    luil::interaction_controller controller {};
    controller.set_tree(make_split_tree(width, true));

    REQUIRE(count_finish(controller.process(luil::pointer_pressed_event { 104.0f, 300.0f, luil::pointer_button::left, at(0) })) == 0u);
    const std::vector<luil::input_action> moved { controller.process(luil::pointer_moved_event { 124.0f, 300.0f, at(16) }) };
    REQUIRE(count_finish(moved) == 0u);
    width += total_delta(moved);
    controller.set_tree(make_split_tree(width, true));
    REQUIRE(count_finish(controller.process(luil::pointer_moved_event { 140.0f, 300.0f, at(32) })) == 0u);

    REQUIRE(count_finish(controller.process(luil::pointer_released_event { 140.0f, 300.0f, luil::pointer_button::left, at(48) })) == 1u);
    // 뗀 뒤의 이동은 끌기가 아니다.
    REQUIRE(count_finish(controller.process(luil::pointer_moved_event { 160.0f, 300.0f, at(64) })) == 0u);
}

TEST_CASE("A split handle without a finish factory stays silent at the end", "[ui][split]")
{
    // 없는 것은 두지 않는다: 알릴 곳이 없으면 끝도 듣지 않는다.
    const auto handle { make_handle(luil::split_axis::horizontal, luil::split_grows::toward_end, 1.0f) };
    REQUIRE(handle->pointer_drag()->on_release == nullptr);
    const std::optional<std::vector<luil::input_action>> stepped { handle->key_step()->on_step(luil::value_step::increase) };
    REQUIRE(stepped.has_value());
    REQUIRE(count_finish(*stepped) == 0u);
}

TEST_CASE("A split handle key step is a whole resize that ends at once", "[ui][split][focus]")
{
    // 키 한 번이 곧 한 번의 바꾸기다 — 변화량 바로 뒤에 끝이 온다.
    luil::split_handle_config config {};
    config.resize = make_resize_factory();
    config.finish = make_finish_factory();
    const luil::split_handle_element handle { luil::ui_element_id { kind_handle }, config };
    const std::optional<std::vector<luil::input_action>> stepped { handle.key_step()->on_step(luil::value_step::increase_page) };
    REQUIRE(stepped.has_value());
    REQUIRE(stepped->size() == 2u);
    REQUIRE(total_delta(*stepped) == luil::split_handle_key_page_step);
    REQUIRE(count_finish(*stepped) == 1u);
    REQUIRE(std::get<luil::app_message>((*stepped)[1]).get<finish_intent>() != nullptr);

    // Home·End는 여전히 내 것이 아니다 — 끝도 내지 않는다.
    REQUIRE(handle.key_step()->on_step(luil::value_step::minimum).has_value() == false);
}

TEST_CASE("A split handle without a resize factory ignores its finish factory", "[ui][split]")
{
    luil::split_handle_config config {};
    config.finish = make_finish_factory();
    const luil::split_handle_element handle { luil::ui_element_id { kind_handle }, config };
    REQUIRE(handle.pointer_drag() == nullptr);
    REQUIRE(handle.key_step() == nullptr);
}

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
