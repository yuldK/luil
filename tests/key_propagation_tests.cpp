#include "luil/ui/ui_interaction.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    // test 정의 kind다.
    constexpr luil::ui_element_kind kind_panel { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_button { luil::application_element_kind(1) };
    constexpr luil::ui_element_kind kind_query_input { luil::application_element_kind(2) };

    constexpr luil::text_input_target query_target { 3 };

    struct handled_intent
    {
        std::u8string by {};
    };

    class test_panel final : public luil::ui_element
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

    // 초점을 받는 최소 텍스트 칸이다.
    class test_text_input final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }

        [[nodiscard]] std::optional<std::size_t> offset_at(const float, const luil::text_measurer&) const override
        {
            return 0u;
        }

        [[nodiscard]] std::optional<luil::text_input_snapshot> text_input() const override
        {
            return luil::text_input_snapshot { u8"abc", 0u, 0u };
        }
    };

    class recording_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            if (kind == kind_query_input)
                return query_target;
            return std::nullopt;
        }

        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request& request) const override
        {
            last_edit = request;
            return luil::input_action { luil::app_message { handled_intent { u8"text" } } };
        }

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            ++keys;
            return { luil::input_action { luil::app_message { handled_intent { u8"policy" } } } };
        }

        mutable luil::text_edit_request last_edit {};
        int keys { 0 };
    };

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] luil::key_pressed_event select_all_key(const int milliseconds)
    {
        luil::key_pressed_event event { luil::key_code::key_a };
        event.control = true;
        event.time = at(milliseconds);
        return event;
    }

    [[nodiscard]] const handled_intent* intent_at(const std::vector<luil::input_action>& actions, const std::size_t index)
    {
        if (index >= actions.size())
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions[index]) };
        return message != nullptr ? message->get<handled_intent>() : nullptr;
    }

    // Ctrl+A만 받는 처리기다. 다른 키는 소비하지 않는다.
    [[nodiscard]] luil::key_handler select_all_handler(std::u8string by, int* const calls = nullptr)
    {
        return [by = std::move(by), calls](const luil::key_pressed_event& event) -> std::optional<std::vector<luil::input_action>> {
            if (calls != nullptr)
                ++*calls;
            if (event.key != luil::key_code::key_a || event.control == false)
                return std::nullopt;
            return std::vector<luil::input_action> { luil::input_action { luil::app_message { handled_intent { by } } } };
        };
    }

    [[nodiscard]] luil::ui_action no_op_action()
    {
        return [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
    }

    struct row_tree
    {
        std::unique_ptr<test_panel> root {};
        test_panel* list {};
        test_panel* row {};
    };

    // root ⊃ 목록 ⊃ 행 ⊃ 단추. 초점은 단추에 선다 — 행 안의 부품에 초점이 있는 자리다.
    [[nodiscard]] row_tree make_row_tree()
    {
        row_tree tree {};
        tree.root = std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root });
        tree.root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        auto list { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"list" }) };
        list->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
        auto row { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"row" }) };
        row->arrange({ { 0.0f, 0.0f, 200.0f, 24.0f }, 1.0f });
        auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"button" }) };
        button->arrange({ { 170.0f, 2.0f, 20.0f, 20.0f }, 1.0f });
        button->set_action(luil::ui_trigger::left_click, no_op_action());
        tree.list = list.get();
        tree.row = row.get();
        row->add(std::move(button));
        list->add(std::move(row));
        tree.root->add(std::move(list));
        return tree;
    }

    // 단추에 Tab으로 초점을 준다.
    void focus_button(luil::interaction_controller& controller)
    {
        static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
        REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"button" });
    }
}

TEST_CASE("A tree answers the path from an element up to the root", "[ui][interaction][keys]")
{
    row_tree built { make_row_tree() };
    const luil::ui_tree tree { std::move(built.root) };

    const std::vector<const luil::ui_element*> path { tree.ancestry_of({ kind_button, u8"button" }) };
    REQUIRE(path.size() == 4u);
    REQUIRE(path[0]->id() == luil::ui_element_id { kind_button, u8"button" });
    REQUIRE(path[1]->id() == luil::ui_element_id { kind_panel, u8"row" });
    REQUIRE(path[2]->id() == luil::ui_element_id { kind_panel, u8"list" });
    REQUIRE(path[3] == tree.root());

    // 없는 id는 길이 없다.
    REQUIRE(tree.ancestry_of({ kind_button, u8"missing" }).empty());
}

TEST_CASE("A hidden branch has no path because focus cannot stand there", "[ui][interaction][keys]")
{
    row_tree built { make_row_tree() };
    built.row->set_visible(false);
    const luil::ui_tree tree { std::move(built.root) };
    REQUIRE(tree.ancestry_of({ kind_button, u8"button" }).empty());
}

TEST_CASE("A key the focused part does not consume reaches the list around it", "[ui][interaction][keys]")
{
    row_tree built { make_row_tree() };
    built.list->set_key_handler(select_all_handler(u8"list"));
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(built.root)));
    focus_button(controller);

    const std::vector<luil::input_action> actions { controller.process(select_all_key(10)) };
    REQUIRE(intent_at(actions, 0) != nullptr);
    REQUIRE(intent_at(actions, 0)->by == u8"list");
    // 목록이 소비했으므로 앱 정책까지 가지 않는다.
    REQUIRE(policy.keys == 0);
    // 초점은 그대로다.
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"button" });
}

