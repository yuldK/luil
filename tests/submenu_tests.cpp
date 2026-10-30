#include "luil/ui/menu_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace {
    class submenu_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::menu_kinds> menu() const override
        {
            return luil::menu_kinds { luil::ui_element_kind::menu, luil::ui_element_kind::menu_item };
        }
    };

    // 하위 메뉴를 담는 항목은 `item{깊이}`, 잎 항목은 `leaf{깊이}`다. 깊이마다 키가 달라 강조가 어느 메뉴의 어느
    // 항목에 섰는지 가려진다.
    [[nodiscard]] std::u8string item_key(const std::size_t depth)
    {
        return u8"item" + std::u8string(1, static_cast<char8_t>(u8'0' + depth));
    }

    [[nodiscard]] std::u8string leaf_key(const std::size_t depth)
    {
        return u8"leaf" + std::u8string(1, static_cast<char8_t>(u8'0' + depth));
    }

    [[nodiscard]] luil::ui_element_id item_id(std::u8string key)
    {
        return luil::ui_element_id { luil::ui_element_kind::menu_item, std::move(key) };
    }

    [[nodiscard]] luil::menu_config submenu_config(const std::size_t depth, std::u8string opened = {})
    {
        luil::menu_config config {};
        config.depth = depth;
        config.parent_surface = depth == 2 ? u8"child" : u8"";
        config.anchor_key = depth > 0 ? item_key(depth - 1) : std::u8string {};
        config.opened_key = std::move(opened);
        config.items.push_back({ .key = item_key(depth), .label = u8"Parent", .children = { { u8"nested", u8"Nested" } } });
        config.items.push_back({ .key = leaf_key(depth), .label = u8"Leaf" });
        config.select = [](const std::u8string&) { return luil::input_action { luil::app_message { 0 } }; };
        config.open_submenu = [depth](const std::u8string&) { return luil::input_action { luil::app_message { static_cast<int>(depth + 1) } }; };
        // 닫음은 `-(깊이 + 1)`이다. 어느 메뉴의 `close_submenu`가 불렸는지 가려진다.
        config.close_submenu = [depth](const luil::ui_action_context&) { return std::vector<luil::input_action> { luil::app_message { -static_cast<int>(depth) - 1 } }; };
        return config;
    }

    std::shared_ptr<const luil::ui_tree> submenu_tree(const std::size_t depth, std::u8string opened = {})
    {
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(
            std::make_unique<luil::menu_element>(submenu_config(depth, std::move(opened))), { 0.0f, 0.0f, 180.0f, 60.0f }, 1.0f));
    }

    // 깊이 0 메뉴에 자식이 있는 형제 `other`를 셋째 줄로 더한다. 여는 메시지가 연 항목의 키를 싣는다.
    std::shared_ptr<const luil::ui_tree> sibling_tree(std::u8string opened)
    {
        auto config { submenu_config(0, std::move(opened)) };
        config.items.push_back({ .key = u8"other", .label = u8"Other", .children = { { u8"nested", u8"Nested" } } });
        config.open_submenu = [](const std::u8string& key) { return luil::input_action { luil::app_message { key } }; };
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::make_unique<luil::menu_element>(std::move(config)), { 0.0f, 0.0f, 180.0f, 90.0f }, 1.0f));
    }

    template<typename message_type>
    [[nodiscard]] const message_type* message_of(const luil::input_action& action)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        return message != nullptr ? message->get<message_type>() : nullptr;
    }

    [[nodiscard]] luil::pointer_moved_event move_to(const float y, const int ms, std::u8string surface = {})
    {
        return luil::pointer_moved_event { .x = 30.0f, .y = y, .time = std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { ms }, .surface = std::move(surface) };
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int ms)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { ms };
    }

    // 항목 줄은 위 여백 4px 뒤로 26px씩이다. 첫 줄·둘째 줄·셋째 줄의 가운데다.
    constexpr float first_row { 17.0f };
    constexpr float second_row { 43.0f };
    constexpr float third_row { 69.0f };

    // 메뉴 옆에 도구 막대 단추를 둔 화면이다. 단추의 owner가 하위 메뉴 항목의 키와 같다.
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

    [[nodiscard]] luil::key_pressed_event key(const luil::key_code code)
    {
        return luil::key_pressed_event { code };
    }
}

TEST_CASE("Submenu hover waits for its deadline and sends one open intent", "[ui][menu][submenu]")
{
    luil::interaction_controller controller {};
    controller.set_tree(submenu_tree(0));
    static_cast<void>(controller.process(luil::pointer_moved_event { 30.0f, 15.0f }));
    REQUIRE(controller.next_deadline() == std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { 300 });
    REQUIRE(controller.advance(std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { 299 }).empty());
    REQUIRE(controller.advance(std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { 300 }).size() == 1u);
    REQUIRE(controller.next_deadline().has_value() == false);
}

