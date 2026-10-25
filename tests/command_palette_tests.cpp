#include "luil/ui/command_palette.h"

#include "luil/ui/app_message.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 명령 팔레트는 명령표 하나로 거르기·줄·Enter·닫기를 짓는다 (command-palette-design.md).

namespace {
    using luil::key_code;

    constexpr luil::text_input_target palette_target { 9 };

    struct run_intent
    {
        std::u8string id {};
    };

    struct close_intent
    {};

    struct edit_intent
    {};

    [[nodiscard]] std::vector<luil::app_command> sample_commands()
    {
        std::vector<luil::app_command> commands {};
        commands.push_back({ .id = u8"file.save", .label = u8"저장", .shortcut = { key_code::key_s, true } });
        commands.push_back({ .id = u8"file.save_as", .label = u8"다른 이름으로 저장", .shortcut = { key_code::key_s, true, true } });
        commands.push_back({ .id = u8"view.zoom_in", .label = u8"Zoom In" });
        commands.push_back({ .id = u8"view.toggle_sidebar", .label = u8"Toggle Sidebar", .enabled = false });
        luil::app_command find { .id = u8"edit.find", .label = u8"Find in Files", .shortcut = { key_code::key_f, true } };
        // 사용자가 다시 정한 단축키가 열에 선다.
        find.user_shortcuts = std::vector<luil::key_chord> { { key_code::key_f, true, true } };
        commands.push_back(std::move(find));
        return commands;
    }

    [[nodiscard]] luil::command_palette_config palette_with(const std::u8string_view query)
    {
        luil::command_palette_config config {};
        config.owner = u8"commands";
        config.commands = sample_commands();
        config.query.text = std::u8string { query };
        config.query.caret = query.size();
        config.query.anchor = query.size();
        config.placeholder = u8"명령 찾기";
        config.empty_label = u8"일치하는 명령 없음";
        config.run = [](const std::u8string& id) { return luil::input_action { luil::app_message { run_intent { id } } }; };
        config.close = [](const luil::ui_action_context&) { return std::vector<luil::input_action> { luil::input_action { luil::app_message { close_intent {} } } }; };
        return config;
    }

    [[nodiscard]] std::vector<std::u8string> keys_of(const std::vector<luil::menu_item_config>& items)
    {
        std::vector<std::u8string> keys {};
        for (const luil::menu_item_config& item : items)
            keys.push_back(item.key);
        return keys;
    }

    class palette_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            return kind == luil::ui_element_kind::command_palette_search ? std::optional<luil::text_input_target> { palette_target } : std::nullopt;
        }

        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request&) const override
        {
            return luil::input_action { luil::app_message { edit_intent {} } };
        }

        [[nodiscard]] std::optional<luil::menu_kinds> menu() const override
        {
            return luil::menu_kinds { luil::ui_element_kind::menu, luil::ui_element_kind::menu_item };
        }

        [[nodiscard]] std::vector<luil::input_action> close_menu() const override
        {
            return { luil::input_action { luil::app_message { close_intent {} } } };
        }
    };

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> palette_tree(luil::command_palette_config config)
    {
        const float height { luil::command_palette_height(config) };
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(luil::make_command_palette(std::move(config)), { 0.0f, 0.0f, 480.0f, height }, 1.0f));
    }

    // 실행이면 그 id, 닫기면 "close"를 차례로 적는다.
    [[nodiscard]] std::vector<std::u8string> outcome(const std::vector<luil::input_action>& actions)
    {
        std::vector<std::u8string> result {};
        for (const luil::input_action& action : actions)
        {
            const auto* const message { std::get_if<luil::app_message>(&action) };
            if (message == nullptr)
                continue;
            if (const auto* const run { message->get<run_intent>() }; run != nullptr)
                result.push_back(run->id);
            else if (message->get<close_intent>() != nullptr)
                result.push_back(u8"close");
        }
        return result;
    }
} // namespace

TEST_CASE("Commands are filtered by label start, word start, contained text and id", "[palette][command]")
{
    const std::vector<luil::app_command> commands { sample_commands() };
    // 이름이 질의로 시작하는 것이 먼저, 이름 안 낱말이 질의로 시작하는 것이 다음이다.
    REQUIRE(luil::filter_commands(commands, u8"저장") == std::vector<std::size_t> { 0u, 1u });
    REQUIRE(luil::filter_commands(commands, u8"in") == std::vector<std::size_t> { 2u, 4u });
    // 대소문자를 접는다.
    REQUIRE(luil::filter_commands(commands, u8"ZOOM") == std::vector<std::size_t> { 2u });
    // 이름에 없으면 id로 찾는다 — 영문 id로 한글 이름의 명령을 부른다.
    REQUIRE(luil::filter_commands(commands, u8"save") == std::vector<std::size_t> { 0u, 1u });
    // 이름 가운데에 든 것은 낱말 시작보다 뒤다.
    REQUIRE(luil::filter_commands(commands, u8"idebar") == std::vector<std::size_t> { 3u });
    REQUIRE(luil::filter_commands(commands, u8"없는 명령").empty());
    // 빈 질의는 전부를 명령표 차례로 준다.
    REQUIRE(luil::filter_commands(commands, u8"  ") == std::vector<std::size_t> { 0u, 1u, 2u, 3u, 4u });
}

