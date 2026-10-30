#include "luil/ui/virtual_list_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/selection_model.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_row_button { luil::application_element_kind(0) };

    struct move_intent
    {
        std::u8string key {};
    };

    struct move_with_intent
    {
        std::u8string key {};
        luil::virtual_list_move_context context {};
    };

    struct toggle_intent
    {
        std::u8string key {};
    };

    struct select_all_intent
    {
    };

    struct policy_key_intent
    {
    };

    class recording_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            ++keys;
            return { luil::make_app_action(policy_key_intent {}) };
        }

        int keys { 0 };
    };

    // 행 안에 선 단추다. 초점을 받는다.
    class row_button final : public luil::ui_element
    {
    public:
        explicit row_button(std::u8string key)
            : ui_element { luil::ui_element_id { kind_row_button, std::move(key) } }
        {
            set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
    };

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] luil::key_pressed_event key(const luil::key_code code, const bool control, const bool shift, const int milliseconds)
    {
        return luil::key_pressed_event { code, control, shift, false, false, at(milliseconds) };
    }

    [[nodiscard]] std::vector<luil::virtual_list_item> make_items(const std::size_t count)
    {
        std::vector<luil::virtual_list_item> items {};
        for (std::size_t index = 0; index < count; ++index)
        {
            luil::virtual_list_item item {};
            item.key = std::u8string { u8"item-" } + static_cast<char8_t>(u8'0' + index);
            item.label = item.key;
            items.push_back(std::move(item));
        }
        return items;
    }

    [[nodiscard]] luil::virtual_list_config base_config()
    {
        luil::virtual_list_config config {};
        config.owner = u8"files";
        config.row_height = 20.0f;
        config.items = make_items(8);
        config.multiple_selection = true;
        config.cursor = u8"item-2";
        return config;
    }

    [[nodiscard]] luil::virtual_list_config selecting_config()
    {
        luil::virtual_list_config config { base_config() };
        config.move_with = [](const std::u8string& key, const luil::virtual_list_move_context& context) { return luil::make_app_action(move_with_intent { key, context }); };
        config.toggle_at_cursor = [](const std::u8string& key) { return luil::make_app_action(toggle_intent { key }); };
        config.select_all = [] { return luil::make_app_action(select_all_intent {}); };
        return config;
    }

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> published(luil::virtual_list_config config)
    {
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::make_unique<luil::virtual_list_element>(std::move(config)), { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f));
    }

    template<typename message_type>
    [[nodiscard]] const message_type* message_at(const std::vector<luil::input_action>& actions, const std::size_t index)
    {
        if (index >= actions.size())
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions[index]) };
        return message != nullptr ? message->get<message_type>() : nullptr;
    }

    // 목록에 Tab으로 초점을 준다.
    void focus_list(luil::interaction_controller& controller)
    {
        static_cast<void>(controller.process(key(luil::key_code::tab, false, false, 0)));
        REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });
    }
}

TEST_CASE("A list with only move keeps its old keys: Shift is invisible and Ctrl+arrow belongs to the app", "[ui][list][selection]")
{
    luil::virtual_list_config config { base_config() };
    config.move = [](const std::u8string& key) { return luil::make_app_action(move_intent { key }); };
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(published(config));
    focus_list(controller);

    const auto shifted { controller.process(key(luil::key_code::arrow_down, false, true, 10)) };
    REQUIRE(message_at<move_intent>(shifted, 0) != nullptr);
    REQUIRE(message_at<move_intent>(shifted, 0)->key == u8"item-3");

    const auto controlled { controller.process(key(luil::key_code::arrow_down, true, false, 20)) };
    REQUIRE(message_at<policy_key_intent>(controlled, 0) != nullptr);
    REQUIRE(policy.keys == 1);

    // select_all이 없으니 Ctrl+A도 앱의 것이다.
    static_cast<void>(controller.process(key(luil::key_code::key_a, true, false, 30)));
    REQUIRE(policy.keys == 2);
}

TEST_CASE("move_with carries the step and the meaning of Shift and Ctrl", "[ui][list][selection]")
{
    luil::interaction_controller controller {};
    controller.set_tree(published(selecting_config()));
    focus_list(controller);

    const auto plain { controller.process(key(luil::key_code::arrow_down, false, false, 10)) };
    REQUIRE(message_at<move_with_intent>(plain, 0) != nullptr);
    REQUIRE(message_at<move_with_intent>(plain, 0)->key == u8"item-3");
    REQUIRE(message_at<move_with_intent>(plain, 0)->context == luil::virtual_list_move_context { luil::value_step::increase, false, false });

    const auto shifted { controller.process(key(luil::key_code::end, false, true, 20)) };
    REQUIRE(message_at<move_with_intent>(shifted, 0) != nullptr);
    REQUIRE(message_at<move_with_intent>(shifted, 0)->key == u8"item-7");
    REQUIRE(message_at<move_with_intent>(shifted, 0)->context == luil::virtual_list_move_context { luil::value_step::maximum, true, false });

    const auto controlled { controller.process(key(luil::key_code::arrow_up, true, false, 30)) };
    REQUIRE(message_at<move_with_intent>(controlled, 0) != nullptr);
    REQUIRE(message_at<move_with_intent>(controlled, 0)->key == u8"item-1");
    REQUIRE(message_at<move_with_intent>(controlled, 0)->context == luil::virtual_list_move_context { luil::value_step::decrease, false, true });

    const auto both { controller.process(key(luil::key_code::page_down, true, true, 40)) };
    REQUIRE(message_at<move_with_intent>(both, 0) != nullptr);
    REQUIRE(message_at<move_with_intent>(both, 0)->context.extend);
    REQUIRE(message_at<move_with_intent>(both, 0)->context.keep_selection);
}

