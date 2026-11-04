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

namespace {
    // 드롭다운 둘(renderer 10~170, theme 200~360)을 담은 tree다. renderer만 열고 닫는다. theme은 늘 닫혀 있다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> pair_tree(const bool renderer_open)
    {
        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 100.0f }, 1.0f });
        float x { 10.0f };
        for (const char8_t* const owner : { u8"renderer", u8"theme" })
        {
            luil::dropdown_config config {};
            config.owner = owner;
            config.text = owner;
            config.open = renderer_open && config.owner == u8"renderer";
            config.set_open = [](const bool target) { return luil::make_app_action(open_intent { target }); };
            auto dropdown { std::make_unique<luil::dropdown_element>(std::move(config)) };
            dropdown->arrange({ { x, 10.0f, 160.0f, luil::dropdown_height }, 1.0f });
            root->add(std::move(dropdown));
            x += 190.0f;
        }
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    [[nodiscard]] luil::pointer_pressed_event closing_press_at(const float x, const int milliseconds, const luil::pointer_device device, const bool closed_popup)
    {
        luil::pointer_pressed_event event { x, 20.0f, luil::pointer_button::left, at(milliseconds) };
        event.device = device;
        event.closed_popup = closed_popup;
        return event;
    }

    [[nodiscard]] luil::pointer_released_event release_on(const float x, const int milliseconds, const luil::pointer_device device)
    {
        luil::pointer_released_event event { x, 20.0f, luil::pointer_button::left, at(milliseconds) };
        event.device = device;
        return event;
    }
}

TEST_CASE("Re-pressing an open dropdown closes it even when the closed frame arrives before the press", "[ui][dropdown][reopen]")
{
    // 표면은 popup 밖 누름에서 닫자는 메시지를 앱에 먼저 내고 누름을 입력 thread에 넣는다. 앱이 닫힌 frame을 먼저 게시하면
    // controller가 누름을 받을 때 칸은 이미 접혀 있다. 실기기에서 열린 칸을 다시 탭하면 여섯 번 가운데 네 번 도로 열렸다.
    // 표면이 그 누름에 "popup을 닫았다"를 실으면 직전 tree의 펼친 칸 액션(`set_open(false)`)이 나간다 (menu-focus-fixes-design.md의 X-4).
    for (const luil::pointer_device device : { luil::pointer_device::mouse, luil::pointer_device::touch })
    {
        CAPTURE(device == luil::pointer_device::touch);
        luil::interaction_controller controller {};
        controller.set_tree(pair_tree(true));
        controller.set_tree(pair_tree(false));
        static_cast<void>(controller.process(closing_press_at(100.0f, 0, device, true)));
        REQUIRE(describe(controller.process(release_on(100.0f, 50, device))) == described_actions { u8"close" });

        // popup을 닫지 않은 누름은 닫힌 tree의 것이라 연다. 항목을 골라 닫은 뒤 다시 누르는 경우다.
        controller.set_tree(pair_tree(true));
        controller.set_tree(pair_tree(false));
        static_cast<void>(controller.process(closing_press_at(100.0f, 1000, device, false)));
        REQUIRE(describe(controller.process(release_on(100.0f, 1050, device))) == described_actions { u8"open" });

        // popup을 닫은 누름이어도 그 popup을 연 칸이 아니면 그대로 연다. 열린 동안 다른 칸을 누르면 그 칸이 열린다.
        controller.set_tree(pair_tree(true));
        controller.set_tree(pair_tree(false));
        static_cast<void>(controller.process(closing_press_at(280.0f, 2000, device, true)));
        REQUIRE(describe(controller.process(release_on(280.0f, 2050, device))) == described_actions { u8"open" });
    }
}
