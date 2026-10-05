#include "host/text_edit_menu.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Touch editing menu buttons can be hit without taking text focus", "[ui][text-menu]")
{
    const auto tree { luil::make_text_edit_menu({ .text = u8"abc", .caret = 3, .anchor = 0 }, true, 576.0f, luil::text_edit_menu_height() * 2.0f, 2.0f) };
    const auto* const copy { tree->hit_test(100.0f, 50.0f) };
    REQUIRE(copy != nullptr);
    REQUIRE(copy->id().owner == u8"copy");
    REQUIRE(copy->enabled());
    REQUIRE(copy->focusable() == false);
    const auto* const paste { tree->hit_test(100.0f, 150.0f) };
    REQUIRE(paste != nullptr);
    REQUIRE(paste->id().owner == u8"paste");
    REQUIRE(paste->enabled());
}

TEST_CASE("Password editing menus disable copying and cutting even when revealed", "[ui][text-menu][secret]")
{
    const auto tree { luil::make_text_edit_menu({ .text = u8"secret", .caret = 6, .anchor = 0, .secret = true }, false, 288.0f, luil::text_edit_menu_height(), 1.0f) };
    REQUIRE(tree->find({ luil::ui_element_kind::menu_item, u8"copy" })->enabled() == false);
    REQUIRE(tree->find({ luil::ui_element_kind::menu_item, u8"cut" })->enabled() == false);
    REQUIRE(tree->find({ luil::ui_element_kind::menu_item, u8"paste" })->enabled());
    REQUIRE(tree->find({ luil::ui_element_kind::menu_item, u8"all" })->enabled());
}

TEST_CASE("An editing menu fits its available surface and waits for composition to finish", "[ui][text-menu][ime]")
{
    const auto tree { luil::make_text_edit_menu({ .text = u8"abc", .composing = true }, false, 288.0f, 60.0f, 1.0f) };
    const auto* const all { tree->find({ luil::ui_element_kind::menu_item, u8"all" }) };
    REQUIRE(all->bounds().y + all->bounds().height <= 60.0f);
    REQUIRE(all->enabled() == false);
    REQUIRE(tree->find({ luil::ui_element_kind::menu_item, u8"paste" })->enabled() == false);
}