TEST_CASE("A cursor-only step in place says nothing but a selecting step in place still speaks", "[ui][list][selection]")
{
    luil::virtual_list_config config { selecting_config() };
    config.cursor = u8"item-0";
    luil::interaction_controller controller {};
    controller.set_tree(published(config));
    focus_list(controller);

    // 첫 항목에서 Ctrl+Home은 커서만 옮기는데 옮길 곳이 없다.
    REQUIRE(message_at<move_with_intent>(controller.process(key(luil::key_code::home, true, false, 10)), 0) == nullptr);
    // 그냥 Home은 커서는 제자리여도 선택이 그 하나로 접히므로 낸다.
    const auto home { controller.process(key(luil::key_code::home, false, false, 20)) };
    REQUIRE(message_at<move_with_intent>(home, 0) != nullptr);
    REQUIRE(message_at<move_with_intent>(home, 0)->key == u8"item-0");
}

TEST_CASE("Ctrl+A and Ctrl+Space are the list's own keys", "[ui][list][selection]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(published(selecting_config()));
    focus_list(controller);

    REQUIRE(message_at<select_all_intent>(controller.process(key(luil::key_code::key_a, true, false, 10)), 0) != nullptr);
    const auto toggled { controller.process(key(luil::key_code::space, true, false, 20)) };
    REQUIRE(message_at<toggle_intent>(toggled, 0) != nullptr);
    REQUIRE(message_at<toggle_intent>(toggled, 0)->key == u8"item-2");
    REQUIRE(policy.keys == 0);

    // 쓰지 않는 조합은 앱으로 흐른다.
    static_cast<void>(controller.process(key(luil::key_code::key_a, true, true, 30)));
    REQUIRE(policy.keys == 1);
}

TEST_CASE("Ctrl+A reaches the list from a button inside a custom row", "[ui][list][selection][keys]")
{
    luil::virtual_list_config config { selecting_config() };
    config.build_row = [](const luil::virtual_list_item& item, std::size_t, bool) -> std::unique_ptr<luil::ui_element> { return std::make_unique<row_button>(item.key); };
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(published(config));

    // 첫 행의 단추를 눌러 초점을 단추에 둔다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 100.0f, 10.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_released_event { 100.0f, 10.0f, luil::pointer_button::left, at(5) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_row_button, u8"item-0" });

    REQUIRE(message_at<select_all_intent>(controller.process(key(luil::key_code::key_a, true, false, 10)), 0) != nullptr);
    REQUIRE(policy.keys == 0);
}

TEST_CASE("A row click with only move_with moves the cursor without touching the selection", "[ui][list][selection]")
{
    luil::virtual_list_config config { selecting_config() };
    config.pick = [](const std::u8string& key, const luil::ui_action_context&) { return luil::make_app_action(toggle_intent { key }); };
    luil::interaction_controller controller {};
    controller.set_tree(published(config));

    static_cast<void>(controller.process(luil::pointer_pressed_event { 100.0f, 90.0f, luil::pointer_button::left, at(0) }));
    const auto clicked { controller.process(luil::pointer_released_event { 100.0f, 90.0f, luil::pointer_button::left, at(5) }) };
    REQUIRE(message_at<toggle_intent>(clicked, 0) != nullptr);
    REQUIRE(message_at<move_with_intent>(clicked, 1) != nullptr);
    REQUIRE(message_at<move_with_intent>(clicked, 1)->key == u8"item-4");
    REQUIRE(message_at<move_with_intent>(clicked, 1)->context == luil::virtual_list_move_context { std::nullopt, false, true });
}

TEST_CASE("Type ahead with move_with is a plain move without a step", "[ui][list][selection]")
{
    luil::interaction_controller controller {};
    controller.set_tree(published(selecting_config()));
    focus_list(controller);

    luil::character_typed_event typed {};
    typed.character = U'i';
    typed.time = at(10);
    const auto found { controller.process(typed) };
    REQUIRE(message_at<move_with_intent>(found, 0) != nullptr);
    REQUIRE(message_at<move_with_intent>(found, 0)->context == luil::virtual_list_move_context {});
}

TEST_CASE("The selection helpers turn list messages into the next selection", "[ui][list][selection]")
{
    // 앱이 하는 일 전체다: 메시지를 받아 도우미로 다음 값을 계산한다.
    const std::vector<luil::virtual_list_item> items { make_items(8) };
    luil::item_selection selection { u8"item-2", u8"item-2", { u8"item-2" } };
    selection = luil::selection_move(selection, items, u8"item-3", { luil::value_step::increase, true, false });
    selection = luil::selection_move(selection, items, u8"item-4", { luil::value_step::increase, true, false });
    REQUIRE(selection.keys == std::vector<std::u8string> { u8"item-2", u8"item-3", u8"item-4" });
    REQUIRE(selection.anchor == u8"item-2");
    REQUIRE(selection.cursor == u8"item-4");
}
