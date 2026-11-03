#include "luil/ui/dropdown_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/root_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    struct toggle_intent
    {
        bool open { false };
    };
}

TEST_CASE("A dropdown toggles only when the config gives an action", "[ui][dropdown]")
{
    const luil::dropdown_element inert { luil::dropdown_config {} };
    REQUIRE(inert.action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(inert.interactive() == false);

    luil::dropdown_config config {};
    config.owner = u8"renderer";
    config.text = u8"CPU";
    config.toggle = [](const luil::ui_action_context&) -> std::vector<luil::input_action> {
        return { luil::input_action { luil::app_message { toggle_intent { true } } } };
    };
    luil::dropdown_element dropdown { std::move(config) };
    dropdown.arrange({ { 10.0f, 10.0f, 160.0f, luil::dropdown_height }, 1.0f });
    REQUIRE(dropdown.id() == luil::ui_element_id { luil::ui_element_kind::dropdown, u8"renderer" });
    REQUIRE(dropdown.cursor() == luil::ui_cursor::hand);
    REQUIRE(dropdown.bounds().height == luil::dropdown_height);

    // 칸 어디를 눌러도 여닫자는 메시지가 난다.
    REQUIRE(dropdown.hit_test(100.0f, 20.0f) == &dropdown);
    auto actions { (*dropdown.action(luil::ui_trigger::left_click))({ dropdown.id(), 100.0f, 20.0f }) };
    REQUIRE(actions.size() == 1);
    auto* const message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<toggle_intent>()->open);
}

namespace {
    struct open_intent
    {
        bool open { false };
    };

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    // 절대 메시지(`set_open`)만 준 드롭다운(10, 10, 160x28) 하나를 담은 tree다. 클릭은 `set_open(!open)`이다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> renderer_tree(const bool open)
    {
        luil::dropdown_config config {};
        config.owner = u8"renderer";
        config.text = u8"CPU";
        config.open = open;
        config.set_open = [](const bool target) { return luil::make_app_action(open_intent { target }); };
        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 100.0f }, 1.0f });
        auto dropdown { std::make_unique<luil::dropdown_element>(std::move(config)) };
        dropdown->arrange({ { 10.0f, 10.0f, 160.0f, luil::dropdown_height }, 1.0f });
        root->add(std::move(dropdown));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    // 액션 하나가 여는 메시지면 "open", 닫는 메시지면 "close"다.
    [[nodiscard]] std::vector<std::u8string> describe(const std::vector<luil::input_action>& actions)
    {
        std::vector<std::u8string> described {};
        for (const luil::input_action& action : actions)
        {
            const auto* const message { std::get_if<luil::app_message>(&action) };
            const auto* const open { message != nullptr ? message->get<open_intent>() : nullptr };
            described.push_back(open == nullptr ? u8"?" : (open->open ? u8"open" : u8"close"));
        }
        return described;
    }

    using described_actions = std::vector<std::u8string>;

    [[nodiscard]] luil::pointer_pressed_event press_at(const int milliseconds, const luil::pointer_device device)
    {
        luil::pointer_pressed_event event { 100.0f, 20.0f, luil::pointer_button::left, at(milliseconds) };
        event.device = device;
        return event;
    }

    [[nodiscard]] luil::pointer_released_event release_at(const int milliseconds, const luil::pointer_device device)
    {
        luil::pointer_released_event event { 100.0f, 20.0f, luil::pointer_button::left, at(milliseconds) };
        event.device = device;
        return event;
    }
}

TEST_CASE("Re-pressing an open dropdown closes it even once the closed frame is published", "[ui][dropdown][reopen]")
{
    // 바깥 누름의 닫힘이 누름과 뗌 사이에 게시되어도 펼쳐 있던 칸의 클릭은 누를 때의 액션이다 (menu-focus-fixes-design.md의 X-4).
    for (const luil::pointer_device device : { luil::pointer_device::mouse, luil::pointer_device::touch })
    {
        luil::interaction_controller controller {};
        controller.set_tree(renderer_tree(true));
        static_cast<void>(controller.process(press_at(0, device)));
        controller.set_tree(renderer_tree(false));
        REQUIRE(describe(controller.process(release_at(50, device))) == described_actions { u8"close" });

        // 게시가 없어도 같은 닫힘이다.
        controller.set_tree(renderer_tree(true));
        static_cast<void>(controller.process(press_at(1000, device)));
        REQUIRE(describe(controller.process(release_at(1050, device))) == described_actions { u8"close" });

        // 닫힌 칸의 누름은 그대로 연다.
        controller.set_tree(renderer_tree(false));
        static_cast<void>(controller.process(press_at(2000, device)));
        REQUIRE(describe(controller.process(release_at(2050, device))) == described_actions { u8"open" });
    }
}
