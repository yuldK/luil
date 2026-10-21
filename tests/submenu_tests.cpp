#include "luil/ui/menu_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    class submenu_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::menu_kinds> menu() const override
        {
            return luil::menu_kinds { luil::ui_element_kind::menu, luil::ui_element_kind::menu_item };
        }
    };

    std::shared_ptr<const luil::ui_tree> submenu_tree(const std::size_t depth)
    {
        luil::menu_config config {};
        config.depth = depth;
        config.parent_surface = depth == 1 ? u8"" : u8"child";
        config.anchor_key = u8"parent";
        config.items.push_back({ .key = u8"parent", .label = u8"Parent", .children = { { u8"leaf", u8"Leaf" } } });
        config.open_submenu = [depth](const std::u8string&) { return luil::input_action { luil::app_message { static_cast<int>(depth + 1) } }; };
        config.close_submenu = [](const luil::ui_action_context&) { return std::vector<luil::input_action> { luil::app_message { -1 } }; };
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(
            std::make_unique<luil::menu_element>(config), { 0.0f, 0.0f, 180.0f, 60.0f }, 1.0f));
    }
} // namespace

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

TEST_CASE("The deepest submenu handles Escape and restores its parent highlight", "[ui][menu][submenu]")
{
    submenu_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(submenu_tree(0));
    controller.set_surface_trees({ { u8"child", submenu_tree(1) }, { u8"grandchild", submenu_tree(2) } });
    const auto actions { controller.process(luil::key_pressed_event { luil::key_code::escape }) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(controller.snapshot().menu_surface == u8"child");
    REQUIRE(controller.snapshot().menu_highlight.owner == u8"parent");
}
