#include "ios/ios_input.h"
#include "macos/macos_input.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>

// iOS 하드웨어 키보드의 HID 자리 변환이다 (src/ios/ios_input.h). UIKit을 모르는 순수 층이라 core test에서 돈다.
namespace {
    // HID 사용 번호다. 변환 표와 같은 값을 손으로 적어 둔다 — 표가 어긋나면 이 test가 잡는다.
    constexpr std::uint32_t hid_a { 0x04 };
    constexpr std::uint32_t hid_c { 0x06 };
    constexpr std::uint32_t hid_z { 0x1D };
    constexpr std::uint32_t hid_1 { 0x1E };
    constexpr std::uint32_t hid_0 { 0x27 };
    constexpr std::uint32_t hid_return { 0x28 };
    constexpr std::uint32_t hid_escape { 0x29 };
    constexpr std::uint32_t hid_backspace { 0x2A };
    constexpr std::uint32_t hid_tab { 0x2B };
    constexpr std::uint32_t hid_space { 0x2C };
    constexpr std::uint32_t hid_caps_lock { 0x39 };
    constexpr std::uint32_t hid_f5 { 0x3E };
    constexpr std::uint32_t hid_insert { 0x49 };
    constexpr std::uint32_t hid_delete_forward { 0x4C };
    constexpr std::uint32_t hid_right { 0x4F };
    constexpr std::uint32_t hid_left { 0x50 };
    constexpr std::uint32_t hid_down { 0x51 };
    constexpr std::uint32_t hid_up { 0x52 };
    constexpr std::uint32_t hid_keypad_enter { 0x58 };
    constexpr std::uint32_t hid_keypad_5 { 0x5D };
    constexpr std::uint32_t hid_left_command { 0xE3 };

    [[nodiscard]] luil::key_code named(const std::uint32_t usage, const char32_t base = 0)
    {
        const std::optional<std::uint16_t> carbon { luil::ios::carbon_key_of_hid(usage) };
        REQUIRE(carbon.has_value());
        luil::macos::key_input input {};
        input.key_code = *carbon;
        input.base_character = base;
        return luil::macos::named_key(input);
    }
}

TEST_CASE("iOS HID usages reach the same named keys as macOS", "[ios][input]")
{
    CHECK(named(hid_a, U'a') == luil::key_code::key_a);
    CHECK(named(hid_c, U'c') == luil::key_code::key_c);
    CHECK(named(hid_z, U'z') == luil::key_code::key_z);
    CHECK(named(hid_1, U'1') == luil::key_code::key_1);
    CHECK(named(hid_0, U'0') == luil::key_code::key_0);
    CHECK(named(hid_return) == luil::key_code::enter);
    CHECK(named(hid_keypad_enter) == luil::key_code::enter);
    CHECK(named(hid_escape) == luil::key_code::escape);
    CHECK(named(hid_backspace) == luil::key_code::backspace);
    CHECK(named(hid_tab) == luil::key_code::tab);
    CHECK(named(hid_space, U' ') == luil::key_code::space);
    CHECK(named(hid_f5) == luil::key_code::f5);
    CHECK(named(hid_insert) == luil::key_code::insert);
    CHECK(named(hid_delete_forward) == luil::key_code::delete_forward);
    CHECK(named(hid_left) == luil::key_code::arrow_left);
    CHECK(named(hid_right) == luil::key_code::arrow_right);
    CHECK(named(hid_up) == luil::key_code::arrow_up);
    CHECK(named(hid_down) == luil::key_code::arrow_down);
    CHECK(named(hid_keypad_5) == luil::key_code::numpad_5);
}

TEST_CASE("iOS HID usages with no macOS key are dropped", "[ios][input]")
{
    CHECK(luil::ios::carbon_key_of_hid(hid_caps_lock).has_value() == false);
    CHECK(luil::ios::carbon_key_of_hid(hid_left_command).has_value() == false);
    CHECK(luil::ios::carbon_key_of_hid(0).has_value() == false);
}

TEST_CASE("iOS text keys go to the text system while editing", "[ios][input]")
{
    CHECK(luil::ios::text_system_key(hid_a));
    CHECK(luil::ios::text_system_key(hid_0));
    CHECK(luil::ios::text_system_key(hid_space));
    CHECK(luil::ios::text_system_key(hid_backspace));
    CHECK(luil::ios::text_system_key(hid_return));
    CHECK(luil::ios::text_system_key(hid_keypad_5));
    CHECK(luil::ios::text_system_key(hid_tab) == false);
    CHECK(luil::ios::text_system_key(hid_escape) == false);
    CHECK(luil::ios::text_system_key(hid_left) == false);
    CHECK(luil::ios::text_system_key(hid_delete_forward) == false);
    CHECK(luil::ios::text_system_key(hid_f5) == false);
    // 수정자 키 자체는 글을 만들지 않는다.
    CHECK(luil::ios::text_system_key(hid_left_command) == false);
}
