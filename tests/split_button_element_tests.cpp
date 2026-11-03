#include "luil/ui/split_button_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/root_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_other { luil::application_element_kind(0) };

    const luil::ui_element_id root_id { luil::ui_element_kind::split_button, u8"save" };
    const luil::ui_element_id body_id { luil::ui_element_kind::split_button_body, u8"save" };
    const luil::ui_element_id arrow_id { luil::ui_element_kind::split_button_arrow, u8"save" };
    const luil::ui_element_id other_id { kind_other, u8"other" };

    struct invoke_intent
    {
    };

    struct open_intent
    {
        bool open { false };
    };

    // 앱 정책까지 온 키의 수를 센다. 처리기가 소비한 키는 여기까지 오지 않는다.
    class key_counting_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            ++keys;
            return {};
        }

        int keys { 0 };
    };

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] luil::split_button_config save_config(const bool open = false)
    {
        luil::split_button_config config {};
        config.owner = u8"save";
        config.look.text = u8"저장";
        config.invoke = luil::make_message_action(invoke_intent {});
        config.open = open;
        config.set_open = [](const bool target) { return luil::make_app_action(open_intent { target }); };
        config.tooltip = u8"문서를 저장한다";
        config.arrow_tooltip = u8"저장 옵션";
        return config;
    }

    // 분할 버튼(10, 10, 160x28)과 그 옆의 다른 버튼 하나를 담은 tree다. 배율은 1이다.
    // 화살표는 오른쪽 28 폭이라 x가 142~170이다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_tree(luil::split_button_config config, const float width = 160.0f)
    {
        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 100.0f }, 1.0f });
        auto split { std::make_unique<luil::split_button_element>(std::move(config)) };
        split->arrange({ { 10.0f, 10.0f, width, 28.0f }, 1.0f });
        root->add(std::move(split));
        auto other { std::make_unique<luil::text_button_element>(other_id, luil::text_button_config { .text = u8"다른" }) };
        other->arrange({ { 300.0f, 10.0f, 80.0f, 28.0f }, 1.0f });
        other->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; });
        root->add(std::move(other));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    // 액션의 뜻을 짧은 글로 옮긴다. 열기·닫기·실행 외의 것은 "?"다.
    [[nodiscard]] std::u8string describe(const luil::input_action& action)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        if (message == nullptr)
            return u8"?";
        if (const auto* const open { message->get<open_intent>() }; open != nullptr)
            return open->open ? u8"open" : u8"close";
        if (message->get<invoke_intent>() != nullptr)
            return u8"invoke";
        return u8"?";
    }

    [[nodiscard]] std::vector<std::u8string> describe(const std::vector<luil::input_action>& actions)
    {
        std::vector<std::u8string> described {};
        for (const luil::input_action& action : actions)
            described.push_back(describe(action));
        return described;
    }

    using described_actions = std::vector<std::u8string>;

    // 키를 넣는 controller 한 벌이다. 시계는 키마다 10ms씩 간다.
    struct keyboard
    {
        key_counting_policy policy {};
        luil::interaction_controller controller { &policy };
        int clock { 0 };

        explicit keyboard(luil::split_button_config config)
        {
            controller.set_tree(make_tree(std::move(config)));
        }

        std::vector<luil::input_action> press(const luil::key_code code, const bool control = false, const bool shift = false, const bool alt = false)
        {
            luil::key_pressed_event event {};
            event.key = code;
            event.control = control;
            event.shift = shift;
            event.alt = alt;
            event.time = at(clock += 10);
            return controller.process(event);
        }

        std::vector<luil::input_action> alt_down()
        {
            return press(luil::key_code::arrow_down, false, false, true);
        }

        [[nodiscard]] luil::ui_element_id focused() const
        {
            return controller.snapshot().focused;
        }
    };

    // 글자 하나가 글꼴 크기의 절반 폭인 측정기다. 12px 글 네 자가 24px이다.
    const luil::text_measurer half_width { [](const std::u8string_view text, const float pixel_size) { return static_cast<float>(text.size()) * pixel_size * 0.5f; } };
}

