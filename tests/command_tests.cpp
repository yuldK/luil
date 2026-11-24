#include "luil/ui/commands.h"
#include "luil/ui/ui_platform.h"

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
    // host는 F 키를 이름 키로 보낸다. 플랫폼 대역의 값은 host마다 뜻이 달라 이름으로 읽지 않는다.
    const luil::key_chord chord { luil::key_code::f5, true, true };
    REQUIRE(luil::shortcut_label(chord) == u8"Ctrl+Shift+F5");
    REQUIRE(luil::shortcut_label({ luil::key_code::f1 }) == u8"F1");
    REQUIRE(luil::shortcut_label({ luil::key_code::f12, false, false, true }) == u8"Alt+F12");
    REQUIRE(luil::shortcut_label({ luil::platform_key_code(0x74) }).empty());
    luil::app_command command { u8"show", u8"Show", chord };
    command.checkable = true;
    const auto item { luil::menu_item_from(command) };
    REQUIRE(item.kind == luil::menu_item_kind::check);
    REQUIRE(item.checked == false);
    REQUIRE(item.shortcut_text == u8"Ctrl+Shift+F5");
    REQUIRE(luil::shortcut_label({}).empty());
}

TEST_CASE("Command matching uses the platform primary shortcut modifier", "[ui][commands]")
{
    const std::vector<luil::app_command> commands { { u8"save", u8"Save", { luil::key_code::key_s, true } } };

    // Windows: Ctrl이 주 단축키다. Windows 키가 섞이면 다른 조합이다.
    luil::key_pressed_event windows { luil::key_code::key_s, true };
    REQUIRE(luil::match_command(commands, windows, luil::command_scope::window) == u8"save");
    windows.meta = true;
    REQUIRE(luil::match_command(commands, windows, luil::command_scope::window).has_value() == false);

    // Android: 주 역할을 Ctrl이 맡는다고 명시한다. Meta가 섞이면 다른 조합이다.
    luil::key_pressed_event android { luil::key_code::key_s, true };
    android.primary_shortcut = true;
    REQUIRE(luil::match_command(commands, android, luil::command_scope::window) == u8"save");
    android.meta = true;
    REQUIRE(luil::match_command(commands, android, luil::command_scope::window).has_value() == false);

    // macOS: Command가 주 단축키다. Command+S가 Ctrl+S 명령이고, Control+S는 다른 조합이다.
    luil::key_pressed_event command { luil::key_code::key_s, false };
    command.meta = true;
    command.primary_shortcut = true;
    REQUIRE(luil::match_command(commands, command, luil::command_scope::window) == u8"save");
    REQUIRE(luil::chord_of(command) == luil::key_chord { luil::key_code::key_s, true });
    luil::key_pressed_event control { luil::key_code::key_s, true };
    control.primary_shortcut = false;
    REQUIRE(luil::match_command(commands, control, luil::command_scope::window).has_value() == false);
    REQUIRE(luil::chord_of(control) == luil::key_chord { luil::key_code::key_s, false });
    command.control = true;
    REQUIRE(luil::match_command(commands, command, luil::command_scope::window).has_value() == false);
}

TEST_CASE("Shortcut labels follow the Command key style and both styles parse", "[ui][commands]")
{
    const luil::ui_platform previous { luil::current_ui_platform() };
    luil::ui_platform mac { previous };
    mac.command_key_shortcuts = true;
    luil::set_ui_platform(mac);
    const luil::key_chord save { luil::key_code::key_s, true, true, true };
    const std::u8string label { luil::shortcut_label(save) };
    luil::set_ui_platform(previous);

    REQUIRE(label == u8"\u2325\u21E7\u2318S");
    REQUIRE(luil::parse_shortcut(label) == save);
    REQUIRE(luil::parse_shortcut(u8"Cmd+Option+Shift+S") == save);
    REQUIRE(luil::shortcut_label(save) == u8"Ctrl+Shift+Alt+S");
    REQUIRE(luil::parse_shortcut(u8"\u2318") == std::nullopt);
}
