#include "luil/ui/virtual_list_element.h"

#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    struct expand_intent
    {
        std::u8string key {};
        bool expanded { false };
    };

    struct move_intent
    {
        std::u8string key {};
    };

    struct policy_key_intent
    {};

    class recording_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            ++keys;
            return { luil::make_app_action(policy_key_intent {}) };
        }

        int keys { 0 };
    };

    [[nodiscard]] luil::virtual_list_item node(std::u8string key, const int depth, const luil::list_expansion expansion)
    {
        luil::virtual_list_item item {};
        item.key = std::move(key);
        item.label = item.key;
        item.depth = depth;
        item.expansion = expansion;
        return item;
    }

    // src/ (펼침) ⊃ ui/ (접힘), main.cpp / docs/ (펼침, 빈 폴더) / README.md
    [[nodiscard]] luil::virtual_list_config make_config(const std::u8string& cursor)
    {
        luil::virtual_list_config config {};
        config.owner = u8"tree";
        config.row_height = 20.0f;
        config.items = {
            node(u8"src", 0, luil::list_expansion::expanded),
            node(u8"src/ui", 1, luil::list_expansion::collapsed),
            node(u8"src/main.cpp", 1, luil::list_expansion::none),
            node(u8"docs", 0, luil::list_expansion::expanded),
            node(u8"README.md", 0, luil::list_expansion::none),
        };
        config.cursor = cursor;
        config.move = [](const std::u8string& key) { return luil::make_app_action(move_intent { key }); };
        config.set_expanded = [](const std::u8string& key, const bool expanded) { return luil::make_app_action(expand_intent { key, expanded }); };
        return config;
    }

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> published(luil::virtual_list_config config)
    {
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::make_unique<luil::virtual_list_element>(std::move(config)), { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f));
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    template<typename message_type>
    [[nodiscard]] const message_type* message_at(const std::vector<luil::input_action>& actions, const std::size_t index)
    {
        if (index >= actions.size())
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions[index]) };
        return message != nullptr ? message->get<message_type>() : nullptr;
    }

    // 커서를 정한 목록에 Tab으로 초점을 주고 키 하나를 누른다.
    [[nodiscard]] std::vector<luil::input_action> press(const std::u8string& cursor, const luil::key_code key, recording_policy* const policy = nullptr)
    {
        luil::interaction_controller controller { policy };
        controller.set_tree(published(make_config(cursor)));
        static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
        REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"tree" });
        return controller.process(luil::key_pressed_event { key, false, false, false, false, at(10) });
    }
} // namespace

TEST_CASE("A tree list indents rows and puts a triangle only on branches", "[ui][list][tree]")
{
    const std::shared_ptr<const luil::ui_tree> tree { published(make_config(u8"src")) };
    REQUIRE(tree->duplicate_ids().empty());
    const luil::ui_element* const root_expander { tree->find({ luil::ui_element_kind::virtual_list_expander, u8"src" }) };
    const luil::ui_element* const child_expander { tree->find({ luil::ui_element_kind::virtual_list_expander, u8"src/ui" }) };
    REQUIRE(root_expander != nullptr);
    REQUIRE(child_expander != nullptr);
    // 깊이 하나가 한 칸 들여쓰기다.
    REQUIRE(child_expander->bounds().x - root_expander->bounds().x == luil::list_indent_step);
    // 잎에는 삼각형이 없다.
    REQUIRE(tree->find({ luil::ui_element_kind::virtual_list_expander, u8"src/main.cpp" }) == nullptr);
}

TEST_CASE("Pressing a triangle sends the absolute expansion and keeps focus on the list", "[ui][list][tree]")
{
    luil::interaction_controller controller {};
    const std::shared_ptr<const luil::ui_tree> tree { published(make_config(u8"src")) };
    controller.set_tree(tree);
    const luil::rect_f box { tree->find({ luil::ui_element_kind::virtual_list_expander, u8"src/ui" })->bounds() };
    const float x { box.x + box.width / 2.0f };
    const float y { box.y + box.height / 2.0f };
    static_cast<void>(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, at(0) }));
    const auto clicked { controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, at(5) }) };
    REQUIRE(message_at<expand_intent>(clicked, 0) != nullptr);
    REQUIRE(message_at<expand_intent>(clicked, 0)->key == u8"src/ui");
    REQUIRE(message_at<expand_intent>(clicked, 0)->expanded);
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"tree" });
}

