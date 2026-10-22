#include "luil/app/menu_popup.h"
#include "luil/ui/accessibility.h"
#include "luil/ui/theme_picker_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <memory>
#include <utility>
#include <variant>

namespace {
    struct selected_theme
    {
        luil::theme_preference value {};
    };

    [[nodiscard]] luil::theme_picker_config themes()
    {
        luil::theme_picker_config config {};
        config.owner = u8"appearance";
        config.selected = luil::theme_preference::dark;
        config.select = [](const luil::theme_preference value) { return luil::make_app_action(selected_theme { value }); };
        return config;
    }
} // namespace

TEST_CASE("Theme preview cards expose a named selection group and keep state controlled", "[ui][theme-picker]")
{
    auto config { themes() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::theme_picker_element>(config), { 0.0f, 0.0f, 700.0f, 200.0f }, 1.0f) };
    REQUIRE(tree.root()->accessibility().role == luil::access_role::radio_group);
    REQUIRE(tree.root()->accessibility().name == u8"테마");
    const luil::ui_element_id light { luil::ui_element_kind::choice, u8"appearance/light" };
    const luil::ui_element_id dark { luil::ui_element_kind::choice, u8"appearance/dark" };
    REQUIRE(tree.focus_order() == std::vector<luil::ui_element_id> { dark });
    REQUIRE(tree.find(dark)->accessibility().selected == true);
    REQUIRE(tree.find(light)->accessibility().selected == false);
    REQUIRE(tree.access_parent_of(light) == tree.root());
    const auto actions { luil::plan_access_request(*tree.find(light), { .command = luil::access_command::select }) };
    REQUIRE(actions.has_value());
    REQUIRE(actions->size() == 1);
    REQUIRE(std::get<luil::app_message>(actions->front()).get<selected_theme>()->value == luil::theme_preference::light);
    REQUIRE(tree.find(dark)->accessibility().selected == true);
}

TEST_CASE("Theme cards share measured height with horizontal and stacked layouts at each DPI", "[ui][theme-picker]")
{
    const auto config { themes() };
    for (const float width : { 240.0f, 700.0f })
        for (const float scale : { 1.0f, 1.5f, 2.0f })
        {
            const float height { luil::theme_picker_element::height_for(config, width) };
            const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::theme_picker_element>(config), { 0.0f, 0.0f, width * scale, height * scale }, scale) };
            REQUIRE(tree.root()->bounds().height == height * scale);
            for (const auto& child : tree.root()->children())
            {
                REQUIRE(child->bounds().x >= 0.0f);
                REQUIRE(child->bounds().y >= 0.0f);
                REQUIRE(child->bounds().x + child->bounds().width <= width * scale + 0.01f);
                REQUIRE(child->bounds().y + child->bounds().height <= height * scale + 0.01f);
            }
        }
    REQUIRE(luil::theme_picker_element::height_for(config, 240.0f) > luil::theme_picker_element::height_for(config, 700.0f));
}

TEST_CASE("Read-only theme cards do not offer keyboard activation", "[ui][theme-picker]")
{
    auto config { themes() };
    config.select = {};
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::theme_picker_element>(config), { 0.0f, 0.0f, 700.0f, 200.0f }, 1.0f) };
    REQUIRE(tree.focus_order().empty());
    for (const auto& child : tree.root()->children())
        REQUIRE(child->action(luil::ui_trigger::left_click) == nullptr);
}

TEST_CASE("Accent swatches wrap without losing selection or their accessible container", "[ui][theme-picker]")
{
    luil::accent_picker_config config {};
    config.owner = u8"accent";
    config.include_system = false;
    config.select = [](const std::u8string&) { return luil::input_action {}; };
    const float height { luil::accent_picker_element::height_for(config, 100.0f) };
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::accent_picker_element>(config), { 0.0f, 0.0f, 150.0f, height * 1.5f }, 1.5f) };
    REQUIRE(tree.root()->children().size() == luil::accent_catalog().size());
    const luil::ui_element_id selected { luil::ui_element_kind::choice, u8"accent/" + config.selected };
    REQUIRE(tree.find(selected) != nullptr);
    REQUIRE(tree.find(selected)->accessibility().selected == true);
    REQUIRE(tree.access_parent_of(selected) == tree.root());
    REQUIRE(tree.focus_order() == std::vector<luil::ui_element_id> { selected });
    for (const auto& child : tree.root()->children())
    {
        REQUIRE(child->bounds().x + child->bounds().width <= 150.01f);
        REQUIRE(child->bounds().y + child->bounds().height <= height * 1.5f + 0.01f);
    }
}

TEST_CASE("Menu popup composition preserves anchor actions and logical size across DPI", "[ui][menu-popup]")
{
    luil::menu_config menu {};
    menu.owner = u8"document";
    menu.items = { { .key = u8"open", .label = u8"열기", .shortcut_text = u8"Ctrl+O" }, { .key = u8"pin", .label = u8"고정", .kind = luil::menu_item_kind::check, .checked = true } };
    menu.select = [](const std::u8string&) { return luil::make_app_action(selected_theme {}); };
    const float height { luil::menu_element::height_for(menu) };
    luil::menu_popup_config config {};
    config.id = u8"document-menu";
    config.anchor = u8"second-window";
    config.x = 30.0f;
    config.y = 40.0f;
    config.scale = 1.5f;
    config.dismiss = [](const luil::popup_dismiss_reason) { return luil::make_app_action(selected_theme {}); };
    const auto popup { luil::make_menu_popup(menu, config) };
    REQUIRE(popup.id == config.id);
    REQUIRE(popup.anchor == config.anchor);
    REQUIRE(popup.x == 30.0f);
    REQUIRE(popup.y == 40.0f);
    REQUIRE(popup.width > 0.0f);
    REQUIRE(popup.height == height);
    REQUIRE(popup.tree->root()->bounds().width == popup.width * 1.5f);
    REQUIRE(popup.tree->root()->bounds().height == height * 1.5f);
    REQUIRE(popup.tree->find({ luil::ui_element_kind::menu_item, u8"pin" })->accessibility().checked == true);
    REQUIRE(std::holds_alternative<luil::app_message>(popup.dismiss(luil::popup_dismiss_reason::escape_key)));
    config.scale = std::numeric_limits<float>::quiet_NaN();
    config.width = 240.0f;
    const auto fallback { luil::make_menu_popup(std::move(menu), config) };
    REQUIRE(fallback.width == 240.0f);
    REQUIRE(fallback.tree->root()->bounds().height == height);
}