TEST_CASE("The innermost handler that answers stops the key", "[ui][interaction][keys]")
{
    row_tree built { make_row_tree() };
    int list_calls { 0 };
    built.row->set_key_handler(select_all_handler(u8"row"));
    built.list->set_key_handler(select_all_handler(u8"list", &list_calls));
    luil::interaction_controller controller {};
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(built.root)));
    focus_button(controller);

    const std::vector<luil::input_action> actions { controller.process(select_all_key(10)) };
    REQUIRE(intent_at(actions, 0) != nullptr);
    REQUIRE(intent_at(actions, 0)->by == u8"row");
    REQUIRE(list_calls == 0);
}

TEST_CASE("An empty answer consumes the key and a missing answer passes it up", "[ui][interaction][keys]")
{
    row_tree built { make_row_tree() };
    int list_calls { 0 };
    // 행은 F5를 삼키고(빈 목록), 나머지는 흘린다.
    built.row->set_key_handler([](const luil::key_pressed_event& event) -> std::optional<std::vector<luil::input_action>> {
        if (event.key == luil::key_code::f5)
            return std::vector<luil::input_action> {};
        return std::nullopt;
    });
    built.list->set_key_handler(select_all_handler(u8"list", &list_calls));
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(built.root)));
    focus_button(controller);

    REQUIRE(controller.process(luil::key_pressed_event { luil::key_code::f5, false, false, false, false, at(10) }).empty());
    REQUIRE(list_calls == 0);
    REQUIRE(policy.keys == 0);

    const std::vector<luil::input_action> actions { controller.process(select_all_key(20)) };
    REQUIRE(intent_at(actions, 0) != nullptr);
    REQUIRE(intent_at(actions, 0)->by == u8"list");
    REQUIRE(list_calls == 1);
}

TEST_CASE("A key nobody consumes falls back to the app policy", "[ui][interaction][keys]")
{
    row_tree built { make_row_tree() };
    int list_calls { 0 };
    built.list->set_key_handler(select_all_handler(u8"list", &list_calls));
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(built.root)));
    focus_button(controller);

    const std::vector<luil::input_action> actions { controller.process(luil::key_pressed_event { luil::key_code::f5, false, false, false, false, at(10) }) };
    REQUIRE(list_calls == 1);
    REQUIRE(policy.keys == 1);
    REQUIRE(intent_at(actions, 0) != nullptr);
    REQUIRE(intent_at(actions, 0)->by == u8"policy");
}

TEST_CASE("A disabled ancestor is skipped and the one above it is still asked", "[ui][interaction][keys]")
{
    row_tree built { make_row_tree() };
    int row_calls { 0 };
    built.row->set_key_handler(select_all_handler(u8"row", &row_calls));
    built.row->set_enabled(false);
    built.list->set_key_handler(select_all_handler(u8"list"));
    luil::interaction_controller controller {};
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(built.root)));
    focus_button(controller);

    const std::vector<luil::input_action> actions { controller.process(select_all_key(10)) };
    REQUIRE(row_calls == 0);
    REQUIRE(intent_at(actions, 0) != nullptr);
    REQUIRE(intent_at(actions, 0)->by == u8"list");
}

TEST_CASE("A focused text box takes Ctrl+A before the list around it", "[ui][interaction][keys]")
{
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto list { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, u8"list" }) };
    list->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    int list_calls { 0 };
    list->set_key_handler(select_all_handler(u8"list", &list_calls));
    auto box { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input, u8"query" }) };
    box->arrange({ { 0.0f, 0.0f, 200.0f, 24.0f }, 1.0f });
    box->set_action(luil::ui_trigger::left_click, no_op_action());
    list->add(std::move(box));
    root->add(std::move(list));

    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 12.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_released_event { 20.0f, 12.0f, luil::pointer_button::left, at(5) }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input, u8"query" });

    const std::vector<luil::input_action> actions { controller.process(select_all_key(10)) };
    REQUIRE(policy.last_edit.command == luil::text::text_edit_command::select_all);
    REQUIRE(intent_at(actions, 0) != nullptr);
    REQUIRE(intent_at(actions, 0)->by == u8"text");
    REQUIRE(list_calls == 0);
}

TEST_CASE("Ctrl Tab reaches app commands without moving keyboard focus", "[ui][interaction][keys]")
{
    row_tree built { make_row_tree() };
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(built.root)));
    focus_button(controller);
    const auto before { controller.snapshot().focused };
    for (const bool backward : { false, true })
    {
        const auto actions { controller.process(luil::key_pressed_event { .key = luil::key_code::tab, .control = true, .shift = backward }) };
        REQUIRE(intent_at(actions, 0) != nullptr);
        REQUIRE(intent_at(actions, 0)->by == u8"policy");
        REQUIRE(controller.snapshot().focused == before);
    }
}

TEST_CASE("Shift F10 opens the nearest enabled context menu at the focused control", "[ui][interaction][keys]")
{
    row_tree built { make_row_tree() };
    built.row->set_action(luil::ui_trigger::right_click, [](const luil::ui_action_context& context) {
        REQUIRE(context.x == 100.0f);
        REQUIRE(context.y == 12.0f);
        return std::vector<luil::input_action> { luil::make_app_action(handled_intent { u8"context" }) };
    });
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(built.root)));
    focus_button(controller);
    const auto actions { controller.process(luil::key_pressed_event { .key = luil::key_code::f10, .shift = true }) };
    REQUIRE(intent_at(actions, 0) != nullptr);
    REQUIRE(intent_at(actions, 0)->by == u8"context");
    REQUIRE(policy.keys == 0);
}