TEST_CASE("Left and right walk the hierarchy", "[ui][list][tree][keys]")
{
    SECTION("Left collapses an expanded branch")
    {
        const auto actions { press(u8"src", luil::key_code::arrow_left) };
        REQUIRE(message_at<expand_intent>(actions, 0) != nullptr);
        REQUIRE(message_at<expand_intent>(actions, 0)->expanded == false);
    }

    SECTION("Left on a leaf or a collapsed branch goes to the parent")
    {
        const auto from_leaf { press(u8"src/main.cpp", luil::key_code::arrow_left) };
        REQUIRE(message_at<move_intent>(from_leaf, 0) != nullptr);
        REQUIRE(message_at<move_intent>(from_leaf, 0)->key == u8"src");
        const auto from_branch { press(u8"src/ui", luil::key_code::arrow_left) };
        REQUIRE(message_at<move_intent>(from_branch, 0) != nullptr);
        REQUIRE(message_at<move_intent>(from_branch, 0)->key == u8"src");
    }

    SECTION("Right expands a collapsed branch and enters an expanded one")
    {
        const auto expanded { press(u8"src/ui", luil::key_code::arrow_right) };
        REQUIRE(message_at<expand_intent>(expanded, 0) != nullptr);
        REQUIRE(message_at<expand_intent>(expanded, 0)->expanded);
        const auto entered { press(u8"src", luil::key_code::arrow_right) };
        REQUIRE(message_at<move_intent>(entered, 0) != nullptr);
        REQUIRE(message_at<move_intent>(entered, 0)->key == u8"src/ui");
    }

    SECTION("Right on an expanded empty branch is swallowed")
    {
        recording_policy policy {};
        REQUIRE(press(u8"docs", luil::key_code::arrow_right, &policy).empty());
        REQUIRE(policy.keys == 0);
    }

    SECTION("Keys with nowhere to go flow up")
    {
        recording_policy policy {};
        static_cast<void>(press(u8"README.md", luil::key_code::arrow_left, &policy));
        REQUIRE(policy.keys == 1);
        static_cast<void>(press(u8"README.md", luil::key_code::arrow_right, &policy));
        REQUIRE(policy.keys == 2);
    }
}

TEST_CASE("Rows report their expansion and expand through assistive technology", "[ui][list][tree][accessibility]")
{
    const std::shared_ptr<const luil::ui_tree> tree { published(make_config(u8"src")) };
    const luil::ui_element* const branch { tree->find({ luil::ui_element_kind::virtual_list_row, u8"src/ui" }) };
    const luil::ui_element* const leaf { tree->find({ luil::ui_element_kind::virtual_list_row, u8"src/main.cpp" }) };
    REQUIRE(branch->accessibility().expanded == false);
    REQUIRE(leaf->accessibility().expanded.has_value() == false);

    const std::optional<std::vector<luil::input_action>> expanded { branch->access_actions({ luil::access_command::expand }) };
    REQUIRE(expanded.has_value());
    REQUIRE(message_at<expand_intent>(*expanded, 0) != nullptr);
    REQUIRE(message_at<expand_intent>(*expanded, 0)->key == u8"src/ui");
    REQUIRE(message_at<expand_intent>(*expanded, 0)->expanded);
}

TEST_CASE("A list without the tree gutter keeps the state but draws no triangle", "[ui][list][tree]")
{
    luil::virtual_list_config config { make_config(u8"src") };
    config.tree_gutter = false;
    const std::shared_ptr<const luil::ui_tree> tree { published(std::move(config)) };
    REQUIRE(tree->find({ luil::ui_element_kind::virtual_list_expander, u8"src" }) == nullptr);
    REQUIRE(tree->find({ luil::ui_element_kind::virtual_list_row, u8"src" })->accessibility().expanded == true);
}