TEST_CASE("A split button stands as a structural root over a body and an arrow", "[ui][split-button]")
{
    const auto owner { make_tree(save_config()) };
    const luil::ui_tree& tree { *owner };
    const luil::ui_element* const root { tree.find(root_id) };
    const luil::ui_element* const body { tree.find(body_id) };
    const luil::ui_element* const arrow { tree.find(arrow_id) };
    REQUIRE(root != nullptr);
    REQUIRE(body != nullptr);
    REQUIRE(arrow != nullptr);
    REQUIRE(root->children().size() == 2u);
    REQUIRE(root->children()[0].get() == body);
    REQUIRE(root->children()[1].get() == arrow);

    // 화살표는 오른쪽 기본 폭이고 본체가 나머지다. 둘 사이에 틈이 없다.
    REQUIRE(root->bounds().x == 10.0f);
    REQUIRE(body->bounds().x == 10.0f);
    REQUIRE(body->bounds().width == 160.0f - luil::split_button_arrow_width);
    REQUIRE(arrow->bounds().x == body->bounds().x + body->bounds().width);
    REQUIRE(arrow->bounds().width == luil::split_button_arrow_width);
    REQUIRE(arrow->bounds().height == 28.0f);
    REQUIRE(arrow->bounds().x + arrow->bounds().width == root->bounds().x + root->bounds().width);

    // 구분선은 화살표의 왼쪽 가장자리라서 화살표 안에 서고, 본체와 화살표 사이에 눌리지 않는 틈이 없다.
    const float edge { arrow->bounds().x };
    REQUIRE(tree.hit_test(edge, 24.0f) == arrow);
    REQUIRE(tree.hit_test(edge + 0.5f, 24.0f) == arrow);
    REQUIRE(tree.hit_test(edge - 0.5f, 24.0f) == body);
}

TEST_CASE("Both parts of a split button share the owner and keep their own ids", "[ui][split-button]")
{
    const auto owner { make_tree(save_config()) };
    const luil::ui_tree& tree { *owner };
    REQUIRE(tree.find(root_id)->id().owner == u8"save");
    REQUIRE(tree.find(body_id)->id().owner == u8"save");
    REQUIRE(tree.find(arrow_id)->id().owner == u8"save");
    REQUIRE(root_id != body_id);
    REQUIRE(body_id != arrow_id);
}

TEST_CASE("A split button scales the arrow with the surface and keeps its width in a narrow slot", "[ui][split-button]")
{
    auto build = [](const float width, const float scale, const float arrow_width = luil::split_button_arrow_width) {
        luil::split_button_config config { save_config() };
        config.arrow_width = arrow_width;
        auto split { std::make_unique<luil::split_button_element>(std::move(config)) };
        split->arrange({ { 0.0f, 0.0f, width, 56.0f }, scale });
        return split;
    };

    // 배율 2에서는 화살표가 물리 56이다.
    const auto scaled { build(320.0f, 2.0f) };
    REQUIRE(scaled->children()[1]->bounds().width == 56.0f);
    REQUIRE(scaled->children()[0]->bounds().width == 320.0f - 56.0f);

    // 앱이 폭을 바꿀 수 있다.
    const auto wide { build(160.0f, 1.0f, 44.0f) };
    REQUIRE(wide->children()[1]->bounds().width == 44.0f);

    // 좁은 슬롯에서 줄어드는 것은 본체이고 화살표 폭은 지킨다.
    const auto narrow { build(40.0f, 1.0f) };
    REQUIRE(narrow->children()[1]->bounds().width == 28.0f);
    REQUIRE(narrow->children()[0]->bounds().width == 12.0f);
    // 화살표 폭에도 못 미치는 슬롯은 화살표가 슬롯 전부를 쓴다.
    const auto tiny { build(20.0f, 1.0f) };
    REQUIRE(tiny->children()[1]->bounds().width == 20.0f);
    REQUIRE(tiny->children()[0]->bounds().width == 0.0f);
}

