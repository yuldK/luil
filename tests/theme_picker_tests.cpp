#include "luil/app/menu_popup.h"
#include "luil/ui/accessibility.h"
#include "luil/ui/theme_picker_element.h"
#include "luil/ui/ui_tree.h"

#include "text_languages_guard.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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
}

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

TEST_CASE("Theme cards keep their size on wide slots and wrap on narrow ones", "[ui][theme-picker]")
{
    const auto config { themes() };
    const float card_height { config.preview_height + 26.0f };
    // 넓은 폭: 카드는 늘어나지 않고 왼쪽부터 한 줄로 선다.
    for (const float width : { 700.0f, 1600.0f })
    {
        const float height { luil::theme_picker_element::height_for(config, width) };
        REQUIRE(height == card_height);
        const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::theme_picker_element>(config), { 0.0f, 0.0f, width, height }, 1.0f) };
        for (std::size_t index = 0; index < tree.root()->children().size(); ++index)
        {
            const luil::rect_f card { tree.root()->children()[index]->bounds() };
            REQUIRE(card.width == config.card_width);
            REQUIRE(card.x == static_cast<float>(index) * (config.card_width + config.gap));
            REQUIRE(card.y == 0.0f);
        }
    }
    // 세 장이 들지 않는 폭: 두 장 뒤에 셋째가 다음 줄로 넘어간다. 카드 크기는 그대로다.
    const float narrow { config.card_width * 2.0f + config.gap + 10.0f };
    const float height { luil::theme_picker_element::height_for(config, narrow) };
    REQUIRE(height == card_height * 2.0f + config.gap);
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::theme_picker_element>(config), { 0.0f, 0.0f, narrow, height }, 1.0f) };
    const luil::rect_f third { tree.root()->children()[2]->bounds() };
    REQUIRE(third.x == 0.0f);
    REQUIRE(third.y == card_height + config.gap);
    REQUIRE(third.width == config.card_width);
}

TEST_CASE("Accent swatches split evenly across the rows they need", "[ui][theme-picker]")
{
    luil::accent_picker_config config {};
    config.owner = u8"accent";
    config.include_system = false;
    // 32px 견본 17개가 드는 폭이다. 카탈로그를 두 줄에 고르게 나누면 둘째 줄이 한두 개로 남지 않는다.
    const float width { 17.0f * 40.0f - 8.0f };
    const float height { luil::accent_picker_element::height_for(config, width) };
    REQUIRE(height == 2.0f * 40.0f - 8.0f);
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::accent_picker_element>(config), { 0.0f, 0.0f, width, height }, 1.0f) };
    std::size_t first_row { 0 };
    for (const auto& child : tree.root()->children())
        if (child->bounds().y == 0.0f)
            ++first_row;
    const std::size_t count { luil::accent_catalog().size() };
    REQUIRE(first_row == (count + 1u) / 2u);
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

TEST_CASE("The picker configs hold no default text and the elements fill the empty fields in Korean", "[ui][theme-picker][default-text]")
{
    // 공개 헤더에는 한국어 기본값이 없다.
    const luil::theme_picker_config blank_themes {};
    REQUIRE(blank_themes.name.empty());
    for (const std::u8string& label : blank_themes.labels)
        REQUIRE(label.empty());
    const luil::accent_picker_config blank_accents {};
    REQUIRE(blank_accents.name.empty());

    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::theme_picker_element>(themes()), { 0.0f, 0.0f, 700.0f, 200.0f }, 1.0f) };
    REQUIRE(tree.root()->accessibility().name == u8"테마");
    const std::vector<std::u8string> card_names { u8"밝게", u8"시스템", u8"어둡게" };
    REQUIRE(tree.root()->children().size() == card_names.size());
    for (std::size_t index = 0; index < card_names.size(); ++index)
        REQUIRE(tree.root()->children()[index]->accessibility().name == card_names[index]);

    luil::accent_picker_config accents {};
    accents.owner = u8"accent";
    const luil::ui_tree accent_tree { luil::make_arranged_tree(std::make_unique<luil::accent_picker_element>(accents), { 0.0f, 0.0f, 400.0f, 200.0f }, 1.0f) };
    REQUIRE(accent_tree.root()->accessibility().name == u8"강조색");
}

