#include "luil/ui/virtual_list_element.h"

#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    struct add_intent
    {
        std::u8string key {};
    };

    struct pick_intent
    {
        std::u8string key {};
    };

    struct scroll_intent
    {
        float delta { 0.0f };
    };

    [[nodiscard]] std::u8string numbered(const std::size_t number)
    {
        const std::string digits { std::to_string(number) };
        std::u8string key { u8"item" };
        key.append(digits.begin(), digits.end());
        return key;
    }

    // 천 줄짜리 다중 선택 목록이다. 다섯 줄마다 하나가 골라져 있다 (모두 이백 줄 — 창 밖에 짓는 한계보다 많다).
    // 창은 이백 픽셀이라 지어지는 행은 앞의 열 줄 남짓과 창에 가까운 고른 행뿐이다.
    [[nodiscard]] luil::virtual_list_config make_config(const std::shared_ptr<int>& built = {})
    {
        luil::virtual_list_config config {};
        config.owner = u8"files";
        config.row_height = 20.0f;
        for (std::size_t index = 0; index < 1000u; ++index)
            config.items.push_back({ numbered(index), numbered(index) });
        config.multiple_selection = true;
        for (std::size_t index = 0; index < 1000u; index += 5u)
            config.selection.push_back(numbered(index));
        config.pick = [](const std::u8string& key, const luil::ui_action_context&) { return luil::make_app_action(pick_intent { key }); };
        config.add_to_selection = [](const std::u8string& key) { return luil::make_app_action(add_intent { key }); };
        config.scroll = [](const float delta) { return luil::make_app_action(scroll_intent { delta }); };
        if (built != nullptr)
            config.build_row = [built](const luil::virtual_list_item&, std::size_t, bool) -> std::unique_ptr<luil::ui_element> {
                ++*built;
                return nullptr;
            };
        return config;
    }

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> published(luil::virtual_list_config config)
    {
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::make_unique<luil::virtual_list_element>(std::move(config)), { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f));
    }

    [[nodiscard]] const luil::ui_element& list_of(const luil::ui_tree& tree)
    {
        const luil::ui_element* const list { tree.find({ luil::ui_element_kind::virtual_list, u8"files" }) };
        REQUIRE(list != nullptr);
        return *list;
    }

    template<typename message_type>
    [[nodiscard]] const message_type* message_at(const std::vector<luil::input_action>& actions, const std::size_t index)
    {
        if (index >= actions.size())
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions[index]) };
        return message != nullptr ? message->get<message_type>() : nullptr;
    }

    [[nodiscard]] luil::virtual_list_item node(std::u8string key, const int depth)
    {
        luil::virtual_list_item item {};
        item.key = std::move(key);
        item.label = item.key;
        item.depth = depth;
        return item;
    }
}

TEST_CASE("A virtual list answers for rows it did not build", "[ui][list][accessibility]")
{
    const std::shared_ptr<const luil::ui_tree> tree { published(make_config()) };
    const luil::access_item_model* const items { list_of(*tree).access_items() };
    REQUIRE(items != nullptr);
    REQUIRE(items->access_item_count() == 1000u);

    // 503번은 창 밖이고 고르지 않았으므로 tree에 없다.
    const luil::ui_element_id distant { luil::virtual_list_row_id(u8"files", u8"item503") };
    REQUIRE(tree->find(distant) == nullptr);
    REQUIRE(items->access_item_id(503u) == distant);
    REQUIRE(items->access_item_index(distant) == 503u);
    REQUIRE(items->access_item_name(503u) == u8"item503");
    // 다른 kind는 같은 키라도 항목이 아니다.
    REQUIRE(items->access_item_index(luil::virtual_list_expander_id(u8"files", u8"item503")).has_value() == false);
    REQUIRE(items->access_item_id(1000u) == luil::ui_element_id {});
}