TEST_CASE("A split button wants the body's width plus the arrow", "[ui][split-button][layout]")
{
    const luil::measure_context measure { 1.0f, &half_width, nullptr };
    constexpr float infinite { std::numeric_limits<float>::infinity() };

    // 짧은 글은 글자 버튼의 최소 폭이다.
    luil::split_button_config config { save_config() };
    config.look.text = u8"OK";
    const luil::split_button_element short_button { config };
    REQUIRE(short_button.preferred_size(measure, infinite) == luil::size_f { luil::text_button_minimum_width + luil::split_button_arrow_width, luil::text_button_height });

    // 긴 글은 글 폭에 양옆 여백을 더하고, 글자 버튼과 같은 식이다.
    config.look.text = u8"abcdefghijkl";
    const luil::split_button_element long_button { config };
    const luil::text_button_element plain { luil::ui_element_id { kind_other, u8"plain" }, { .text = u8"abcdefghijkl" } };
    REQUIRE(long_button.preferred_size(measure, infinite)->width == plain.preferred_size(measure, infinite)->width + luil::split_button_arrow_width);
    REQUIRE(long_button.preferred_size(measure, infinite)->height == luil::text_button_height);

    // 본체가 없는 드롭다운 단추도 같은 식이다. 화살표 폭이 글 자리 밖에 선다.
    config.invoke = nullptr;
    const luil::split_button_element dropdown { config };
    REQUIRE(dropdown.preferred_size(measure, infinite) == long_button.preferred_size(measure, infinite));

    // 화살표 폭을 키우면 그만큼 넓어진다.
    config.arrow_width = 40.0f;
    const luil::split_button_element wide { config };
    REQUIRE(wide.preferred_size(measure, infinite)->width == long_button.preferred_size(measure, infinite)->width + 12.0f);
}

TEST_CASE("Without an invoke action only the arrow stands and it takes the whole slot", "[ui][split-button]")
{
    luil::split_button_config config { save_config() };
    config.invoke = nullptr;
    config.look.default_button = true;
    const auto owner { make_tree(std::move(config)) };
    const luil::ui_tree& tree { *owner };
    const luil::ui_element* const root { tree.find(root_id) };
    REQUIRE(root->children().size() == 1u);
    REQUIRE(tree.find(body_id) == nullptr);
    const luil::ui_element* const arrow { tree.find(arrow_id) };
    REQUIRE(arrow != nullptr);
    REQUIRE(arrow->bounds().x == root->bounds().x);
    REQUIRE(arrow->bounds().width == root->bounds().width);
    // 눌러야 하는 본체가 없으니 Enter의 기본 대상도 없다.
    REQUIRE(arrow->default_button() == false);
    // 한 자리뿐이라 묶음이 아니다.
    REQUIRE(root->focus_group() == luil::focus_axis::none);
}

TEST_CASE("The body fires the invoke action and the arrow fires the opposite of the open state", "[ui][split-button]")
{
    for (const bool open : { false, true })
    {
        CAPTURE(open);
        const auto owner { make_tree(save_config(open)) };
        const luil::ui_tree& tree { *owner };
        const luil::ui_element* const body { tree.find(body_id) };
        const luil::ui_element* const arrow { tree.find(arrow_id) };

        REQUIRE(body->action(luil::ui_trigger::left_click) != nullptr);
        REQUIRE(describe((*body->action(luil::ui_trigger::left_click))({ body_id, 50.0f, 24.0f })) == described_actions { u8"invoke" });
        // 누름이 build 때의 열림 반대를 담은 절대 메시지다.
        REQUIRE(arrow->action(luil::ui_trigger::left_click) != nullptr);
        REQUIRE(describe((*arrow->action(luil::ui_trigger::left_click))({ arrow_id, 156.0f, 24.0f })) == described_actions { open ? u8"close" : u8"open" });
        REQUIRE(body->cursor() == luil::ui_cursor::hand);
        REQUIRE(arrow->cursor() == luil::ui_cursor::hand);
    }
}

