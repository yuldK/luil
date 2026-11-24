#include "macos/macos_input.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>

// macOS 키 변환이다 (src/macos/macos_input.h). AppKit을 모르는 순수 층이라 core test에서 돈다.
namespace {
    // Carbon `kVK_*` 값이다. 변환 표와 같은 값을 손으로 적어 둔다 — 표가 어긋나면 이 test가 잡는다.
    constexpr std::uint16_t vk_a { 0x00 };
    constexpr std::uint16_t vk_c { 0x08 };
    constexpr std::uint16_t vk_z { 0x06 };
    constexpr std::uint16_t vk_5 { 0x17 };
    constexpr std::uint16_t vk_return { 0x24 };
    constexpr std::uint16_t vk_tab { 0x30 };
    constexpr std::uint16_t vk_space { 0x31 };
    constexpr std::uint16_t vk_delete { 0x33 };
    constexpr std::uint16_t vk_escape { 0x35 };
    constexpr std::uint16_t vk_keypad_5 { 0x57 };
    constexpr std::uint16_t vk_f5 { 0x60 };
    constexpr std::uint16_t vk_forward_delete { 0x75 };
    constexpr std::uint16_t vk_left { 0x7B };
    constexpr std::uint16_t vk_right { 0x7C };
    constexpr std::uint16_t vk_down { 0x7D };
    constexpr std::uint16_t vk_up { 0x7E };

    [[nodiscard]] luil::macos::key_input key(const std::uint16_t code, const char32_t base = 0)
    {
        luil::macos::key_input input {};
        input.key_code = code;
        input.base_character = base;
        return input;
    }
}

TEST_CASE("macOS keys that make no character are always keys", "[macos][input]")
{
    CHECK(luil::macos::named_key(key(vk_return)) == luil::key_code::enter);
    CHECK(luil::macos::named_key(key(vk_tab)) == luil::key_code::tab);
    CHECK(luil::macos::named_key(key(vk_escape)) == luil::key_code::escape);
    CHECK(luil::macos::named_key(key(vk_delete)) == luil::key_code::backspace);
    CHECK(luil::macos::named_key(key(vk_forward_delete)) == luil::key_code::delete_forward);
    CHECK(luil::macos::named_key(key(vk_f5)) == luil::key_code::f5);

    const std::optional<luil::key_pressed_event> enter { luil::macos::key_event_for(key(vk_return), false) };
    REQUIRE(enter.has_value());
    CHECK(enter->key == luil::key_code::enter);
    CHECK(enter->shortcut_modifier_down() == false);
}

TEST_CASE("macOS character keys are keys only with Command or Control", "[macos][input]")
{
    CHECK(luil::macos::key_event_for(key(vk_a, U'a'), false).has_value() == false);
    // 키 잡기 칸은 수정자 없는 글자 키도 받는다.
    CHECK(luil::macos::key_event_for(key(vk_a, U'a'), true).has_value());
    // Space는 실행 키라 늘 키다.
    CHECK(luil::macos::key_event_for(key(vk_space, U' '), false).has_value());

    luil::macos::key_input copy { key(vk_c, U'c') };
    copy.command = true;
    const std::optional<luil::key_pressed_event> event { luil::macos::key_event_for(copy, false) };
    REQUIRE(event.has_value());
    CHECK(event->key == luil::key_code::key_c);
    // Command가 주 단축키다. 물리 Control은 눌리지 않았다.
    CHECK(event->primary_shortcut_down());
    CHECK(event->control == false);
    CHECK(event->meta);
}

TEST_CASE("macOS shortcut names follow the layout's letter, not the key position", "[macos][input]")
{
    // 배열이 그 자리에서 다른 영문자를 내면(드보락 등) 단축키 이름은 그 글자다.
    luil::macos::key_input dvorak { key(vk_z, U'v') };
    dvorak.command = true;
    CHECK(luil::macos::key_event_for(dvorak, false)->key == luil::key_code::key_v);
    // 한글 배열처럼 영문자가 아닌 글자를 내면 자리로 고른다 (Command+ㅋ은 Command+Z다).
    luil::macos::key_input hangul { key(vk_z, U'ㅋ') };
    hangul.command = true;
    CHECK(luil::macos::key_event_for(hangul, false)->key == luil::key_code::key_z);
    // 키패드 숫자는 글자 5가 아니라 키패드 키다.
    CHECK(luil::macos::named_key(key(vk_keypad_5, U'5')) == luil::key_code::numpad_5);
    CHECK(luil::macos::named_key(key(vk_5, U'5')) == luil::key_code::key_5);
}

TEST_CASE("macOS Command arrows move to line and document ends", "[macos][input]")
{
    luil::macos::key_input left { key(vk_left) };
    left.command = true;
    const auto line_start { luil::macos::key_event_for(left, false) };
    REQUIRE(line_start.has_value());
    CHECK(line_start->key == luil::key_code::home);
    CHECK(line_start->primary_shortcut_down() == false);
    CHECK(line_start->shortcut_modifier_down() == false);

    luil::macos::key_input right { key(vk_right) };
    right.command = true;
    CHECK(luil::macos::key_event_for(right, false)->key == luil::key_code::end);

    luil::macos::key_input up { key(vk_up) };
    up.command = true;
    const auto document_start { luil::macos::key_event_for(up, false) };
    REQUIRE(document_start.has_value());
    CHECK(document_start->key == luil::key_code::home);
    CHECK(document_start->primary_shortcut_down());

    luil::macos::key_input down { key(vk_down) };
    down.command = true;
    CHECK(luil::macos::key_event_for(down, false)->key == luil::key_code::end);
}

TEST_CASE("macOS Option is word navigation and Option+Backspace deletes a word", "[macos][input]")
{
    luil::macos::key_input left { key(vk_left) };
    left.option = true;
    const auto word { luil::macos::key_event_for(left, false) };
    REQUIRE(word.has_value());
    CHECK(word->key == luil::key_code::arrow_left);
    CHECK(word->word_navigation_down());
    CHECK(word->primary_shortcut_down() == false);

    CHECK(luil::macos::backspace_key(vk_delete));
    CHECK(luil::macos::backspace_character(key(vk_delete)) == U'\b');
    luil::macos::key_input word_delete { key(vk_delete) };
    word_delete.option = true;
    CHECK(luil::macos::backspace_character(word_delete) == char32_t { 0x7F });
}

TEST_CASE("AppKit function key characters are not text", "[macos][input]")
{
    CHECK(luil::macos::function_key_character(char32_t { 0xF708 }));
    CHECK(luil::macos::function_key_character(U'a') == false);
    CHECK(luil::macos::function_key_character(U'한') == false);
}