TEST_CASE("The picker words and the accent names follow the library language", "[ui][theme-picker][i18n]")
{
    const luil_test::text_languages_guard guard {};
    // 시스템 항목도 세운다. 전역이라 끝에서 내려 다른 test에 새지 않게 한다.
    struct system_accent_scope
    {
        system_accent_scope()
        {
            luil::set_system_accent(luil::make_ui_color(0, 120, 212));
        }

        ~system_accent_scope()
        {
            luil::set_system_accent(std::nullopt);
        }
    };
    const system_accent_scope scope {};

    luil::accent_picker_config accents {};
    accents.owner = u8"accent";
    accents.include_system = true;
    const auto swatch = [](const luil::ui_tree& tree, const std::u8string_view id) -> const luil::ui_element&
    {
        const luil::ui_element* const found { tree.find({ luil::ui_element_kind::choice, u8"accent/" + std::u8string { id } }) };
        REQUIRE(found != nullptr);
        return *found;
    };
    const auto check_language = [&](const std::u8string_view picker, const std::u8string_view accent_picker, const std::array<std::u8string_view, 3> cards, const std::u8string_view blue, const std::u8string_view system)
    {
        const luil::ui_tree themes_tree { luil::make_arranged_tree(std::make_unique<luil::theme_picker_element>(themes()), { 0.0f, 0.0f, 700.0f, 200.0f }, 1.0f) };
        CHECK(themes_tree.root()->accessibility().name == picker);
        REQUIRE(themes_tree.root()->children().size() == cards.size());
        for (std::size_t index = 0; index < cards.size(); ++index)
            CHECK(themes_tree.root()->children()[index]->accessibility().name == cards[index]);

        const luil::ui_tree accent_tree { luil::make_arranged_tree(std::make_unique<luil::accent_picker_element>(accents), { 0.0f, 0.0f, 400.0f, 200.0f }, 1.0f) };
        CHECK(accent_tree.root()->accessibility().name == accent_picker);
        // 툴팁과 접근 이름이 같은 글이다.
        CHECK(swatch(accent_tree, u8"blue").accessibility().name == blue);
        CHECK(swatch(accent_tree, u8"blue").tooltip() == blue);
        CHECK(swatch(accent_tree, luil::system_accent_id).accessibility().name == system);
    };

    check_language(u8"테마", u8"강조색", { u8"밝게", u8"시스템", u8"어둡게" }, u8"블루", u8"시스템");
    guard.use(u8"en");
    check_language(u8"Theme", u8"Accent color", { u8"Light", u8"System", u8"Dark" }, u8"Blue", u8"System");
    guard.use(u8"ko");
    check_language(u8"테마", u8"강조색", { u8"밝게", u8"시스템", u8"어둡게" }, u8"블루", u8"시스템");
}

TEST_CASE("The picker fields the app fills are used as given and the others stay default", "[ui][theme-picker][default-text]")
{
    auto config { themes() };
    config.name = u8"Theme";
    // 칸마다 따로 판정한다. 가운데 하나만 덮어도 나머지는 기본 낱말이다.
    config.labels[1] = u8"Auto";
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::theme_picker_element>(config), { 0.0f, 0.0f, 700.0f, 200.0f }, 1.0f) };
    REQUIRE(tree.root()->accessibility().name == u8"Theme");
    REQUIRE(tree.find({ luil::ui_element_kind::choice, u8"appearance/light" })->accessibility().name == u8"밝게");
    REQUIRE(tree.find({ luil::ui_element_kind::choice, u8"appearance/system" })->accessibility().name == u8"Auto");
    REQUIRE(tree.find({ luil::ui_element_kind::choice, u8"appearance/dark" })->accessibility().name == u8"어둡게");

    // 공백만 있는 글은 앱이 준 글이다. 기본 낱말로 바꾸지 않는다.
    config.labels[0] = u8" ";
    const luil::ui_tree spaced { luil::make_arranged_tree(std::make_unique<luil::theme_picker_element>(config), { 0.0f, 0.0f, 700.0f, 200.0f }, 1.0f) };
    REQUIRE(spaced.find({ luil::ui_element_kind::choice, u8"appearance/light" })->accessibility().name == u8" ");

    luil::accent_picker_config accents {};
    accents.owner = u8"accent";
    accents.name = u8"Accent color";
    const luil::ui_tree accent_tree { luil::make_arranged_tree(std::make_unique<luil::accent_picker_element>(accents), { 0.0f, 0.0f, 400.0f, 200.0f }, 1.0f) };
    REQUIRE(accent_tree.root()->accessibility().name == u8"Accent color");
}