TEST_CASE("A disabled split button blocks both parts and invoke_enabled blocks only the body", "[ui][split-button]")
{
    {
        luil::split_button_config config { save_config() };
        config.enabled = false;
        const auto owner { make_tree(std::move(config)) };
        const luil::ui_tree& tree { *owner };
        REQUIRE(tree.find(body_id)->enabled() == false);
        REQUIRE(tree.find(arrow_id)->enabled() == false);
    }
    {
        luil::split_button_config config { save_config() };
        config.invoke_enabled = false;
        const auto owner { make_tree(std::move(config)) };
        const luil::ui_tree& tree { *owner };
        REQUIRE(tree.find(body_id)->enabled() == false);
        REQUIRE(tree.find(arrow_id)->enabled());
    }

    // 클릭으로도 막힌다. 본체는 실행되지 않고 메뉴는 그대로 열린다.
    luil::split_button_config config { save_config() };
    config.invoke_enabled = false;
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(std::move(config)));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 50.0f, 24.0f, luil::pointer_button::left, at(0) }));
    REQUIRE(controller.process(luil::pointer_released_event { 50.0f, 24.0f, luil::pointer_button::left, at(10) }).empty());
    static_cast<void>(controller.process(luil::pointer_pressed_event { 156.0f, 24.0f, luil::pointer_button::left, at(100) }));
    REQUIRE(describe(controller.process(luil::pointer_released_event { 156.0f, 24.0f, luil::pointer_button::left, at(110) })) == described_actions { u8"open" });
}

