#include "host/text_edit_menu.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string_view>

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

TEST_CASE("A caret inside a link adds a row to open the link and copy its address", "[ui][text-menu][link]")
{
    const std::function<luil::input_action(std::u8string_view)> activate { [](std::u8string_view) { return luil::input_action {}; } };
    const luil::text_input_snapshot value { .text = u8"see a.b", .caret = 5, .anchor = 5, .read_only = true, .link = u8"https://a.b", .activate_link = &activate };
    REQUIRE(luil::text_edit_menu_has_link(value));
    REQUIRE(luil::text_edit_menu_height(true) > luil::text_edit_menu_height());
    const auto tree { luil::make_text_edit_menu(value, true, 288.0f, luil::text_edit_menu_height(true), 1.0f) };
    const auto* const open { tree->find({ luil::ui_element_kind::menu_item, u8"open-link" }) };
    const auto* const copy { tree->find({ luil::ui_element_kind::menu_item, u8"copy-link" }) };
    REQUIRE(open != nullptr);
    REQUIRE(copy != nullptr);
    REQUIRE(open->enabled());
    REQUIRE(copy->enabled());
    REQUIRE(tree->hit_test(open->bounds().x + 4.0f, open->bounds().y + 4.0f) == open);
    REQUIRE(open->bounds().y + open->bounds().height <= luil::text_edit_menu_height(true));

    // 여는 메시지가 없으면 주소 복사만 선다. 암호 칸에는 링크 줄이 없다.
    const luil::text_input_snapshot shown { .text = u8"see a.b", .caret = 5, .anchor = 5, .link = u8"https://a.b" };
    const auto display_only { luil::make_text_edit_menu(shown, false, 288.0f, luil::text_edit_menu_height(true), 1.0f) };
    REQUIRE(display_only->find({ luil::ui_element_kind::menu_item, u8"open-link" })->enabled() == false);
    REQUIRE(display_only->find({ luil::ui_element_kind::menu_item, u8"copy-link" })->enabled());
    const luil::text_input_snapshot secret { .text = u8"a.b", .secret = true, .link = u8"https://a.b" };
    REQUIRE(luil::text_edit_menu_has_link(secret) == false);
    REQUIRE(luil::make_text_edit_menu(secret, false, 288.0f, luil::text_edit_menu_height(), 1.0f)->find({ luil::ui_element_kind::menu_item, u8"open-link" }) == nullptr);
}

TEST_CASE("An editing menu fits its available surface and waits for composition to finish", "[ui][text-menu][ime]")
{
    const auto tree { luil::make_text_edit_menu({ .text = u8"abc", .composing = true }, false, 288.0f, 60.0f, 1.0f) };
    const auto* const all { tree->find({ luil::ui_element_kind::menu_item, u8"all" }) };
    REQUIRE(all->bounds().y + all->bounds().height <= 60.0f);
    REQUIRE(all->enabled() == false);
    REQUIRE(tree->find({ luil::ui_element_kind::menu_item, u8"paste" })->enabled() == false);
}
