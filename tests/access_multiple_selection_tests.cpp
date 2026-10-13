#include "luil/ui/accessibility.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/virtual_list_element.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 여러 항목을 함께 고르는 선택 container다 (`access_info::multiple_selection`).
// platform을 모르는 층의 몫 — 명령의 계획, 발행본 대조의 선택 알림, 가상 목록의 다중 선택 — 을 본다.
namespace {
    struct pick_intent
    {
        std::u8string key {};
        bool control { false };
        bool shift { false };
    };

    struct add_intent
    {
        std::u8string key {};
    };

    struct remove_intent
    {
        std::u8string key {};
    };

    [[nodiscard]] std::vector<luil::virtual_list_item> items(const std::size_t count)
    {
        std::vector<luil::virtual_list_item> result {};
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::u8string key { static_cast<char8_t>(u8'a' + index) };
            result.push_back({ key, key });
        }
        return result;
    }

    // 다섯 줄짜리 다중 선택 가상 목록이다.
    [[nodiscard]] luil::ui_tree make_tree(std::vector<std::u8string> selection, const bool multiple = true, const std::size_t count = 5u)
    {
        luil::virtual_list_config config {};
        config.owner = u8"files";
        config.row_height = 20.0f;
        config.items = items(count);
        config.multiple_selection = multiple;
        config.selection = std::move(selection);
        config.pick = [](const std::u8string& key, const luil::ui_action_context& context) { return luil::make_app_action(pick_intent { key, context.control, context.shift }); };
        config.add_to_selection = [](const std::u8string& key) { return luil::make_app_action(add_intent { key }); };
        config.remove_from_selection = [](const std::u8string& key) { return luil::make_app_action(remove_intent { key }); };
        return luil::make_arranged_tree(std::make_unique<luil::virtual_list_element>(std::move(config)), { 0.0f, 0.0f, 200.0f, 400.0f }, 1.0f);
    }

    [[nodiscard]] const luil::ui_element& row(const luil::ui_tree& tree, const std::u8string& key)
    {
        const luil::ui_element* const found { tree.find({ luil::ui_element_kind::virtual_list_row, key }) };
        REQUIRE(found != nullptr);
        return *found;
    }

    template<typename intent_type>
    [[nodiscard]] const intent_type* intent_of(const std::optional<std::vector<luil::input_action>>& actions)
    {
        REQUIRE(actions.has_value());
        REQUIRE(actions->size() == 1u);
        const auto* const message { std::get_if<luil::app_message>(&actions->front()) };
        REQUIRE(message != nullptr);
        return message->get<intent_type>();
    }

    // 내 것이지만 할 일이 없다 (빈 계획).
    [[nodiscard]] bool nothing_to_do(const std::optional<std::vector<luil::input_action>>& actions)
    {
        return actions.has_value() && actions->empty();
    }

    [[nodiscard]] luil::access_request request(const luil::access_command command, const bool multiple = true)
    {
        luil::access_request value { command };
        value.multiple_selection = multiple;
        return value;
    }

    [[nodiscard]] std::vector<luil::access_change> diff(const std::vector<std::u8string>& before, const std::vector<std::u8string>& after, const std::size_t count = 5u)
    {
        return luil::diff_access_snapshots(luil::make_access_snapshot(make_tree(before, true, count)), luil::make_access_snapshot(make_tree(after, true, count)));
    }
} // namespace

TEST_CASE("A multiple selection list announces itself and selects every chosen row", "[ui][accessibility][multi-select]")
{
    const luil::ui_tree tree { make_tree({ u8"a", u8"c", u8"d" }) };
    const luil::ui_element& list { *tree.find({ luil::ui_element_kind::virtual_list, u8"files" }) };
    REQUIRE(list.accessibility().multiple_selection);
    REQUIRE(luil::access_multiple_selection(tree, row(tree, u8"b").id()));

    const std::vector<const luil::ui_element*> selected { luil::access_selected_items(list) };
    REQUIRE(selected.size() == 3u);
    REQUIRE(selected[0]->id().owner == u8"a");
    REQUIRE(selected[1]->id().owner == u8"c");
    REQUIRE(selected[2]->id().owner == u8"d");
    REQUIRE(row(tree, u8"b").accessibility().selected == false);

    // 꺼진 목록은 지금까지의 단일 선택이다 — `selection`을 읽지 않는다.
    const luil::ui_tree single { make_tree({ u8"a", u8"c" }, false) };
    REQUIRE(single.find({ luil::ui_element_kind::virtual_list, u8"files" })->accessibility().multiple_selection == false);
    REQUIRE(luil::access_multiple_selection(single, { luil::ui_element_kind::virtual_list_row, u8"a" }) == false);
    REQUIRE(luil::access_selected_items(*single.find({ luil::ui_element_kind::virtual_list, u8"files" })).empty());
}

TEST_CASE("Adding and removing a row reach the app as its own commands", "[ui][accessibility][multi-select]")
{
    const luil::ui_tree tree { make_tree({ u8"a" }) };

    // 고르지 않은 행의 더하기는 앱의 더하기다. 이미 골라진 행의 더하기는 할 일이 없다.
    const auto* const added { intent_of<add_intent>(luil::plan_access_request(row(tree, u8"b"), request(luil::access_command::add_to_selection))) };
    REQUIRE(added != nullptr);
    REQUIRE(added->key == u8"b");
    REQUIRE(nothing_to_do(luil::plan_access_request(row(tree, u8"a"), request(luil::access_command::add_to_selection))));

    const auto* const removed { intent_of<remove_intent>(luil::plan_access_request(row(tree, u8"a"), request(luil::access_command::remove_from_selection))) };
    REQUIRE(removed != nullptr);
    REQUIRE(removed->key == u8"a");
    REQUIRE(nothing_to_do(luil::plan_access_request(row(tree, u8"b"), request(luil::access_command::remove_from_selection))));

    // 다중 선택의 고르기는 "이것 하나만"이라 이미 골라진 행도 앱에 간다 (행의 누름, 수식 키 없음).
    const auto* const picked { intent_of<pick_intent>(luil::plan_access_request(row(tree, u8"a"), request(luil::access_command::select))) };
    REQUIRE(picked != nullptr);
    REQUIRE(picked->key == u8"a");
    REQUIRE(picked->control == false);
    // 단일 선택으로 물으면 이미 골라진 것은 할 일이 없다.
    REQUIRE(nothing_to_do(luil::plan_access_request(row(tree, u8"a"), request(luil::access_command::select, false))));
}