TEST_CASE("Palette rows come from the command table with their real shortcuts", "[palette][command]")
{
    const std::vector<luil::menu_item_config> all { luil::command_palette_items(palette_with(u8"")) };
    REQUIRE(keys_of(all) == std::vector<std::u8string> { u8"file.save", u8"file.save_as", u8"view.zoom_in", u8"view.toggle_sidebar", u8"edit.find" });
    REQUIRE(all[0].shortcut_text == u8"Ctrl+S");
    // 다시 정한 단축키가 기본 단축키를 대신한다.
    REQUIRE(all[4].shortcut_text == u8"Ctrl+Shift+F");
    // 꺼진 명령은 흐리게 서고 고를 수 없다.
    REQUIRE(all[3].enabled == false);

    // 결과가 없으면 고를 수 없는 안내 줄 하나다.
    const std::vector<luil::menu_item_config> none { luil::command_palette_items(palette_with(u8"없는 명령")) };
    REQUIRE(none.size() == 1u);
    REQUIRE(none[0].label == u8"일치하는 명령 없음");
    REQUIRE(none[0].enabled == false);

    // 줄 수 상한을 넘는 결과는 잘라 낸다.
    luil::command_palette_config short_list { palette_with(u8"") };
    short_list.maximum_rows = 2;
    REQUIRE(luil::command_palette_items(short_list).size() == 2u);
}

TEST_CASE("A composing query falls back to its committed text when it matches nothing", "[palette][command][ime]")
{
    // "다른 ㅇ"은 어디에도 없다. 확정 글 "다른"으로 물러서 목록이 비지 않는다.
    luil::command_palette_config config { palette_with(u8"다른") };
    config.query.composing = true;
    config.query.composition_text = u8"다른 ㅇ";
    REQUIRE(keys_of(luil::command_palette_items(config)) == std::vector<std::u8string> { u8"file.save_as" });
}

TEST_CASE("Enter runs the first result and the palette closes after running", "[palette][command][menu]")
{
    palette_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(palette_tree(palette_with(u8"저장")));
    // 검색 칸에 초점을 준다. 메뉴가 열린 동안 Enter·↑↓는 메뉴의 것이다.
    const float field_y { luil::command_palette_padding + luil::command_palette_field_height / 2.0f };
    static_cast<void>(controller.process(luil::pointer_pressed_event { 40.0f, field_y, luil::pointer_button::left }));
    static_cast<void>(controller.process(luil::pointer_released_event { 40.0f, field_y, luil::pointer_button::left }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { luil::ui_element_kind::command_palette_search, u8"commands" });

    // 강조가 없으면 첫 결과다. 실행 다음에 닫기가 함께 나간다.
    REQUIRE(outcome(controller.process(luil::key_pressed_event { key_code::enter })) == std::vector<std::u8string> { u8"file.save", u8"close" });

    // ↓ 두 번이면 둘째 결과다.
    static_cast<void>(controller.process(luil::key_pressed_event { key_code::arrow_down }));
    static_cast<void>(controller.process(luil::key_pressed_event { key_code::arrow_down }));
    REQUIRE(outcome(controller.process(luil::key_pressed_event { key_code::enter })) == std::vector<std::u8string> { u8"file.save_as", u8"close" });

    // 질의가 바뀌어 강조한 항목이 사라지면 Enter는 새 첫 결과다.
    controller.set_tree(palette_tree(palette_with(u8"zoom")));
    REQUIRE(outcome(controller.process(luil::key_pressed_event { key_code::enter })) == std::vector<std::u8string> { u8"view.zoom_in", u8"close" });

    // 결과가 없으면 Enter는 아무것도 하지 않는다. Esc는 정책의 닫기다.
    controller.set_tree(palette_tree(palette_with(u8"없는 명령")));
    REQUIRE(outcome(controller.process(luil::key_pressed_event { key_code::enter })).empty());
    REQUIRE(outcome(controller.process(luil::key_pressed_event { key_code::escape })) == std::vector<std::u8string> { u8"close" });
}

TEST_CASE("Clicking a palette row runs it and closes", "[palette][command][menu]")
{
    const auto tree { palette_tree(palette_with(u8"in")) };
    const luil::ui_element* const row { tree->find({ luil::ui_element_kind::menu_item, u8"edit.find" }) };
    REQUIRE(row != nullptr);
    const luil::ui_action* const action { row->action(luil::ui_trigger::left_click) };
    REQUIRE(action != nullptr);
    REQUIRE(outcome((*action)(luil::ui_action_context { row->id() })) == std::vector<std::u8string> { u8"edit.find", u8"close" });
    // 꺼진 명령은 눌러도 실행되지 않는다.
    const auto all { palette_tree(palette_with(u8"")) };
    REQUIRE(all->find({ luil::ui_element_kind::menu_item, u8"view.toggle_sidebar" })->enabled() == false);
}

TEST_CASE("The palette entry is a caption client button with the shortcut in its tooltip", "[palette][command][caption]")
{
    luil::command_palette_entry_config config {
        .owner = u8"caption/commands",
        .text = u8"명령 찾기…",
        .tooltip = u8"명령 찾기",
        .shortcut = { key_code::key_p, true, true },
        .open = [](const luil::ui_action_context&) { return std::vector<luil::input_action> { luil::input_action { luil::app_message { run_intent { u8"open" } } } }; },
    };
    const auto entry { luil::make_command_palette_entry(std::move(config)) };
    REQUIRE(entry->caption_client());
    REQUIRE(entry->tooltip() == u8"명령 찾기 (Ctrl+Shift+P)");
    REQUIRE(entry->id() == luil::ui_element_id { luil::ui_element_kind::command_palette_entry, u8"caption/commands" });
    REQUIRE(outcome((*entry->action(luil::ui_trigger::left_click))(luil::ui_action_context { entry->id() })) == std::vector<std::u8string> { u8"open" });
}