TEST_CASE("Hovering a non-menu element that shares a submenu key starts no submenu timer", "[ui][menu][submenu]")
{
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    auto menu { std::make_unique<luil::menu_element>(submenu_config(0)) };
    auto button { std::make_unique<test_panel>(luil::ui_element_id { luil::application_element_kind(0), item_key(0) }) };
    button->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; });
    root->add(std::move(menu));
    root->add(std::move(button));
    root->arrange({ { 0.0f, 0.0f, 400.0f, 60.0f }, 1.0f });
    root->children()[0]->arrange({ { 0.0f, 0.0f, 180.0f, 60.0f }, 1.0f });
    root->children()[1]->arrange({ { 200.0f, 0.0f, 40.0f, 40.0f }, 1.0f });

    luil::interaction_controller controller {};
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));
    static_cast<void>(controller.process(luil::pointer_moved_event { 210.0f, 15.0f }));
    REQUIRE(controller.snapshot().hovered.owner == item_key(0));
    REQUIRE(controller.next_deadline().has_value() == false);
    // 메뉴 항목 위에서는 시계가 그대로 선다.
    static_cast<void>(controller.process(luil::pointer_moved_event { 30.0f, 15.0f }));
    REQUIRE(controller.next_deadline().has_value());
}

TEST_CASE("The deepest submenu handles Escape and restores its parent highlight", "[ui][menu][submenu]")
{
    submenu_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(submenu_tree(0));
    controller.set_surface_trees({ { u8"child", submenu_tree(1) }, { u8"grandchild", submenu_tree(2) } });
    const auto actions { controller.process(key(luil::key_code::escape)) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(controller.snapshot().menu_surface == u8"child");
    REQUIRE(controller.snapshot().menu_highlight == item_id(item_key(1)));
}

TEST_CASE("A submenu closed outside the key path still restores its parent highlight", "[ui][menu][submenu]")
{
    // 플랫폼은 Esc를 가장 깊은 popup의 닫힘으로 삼킨다. controller가 보는 것은 tree가 사라진 것뿐이다.
    submenu_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(submenu_tree(0));
    controller.set_surface_trees({ { u8"child", submenu_tree(1) }, { u8"grandchild", submenu_tree(2) } });
    controller.set_surface_trees({ { u8"child", submenu_tree(1) } });
    REQUIRE(controller.snapshot().menu_surface == u8"child");
    REQUIRE(controller.snapshot().menu_highlight == item_id(item_key(1)));
    controller.set_surface_trees({});
    REQUIRE(controller.snapshot().menu_surface.empty());
    REQUIRE(controller.snapshot().menu_highlight == item_id(item_key(0)));
}

TEST_CASE("A keyboard-opened submenu highlights its first item and the first Down moves to the second", "[ui][menu][submenu]")
{
    submenu_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(submenu_tree(0));
    static_cast<void>(controller.process(key(luil::key_code::arrow_down)));
    REQUIRE(controller.snapshot().menu_highlight == item_id(item_key(0)));
    REQUIRE(controller.process(key(luil::key_code::arrow_right)).size() == 1u);

    controller.set_tree(submenu_tree(0, item_key(0)));
    controller.set_surface_trees({ { u8"child", submenu_tree(1) } });
    REQUIRE(controller.snapshot().menu_surface == u8"child");
    REQUIRE(controller.snapshot().menu_highlight == item_id(item_key(1)));
    static_cast<void>(controller.process(key(luil::key_code::arrow_down)));
    REQUIRE(controller.snapshot().menu_highlight == item_id(leaf_key(1)));

    // ←는 한 단계만 닫고 강조를 부모 항목으로 돌린다.
    REQUIRE(controller.process(key(luil::key_code::arrow_left)).size() == 1u);
    REQUIRE(controller.snapshot().menu_surface.empty());
    REQUIRE(controller.snapshot().menu_highlight == item_id(item_key(0)));
    controller.set_tree(submenu_tree(0));
    controller.set_surface_trees({});
    REQUIRE(controller.snapshot().menu_highlight == item_id(item_key(0)));
}

TEST_CASE("A hover-opened submenu starts unhighlighted and its first Down lands on the first item", "[ui][menu][submenu]")
{
    submenu_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(submenu_tree(0, item_key(0)));
    controller.set_surface_trees({ { u8"child", submenu_tree(1) } });
    REQUIRE(controller.snapshot().menu_highlight == luil::ui_element_id {});
    // 강조 없는 Enter는 아무것도 실행하지 않는다.
    REQUIRE(controller.process(key(luil::key_code::enter)).empty());
    static_cast<void>(controller.process(key(luil::key_code::arrow_down)));
    REQUIRE(controller.snapshot().menu_surface == u8"child");
    REQUIRE(controller.snapshot().menu_highlight == item_id(item_key(1)));
}

TEST_CASE("Right on a parent whose submenu is already open steps into the child", "[ui][menu][submenu]")
{
    submenu_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(submenu_tree(0));
    static_cast<void>(controller.process(key(luil::key_code::arrow_down)));
    REQUIRE(controller.snapshot().menu_highlight == item_id(item_key(0)));
    // hover가 자식을 열었다. 강조는 아직 부모 항목에 있다.
    controller.set_tree(submenu_tree(0, item_key(0)));
    controller.set_surface_trees({ { u8"child", submenu_tree(1) } });
    REQUIRE(controller.snapshot().menu_surface.empty());
    REQUIRE(controller.process(key(luil::key_code::arrow_right)).empty());
    REQUIRE(controller.snapshot().menu_surface == u8"child");
    REQUIRE(controller.snapshot().menu_highlight == item_id(item_key(1)));
}

TEST_CASE("Right on a parent whose submenu tree is missing sends the open intent again", "[ui][menu][submenu]")
{
    submenu_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(submenu_tree(0, item_key(0)));
    static_cast<void>(controller.process(key(luil::key_code::arrow_down)));
    REQUIRE(controller.snapshot().menu_highlight == item_id(item_key(0)));
    REQUIRE(controller.process(key(luil::key_code::arrow_right)).size() == 1u);
}

TEST_CASE("Resting on a leaf sibling past the hover delay closes the open submenu once", "[ui][menu][submenu]")
{
    submenu_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(submenu_tree(0, item_key(0)));
    controller.set_surface_trees({ { u8"child", submenu_tree(1) } });
    static_cast<void>(controller.process(move_to(second_row, 0)));
    REQUIRE(controller.next_deadline() == at(300));
    // 자식으로 가는 길에 형제를 지나는 이동은 시계를 다시 센다. 멎은 뒤에야 닫는다.
    static_cast<void>(controller.process(move_to(second_row + 2.0f, 100)));
    REQUIRE(controller.next_deadline() == at(400));
    REQUIRE(controller.advance(at(399)).empty());
    const auto actions { controller.advance(at(400)) };
    REQUIRE(actions.size() == 1u);
    // 열린 자식(깊이 1)의 닫음이다. ←·Esc가 부르는 그 메시지다.
    const auto* const tag { message_of<int>(actions.front()) };
    REQUIRE(tag != nullptr);
    REQUIRE(*tag == -2);
    REQUIRE(controller.next_deadline().has_value() == false);
    REQUIRE(controller.advance(at(1000)).empty());

    // 앱이 열림 경로를 비웠다. 강조는 앵커로 돌아가지 않는다 — 포인터가 선 형제가 hover로 그려진다.
    controller.set_tree(submenu_tree(0));
    controller.set_surface_trees({});
    REQUIRE(controller.snapshot().menu_surface.empty());
    REQUIRE(controller.snapshot().menu_highlight != item_id(item_key(0)));
    REQUIRE(controller.snapshot().hovered == item_id(leaf_key(0)));
    REQUIRE(controller.next_deadline().has_value() == false);
}

TEST_CASE("Resting on a sibling with children past the hover delay switches to its submenu", "[ui][menu][submenu]")
{
    submenu_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(sibling_tree(item_key(0)));
    controller.set_surface_trees({ { u8"child", submenu_tree(1) } });
    static_cast<void>(controller.process(move_to(third_row, 0)));
    REQUIRE(controller.next_deadline() == at(300));
    REQUIRE(controller.advance(at(299)).empty());
    const auto actions { controller.advance(at(300)) };
    REQUIRE(actions.size() == 1u);
    const auto* const opened { message_of<std::u8string>(actions.front()) };
    REQUIRE(opened != nullptr);
    REQUIRE(*opened == u8"other");
}

TEST_CASE("A sibling hover that leaves before the delay changes nothing", "[ui][menu][submenu]")
{
    submenu_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(submenu_tree(0, item_key(0)));
    controller.set_surface_trees({ { u8"child", submenu_tree(1) } });
    static_cast<void>(controller.process(move_to(second_row, 0)));
    REQUIRE(controller.next_deadline().has_value());
    // 열린 자식을 연 항목으로 돌아왔다.
    static_cast<void>(controller.process(move_to(first_row, 100)));
    REQUIRE(controller.next_deadline().has_value() == false);
    REQUIRE(controller.advance(at(1000)).empty());
}

TEST_CASE("Reaching the open submenu before the delay cancels the pending sibling close", "[ui][menu][submenu]")
{
    submenu_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(submenu_tree(0, item_key(0)));
    controller.set_surface_trees({ { u8"child", submenu_tree(1) } });
    static_cast<void>(controller.process(move_to(second_row, 0)));
    REQUIRE(controller.next_deadline() == at(300));
    // 형제를 지나 자식 메뉴의 잎에 닿았다.
    static_cast<void>(controller.process(move_to(second_row, 100, u8"child")));
    REQUIRE(controller.snapshot().hovered_surface == u8"child");
    REQUIRE(controller.snapshot().hovered == item_id(leaf_key(1)));
    REQUIRE(controller.next_deadline().has_value() == false);
    REQUIRE(controller.advance(at(1000)).empty());
}
