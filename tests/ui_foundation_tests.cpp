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

    // 자식을 담는 칸이다. 누를 수 있게 하면 Tab의 자리가 된다.
    class focus_cell final : public luil::ui_element
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
        {}
    };

    [[nodiscard]] std::unique_ptr<focus_cell> make_focus_cell(std::u8string owner, const luil::rect_f& slot, const bool stop)
    {
        auto cell { std::make_unique<focus_cell>(luil::ui_element_id { luil::application_element_kind(2), std::move(owner) }) };
        cell->arrange({ slot, 1.0f });
        if (stop)
            cell->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; });
        return cell;
    }
} // namespace

TEST_CASE("A focus leave action runs once when focus leaves its subtree", "[ui][interaction][focus]")
{
    std::vector<std::u8string> left {};
    const auto record = [&left](const luil::ui_action_context& context) {
        left.push_back(context.element.owner);
        return std::vector<luil::input_action> {};
    };
    // outer ⊃ inner ⊃ {a, b}, outer ⊃ c, 그리고 바깥의 d다. 훅은 outer와 inner에 있다.
    auto root { std::make_unique<focus_cell>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 300.0f, 300.0f }, 1.0f });
    auto outer { make_focus_cell(u8"outer", { 0.0f, 0.0f, 300.0f, 150.0f }, false) };
    outer->set_focus_leave_action(record);
    auto inner { make_focus_cell(u8"inner", { 0.0f, 0.0f, 300.0f, 80.0f }, false) };
    inner->set_focus_leave_action(record);
    inner->add(make_focus_cell(u8"a", { 0.0f, 0.0f, 100.0f, 30.0f }, true));
    inner->add(make_focus_cell(u8"b", { 0.0f, 40.0f, 100.0f, 30.0f }, true));
    outer->add(std::move(inner));
    outer->add(make_focus_cell(u8"c", { 0.0f, 100.0f, 100.0f, 30.0f }, true));
    root->add(std::move(outer));
    root->add(make_focus_cell(u8"d", { 0.0f, 200.0f, 100.0f, 30.0f }, true));

    luil::interaction_controller controller {};
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));
    const luil::key_pressed_event tab { luil::key_code::tab };
    // 들어오는 것과 가지 안에서 옮기는 것은 떠남이 아니다.
    static_cast<void>(controller.process(tab));
    static_cast<void>(controller.process(tab));
    REQUIRE(controller.snapshot().focused.owner == u8"b");
    REQUIRE(left.empty());
    // inner만 떠나고 outer에는 남는다.
    static_cast<void>(controller.process(tab));
    REQUIRE(left == std::vector<std::u8string> { u8"inner" });
    // c에서 d로 나가면 outer가 떠난다. inner는 이미 떠났으므로 다시 오지 않는다.
    static_cast<void>(controller.process(tab));
    REQUIRE(left == std::vector<std::u8string> { u8"inner", u8"outer" });

    // 겹친 가지를 한꺼번에 떠나면 안쪽부터다 (a에서 Shift+Tab으로 d까지 돈다).
    left.clear();
    static_cast<void>(controller.process(tab));
    REQUIRE(controller.snapshot().focused.owner == u8"a");
    luil::key_pressed_event back { luil::key_code::tab };
    back.shift = true;
    static_cast<void>(controller.process(back));
    REQUIRE(controller.snapshot().focused.owner == u8"d");
    REQUIRE(left == std::vector<std::u8string> { u8"inner", u8"outer" });

    // 빈 곳을 눌러 초점이 사라진 것도 떠남이다.
    left.clear();
    static_cast<void>(controller.process(luil::access_focus_event { luil::ui_element_id { luil::application_element_kind(2), u8"b" } }));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 250.0f, 280.0f, luil::pointer_button::left }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
    REQUIRE(left == std::vector<std::u8string> { u8"inner", u8"outer" });

    // 창의 비활성화는 떠남이 아니다.
    left.clear();
    static_cast<void>(controller.process(luil::access_focus_event { luil::ui_element_id { luil::application_element_kind(2), u8"a" } }));
    static_cast<void>(controller.process(luil::surface_focus_lost_event {}));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
    REQUIRE(left.empty());

    // 다른 표면(후보 popup)으로 옮겨 간 초점도 떠남이 아니다. 보조 기술이 후보 행에 서도 콤보가 닫히지 않는다.
    auto popup_root { std::make_unique<focus_cell>(luil::ui_element_id { luil::ui_element_kind::root }) };
    popup_root->arrange({ { 0.0f, 0.0f, 100.0f, 60.0f }, 1.0f });
    popup_root->add(make_focus_cell(u8"row", { 0.0f, 0.0f, 100.0f, 24.0f }, true));
    controller.set_surface_trees({ { u8"suggestions", std::make_shared<const luil::ui_tree>(std::move(popup_root)) } });
    static_cast<void>(controller.process(luil::access_focus_event { luil::ui_element_id { luil::application_element_kind(2), u8"a" } }));
    static_cast<void>(controller.process(luil::access_focus_event { luil::ui_element_id { luil::application_element_kind(2), u8"row" }, u8"suggestions" }));
    REQUIRE(controller.snapshot().focused.owner == u8"row");
    REQUIRE(controller.snapshot().focused_surface == u8"suggestions");
    REQUIRE(left.empty());
}

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
