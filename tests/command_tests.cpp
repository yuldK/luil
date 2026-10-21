#include "luil/ui/commands.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Command matching respects focused overrides modal isolation and disabled state", "[ui][commands]")
{
    std::vector<luil::app_command> commands {
        { u8"window", u8"Window", { luil::key_code::key_a, true }, luil::command_scope::window },
        { u8"focus", u8"Focus", { luil::key_code::key_a, true }, luil::command_scope::focused },
        { u8"modal", u8"Modal", { luil::key_code::key_a, true }, luil::command_scope::modal }
    };
    const luil::key_pressed_event key { luil::key_code::key_a, true };
    REQUIRE(luil::match_command(commands, key, luil::command_scope::focused) == u8"focus");
    REQUIRE(luil::match_command(commands, key, luil::command_scope::modal) == u8"modal");
    commands[2].enabled = false;
    REQUIRE(luil::match_command(commands, key, luil::command_scope::modal).has_value() == false);
    commands[1].enabled = false;
    REQUIRE(luil::match_command(commands, key, luil::command_scope::focused) == u8"window");
    REQUIRE(luil::find_conflicts(commands).empty());
    commands.push_back(commands[0]);
    commands.back().id = u8"duplicate";
    REQUIRE(luil::find_conflicts(commands).size() == 1u);
}

TEST_CASE("Command menu values preserve check state and format function keys", "[ui][commands]")
{
    const luil::key_chord chord { luil::platform_key_code(0x74), true, true };
    REQUIRE(luil::shortcut_label(chord) == u8"Ctrl+Shift+F5");
    luil::app_command command { u8"show", u8"Show", chord };
    command.checkable = true;
    const auto item { luil::menu_item_from(command) };
    REQUIRE(item.kind == luil::menu_item_kind::check);
    REQUIRE(item.checked == false);
    REQUIRE(item.shortcut_text == u8"Ctrl+Shift+F5");
    REQUIRE(luil::shortcut_label({}).empty());
}