TEST_CASE("Reading the selection from the model has no offscreen limit", "[ui][list][accessibility]")
{
    const std::shared_ptr<const luil::ui_tree> tree { published(make_config()) };
    const luil::ui_element& list { list_of(*tree) };
    // tree에 선 고른 행은 창 밖 한계에 묶인다.
    REQUIRE(luil::access_selected_items(list).size() < 200u);
    // 모델은 모두를 모델 순서로 답한다.
    const std::vector<luil::ui_element_id> selected { luil::access_selected_ids(list) };
    REQUIRE(selected.size() == 200u);
    REQUIRE(selected.front() == luil::virtual_list_row_id(u8"files", u8"item0"));
    REQUIRE(selected.back() == luil::virtual_list_row_id(u8"files", u8"item995"));
}

TEST_CASE("A probe row answers like a built row without building the app content", "[ui][list][accessibility]")
{
    const std::shared_ptr<int> built { std::make_shared<int>(0) };
    const std::shared_ptr<const luil::ui_tree> tree { published(make_config(built)) };
    const int after_build { *built };
    REQUIRE(after_build > 0);
    const luil::access_item_model& items { *list_of(*tree).access_items() };

    const std::unique_ptr<luil::ui_element> plain { items.access_item_probe(503u) };
    const std::unique_ptr<luil::ui_element> chosen { items.access_item_probe(505u) };
    REQUIRE(plain != nullptr);
    REQUIRE(chosen != nullptr);
    REQUIRE(*built == after_build);
    REQUIRE(plain->accessibility().role == luil::access_role::list_item);
    REQUIRE(plain->accessibility().name == u8"item503");
    REQUIRE(plain->accessibility().selected == false);
    REQUIRE(chosen->accessibility().selected == true);
    REQUIRE(items.access_item_probe(1000u) == nullptr);

    // 실행도 지어진 행과 같은 길이다 — 더하기는 앱의 메시지, 이미 고른 것을 더하기는 할 일 없음이다.
    luil::access_request add { luil::access_command::add_to_selection };
    add.multiple_selection = true;
    const std::optional<std::vector<luil::input_action>> added { luil::plan_access_request(*plain, add) };
    REQUIRE(added.has_value());
    REQUIRE(message_at<add_intent>(*added, 0) != nullptr);
    REQUIRE(message_at<add_intent>(*added, 0)->key == u8"item503");
    const std::optional<std::vector<luil::input_action>> again { luil::plan_access_request(*chosen, add) };
    REQUIRE(again.has_value());
    REQUIRE(again->empty());

    // 고르기는 행의 누름이다.
    luil::access_request select { luil::access_command::select };
    select.multiple_selection = true;
    const std::optional<std::vector<luil::input_action>> picked { luil::plan_access_request(*plain, select) };
    REQUIRE(picked.has_value());
    REQUIRE(message_at<pick_intent>(*picked, 0) != nullptr);
    REQUIRE(message_at<pick_intent>(*picked, 0)->key == u8"item503");
}

TEST_CASE("Realizing an item scrolls it into the window", "[ui][list][accessibility]")
{
    const std::shared_ptr<const luil::ui_tree> tree { published(make_config()) };
    const luil::access_item_model& items { *list_of(*tree).access_items() };
    const std::optional<std::vector<luil::input_action>> distant { items.access_realize(503u) };
    REQUIRE(distant.has_value());
    REQUIRE(message_at<scroll_intent>(*distant, 0) != nullptr);
    // 행 바닥이 창 바닥에 닿는 만큼이다 (키보드 되살리기와 같은 계산).
    REQUIRE(message_at<scroll_intent>(*distant, 0)->delta == 504.0f * 20.0f - 200.0f);
    const std::optional<std::vector<luil::input_action>> close { items.access_realize(2u) };
    REQUIRE(close.has_value());
    REQUIRE(close->empty());

    // 흘릴 길이 없으면 들일 수 없다.
    luil::virtual_list_config fixed { make_config() };
    fixed.scroll = nullptr;
    const std::shared_ptr<const luil::ui_tree> still { published(std::move(fixed)) };
    REQUIRE(list_of(*still).access_items()->access_realize(503u).has_value() == false);
}