TEST_CASE("Rows without app handlers cannot be added or removed", "[ui][accessibility][multi-select]")
{
    // 받는 앱이 없으면 클릭(뒤집기)으로 대신하지 않는다 — 할 수 없는 일이다.
    class item final : public luil::ui_element
    {
    public:
        explicit item(const bool selected)
            : ui_element { luil::ui_element_id { luil::application_element_kind(0), u8"item" } }
            , selected_ { selected }
        {
            set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(add_intent {}) }; });
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}

        [[nodiscard]] luil::access_info accessibility() const override
        {
            return { .role = luil::access_role::list_item, .name = u8"item", .selected = selected_ };
        }

    private:
        bool selected_ { false };
    };

    const item unselected { false };
    REQUIRE(luil::plan_access_request(unselected, request(luil::access_command::add_to_selection)).has_value() == false);
    const item selected { true };
    REQUIRE(luil::plan_access_request(selected, request(luil::access_command::remove_from_selection)).has_value() == false);

    // 앱이 받겠다고 하면 그것이 답한다.
    item handled { false };
    handled.set_access_handler([](const luil::access_request& value) -> std::optional<std::vector<luil::input_action>> {
        if (value.command == luil::access_command::add_to_selection)
            return std::vector<luil::input_action> { luil::make_app_action(add_intent { u8"handled" }) };
        return std::nullopt;
    });
    REQUIRE(intent_of<add_intent>(luil::plan_access_request(handled, request(luil::access_command::add_to_selection)))->key == u8"handled");
    // 답하지 않은 명령은 지금까지의 기본(클릭)으로 물러선다.
    REQUIRE(intent_of<add_intent>(luil::plan_access_request(handled, request(luil::access_command::invoke)))->key.empty());
}

TEST_CASE("A pressed row hands its modifiers to the pick factory", "[ui][accessibility][multi-select]")
{
    const luil::ui_tree tree { make_tree({}) };
    const luil::ui_action* const click { row(tree, u8"c").action(luil::ui_trigger::left_click) };
    REQUIRE(click != nullptr);
    luil::ui_action_context context { row(tree, u8"c").id(), 10.0f, 50.0f };
    context.control = true;
    const std::vector<luil::input_action> actions { (*click)(context) };
    REQUIRE(actions.size() == 1u);
    const auto* const picked { std::get<luil::app_message>(actions.front()).get<pick_intent>() };
    REQUIRE(picked != nullptr);
    REQUIRE(picked->key == u8"c");
    REQUIRE(picked->control);
}

TEST_CASE("Selection changes in a multiple selection list are announced per item", "[ui][accessibility][multi-select]")
{
    // 셋을 더하면 항목마다 더해짐이다.
    std::vector<luil::access_change> changes { diff({}, { u8"a", u8"b", u8"c" }) };
    REQUIRE(changes.size() == 3u);
    for (const luil::access_change& change : changes)
        REQUIRE(change.kind == luil::access_change_kind::added_to_selection);
    REQUIRE(changes[0].id.owner == u8"a");
    REQUIRE(changes[2].id.owner == u8"c");

    // 하나를 빼면 빠짐 하나다.
    changes = diff({ u8"a", u8"b", u8"c" }, { u8"a", u8"c" });
    REQUIRE(changes.size() == 1u);
    REQUIRE(changes[0].kind == luil::access_change_kind::removed_from_selection);
    REQUIRE(changes[0].id.owner == u8"b");

    // 이미 골라진 것이 있는데 하나를 더하면 더해짐이다.
    changes = diff({ u8"a" }, { u8"a", u8"d" });
    REQUIRE(changes.size() == 1u);
    REQUIRE(changes[0].kind == luil::access_change_kind::added_to_selection);

    // 클릭으로 하나만 고른 것은 그 항목의 골라짐 하나다 (나머지가 풀린 것을 함께 말한다).
    changes = diff({ u8"a", u8"c" }, { u8"e" });
    REQUIRE(changes.size() == 1u);
    REQUIRE(changes[0].kind == luil::access_change_kind::selected);
    REQUIRE(changes[0].id.owner == u8"e");

    // 골라져 있던 하나만 남기고 나머지를 풀어도 그 하나의 골라짐이다.
    changes = diff({ u8"b", u8"e" }, { u8"e" });
    REQUIRE(changes.size() == 1u);
    REQUIRE(changes[0].kind == luil::access_change_kind::selected);
    REQUIRE(changes[0].id.owner == u8"e");
}

TEST_CASE("Many selection changes at once invalidate the selection", "[ui][accessibility][multi-select]")
{
    std::vector<std::u8string> everything {};
    for (const luil::virtual_list_item& item : items(22u))
        everything.push_back(item.key);
    const std::vector<luil::access_change> changes { diff({}, everything, 22u) };
    REQUIRE(changes.size() == 1u);
    REQUIRE(changes[0].kind == luil::access_change_kind::selection_invalidated);
    REQUIRE(changes[0].id == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });
}
