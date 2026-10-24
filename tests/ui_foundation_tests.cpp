#include "luil/ui/app_message.h"
#include "luil/ui/label_element.h"
#include "luil/ui/scrollbar_element.h"
#include "luil/ui/spinner_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

TEST_CASE("Horizontal scrollbars use x for geometry dragging and key steps", "[ui][scrollbar]")
{
    float delta {};
    luil::scrollbar_config config {};
    config.content_height = 400.0f;
    config.viewport_height = 100.0f;
    config.scroll_offset = 150.0f;
    config.axis = luil::scrollbar_axis::horizontal;
    config.scroll = [&delta](const float value) { delta = value; return luil::input_action {}; };
    luil::scrollbar_element bar { { luil::application_element_kind(0) }, config };
    bar.arrange({ { 20.0f, 10.0f, 400.0f, 20.0f }, 2.0f });
    REQUIRE(bar.thumb_start() == 170.0f);
    REQUIRE(bar.thumb_length() == 100.0f);
    REQUIRE(bar.key_step()->axis == luil::focus_axis::horizontal);
    REQUIRE(bar.accessibility().orientation == luil::access_orientation::horizontal);
    static_cast<void>(bar.pointer_drag()->on_move({}, { .x = 150.0f, .y = 999.0f }));
    REQUIRE(delta == 150.0f);
    static_cast<void>(bar.key_step()->on_step(luil::value_step::decrease_page));
    REQUIRE(delta == -100.0f);
    static_cast<void>(bar.key_step()->on_step(luil::value_step::maximum));
    REQUIRE(delta == 150.0f);
}

TEST_CASE("A hidden or empty spinner does not schedule another frame", "[ui][spinner]")
{
    luil::spinner_element spinner { { luil::application_element_kind(0) }, { u8"Loading" } };
    REQUIRE(spinner.next_update({}, {}).has_value() == false);
    spinner.arrange({ { 0.0f, 0.0f, 24.0f, 24.0f } });
    REQUIRE(spinner.next_update({}, {}).has_value());
    REQUIRE(spinner.accessibility().role == luil::access_role::progress);
    REQUIRE(spinner.accessibility().name == u8"Loading");
    REQUIRE(spinner.accessibility().range.has_value() == false);
    spinner.set_visible(false);
    REQUIRE(spinner.next_update({}, {}).has_value() == false);
}

TEST_CASE("A spinner stands still and schedules no frame when motion is reduced", "[ui][spinner]")
{
    luil::spinner_element spinner { { luil::application_element_kind(0) }, { u8"Loading" } };
    spinner.arrange({ { 0.0f, 0.0f, 24.0f, 24.0f } });
    REQUIRE(spinner.next_update({}, {}).has_value());
    REQUIRE(spinner.next_update({ .reduce_motion = true }, {}).has_value() == false);
}

namespace {
    constexpr luil::ui_element_kind combo_input_kind { luil::application_element_kind(1) };

    // 열린 후보의 탐색 키를 바깥으로 넘기는 칸이다 (편집 가능한 콤보의 입력 칸).
    class parent_navigation_input final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}

        [[nodiscard]] std::optional<std::size_t> offset_at(const float, const luil::text_measurer&) const override
        {
            return 0u;
        }

        [[nodiscard]] std::optional<luil::text_input_snapshot> text_input() const override
        {
            luil::text_input_snapshot value { u8"abc", 1u, 1u };
            value.navigation_keys_to_parent = true;
            return value;
        }
    };

    class edit_recording_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            return kind == combo_input_kind ? std::optional<luil::text_input_target> { luil::text_input_target { 3 } } : std::nullopt;
        }

        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request& request) const override
        {
            edits.push_back(request);
            return luil::input_action { luil::app_message { 1 } };
        }

        mutable std::vector<luil::text_edit_request> edits {};
    };

    // 칸을 감싼 쪽(콤보)이다. 받은 키를 세고 소비한다.
    class counting_parent final : public luil::ui_element
    {
    public:
        explicit counting_parent(int& keys)
            : ui_element { luil::ui_element_id { luil::ui_element_kind::root } }
        {
            set_key_handler([&keys](const luil::key_pressed_event&) -> std::optional<std::vector<luil::input_action>> {
                ++keys;
                return std::vector<luil::input_action> {};
            });
        }

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };
} // namespace

TEST_CASE("Home and End stay with a text box that hands navigation keys to its parent", "[ui][interaction][keys]")
{
    int parent_keys { 0 };
    auto root { std::make_unique<counting_parent>(parent_keys) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto input { std::make_unique<parent_navigation_input>(luil::ui_element_id { combo_input_kind, u8"query" }) };
    input->arrange({ { 0.0f, 0.0f, 200.0f, 24.0f }, 1.0f });
    input->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; });
    root->add(std::move(input));

    edit_recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 12.0f, luil::pointer_button::left }));
    static_cast<void>(controller.process(luil::pointer_released_event { 20.0f, 12.0f, luil::pointer_button::left }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { combo_input_kind, u8"query" });
    policy.edits.clear();
    parent_keys = 0;

    // Home·End는 caret을 옮긴다 (Shift면 고름을 넓힌다). 콤보에 가지 않는다.
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::home }));
    luil::key_pressed_event shift_end { luil::key_code::end };
    shift_end.shift = true;
    static_cast<void>(controller.process(shift_end));
    REQUIRE(policy.edits.size() == 2u);
    REQUIRE(policy.edits[0].command == luil::text::text_edit_command::move_line_start);
    REQUIRE(policy.edits[0].extend == false);
    REQUIRE(policy.edits[1].command == luil::text::text_edit_command::move_line_end);
    REQUIRE(policy.edits[1].extend);
    REQUIRE(parent_keys == 0);

    // 후보를 오가는 키는 그대로 콤보의 것이다.
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::arrow_down }));
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::page_up }));
    REQUIRE(policy.edits.size() == 2u);
    REQUIRE(parent_keys == 2);
}

TEST_CASE("Middle clicks do not execute actions or replace an active left press", "[ui][pointer]")
{
    auto element { std::make_unique<luil::label_element>(luil::ui_element_id { luil::application_element_kind(0) }, luil::label_config {}) };
    element->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) {
        return std::vector<luil::input_action> { luil::app_message { 7 } };
    });
    element->set_bounds({ 0.0f, 0.0f, 100.0f, 100.0f });
    auto tree { std::make_shared<luil::ui_tree>(std::move(element)) };
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    REQUIRE(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::middle }).empty());
    REQUIRE(controller.process(luil::pointer_released_event { 10.0f, 10.0f, luil::pointer_button::middle }).empty());
    static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::left }));
    const auto pressed { controller.snapshot().pressed };
    static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::middle }));
    REQUIRE(controller.snapshot().pressed == pressed);
    REQUIRE(controller.process(luil::pointer_released_event { 10.0f, 10.0f, luil::pointer_button::left }).size() == 1u);
}