TEST_CASE("Item positions count siblings across the whole model", "[ui][list][accessibility]")
{
    const std::shared_ptr<const luil::ui_tree> flat { published(make_config()) };
    const luil::access_item_model& rows { *list_of(*flat).access_items() };
    REQUIRE(rows.access_item_position_of(503u) == luil::access_item_position { 504u, 1000u, 0 });

    // src/ ⊃ ui/, main.cpp — docs/ — README.md
    luil::virtual_list_config config {};
    config.owner = u8"files";
    config.items = { node(u8"src", 0), node(u8"src/ui", 1), node(u8"src/main.cpp", 1), node(u8"docs", 0), node(u8"README.md", 0) };
    const std::shared_ptr<const luil::ui_tree> nested { published(std::move(config)) };
    const luil::access_item_model& tree_items { *list_of(*nested).access_items() };
    REQUIRE(tree_items.access_item_position_of(0u) == luil::access_item_position { 1u, 3u, 0 });
    REQUIRE(tree_items.access_item_position_of(2u) == luil::access_item_position { 2u, 2u, 1 });
    REQUIRE(tree_items.access_item_position_of(3u) == luil::access_item_position { 2u, 3u, 0 });
    REQUIRE(tree_items.access_item_position_of(4u) == luil::access_item_position { 3u, 3u, 0 });
}

TEST_CASE("A built row finds its model container", "[ui][list][accessibility]")
{
    const std::shared_ptr<const luil::ui_tree> tree { published(make_config()) };
    const luil::ui_element_id close { luil::virtual_list_row_id(u8"files", u8"item2") };
    REQUIRE(tree->find(close) != nullptr);
    REQUIRE(luil::access_item_container_of(*tree, close) == &list_of(*tree));
    // 모델이 없는 container의 항목은 모델 container가 없다.
    REQUIRE(luil::access_item_container_of(*tree, { luil::ui_element_kind::virtual_list, u8"files" }) == nullptr);
}

TEST_CASE("Two lists showing the same key on one surface keep distinct row ids", "[ui][list][accessibility]")
{
    // 저장소 계층과 변경 표처럼 두 목록이 같은 경로를 보인다.
    const auto list = [](std::u8string owner) {
        luil::virtual_list_config config {};
        config.owner = std::move(owner);
        config.items = { { u8"trunk/CMakeLists.txt", u8"CMakeLists.txt" }, { u8"README.md", u8"README.md" } };
        return std::make_unique<luil::virtual_list_element>(std::move(config));
    };
    auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { luil::application_element_kind(70), u8"pair" }, luil::stack_config { .direction = luil::stack_direction::row }) };
    row->add_flexible(list(u8"tree"));
    row->add_flexible(list(u8"changes"));
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(row), { 0.0f, 0.0f, 400.0f, 200.0f }, 1.0f) };

    REQUIRE(tree.duplicate_ids().empty());
    const luil::ui_element_id in_tree { luil::virtual_list_row_id(u8"tree", u8"trunk/CMakeLists.txt") };
    const luil::ui_element_id in_changes { luil::virtual_list_row_id(u8"changes", u8"trunk/CMakeLists.txt") };
    REQUIRE(in_tree != in_changes);
    REQUIRE(tree.find(in_tree) != nullptr);
    REQUIRE(tree.find(in_changes) != nullptr);

    // 각 목록의 모델은 제 행만 제 항목으로 안다. 키가 같아도 다른 목록의 행은 아니다.
    const luil::access_item_model& tree_items { *tree.find({ luil::ui_element_kind::virtual_list, u8"tree" })->access_items() };
    REQUIRE(tree_items.access_item_index(in_tree) == 0u);
    REQUIRE(tree_items.access_item_index(in_changes).has_value() == false);
    REQUIRE(luil::access_item_container_of(tree, in_changes) == tree.find({ luil::ui_element_kind::virtual_list, u8"changes" }));
    // 목록 owner가 다른 목록 owner의 앞부분이어도 갈린다 (`tree`와 `tree2`).
    REQUIRE(tree_items.access_item_index(luil::virtual_list_row_id(u8"tree2", u8"README.md")).has_value() == false);
}