TEST_CASE("The arrow cannot be pressed without an absolute open message", "[ui][split-button]")
{
    luil::split_button_config config { save_config() };
    config.set_open = nullptr;
    const auto owner { make_tree(std::move(config)) };
    const luil::ui_tree& tree { *owner };
    const luil::ui_element* const arrow { tree.find(arrow_id) };
    REQUIRE(arrow->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(arrow->tab_stop() == false);
    REQUIRE(arrow->cursor() != luil::ui_cursor::hand);
    // 본체는 그대로 눌린다.
    REQUIRE(tree.find(body_id)->action(luil::ui_trigger::left_click) != nullptr);
}

TEST_CASE("Each part keeps its own hover and tooltip", "[ui][split-button]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(save_config()));
    static_cast<void>(controller.process(luil::pointer_moved_event { 50.0f, 24.0f, at(0) }));
    REQUIRE(controller.snapshot().hovered == body_id);
    static_cast<void>(controller.process(luil::pointer_moved_event { 156.0f, 24.0f, at(10) }));
    REQUIRE(controller.snapshot().hovered == arrow_id);

    const auto owner { make_tree(save_config()) };

    const luil::ui_tree& tree { *owner };
    REQUIRE(tree.find(body_id)->tooltip() == u8"문서를 저장한다");
    REQUIRE(tree.find(arrow_id)->tooltip() == u8"저장 옵션");

    // 본체가 없으면 버튼 전체의 툴팁이 화살표에 선다. 화살표 툴팁이 따로 있으면 그것이 먼저다.
    luil::split_button_config dropdown { save_config() };
    dropdown.invoke = nullptr;
    dropdown.arrow_tooltip = {};
    REQUIRE(make_tree(dropdown)->find(arrow_id)->tooltip() == u8"문서를 저장한다");
    dropdown.arrow_tooltip = u8"펼치기";
    REQUIRE(make_tree(dropdown)->find(arrow_id)->tooltip() == u8"펼치기");
}

TEST_CASE("Only the body is the Enter default and only when the look says so", "[ui][split-button]")
{
    luil::split_button_config config { save_config() };
    config.look.default_button = true;
    const auto owner { make_tree(config) };
    const luil::ui_tree& tree { *owner };
    REQUIRE(tree.find(body_id)->default_button());
    REQUIRE(tree.find(arrow_id)->default_button() == false);
    REQUIRE(tree.find(root_id)->default_button() == false);

    REQUIRE(make_tree(save_config())->find(body_id)->default_button() == false);
}

TEST_CASE("Tab lands once on the body and the arrow is reached with the arrow keys", "[ui][split-button][focus]")
{
    keyboard board { save_config() };
    // 한 번에 본체에 서고 두 번째 Tab은 이웃 버튼으로 간다. 화살표에서 멈추지 않는다.
    board.press(luil::key_code::tab);
    REQUIRE(board.focused() == body_id);
    board.press(luil::key_code::tab);
    REQUIRE(board.focused() == other_id);
    // 되돌아올 때도 한 자리다.
    board.press(luil::key_code::tab, false, true);
    REQUIRE(board.focused() == body_id);
    board.press(luil::key_code::tab, false, true);
    REQUIRE(board.focused() == other_id);
    board.press(luil::key_code::tab);
    REQUIRE(board.focused() == body_id);

    // ←/→는 절반 사이를 옮기고 끝에서 돈다. 앱 정책까지 가지 않는다.
    board.press(luil::key_code::arrow_right);
    REQUIRE(board.focused() == arrow_id);
    board.press(luil::key_code::arrow_right);
    REQUIRE(board.focused() == body_id);
    board.press(luil::key_code::arrow_left);
    REQUIRE(board.focused() == arrow_id);
    REQUIRE(board.policy.keys == 0);

    // Home·End는 본체·화살표로 간다.
    board.press(luil::key_code::home);
    REQUIRE(board.focused() == body_id);
    board.press(luil::key_code::end);
    REQUIRE(board.focused() == arrow_id);
}

TEST_CASE("When the body is disabled the arrow is the one Tab stop", "[ui][split-button][focus]")
{
    luil::split_button_config config { save_config() };
    config.invoke_enabled = false;
    keyboard board { std::move(config) };
    board.press(luil::key_code::tab);
    REQUIRE(board.focused() == arrow_id);
    board.press(luil::key_code::tab);
    REQUIRE(board.focused() == other_id);
}

TEST_CASE("A dropdown button without a body is a single Tab stop", "[ui][split-button][focus]")
{
    luil::split_button_config config { save_config() };
    config.invoke = nullptr;
    keyboard board { std::move(config) };
    board.press(luil::key_code::tab);
    REQUIRE(board.focused() == arrow_id);
    board.press(luil::key_code::tab);
    REQUIRE(board.focused() == other_id);
}

TEST_CASE("Alt+Down and F4 open the menu from either part", "[ui][split-button][keys]")
{
    keyboard board { save_config() };
    board.press(luil::key_code::tab);
    REQUIRE(board.focused() == body_id);
    REQUIRE(describe(board.alt_down()) == described_actions { u8"open" });
    REQUIRE(describe(board.press(luil::key_code::f4)) == described_actions { u8"open" });

    board.press(luil::key_code::arrow_right);
    REQUIRE(board.focused() == arrow_id);
    REQUIRE(describe(board.alt_down()) == described_actions { u8"open" });
    REQUIRE(describe(board.press(luil::key_code::f4)) == described_actions { u8"open" });
    // 처리기가 소비하므로 앱 정책까지 가지 않는다.
    REQUIRE(board.policy.keys == 0);
}

TEST_CASE("Down opens the menu from the arrow only and the body's Down goes on to the app", "[ui][split-button][keys]")
{
    keyboard board { save_config() };
    board.press(luil::key_code::tab);
    REQUIRE(board.focused() == body_id);
    // 목록 안의 버튼처럼 본체의 ↓는 가로채지 않는다.
    REQUIRE(board.press(luil::key_code::arrow_down).empty());
    REQUIRE(board.policy.keys == 1);

    board.press(luil::key_code::arrow_right);
    REQUIRE(board.focused() == arrow_id);
    REQUIRE(describe(board.press(luil::key_code::arrow_down)) == described_actions { u8"open" });
    REQUIRE(board.policy.keys == 1);
}

TEST_CASE("An open menu swallows the open keys and a chord with Ctrl or Shift is never taken", "[ui][split-button][keys]")
{
    keyboard open_board { save_config(true) };
    open_board.press(luil::key_code::tab);
    open_board.press(luil::key_code::arrow_right);
    REQUIRE(open_board.focused() == arrow_id);
    // 이미 열려 있으면 앱 단축키로 새지 않게 삼킨다.
    REQUIRE(open_board.alt_down().empty());
    REQUIRE(open_board.press(luil::key_code::f4).empty());
    REQUIRE(open_board.press(luil::key_code::arrow_down).empty());
    REQUIRE(open_board.policy.keys == 0);

    keyboard board { save_config() };
    board.press(luil::key_code::tab);
    board.press(luil::key_code::arrow_right);
    REQUIRE(board.focused() == arrow_id);
    // Ctrl·Shift가 함께면 앱의 단축키다. 처리기가 받지 않아 정책까지 간다.
    REQUIRE(board.press(luil::key_code::arrow_down, true, false, true).empty());
    REQUIRE(board.press(luil::key_code::arrow_down, false, true, true).empty());
    REQUIRE(board.press(luil::key_code::arrow_down, true).empty());
    REQUIRE(board.press(luil::key_code::arrow_down, false, true).empty());
    REQUIRE(board.policy.keys == 4);
}

TEST_CASE("Enter and Space run the focused part through its click path", "[ui][split-button][keys]")
{
    keyboard board { save_config() };
    board.press(luil::key_code::tab);
    REQUIRE(board.focused() == body_id);
    REQUIRE(describe(board.press(luil::key_code::enter)) == described_actions { u8"invoke" });
    REQUIRE(describe(board.press(luil::key_code::space)) == described_actions { u8"invoke" });

    board.press(luil::key_code::arrow_right);
    REQUIRE(board.focused() == arrow_id);
    REQUIRE(describe(board.press(luil::key_code::enter)) == described_actions { u8"open" });
    REQUIRE(describe(board.press(luil::key_code::space)) == described_actions { u8"open" });

    // 열려 있으면 같은 키가 닫는다.
    keyboard open_board { save_config(true) };
    open_board.press(luil::key_code::tab);
    open_board.press(luil::key_code::arrow_right);
    REQUIRE(describe(open_board.press(luil::key_code::enter)) == described_actions { u8"close" });
}

TEST_CASE("A dropdown button takes Enter, Space, Down, Alt+Down and F4 on its one part", "[ui][split-button][keys]")
{
    luil::split_button_config config { save_config() };
    config.invoke = nullptr;
    keyboard board { std::move(config) };
    board.press(luil::key_code::tab);
    REQUIRE(board.focused() == arrow_id);
    REQUIRE(describe(board.press(luil::key_code::enter)) == described_actions { u8"open" });
    REQUIRE(describe(board.press(luil::key_code::space)) == described_actions { u8"open" });
    REQUIRE(describe(board.press(luil::key_code::arrow_down)) == described_actions { u8"open" });
    REQUIRE(describe(board.alt_down()) == described_actions { u8"open" });
    REQUIRE(describe(board.press(luil::key_code::f4)) == described_actions { u8"open" });
    REQUIRE(board.policy.keys == 0);
}

TEST_CASE("Leaving the button with the keyboard closes an open menu but moving between the parts does not", "[ui][split-button][focus]")
{
    keyboard open_board { save_config(true) };
    open_board.press(luil::key_code::tab);
    REQUIRE(open_board.focused() == body_id);
    // 절반 사이의 이동은 떠남이 아니다. 돌아와도 마찬가지다.
    REQUIRE(open_board.press(luil::key_code::arrow_right).empty());
    REQUIRE(open_board.focused() == arrow_id);
    REQUIRE(open_board.press(luil::key_code::arrow_left).empty());
    REQUIRE(open_board.focused() == body_id);
    // 이웃 버튼으로 가면 목표 상태를 나르는 닫기가 나간다.
    REQUIRE(describe(open_board.press(luil::key_code::tab)) == described_actions { u8"close" });
    REQUIRE(open_board.focused() == other_id);

    // 닫혀 있으면 훅이 없다.
    keyboard closed_board { save_config() };
    closed_board.press(luil::key_code::tab);
    REQUIRE(closed_board.press(luil::key_code::tab).empty());
    REQUIRE(closed_board.focused() == other_id);

    // 훅은 열린 동안에만 root에 선다.
    REQUIRE(make_tree(save_config(true))->find(root_id)->focus_leave_action() != nullptr);
    REQUIRE(make_tree(save_config())->find(root_id)->focus_leave_action() == nullptr);
    luil::split_button_config no_message { save_config(true) };
    no_message.set_open = nullptr;
    REQUIRE(make_tree(std::move(no_message))->find(root_id)->focus_leave_action() == nullptr);
}

TEST_CASE("Pressing the arrow while the menu is open closes it", "[ui][split-button][reopen]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(save_config(true)));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 156.0f, 24.0f, luil::pointer_button::left, at(0) }));
    REQUIRE(describe(controller.process(luil::pointer_released_event { 156.0f, 24.0f, luil::pointer_button::left, at(50) })) == described_actions { u8"close" });
}

TEST_CASE("Re-pressing the arrow closes the menu even once the closed frame is published", "[ui][split-button][reopen]")
{
    // 열린 동안 화살표를 누르면 바깥 누름이 먼저 `set_open(false)`를 내고(`window_surface.cpp`) 클릭이 이어진다.
    // 그 사이 `open == false` frame이 게시되어도 펼쳐 있던 화살표의 클릭은 **누를 때의 tree**의 액션이라 닫힌다
    // (menu-focus-fixes-design.md의 X-4). 전에는 뗌 tree의 `set_open(true)`가 나가 도로 열렸다.
    luil::interaction_controller controller {};
    controller.set_tree(make_tree(save_config(true)));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 156.0f, 24.0f, luil::pointer_button::left, at(0) }));
    controller.set_tree(make_tree(save_config(false)));
    REQUIRE(describe(controller.process(luil::pointer_released_event { 156.0f, 24.0f, luil::pointer_button::left, at(50) })) == described_actions { u8"close" });

    // 닫힌 화살표의 누름은 지금처럼 뗌 tree의 것이다. 누르는 사이 열린 frame이 게시되면 그 tree의 `set_open(false)`다.
    controller.set_tree(make_tree(save_config(false)));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 156.0f, 24.0f, luil::pointer_button::left, at(1000) }));
    controller.set_tree(make_tree(save_config(true)));
    REQUIRE(describe(controller.process(luil::pointer_released_event { 156.0f, 24.0f, luil::pointer_button::left, at(1050) })) == described_actions { u8"close" });
}

TEST_CASE("Tab from the arrow leaves the split button for the next stop", "[ui][split-button][focus]")
{
    // 화살표는 묶음의 대표가 아닌 구성원이다. 전에는 Tab이 순서의 처음(본체)으로 갔다 (menu-focus-fixes-design.md의 X-3).
    keyboard board { save_config() };
    board.press(luil::key_code::tab);
    board.press(luil::key_code::arrow_right);
    REQUIRE(board.focused() == arrow_id);
    board.press(luil::key_code::tab);
    REQUIRE(board.focused() == other_id);
    board.press(luil::key_code::tab);
    board.press(luil::key_code::arrow_right);
    REQUIRE(board.focused() == arrow_id);
    board.press(luil::key_code::tab, false, true);
    REQUIRE(board.focused() == other_id);

    // 열린 동안 화살표에서 Tab으로 떠나면 초점 떠남 훅이 닫는다.
    keyboard open_board { save_config(true) };
    open_board.press(luil::key_code::tab);
    open_board.press(luil::key_code::arrow_right);
    REQUIRE(open_board.focused() == arrow_id);
    REQUIRE(describe(open_board.press(luil::key_code::tab)) == described_actions { u8"close" });
    REQUIRE(open_board.focused() == other_id);
}
