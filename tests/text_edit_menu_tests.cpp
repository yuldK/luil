#include "host/text_edit_menu.h"

#include "text/library_text.h"
#include "text_languages_guard.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    [[nodiscard]] luil::string_lookup strings_for(const std::u8string_view language)
    {
        const std::vector<std::u8string> preferences { std::u8string { language } };
        return luil::select_strings(luil::library_text::table, preferences);
    }
}

TEST_CASE("Touch editing menu buttons can be hit without taking text focus", "[ui][text-menu]")
{
    const auto tree { luil::make_text_edit_menu({ .text = u8"abc", .caret = 3, .anchor = 0 }, strings_for(u8"ko"), 576.0f, luil::text_edit_menu_height() * 2.0f, 2.0f) };
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
    const auto tree { luil::make_text_edit_menu({ .text = u8"secret", .caret = 6, .anchor = 0, .secret = true }, strings_for(u8"en"), 288.0f, luil::text_edit_menu_height(), 1.0f) };
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
    const auto tree { luil::make_text_edit_menu(value, strings_for(u8"ko"), 288.0f, luil::text_edit_menu_height(true), 1.0f) };
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
    const auto display_only { luil::make_text_edit_menu(shown, strings_for(u8"en"), 288.0f, luil::text_edit_menu_height(true), 1.0f) };
    REQUIRE(display_only->find({ luil::ui_element_kind::menu_item, u8"open-link" })->enabled() == false);
    REQUIRE(display_only->find({ luil::ui_element_kind::menu_item, u8"copy-link" })->enabled());
    const luil::text_input_snapshot secret { .text = u8"a.b", .secret = true, .link = u8"https://a.b" };
    REQUIRE(luil::text_edit_menu_has_link(secret) == false);
    REQUIRE(luil::make_text_edit_menu(secret, strings_for(u8"en"), 288.0f, luil::text_edit_menu_height(), 1.0f)->find({ luil::ui_element_kind::menu_item, u8"open-link" }) == nullptr);
}

TEST_CASE("An editing menu fits its available surface and waits for composition to finish", "[ui][text-menu][ime]")
{
    const auto tree { luil::make_text_edit_menu({ .text = u8"abc", .composing = true }, strings_for(u8"en"), 288.0f, 60.0f, 1.0f) };
    const auto* const all { tree->find({ luil::ui_element_kind::menu_item, u8"all" }) };
    REQUIRE(all->bounds().y + all->bounds().height <= 60.0f);
    REQUIRE(all->enabled() == false);
    REQUIRE(tree->find({ luil::ui_element_kind::menu_item, u8"paste" })->enabled() == false);
}

TEST_CASE("The editing menu takes its button words from the strings it is given", "[ui][text-menu][i18n]")
{
    const std::function<luil::input_action(std::u8string_view)> activate { [](std::u8string_view) { return luil::input_action {}; } };
    const luil::text_input_snapshot value { .text = u8"see a.b", .caret = 5, .anchor = 5, .link = u8"https://a.b", .activate_link = &activate };
    const auto words = [&](const std::u8string_view language)
    {
        const auto tree { luil::make_text_edit_menu(value, strings_for(language), 288.0f, luil::text_edit_menu_height(true), 1.0f) };
        std::vector<std::u8string> names {};
        for (const std::u8string_view key : { u8"copy", u8"cut", u8"paste", u8"all", u8"open-link", u8"copy-link" })
            names.push_back(tree->find({ luil::ui_element_kind::menu_item, std::u8string { key } })->accessibility().name);
        return names;
    };

    const std::vector<std::u8string> korean { u8"복사", u8"잘라내기", u8"붙여넣기", u8"전체 선택", u8"링크 열기", u8"주소 복사" };
    const std::vector<std::u8string> english { u8"Copy", u8"Cut", u8"Paste", u8"Select all", u8"Open link", u8"Copy link" };
    CHECK(words(u8"ko") == korean);
    CHECK(words(u8"en") == english);
}

TEST_CASE("The editing menu follows the app's languages, or the OS with English behind it", "[ui][text-menu][i18n]")
{
    using languages = std::vector<std::u8string>;
    // 앱이 정하지 않았으면 OS 태그와 끝의 `en`이다.
    CHECK(luil::text_edit_menu_languages(std::nullopt, u8"ko-KR") == languages { u8"ko-KR", u8"en" });
    CHECK(luil::text_edit_menu_languages(std::nullopt, u8"en-US") == languages { u8"en-US", u8"en" });
    // OS를 읽지 못해도 지금처럼 영어다.
    CHECK(luil::text_edit_menu_languages(std::nullopt, {}) == languages { u8"en" });
    // 앱이 정했으면 OS를 보지 않는다. 빈 목록도 정한 것이다 (라이브러리 기본 문구처럼 한국어로 고른다).
    const languages chosen { u8"en" };
    CHECK(luil::text_edit_menu_languages(chosen, u8"ko-KR") == chosen);
    CHECK(luil::text_edit_menu_languages(languages {}, u8"en-US").empty());

    const auto picked = [](const std::optional<languages>& app, const std::u8string_view os)
    {
        return std::u8string { luil::select_strings(luil::library_text::table, luil::text_edit_menu_languages(app, os)).tag() };
    };
    CHECK(picked(std::nullopt, u8"ko-KR") == u8"ko");
    CHECK(picked(std::nullopt, u8"en-GB") == u8"en");
    // 표에 없는 언어의 OS는 한국어가 아니라 영어로 물러선다.
    CHECK(picked(std::nullopt, u8"fr-FR") == u8"en");
    CHECK(picked(chosen, u8"ko-KR") == u8"en");
    CHECK(picked(languages { u8"ko" }, u8"en-US") == u8"ko");
    CHECK(picked(languages {}, u8"en-US") == u8"ko");
}

TEST_CASE("The editing menu reads the app's languages from the library state", "[ui][text-menu][i18n]")
{
    const luil_test::text_languages_guard guard {};
    luil::reset_text_languages();
    CHECK(luil::text_edit_menu_strings(u8"ko-KR").tag() == u8"ko");
    CHECK(luil::text_edit_menu_strings(u8"en-US").tag() == u8"en");
    CHECK(luil::text_edit_menu_strings(u8"fr-FR").tag() == u8"en");
    CHECK(luil::text_edit_menu_strings({}).tag() == u8"en");

    guard.use(u8"en");
    CHECK(luil::text_edit_menu_strings(u8"ko-KR").tag() == u8"en");
    guard.use(u8"ko");
    CHECK(luil::text_edit_menu_strings(u8"en-US").tag() == u8"ko");
}
