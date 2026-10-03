#include "luil/ui/ui_interaction.h"

#include "luil/messaging/channel.h"
#include "luil/messaging/latest_slot.h"
#include "luil/ui/app_message.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    // test 정의 kind다.
    // 앱이 자기 kind를 정의하는 것과 같은 경로다.
    constexpr luil::ui_element_kind kind_button { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_query_input { luil::application_element_kind(1) };
    constexpr luil::ui_element_kind kind_menu { luil::application_element_kind(2) };
    constexpr luil::ui_element_kind kind_menu_item { luil::application_element_kind(3) };
    constexpr luil::ui_element_kind kind_card { luil::application_element_kind(4) };

    constexpr luil::text_input_target query_target { 7 };

    // 앱이 logic inbox로 나르는 메시지의 대역이다.
    struct fake_intent
    {
        std::u8string name {};
    };

    class test_panel final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    // offset_at·text_input을 내주는 최소 텍스트 박스다.
    class test_text_input final : public luil::ui_element
    {
    public:
        test_text_input(luil::ui_element_id id, std::u8string text, const std::size_t caret, const std::size_t anchor)
            : ui_element { std::move(id) }
            , text_ { std::move(text) }
            , caret_ { caret }
            , anchor_ { anchor }
        {
            set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        }

        // 칸이 자기 안에 두는 부품이다 (실물의 지우기 버튼과 같은 자리).
        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}

        [[nodiscard]] std::optional<std::size_t> offset_at(const float x, const luil::text_measurer&) const override
        {
            // 픽셀 10개가 글자 하나다.
            // test가 좌표→offset을 예측할 수 있다.
            const float relative { x - bounds().x };
            const auto offset { static_cast<std::size_t>(relative / 10.0f) };
            return offset < text_.size() ? offset : text_.size();
        }

        [[nodiscard]] std::optional<luil::text_input_snapshot> text_input() const override
        {
            return luil::text_input_snapshot { text_, caret_, anchor_ };
        }

    private:
        std::u8string text_ {};
        std::size_t caret_ { 0 };
        std::size_t anchor_ { 0 };
    };

    // 라우팅 호출을 기록하는 policy다.
    class recording_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            if (kind == kind_query_input)
                return query_target;
            return std::nullopt;
        }

        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request& request) const override
        {
            last_edit = request;
            ++edit_count;
            return luil::input_action { luil::app_message { fake_intent { u8"edit" } } };
        }

        [[nodiscard]] std::optional<luil::menu_kinds> menu() const override
        {
            return luil::menu_kinds { kind_menu, kind_menu_item };
        }

        [[nodiscard]] std::vector<luil::input_action> close_menu() const override
        {
            return { luil::input_action { luil::app_message { fake_intent { u8"close-menu" } } } };
        }

        [[nodiscard]] std::vector<luil::input_action> on_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event&, const float scroll_delta) override
        {
            last_wheel_delta = scroll_delta;
            last_wheel_tree = &tree;
            return { luil::input_action { luil::app_message { fake_intent { u8"wheel" } } } };
        }

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event& event, const luil::interaction_snapshot&) override
        {
            last_key = event.key;
            return { luil::input_action { luil::app_message { fake_intent { u8"key" } } } };
        }

        void on_click(const luil::ui_element& element) override
        {
            clicked.push_back(element.id());
        }

        mutable luil::text_edit_request last_edit {};
        mutable int edit_count { 0 };
        float last_wheel_delta { 0.0f };
        const luil::ui_tree* last_wheel_tree { nullptr };
        luil::key_code last_key { luil::key_code::none };
        std::vector<luil::ui_element_id> clicked {};
    };

    // Tab 순서를 앱이 다시 정하는 정책이다.
    // 화면 배치와 논리적 순서가 다른 화면이 쓰는 구멍이다.
    class ordering_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::vector<luil::ui_element_id> order_focus(const luil::ui_tree&, std::vector<luil::ui_element_id> order) override
        {
            // 하나는 걸러 내고 나머지는 뒤집는다.
            std::erase_if(order, [](const luil::ui_element_id& id) { return id.owner == u8"two"; });
            std::reverse(order.begin(), order.end());
            return order;
        }
    };

    [[nodiscard]] const fake_intent* intent_of(const luil::input_action& action)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        return message != nullptr ? message->get<fake_intent>() : nullptr;
    }

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> single_button_tree(int* const clicks = nullptr)
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"one" }) };
        button->arrange({ { 10.0f, 10.0f, 40.0f, 20.0f }, 1.0f });
        button->set_action(luil::ui_trigger::left_click, [clicks](const luil::ui_action_context&) -> std::vector<luil::input_action> {
            if (clicks != nullptr)
                ++*clicks;
            return { luil::input_action { luil::app_message { fake_intent { u8"click" } } } };
        });
        root->add(std::move(button));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }
} // namespace

TEST_CASE("app_message round trips a typed value and rejects other types", "[ui][message]")
{
    const luil::app_message message { fake_intent { u8"hello" } };
    REQUIRE(message.empty() == false);
    REQUIRE(message.get<fake_intent>() != nullptr);
    REQUIRE(message.get<fake_intent>()->name == u8"hello");
    REQUIRE(message.get<int>() == nullptr);

    const luil::app_message copied { message };
    REQUIRE(copied.get<fake_intent>() == message.get<fake_intent>());

    const luil::app_message empty {};
    REQUIRE(empty.empty());
    REQUIRE(empty.get<fake_intent>() == nullptr);
}

TEST_CASE("A click fires on press and release over the same element", "[ui][interaction]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());

    auto actions { controller.process(luil::pointer_pressed_event { 20.0f, 15.0f, luil::pointer_button::left, at(0) }) };
    REQUIRE(actions.empty());
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id { kind_button, u8"one" });

    actions = controller.process(luil::pointer_released_event { 21.0f, 15.0f, luil::pointer_button::left, at(50) });
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0]) != nullptr);
    REQUIRE(intent_of(actions[0])->name == u8"click");
    // 클릭 확정은 policy에도 알려진다.
    REQUIRE(policy.clicked.size() == 1u);
    REQUIRE(policy.clicked[0].kind == kind_button);

    // 누른 뒤 벗어나 떼면 클릭이 아니다.
    actions = controller.process(luil::pointer_pressed_event { 20.0f, 15.0f, luil::pointer_button::left, at(2000) });
    actions = controller.process(luil::pointer_released_event { 150.0f, 150.0f, luil::pointer_button::left, at(2050) });
    REQUIRE(actions.empty());
}

TEST_CASE("Hover starts the tooltip clock and leaves with the pointer", "[ui][interaction]")
{
    luil::interaction_controller controller {};
    controller.set_tree(single_button_tree());

    static_cast<void>(controller.process(luil::pointer_moved_event { 20.0f, 15.0f, at(100) }));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id { kind_button, u8"one" });
    REQUIRE(controller.snapshot().hover_started_at == at(100));

    // 같은 대상의 발행본 교체와 같은 자리 메시지는 원래 지연을 보존한다.
    controller.set_tree(single_button_tree());
    static_cast<void>(controller.process(luil::pointer_moved_event { 20.0f, 15.0f, at(200) }));
    REQUIRE(controller.snapshot().hover_started_at == at(100));

    static_cast<void>(controller.process(luil::pointer_left_event {}));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id {});
    REQUIRE(controller.snapshot().hover_started_at.has_value() == false);
}

TEST_CASE("Wheel routing is delegated to the policy with the converted delta", "[ui][interaction][policy]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());

    const auto actions { controller.process(luil::mouse_wheel_event { 20.0f, 15.0f, 120.0f, at(0) }) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"wheel");
    // 위로 한 눈금 = 내용이 위로 = 음수 델타다.
    REQUIRE(policy.last_wheel_delta == -luil::input_wheel_scroll_step);
}

TEST_CASE("Wheel routing picks the first route whose element covers the pointer", "[ui][interaction][wheel]")
{
    // 200×200 root 안에 위(카드)와 아래(버튼) 두 element를 둔다.
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto upper { std::make_unique<test_panel>(luil::ui_element_id { kind_card }) };
    upper->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    root->add(std::move(upper));
    auto lower { std::make_unique<test_panel>(luil::ui_element_id { kind_button }) };
    lower->arrange({ { 0.0f, 100.0f, 200.0f, 100.0f }, 1.0f });
    root->add(std::move(lower));
    const luil::ui_tree tree { std::move(root) };

    const luil::scroll_route routes[] {
        { luil::ui_element_id { kind_card }, [](const float delta) { return luil::make_app_action(fake_intent { delta > 0.0f ? u8"card-down" : u8"card-up" }); } },
        { luil::ui_element_id { kind_button }, [](const float) { return luil::make_app_action(fake_intent { u8"button" }); } },
    };

    // 위 카드 위에서는 첫 route가 임자다.
    auto actions { luil::route_wheel(tree, 50.0f, 50.0f, 48.0f, routes) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"card-down");

    // 아래 버튼 위에서는 다음 route로 넘어간다.
    actions = luil::route_wheel(tree, 50.0f, 150.0f, 48.0f, routes);
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"button");

    // 아무것도 덮지 않으면(트리에 없는 자리) 빈 목록이다.
    REQUIRE(luil::route_wheel(tree, 500.0f, 500.0f, 48.0f, routes).empty());
}

TEST_CASE("Alphanumeric shortcuts have platform-neutral names and keep Win32 aliases", "[ui][interaction][events]")
{
    // 종전 Win32 호출도 이름 키로 이어져 앱 단축키를 깨지 않는다.
    constexpr luil::key_code control_s { luil::platform_key_code(0x53u) };
    STATIC_REQUIRE(control_s == luil::key_code::key_s);
    STATIC_REQUIRE(luil::platform_key_of(control_s) == 0x53u);
    STATIC_REQUIRE(luil::platform_key_code('7') == luil::key_code::key_7);
    STATIC_REQUIRE(luil::platform_key_of(luil::key_code::key_7) == '7');
    // 이름 키는 platform 대역이 아니다.
    STATIC_REQUIRE(luil::platform_key_of(luil::key_code::enter) == 0u);

    // platform 대역 키도 이름 키와 같은 경로로 정책에 닿는다.
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());
    const auto actions { controller.process(luil::key_pressed_event { control_s, true, false, false, false }) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(policy.last_key == control_s);
}

TEST_CASE("Keys unconsumed by text input flow to the policy", "[ui][interaction][policy]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());

    const auto actions { controller.process(luil::key_pressed_event { luil::key_code::f5 }) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"key");
    REQUIRE(policy.last_key == luil::key_code::f5);
}

TEST_CASE("A focused text input consumes characters and editing keys through the policy", "[ui][interaction][policy]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto input { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input }, u8"abcdef", 2u, 5u) };
    input->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    root->add(std::move(input));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    // 누르면 초점이 생기고 caret 배치 요청이 나간다 (10px = 1글자).
    auto actions { controller.process(luil::pointer_pressed_event { 30.0f, 10.0f, luil::pointer_button::left, at(0) }) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input });
    REQUIRE(policy.last_edit.command == luil::text::text_edit_command::place_caret);
    REQUIRE(policy.last_edit.target == query_target);
    REQUIRE(policy.last_edit.offset == 3u);
    static_cast<void>(controller.process(luil::pointer_released_event { 30.0f, 10.0f, luil::pointer_button::left, at(20) }));

    // 문자 입력은 insert 편집 요청이 된다.
    actions = controller.process(luil::character_typed_event { U'한' });
    REQUIRE(actions.size() == 1u);
    REQUIRE(policy.last_edit.command == luil::text::text_edit_command::insert);
    REQUIRE(policy.last_edit.text == u8"한");

    // Ctrl+C는 선택 구간을 클립보드 복사 요청으로 만든다 (caret 2, anchor 5).
    actions = controller.process(luil::key_pressed_event { luil::key_code::key_c, true, false });
    REQUIRE(actions.size() == 1u);
    const auto* const copy { std::get_if<luil::clipboard_copy_request>(&actions[0]) };
    REQUIRE(copy != nullptr);
    REQUIRE(copy->text == u8"cde");

    // Ctrl+V는 대상 target의 붙여넣기 요청이다.
    actions = controller.process(luil::key_pressed_event { luil::key_code::key_v, true, false });
    REQUIRE(actions.size() == 1u);
    const auto* const paste { std::get_if<luil::clipboard_paste_request>(&actions[0]) };
    REQUIRE(paste != nullptr);
    REQUIRE(paste->target == query_target);
}

TEST_CASE("Text editing separates primary shortcuts from word navigation", "[ui][interaction][text][events]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto input { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input }, u8"abcdef", 2u, 5u) };
    input->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    root->add(std::move(input));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 30.0f, 10.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_released_event { 30.0f, 10.0f, luil::pointer_button::left, at(20) }));

    // Command+왼쪽은 Ctrl+왼쪽의 낱말 이동으로 바뀌면 안 된다.
    luil::key_pressed_event command_left { luil::key_code::arrow_left };
    command_left.meta = true;
    command_left.primary_shortcut = true;
    command_left.word_navigation = false;
    static_cast<void>(controller.process(command_left));
    REQUIRE(policy.last_edit.command == luil::text::text_edit_command::move_left);

    // Option+왼쪽은 주 단축키 없이도 낱말 단위로 움직인다.
    luil::key_pressed_event option_left { luil::key_code::arrow_left };
    option_left.alt = true;
    option_left.primary_shortcut = false;
    option_left.word_navigation = true;
    static_cast<void>(controller.process(option_left));
    REQUIRE(policy.last_edit.command == luil::text::text_edit_command::move_word_left);

    luil::key_pressed_event command_copy { luil::key_code::key_c };
    command_copy.meta = true;
    command_copy.primary_shortcut = true;
    command_copy.word_navigation = false;
    const auto copied { controller.process(command_copy) };
    REQUIRE(copied.size() == 1u);
    REQUIRE(std::get_if<luil::clipboard_copy_request>(&copied[0]) != nullptr);

    // 명시한 역할이 물리 Control보다 우선한다.
    luil::key_pressed_event control_copy { luil::key_code::key_c, true };
    control_copy.primary_shortcut = false;
    control_copy.word_navigation = false;
    const auto uncopied { controller.process(control_copy) };
    for (const luil::input_action& action : uncopied)
        REQUIRE(std::get_if<luil::clipboard_copy_request>(&action) == nullptr);
}

TEST_CASE("Pressing elsewhere or losing the window releases the text focus", "[ui][interaction][policy]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto input { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input }, u8"abcdef", 2u, 2u) };
    input->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    root->add(std::move(input));
    auto disabled { std::make_unique<test_panel>(luil::ui_element_id { kind_card }) };
    disabled->arrange({ { 0.0f, 100.0f, 100.0f, 20.0f }, 1.0f });
    disabled->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    disabled->set_enabled(false);
    root->add(std::move(disabled));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    // 텍스트 박스를 누르면 초점이 생긴다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 30.0f, 10.0f, luil::pointer_button::left, at(0) }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input });
    static_cast<void>(controller.process(luil::pointer_released_event { 30.0f, 10.0f, luil::pointer_button::left, at(20) }));

    // hit가 없는 빈 곳을 눌러도 "다른 곳을 누른" 것이라 초점이 풀린다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 150.0f, 150.0f, luil::pointer_button::left, at(40) }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id {});
    static_cast<void>(controller.process(luil::pointer_released_event { 150.0f, 150.0f, luil::pointer_button::left, at(60) }));

    // 비활성 element도 마찬가지다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 30.0f, 10.0f, luil::pointer_button::left, at(80) }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input });
    static_cast<void>(controller.process(luil::pointer_released_event { 30.0f, 10.0f, luil::pointer_button::left, at(100) }));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 50.0f, 110.0f, luil::pointer_button::left, at(120) }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id {});

    // 주 창 표면은 빈 id다. HWND keyboard focus를 잃으면 같은 규칙으로 풀린다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 30.0f, 10.0f, luil::pointer_button::left, at(140) }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input });
    static_cast<void>(controller.process(luil::surface_focus_lost_event {}));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id {});
}

TEST_CASE("Menu keys reach a menu whose root carries an owner", "[ui][interaction][policy]")
{
    // `menu_config::owner`를 채우면 root id가 `{ menu, owner }`다.
    // 정책이 주는 것은 kind뿐이라 owner가 무엇이든 그 메뉴가 열린 것으로 보아야 한다.
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto menu { std::make_unique<test_panel>(luil::ui_element_id { kind_menu, u8"context" }) };
    menu->arrange({ { 10.0f, 10.0f, 100.0f, 30.0f }, 1.0f });
    auto item { std::make_unique<test_panel>(luil::ui_element_id { kind_menu_item, u8"only" }) };
    item->arrange({ { 10.0f, 10.0f, 100.0f, 20.0f }, 1.0f });
    menu->add(std::move(item));
    root->add(std::move(menu));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::arrow_down }));
    REQUIRE(controller.snapshot().menu_highlight == luil::ui_element_id { kind_menu_item, u8"only" });

    const auto actions { controller.process(luil::key_pressed_event { luil::key_code::escape }) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"close-menu");
}

TEST_CASE("Menu keys highlight items and escape asks the policy to close", "[ui][interaction][policy]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto menu { std::make_unique<test_panel>(luil::ui_element_id { kind_menu }) };
    menu->arrange({ { 10.0f, 10.0f, 100.0f, 60.0f }, 1.0f });
    menu->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    for (const auto& [name, enabled] : { std::pair { u8"first", false }, std::pair { u8"second", true } })
    {
        auto item { std::make_unique<test_panel>(luil::ui_element_id { kind_menu_item, name }) };
        item->arrange({ { 10.0f, name == std::u8string_view { u8"first" } ? 10.0f : 40.0f, 100.0f, 20.0f }, 1.0f });
        item->set_action(
            luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_message { fake_intent { u8"item" } } } }; });
        item->set_enabled(enabled);
        menu->add(std::move(item));
    }
    root->add(std::move(menu));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    // 첫 ↓는 첫 활성 항목(second)을 고른다.
    //  - 비활성(first)은 건너뛴다.
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::arrow_down }));
    REQUIRE(controller.snapshot().menu_highlight == luil::ui_element_id { kind_menu_item, u8"second" });

    // Enter는 강조 항목의 클릭 액션을 실행한다.
    auto actions { controller.process(luil::key_pressed_event { luil::key_code::enter }) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"item");

    // Esc는 policy의 닫기 메시지다.
    actions = controller.process(luil::key_pressed_event { luil::key_code::escape });
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"close-menu");
}

TEST_CASE("Dragging past the threshold starts a drag and dropping runs the target action", "[ui][interaction][drag]")
{
    luil::interaction_controller controller {};

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });

    auto source { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"dragme" }) };
    source->arrange({ { 0.0f, 0.0f, 50.0f, 50.0f }, 1.0f });
    luil::drag_source drag {};
    drag.make_payload = [](const luil::ui_action_context& context) {
        luil::drag_payload payload {};
        payload.source = context.element;
        payload.dragged_owner = context.element.owner;
        return payload;
    };
    source->set_drag_source(std::move(drag));

    auto target { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"target" }) };
    target->arrange({ { 100.0f, 100.0f, 50.0f, 50.0f }, 1.0f });
    luil::drop_target drop {};
    drop.accepts = [](const luil::drag_payload& payload) { return payload.dragged_owner == u8"dragme"; };
    drop.on_drop
        = [](const luil::drag_payload&, const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_message { fake_intent { u8"dropped" } } } }; };
    target->set_drop_target(std::move(drop));

    root->add(std::move(source));
    root->add(std::move(target));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_moved_event { 30.0f, 30.0f, at(10) }));
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(controller.snapshot().drag->payload.dragged_owner == u8"dragme");

    // 수락하는 대상 위로 오면 강조 대상이 된다.
    static_cast<void>(controller.process(luil::pointer_moved_event { 120.0f, 120.0f, at(20) }));
    REQUIRE(controller.snapshot().drag->hovered_drop_target == luil::ui_element_id { kind_card, u8"target" });

    const auto actions { controller.process(luil::pointer_released_event { 120.0f, 120.0f, luil::pointer_button::left, at(30) }) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"dropped");
    REQUIRE(controller.snapshot().drag.has_value() == false);
}

TEST_CASE("A file drag highlights the accepting target and leaves without a trace", "[ui][interaction][drag]")
{
    luil::interaction_controller controller {};

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });

    auto target { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"files" }) };
    target->arrange({ { 100.0f, 100.0f, 50.0f, 50.0f }, 1.0f });
    luil::drop_target drop {};
    // 파일을 받는 칸의 판정이다 — 밖에서 온 끌기만 받는다.
    drop.accepts = [](const luil::drag_payload& payload) { return payload.files.empty() == false; };
    drop.on_drop = [](const luil::drag_payload&, const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
    target->set_drop_target(std::move(drop));
    root->add(std::move(target));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    // 들어오면 표시가 서고 ghost는 그리지 않는다 (custom_visual — 그림은 탐색기의 것이다).
    static_cast<void>(controller.process(luil::file_drag_entered_event { 10.0f, 10.0f, { u8"C:\\a.txt" }, {} }));
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(controller.snapshot().drag->payload.custom_visual);
    REQUIRE(controller.snapshot().drag->payload.files.size() == 1u);
    REQUIRE(controller.snapshot().drag->hovered_drop_target == luil::ui_element_id {});

    // 받는 칸 위로 오면 강조 대상이 된다.
    static_cast<void>(controller.process(luil::file_drag_moved_event { 120.0f, 120.0f, {} }));
    REQUIRE(controller.snapshot().drag->hovered_drop_target == luil::ui_element_id { kind_card, u8"files" });

    // 떠나면 흔적 없이 사라진다 — 놓기도 표시로는 떠남이다.
    static_cast<void>(controller.process(luil::file_drag_left_event {}));
    REQUIRE(controller.snapshot().drag.has_value() == false);
}

TEST_CASE("A custom-visual drag still highlights its drop target", "[ui][tree][drag]")
{
    // custom_visual이 누르는 것은 ghost 하나다.
    // 강조까지 함께 삼키면 밖에서 온 파일 끌기가 놓을 자리를 잃는다
    // (os-dragdrop-design.md).
    SECTION("스스로 그리는 끌기도 강조는 받는다")
    {
        luil::drag_payload payload {};
        payload.custom_visual = true;
        const luil::drag_overlay_plan plan { luil::plan_drag_overlay(payload) };
        REQUIRE(plan.highlight_target);
        REQUIRE(plan.ghost == false);
    }

    SECTION("안에서 시작한 끌기는 강조와 ghost를 함께 그린다")
    {
        luil::drag_payload payload {};
        payload.dragged_owner = u8"dragme";
        const luil::drag_overlay_plan plan { luil::plan_drag_overlay(payload) };
        REQUIRE(plan.highlight_target);
        REQUIRE(plan.ghost);
    }

    SECTION("파일 끌기는 custom_visual과 강조 대상을 함께 세운다")
    {
        luil::interaction_controller controller {};

        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        auto target { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"files" }) };
        target->arrange({ { 100.0f, 100.0f, 50.0f, 50.0f }, 1.0f });
        luil::drop_target drop {};
        drop.accepts = [](const luil::drag_payload& payload) { return payload.files.empty() == false; };
        drop.on_drop = [](const luil::drag_payload&, const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
        target->set_drop_target(std::move(drop));
        root->add(std::move(target));
        controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

        // 받는 칸 위로 곧장 들어온다.
        static_cast<void>(controller.process(luil::file_drag_entered_event { 120.0f, 120.0f, { u8"C:\\a.txt" }, {} }));
        REQUIRE(controller.snapshot().drag.has_value());
        // 계약은 둘을 함께 요구한다 — custom_visual로 ghost를 누르면서도
        // 수락 대상은 강조한다.
        REQUIRE(controller.snapshot().drag->payload.custom_visual);
        REQUIRE(controller.snapshot().drag->hovered_drop_target == luil::ui_element_id { kind_card, u8"files" });
        REQUIRE(luil::plan_drag_overlay(controller.snapshot().drag->payload).highlight_target);
    }
}

TEST_CASE("File drag events do not disturb an internal drag", "[ui][interaction][drag]")
{
    // 실제로는 서로 배타지만(OS 끌기 동안 press·move가 오지 않는다),
    // 늦게 도착한 떠남·이동이 내부 끌기를 지우면 안 된다.
    luil::interaction_controller controller {};

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto source { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"dragme" }) };
    source->arrange({ { 0.0f, 0.0f, 50.0f, 50.0f }, 1.0f });
    luil::drag_source drag {};
    drag.make_payload = [](const luil::ui_action_context& context) {
        luil::drag_payload payload {};
        payload.dragged_owner = context.element.owner;
        return payload;
    };
    source->set_drag_source(std::move(drag));
    root->add(std::move(source));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_moved_event { 30.0f, 30.0f, at(10) }));
    REQUIRE(controller.snapshot().drag.has_value());

    static_cast<void>(controller.process(luil::file_drag_moved_event { 150.0f, 150.0f, {} }));
    static_cast<void>(controller.process(luil::file_drag_left_event {}));
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(controller.snapshot().drag->payload.dragged_owner == u8"dragme");
    REQUIRE(controller.snapshot().drag->x == 30.0f);
}

TEST_CASE("Escape cancels an active drag before any policy routing", "[ui][interaction][drag]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto source { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"dragme" }) };
    source->arrange({ { 0.0f, 0.0f, 50.0f, 50.0f }, 1.0f });
    luil::drag_source drag {};
    drag.make_payload = [](const luil::ui_action_context& context) {
        luil::drag_payload payload {};
        payload.source = context.element;
        return payload;
    };
    source->set_drag_source(std::move(drag));
    root->add(std::move(source));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_moved_event { 40.0f, 40.0f, at(10) }));
    REQUIRE(controller.snapshot().drag.has_value());

    const auto actions { controller.process(luil::key_pressed_event { luil::key_code::escape }) };
    REQUIRE(actions.empty());
    REQUIRE(controller.snapshot().drag.has_value() == false);
    // policy의 on_key로 흘러가지 않았다.
    REQUIRE(policy.last_key == luil::key_code::none);
}

TEST_CASE("A double click fires only when a double click action is registered", "[ui][interaction]")
{
    int double_clicks { 0 };
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"double" }) };
    button->arrange({ { 0.0f, 0.0f, 50.0f, 50.0f }, 1.0f });
    button->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    button->set_action(luil::ui_trigger::double_click, [&double_clicks](const luil::ui_action_context&) -> std::vector<luil::input_action> {
        ++double_clicks;
        return {};
    });
    root->add(std::move(button));

    luil::interaction_config config {};
    config.double_click_time = 500ms;
    luil::interaction_controller controller { nullptr, config };
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_released_event { 10.0f, 10.0f, luil::pointer_button::left, at(20) }));
    REQUIRE(double_clicks == 0);

    // 임계 시간 안의 두 번째 클릭은 더블 클릭이다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 11.0f, 10.0f, luil::pointer_button::left, at(200) }));
    static_cast<void>(controller.process(luil::pointer_released_event { 11.0f, 10.0f, luil::pointer_button::left, at(220) }));
    REQUIRE(double_clicks == 1);

    // 임계 시간을 넘긴 클릭은 다시 단일 클릭이다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 11.0f, 10.0f, luil::pointer_button::left, at(2000) }));
    static_cast<void>(controller.process(luil::pointer_released_event { 11.0f, 10.0f, luil::pointer_button::left, at(2020) }));
    REQUIRE(double_clicks == 1);
}

namespace {
    constexpr luil::ui_element_kind kind_popup_item { luil::application_element_kind(5) };

    // popup 표면의 tree다.
    // popup 창의 (0,0)에서 시작하는 분리 tree라 좌표가 작다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> popup_item_tree(int* const clicks = nullptr)
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root, u8"popup" }) };
        root->arrange({ { 0.0f, 0.0f, 120.0f, 80.0f }, 1.0f });
        auto item { std::make_unique<test_panel>(luil::ui_element_id { kind_popup_item, u8"first" }) };
        item->arrange({ { 0.0f, 0.0f, 120.0f, 24.0f }, 1.0f });
        item->set_action(luil::ui_trigger::left_click, [clicks](const luil::ui_action_context&) -> std::vector<luil::input_action> {
            if (clicks != nullptr)
                ++*clicks;
            return { luil::input_action { luil::app_message { fake_intent { u8"popup-click" } } } };
        });
        root->add(std::move(item));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }
} // namespace

TEST_CASE("Popup surface events hit the popup tree instead of the main tree", "[ui][interaction][popup]")
{
    luil::interaction_controller controller {};
    controller.set_tree(single_button_tree());
    luil::surface_tree_list popups {};
    popups.emplace_back(u8"menu", popup_item_tree());
    controller.set_surface_trees(std::move(popups));

    // 같은 좌표라도 표식이 popup이면 popup tree에 hit한다.
    luil::pointer_moved_event on_popup { 20.0f, 15.0f, at(100) };
    on_popup.surface = u8"menu";
    static_cast<void>(controller.process(on_popup));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id { kind_popup_item, u8"first" });

    // 표식이 비면 주 창이다.
    static_cast<void>(controller.process(luil::pointer_moved_event { 20.0f, 15.0f, at(200) }));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id { kind_button, u8"one" });

    // 모르는 표식은 아무것도 맞히지 않는다.
    luil::pointer_moved_event unknown { 20.0f, 15.0f, at(300) };
    unknown.surface = u8"gone";
    static_cast<void>(controller.process(unknown));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id {});
}

TEST_CASE("A click on a popup surface runs the popup element's action", "[ui][interaction][popup]")
{
    luil::interaction_controller controller {};
    int main_clicks { 0 };
    int popup_clicks { 0 };
    controller.set_tree(single_button_tree(&main_clicks));
    luil::surface_tree_list popups {};
    popups.emplace_back(u8"menu", popup_item_tree(&popup_clicks));
    controller.set_surface_trees(std::move(popups));

    luil::pointer_pressed_event press { 20.0f, 15.0f, luil::pointer_button::left, at(0) };
    press.surface = u8"menu";
    static_cast<void>(controller.process(press));
    luil::pointer_released_event release { 20.0f, 15.0f, luil::pointer_button::left, at(50) };
    release.surface = u8"menu";
    auto actions { controller.process(release) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0]) != nullptr);
    REQUIRE(intent_of(actions[0])->name == u8"popup-click");
    REQUIRE(popup_clicks == 1);
    REQUIRE(main_clicks == 0);
}

TEST_CASE("Wheel over a popup routes that popup's tree to the policy", "[ui][interaction][popup]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());
    auto popup_tree { popup_item_tree() };
    const luil::ui_tree* const popup_pointer { popup_tree.get() };
    luil::surface_tree_list popups {};
    popups.emplace_back(u8"menu", std::move(popup_tree));
    controller.set_surface_trees(std::move(popups));

    luil::mouse_wheel_event wheel { 20.0f, 15.0f, 120.0f, at(100) };
    wheel.surface = u8"menu";
    static_cast<void>(controller.process(wheel));
    REQUIRE(policy.last_wheel_tree == popup_pointer);
}

TEST_CASE("Text focus follows the lifetime and keyboard focus of its surface", "[ui][interaction][window]")
{
    // 보조 창 tree에 텍스트 박스 하나가 있다.
    const luil::ui_element_id box_id { kind_query_input, u8"tool-query" };
    auto window_root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root, u8"tool" }) };
    window_root->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    auto box { std::make_unique<test_text_input>(box_id, u8"abc", 0, 0) };
    box->arrange({ { 10.0f, 10.0f, 120.0f, 24.0f }, 1.0f });
    window_root->add(std::move(box));

    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());
    luil::surface_tree_list surfaces {};
    surfaces.emplace_back(u8"tool", std::make_shared<const luil::ui_tree>(std::move(window_root)));
    controller.set_surface_trees(std::move(surfaces));

    // 보조 창 표면의 텍스트 박스를 누르면 초점과 함께 표면도 기억한다.
    luil::pointer_pressed_event press { 20.0f, 15.0f, luil::pointer_button::left, at(0) };
    press.surface = u8"tool";
    static_cast<void>(controller.process(press));
    REQUIRE(controller.snapshot().focused_input == box_id);
    REQUIRE(controller.snapshot().focused_surface == u8"tool");

    // 표면이 살아 있는 동안 다른 이벤트가 흘러도 초점이 남는다.
    //  - 주 tree에는 이 element가 없으므로 표면을 기억하지 않으면 곧바로 풀린다.
    luil::pointer_released_event release { 20.0f, 15.0f, luil::pointer_button::left, at(50) };
    release.surface = u8"tool";
    static_cast<void>(controller.process(release));
    REQUIRE(controller.snapshot().focused_input == box_id);

    // 다른 표면의 늦은 focus 상실은 현재 창의 초점을 건드리지 않는다.
    static_cast<void>(controller.process(luil::surface_focus_lost_event {}));
    REQUIRE(controller.snapshot().focused_input == box_id);
    REQUIRE(controller.snapshot().focused_surface == u8"tool");

    // 이 보조 창 자체가 keyboard focus를 잃으면 텍스트 초점도 풀린다.
    static_cast<void>(controller.process(luil::surface_focus_lost_event { u8"tool" }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id {});
    REQUIRE(controller.snapshot().focused_surface.empty());

    // 다시 초점을 준 뒤 표면이 사라지는 경우도 같은 불변식을 지킨다.
    static_cast<void>(controller.process(press));
    REQUIRE(controller.snapshot().focused_input == box_id);

    // 보조 창이 닫히면(표면이 사라지면) 초점도 풀린다.
    controller.set_surface_trees({});
    static_cast<void>(controller.process(luil::pointer_moved_event { 5.0f, 5.0f, at(100) }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id {});
    REQUIRE(controller.snapshot().focused_surface.empty());
}

TEST_CASE("A text box inside a popup takes focus and receives keys from the anchor window", "[ui][interaction][popup]")
{
    // popup tree에 텍스트 박스 하나가 있다 (검색 칸이 있는 메뉴).
    const luil::ui_element_id box_id { kind_query_input, u8"menu-search" };
    auto popup_root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root, u8"menu" }) };
    popup_root->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    auto box { std::make_unique<test_text_input>(box_id, u8"abc", 3, 3) };
    box->arrange({ { 10.0f, 10.0f, 120.0f, 24.0f }, 1.0f });
    popup_root->add(std::move(box));

    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());
    luil::surface_tree_list popups {};
    popups.emplace_back(u8"menu", std::make_shared<const luil::ui_tree>(std::move(popup_root)));
    controller.set_surface_trees(std::move(popups));

    // popup 위의 누름도 표면을 기억한다 — controller에는 popup과 보조 창의 구분이 없다.
    luil::pointer_pressed_event press { 20.0f, 15.0f, luil::pointer_button::left, at(0) };
    press.surface = u8"menu";
    static_cast<void>(controller.process(press));
    REQUIRE(controller.snapshot().focused_input == box_id);
    REQUIRE(controller.snapshot().focused_surface == u8"menu");

    // 문자에는 표면 표식이 없고, 키의 표식은 **앵커 창의 것**이다
    // (여기서는 빈 값 = 주 창). popup은 keyboard focus를 받지 못하므로 앵커 창이
    // 받은 것이 초점 element로 가는 이 경로가 유일하다 (popup-ime-design.md).
    //  - 그래서 표식과 초점의 표면이 어긋나는 것이 popup에서는 정상이고, 초점이
    //    있으면 표식을 보지 않는다 — 이 test가 그 규칙의 안전선이다
    //    (key-surface-routing-design.md).
    policy.edit_count = 0;
    const auto typed { controller.process(luil::character_typed_event { U'한' }) };
    REQUIRE(typed.size() == 1);
    REQUIRE(policy.edit_count == 1);
    REQUIRE(policy.last_edit.target == query_target);
    REQUIRE(policy.last_edit.command == luil::text::text_edit_command::insert);
    REQUIRE(policy.last_edit.text == u8"한");

    const auto moved { controller.process(luil::key_pressed_event { luil::key_code::arrow_left }) };
    REQUIRE(moved.size() == 1);
    REQUIRE(policy.last_edit.target == query_target);
    REQUIRE(policy.last_edit.command == luil::text::text_edit_command::move_left);

    // popup이 닫히면(표면이 사라지면) 초점도 풀린다.
    // 조합 중이던 TSF 문서를 앵커 표면이 떼는 근거다 (popup-ime-design.md).
    controller.set_surface_trees({});
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id {});
    REQUIRE(controller.snapshot().focused_surface.empty());
}

TEST_CASE("A popup's text focus outlives its anchor's focus-lost event alone", "[ui][interaction][popup]")
{
    // 주 창(빈 표면 id)에 붙은 popup 안에 텍스트 박스가 있다.
    const luil::ui_element_id box_id { kind_query_input, u8"menu-search" };
    auto popup_root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root, u8"menu" }) };
    popup_root->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    auto box { std::make_unique<test_text_input>(box_id, u8"abc", 3, 3) };
    box->arrange({ { 10.0f, 10.0f, 120.0f, 24.0f }, 1.0f });
    popup_root->add(std::move(box));

    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());
    luil::surface_tree_list popups {};
    popups.emplace_back(u8"menu", std::make_shared<const luil::ui_tree>(std::move(popup_root)));
    controller.set_surface_trees(std::move(popups));

    luil::pointer_pressed_event press { 20.0f, 15.0f, luil::pointer_button::left, at(0) };
    press.surface = u8"menu";
    static_cast<void>(controller.process(press));
    REQUIRE(controller.snapshot().focused_surface == u8"menu");

    // 앵커 창(주 창)이 focus를 잃었다는 이벤트 **하나만으로는** 거둬지지 않는다.
    // 초점 표면이 popup이라 id가 다르기 때문이다.
    static_cast<void>(controller.process(luil::surface_focus_lost_event {}));
    REQUIRE(controller.snapshot().focused_input == box_id);
    REQUIRE(controller.snapshot().focused_surface == u8"menu");

    // 그래서 UI thread의 소유자가 붙은 popup의 id로도 낸다
    // (popup-ime-design.md). popup은 열린 채로 남고 초점만 풀린다.
    static_cast<void>(controller.process(luil::surface_focus_lost_event { u8"menu" }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id {});
    REQUIRE(controller.snapshot().focused_surface.empty());
}

namespace {
    // 몸짓(끌기·누름·글 잡기)을 세울 것만 담은 tree다.
    // 주 창으로도 보조 표면으로도 쓴다 — controller에는 둘의 구분이 없다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> gesture_tree()
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root, u8"gesture" }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });

        // 끌 것이다.
        auto source { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"tool-drag" }) };
        source->arrange({ { 0.0f, 0.0f, 50.0f, 50.0f }, 1.0f });
        luil::drag_source drag {};
        drag.make_payload = [](const luil::ui_action_context& context) {
            luil::drag_payload payload {};
            payload.dragged_owner = context.element.owner;
            return payload;
        };
        source->set_drag_source(std::move(drag));
        root->add(std::move(source));

        // 놓을 것이다.
        auto target { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"tool-drop" }) };
        target->arrange({ { 130.0f, 100.0f, 60.0f, 60.0f }, 1.0f });
        luil::drop_target drop {};
        drop.accepts = [](const luil::drag_payload& payload) { return payload.dragged_owner == u8"tool-drag"; };
        drop.on_drop = [](const luil::drag_payload&, const luil::ui_action_context&) -> std::vector<luil::input_action> {
            return { luil::input_action { luil::app_message { fake_intent { u8"tool-dropped" } } } };
        };
        target->set_drop_target(std::move(drop));
        root->add(std::move(target));

        // 누를 것이다.
        auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"tool-button" }) };
        button->arrange({ { 100.0f, 0.0f, 60.0f, 30.0f }, 1.0f });
        button->set_action(luil::ui_trigger::left_click,
            [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_message { fake_intent { u8"tool-click" } } } }; });
        root->add(std::move(button));

        // 잡을 글이다.
        auto box { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input, u8"tool-query" }, u8"abc", 0, 0) };
        box->arrange({ { 0.0f, 100.0f, 120.0f, 24.0f }, 1.0f });
        root->add(std::move(box));

        return std::make_shared<const luil::ui_tree>(std::move(root));
    }
} // namespace

TEST_CASE("A vanished surface's press stops swallowing later pointer moves", "[ui][interaction][popup]")
{
    // 보조 표면의 글 칸을 잡으면 잡은 대상과 함께 그 표면도 기억된다.
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());
    luil::surface_tree_list surfaces {};
    surfaces.emplace_back(u8"tool", gesture_tree());
    controller.set_surface_trees(std::move(surfaces));

    luil::pointer_pressed_event press { 20.0f, 110.0f, luil::pointer_button::left, at(0) };
    press.surface = u8"tool";
    static_cast<void>(controller.process(press));
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id { kind_query_input, u8"tool-query" });

    // 앱이 다음 frame에서 그 표면을 뺐다.
    // 뗌은 사라진 표면 id로 오므로 여기서 거두지 않으면 아무도 거두지 않는다.
    controller.set_surface_trees({});
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});

    // 잡은 대상이 남으면 주 창의 이동이 **통째로 삼켜진다** —
    // 죽은 tree에서 그 칸을 찾다 조기 반환하기 때문이다.
    static_cast<void>(controller.process(luil::pointer_moved_event { 20.0f, 15.0f, at(50) }));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id { kind_button, u8"one" });
}

TEST_CASE("A release for a surface that is already gone still clears the press", "[ui][interaction][popup]")
{
    int main_clicks { 0 };
    luil::interaction_controller controller {};
    controller.set_tree(single_button_tree(&main_clicks));
    luil::surface_tree_list surfaces {};
    surfaces.emplace_back(u8"tool", gesture_tree());
    controller.set_surface_trees(std::move(surfaces));

    SECTION("누른 채 사라진 표면의 합성 뗌")
    {
        luil::pointer_pressed_event press { 120.0f, 15.0f, luil::pointer_button::left, at(0) };
        press.surface = u8"tool";
        static_cast<void>(controller.process(press));
        REQUIRE(controller.snapshot().pressed == luil::ui_element_id { kind_button, u8"tool-button" });
        controller.set_surface_trees({});

        // HWND 파괴·capture 상실이 이전 표면 id로 내는 뗌이다
        // (window_surface::cancel_pointer_press와 같은 모양 — 좌표가 client 밖이다).
        luil::pointer_released_event synthetic { -10000.0f, -10000.0f, luil::pointer_button::left, at(50) };
        synthetic.surface = u8"tool";
        const auto nothing { controller.process(synthetic) };
        REQUIRE(nothing.empty());
        REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});
    }

    SECTION("끌던 중에 온 죽은 표면의 뗌은 놓기가 아니다")
    {
        luil::pointer_pressed_event press { 10.0f, 10.0f, luil::pointer_button::left, at(0) };
        press.surface = u8"tool";
        static_cast<void>(controller.process(press));
        luil::pointer_moved_event started { 40.0f, 40.0f, at(10) };
        started.surface = u8"tool";
        static_cast<void>(controller.process(started));
        luil::pointer_moved_event onto_target { 150.0f, 130.0f, at(20) };
        onto_target.surface = u8"tool";
        static_cast<void>(controller.process(onto_target));
        REQUIRE(controller.snapshot().drag.has_value());
        REQUIRE(controller.snapshot().drag->hovered_drop_target == luil::ui_element_id { kind_card, u8"tool-drop" });

        // 그 자리가 수락 대상이어도 뗌의 표면을 모르면 놓을 자리도 모른다.
        // 거두기만 하고 drop은 실행하지 않는다.
        luil::pointer_released_event synthetic { 150.0f, 130.0f, luil::pointer_button::left, at(30) };
        synthetic.surface = u8"gone";
        const auto nothing { controller.process(synthetic) };
        REQUIRE(nothing.empty());
        REQUIRE(controller.snapshot().drag.has_value() == false);
    }

    // 어느 쪽이든 그 뒤 주 창의 클릭이 정상으로 실행된다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 15.0f, luil::pointer_button::left, at(100) }));
    const auto clicked { controller.process(luil::pointer_released_event { 20.0f, 15.0f, luil::pointer_button::left, at(120) }) };
    REQUIRE(clicked.size() == 1u);
    REQUIRE(intent_of(clicked[0]) != nullptr);
    REQUIRE(intent_of(clicked[0])->name == u8"click");
    REQUIRE(main_clicks == 1);
}

TEST_CASE("A drag started on a surface dies with that surface", "[ui][interaction][drag]")
{
    luil::interaction_controller controller {};
    controller.set_tree(gesture_tree());
    luil::surface_tree_list surfaces {};
    surfaces.emplace_back(u8"tool", gesture_tree());
    controller.set_surface_trees(std::move(surfaces));

    SECTION("보조 표면의 끌기는 그 표면과 함께 죽는다")
    {
        luil::pointer_pressed_event press { 10.0f, 10.0f, luil::pointer_button::left, at(0) };
        press.surface = u8"tool";
        static_cast<void>(controller.process(press));
        luil::pointer_moved_event moved { 40.0f, 40.0f, at(10) };
        moved.surface = u8"tool";
        static_cast<void>(controller.process(moved));
        REQUIRE(controller.snapshot().drag.has_value());
        REQUIRE(controller.snapshot().drag->surface == u8"tool");

        // 창이 닫히면 끌던 손도 함께 사라진다. 뗌은 오지 않는다.
        controller.set_surface_trees({});
        REQUIRE(controller.snapshot().drag.has_value() == false);
    }

    SECTION("주 창의 끌기는 표면 목록이 바뀌어도 남는다")
    {
        static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::left, at(0) }));
        static_cast<void>(controller.process(luil::pointer_moved_event { 40.0f, 40.0f, at(10) }));
        REQUIRE(controller.snapshot().drag.has_value());
        REQUIRE(controller.snapshot().drag->surface.empty());

        // popup 하나가 닫힌 것으로 주 창의 끌기를 거두면 과하다.
        controller.set_surface_trees({});
        REQUIRE(controller.snapshot().drag.has_value());
    }
}

TEST_CASE("A drag remembers the surface it started on", "[ui][interaction][drag]")
{
    // 표시가 어느 창에 설지를 이 한 값이 정한다 — ghost는 좌표로만 그려져
    // tree가 갈라 주지 못한다 (multi-window-design.md).
    luil::interaction_controller controller {};
    controller.set_tree(gesture_tree());
    luil::surface_tree_list surfaces {};
    surfaces.emplace_back(u8"tool", gesture_tree());
    controller.set_surface_trees(std::move(surfaces));

    SECTION("주 창에서 시작하면 표면이 비어 있다")
    {
        static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::left, at(0) }));
        static_cast<void>(controller.process(luil::pointer_moved_event { 40.0f, 40.0f, at(10) }));
        REQUIRE(controller.snapshot().drag.has_value());
        REQUIRE(controller.snapshot().drag->surface.empty());
    }

    SECTION("보조 표면에서 시작하면 그 id를 남긴다")
    {
        luil::pointer_pressed_event press { 10.0f, 10.0f, luil::pointer_button::left, at(0) };
        press.surface = u8"tool";
        static_cast<void>(controller.process(press));
        luil::pointer_moved_event moved { 40.0f, 40.0f, at(10) };
        moved.surface = u8"tool";
        static_cast<void>(controller.process(moved));
        REQUIRE(controller.snapshot().drag.has_value());
        REQUIRE(controller.snapshot().drag->surface == u8"tool");
    }

    SECTION("파일 끌기도 들어온 표면을 남긴다")
    {
        static_cast<void>(controller.process(luil::file_drag_entered_event { 20.0f, 20.0f, { u8"C:\\a.txt" }, u8"tool" }));
        REQUIRE(controller.snapshot().drag.has_value());
        REQUIRE(controller.snapshot().drag->surface == u8"tool");
    }
}

TEST_CASE("Hover and press remember their surface", "[ui][interaction][window]")
{
    // 값마다 표식이 짝으로 산다. 표면 경계의 필터가 이 표식으로 남의 것을
    // 가른다 (multi-window-design.md).
    luil::interaction_controller controller {};
    controller.set_tree(single_button_tree());
    luil::surface_tree_list surfaces {};
    surfaces.emplace_back(u8"tool", popup_item_tree());
    controller.set_surface_trees(std::move(surfaces));

    // 주 창의 hover는 빈 표식이다 (표면 표식의 통상 규칙).
    static_cast<void>(controller.process(luil::pointer_moved_event { 20.0f, 15.0f, at(0) }));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id { kind_button, u8"one" });
    REQUIRE(controller.snapshot().hovered_surface.empty());

    // 보조 표면으로 옮기면 표식도 함께 옮긴다.
    luil::pointer_moved_event on_tool { 20.0f, 15.0f, at(10) };
    on_tool.surface = u8"tool";
    static_cast<void>(controller.process(on_tool));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id { kind_popup_item, u8"first" });
    REQUIRE(controller.snapshot().hovered_surface == u8"tool");

    // 누름은 시작한 표면을 남긴다.
    luil::pointer_pressed_event press { 20.0f, 15.0f, luil::pointer_button::left, at(20) };
    press.surface = u8"tool";
    static_cast<void>(controller.process(press));
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id { kind_popup_item, u8"first" });
    REQUIRE(controller.snapshot().pressed_surface == u8"tool");

    // 뗄 때 값과 표식이 함께 빈다.
    luil::pointer_released_event release { 20.0f, 15.0f, luil::pointer_button::left, at(40) };
    release.surface = u8"tool";
    static_cast<void>(controller.process(release));
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});
    REQUIRE(controller.snapshot().pressed_surface.empty());

    // 다른 표면의 떠남은 이 hover를 건드리지 않는다.
    // popup은 앵커 창 위에 떠 있어, 그리로 들어가는 것이 앵커 창에서는 떠남으로
    // 온다 — 표면을 가리지 않으면 popup에 들어서는 순간 그 hover가 지워진다.
    static_cast<void>(controller.process(luil::pointer_left_event {}));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id { kind_popup_item, u8"first" });
    REQUIRE(controller.snapshot().hovered_surface == u8"tool");

    // 그 표면을 벗어나면 값과 표식이 함께 빈다.
    static_cast<void>(controller.process(luil::pointer_left_event { u8"tool" }));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id {});
    REQUIRE(controller.snapshot().hovered_surface.empty());
}

TEST_CASE("Leaving one surface keeps the hover that moved to another", "[ui][interaction][window]")
{
    // popup을 여닫는 동안 실제로 오는 순서다: 새 표면의 `WM_MOUSEMOVE`가 먼저
    // 닿고 옛 표면의 `WM_MOUSELEAVE`가 뒤따른다.
    luil::interaction_controller controller {};
    controller.set_tree(single_button_tree());
    luil::surface_tree_list surfaces {};
    surfaces.emplace_back(u8"menu", popup_item_tree());
    controller.set_surface_trees(std::move(surfaces));

    // 주 창 → popup.
    static_cast<void>(controller.process(luil::pointer_moved_event { 20.0f, 15.0f, at(0) }));
    luil::pointer_moved_event on_menu { 20.0f, 15.0f, at(10) };
    on_menu.surface = u8"menu";
    static_cast<void>(controller.process(on_menu));
    static_cast<void>(controller.process(luil::pointer_left_event {}));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id { kind_popup_item, u8"first" });

    // 뒤늦은 떠남이 자리의 기억까지 버리면, 그 표면의 tree가 다시 올 때
    // hover 재판정이 멈춰 커서 아래가 바뀌어도 강조가 얼어붙는다.
    luil::surface_tree_list refreshed {};
    refreshed.emplace_back(u8"menu", popup_item_tree());
    controller.set_surface_trees(std::move(refreshed));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id { kind_popup_item, u8"first" });
    REQUIRE(controller.snapshot().hovered_surface == u8"menu");

    // popup → 주 창.
    static_cast<void>(controller.process(luil::pointer_moved_event { 20.0f, 15.0f, at(20) }));
    static_cast<void>(controller.process(luil::pointer_left_event { u8"menu" }));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id { kind_button, u8"one" });
    REQUIRE(controller.snapshot().hovered_surface.empty());

    // 앱을 아주 벗어나는 것은 여전히 거둔다.
    static_cast<void>(controller.process(luil::pointer_left_event {}));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id {});
}

namespace {
    // 두 표면에 똑같이 두는 tree다.
    // 라이브러리 자신이 컨테이너 owner를 물려주지 않는 항목 수준 id를 만들므로
    // (`list_row`는 항목 키만 쓴다) 표면이 달라도 id가 같은 것은 유효하다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> row_double_click_tree(int* const doubles)
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        auto row { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::list_row, u8"3" }) };
        row->arrange({ { 10.0f, 10.0f, 40.0f, 20.0f }, 1.0f });
        row->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        row->set_action(luil::ui_trigger::double_click, [doubles](const luil::ui_action_context&) -> std::vector<luil::input_action> {
            ++*doubles;
            return {};
        });
        root->add(std::move(row));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }
} // namespace

TEST_CASE("A double click does not carry across surfaces", "[ui][interaction][window]")
{
    // 좌표는 표면마다 자기 client 기준이라 "같은 자리"가 우연이 아니라 흔하다.
    // 표면을 빼면 각 창이 한 번씩 눌렸는데 더블 클릭이 돈다
    // (multi-window-design.md).
    int main_doubles { 0 };
    int tool_doubles { 0 };
    luil::interaction_config config {};
    config.double_click_time = 500ms;
    luil::interaction_controller controller { nullptr, config };
    controller.set_tree(row_double_click_tree(&main_doubles));
    luil::surface_tree_list surfaces {};
    surfaces.emplace_back(u8"tool", row_double_click_tree(&tool_doubles));
    controller.set_surface_trees(std::move(surfaces));

    // 주 창의 행을 한 번 누르고 뗀다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 15.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_released_event { 20.0f, 15.0f, luil::pointer_button::left, at(20) }));

    // 곧바로 보조 창의 **같은 id**를 **같은 client 좌표**에서 한 번 누르고 뗀다.
    luil::pointer_pressed_event press { 20.0f, 15.0f, luil::pointer_button::left, at(100) };
    press.surface = u8"tool";
    static_cast<void>(controller.process(press));
    luil::pointer_released_event release { 20.0f, 15.0f, luil::pointer_button::left, at(120) };
    release.surface = u8"tool";
    static_cast<void>(controller.process(release));
    REQUIRE(main_doubles == 0);
    REQUIRE(tool_doubles == 0);

    // 같은 표면에서 이어 누르면 그대로 돈다 — 판정을 죽인 것이 아니다.
    luil::pointer_pressed_event again { 20.0f, 15.0f, luil::pointer_button::left, at(200) };
    again.surface = u8"tool";
    static_cast<void>(controller.process(again));
    luil::pointer_released_event lift { 20.0f, 15.0f, luil::pointer_button::left, at(220) };
    lift.surface = u8"tool";
    static_cast<void>(controller.process(lift));
    REQUIRE(tool_doubles == 1);
    REQUIRE(main_doubles == 0);
}

TEST_CASE("The text input view shows the composing text while composing", "[ui][interaction][text]")
{
    luil::text::text_edit_state state {};
    state.text = u8"확정";
    state.caret = state.text.size();
    state.anchor = state.caret;

    // 조합이 없으면 보이는 글이 확정된 글이다.
    const luil::text_input_view plain { luil::make_text_input_view(state, std::nullopt, query_target) };
    REQUIRE(plain.composing == false);
    REQUIRE(plain.displayed_text() == u8"확정");

    // 조합 중에는 아직 확정되지 않은 글자까지 보인다.
    // 점진 검색이 이 글을 봐야 화면과 결과가 어긋나지 않는다 (받침 없는 "토").
    luil::text_composition_event event {};
    event.target = query_target;
    event.composing = true;
    event.text = u8"확정토";
    event.caret = event.text.size();
    event.composing_begin = std::u8string_view { u8"확정" }.size();
    event.composing_end = event.text.size();
    const luil::text_input_view composing { luil::make_text_input_view(state, event, query_target) };
    REQUIRE(composing.composing);
    REQUIRE(composing.text == u8"확정");
    REQUIRE(composing.displayed_text() == u8"확정토");

    // 다른 칸의 조합은 이 칸으로 새지 않는다.
    const luil::text_input_view other { luil::make_text_input_view(state, event, static_cast<luil::text_input_target>(99)) };
    REQUIRE(other.composing == false);
    REQUIRE(other.displayed_text() == u8"확정");
}

TEST_CASE("Incremental search falls back to the settled text when a composing step matches nothing", "[ui][interaction][text]")
{
    // "토스트"를 찾는 목록이다. 이 test의 매칭 정책은 부분 일치다.
    const auto any_match = [](const std::u8string_view query) { return std::u8string_view { u8"토스트 보내기" }.find(query) != std::u8string_view::npos; };

    luil::text::text_edit_state state {};
    luil::text_composition_event event {};
    event.target = query_target;
    event.composing = true;

    // 첫 글자를 조합하는 중이다. 확정된 글이 없으므로 물러서지 않는다.
    //  - 빈 질의로 물러서면 좁히기가 아니라 목록 전체가 되살아난다.
    event.text = u8"ㅌ";
    const luil::text_input_view first { luil::make_text_input_view(state, event, query_target) };
    REQUIRE(luil::search_query(first, any_match) == u8"ㅌ");

    // 맞는 조합 단계는 그대로 쓴다.
    event.text = u8"토";
    const luil::text_input_view matching { luil::make_text_input_view(state, event, query_target) };
    REQUIRE(luil::search_query(matching, any_match) == u8"토");

    // "토"가 확정된 뒤 "스"를 조합하는 중이다.
    // "토ㅅ"은 어디에도 없지만 목록이 사라지지 않도록 "토"로 물러선다.
    state.text = u8"토";
    state.caret = state.text.size();
    state.anchor = state.caret;
    event.text = u8"토ㅅ";
    const luil::text_input_view middle { luil::make_text_input_view(state, event, query_target) };
    REQUIRE(luil::search_query(middle, any_match) == u8"토");

    // 다음 단계가 다시 맞으면 좁은 쪽으로 돌아온다.
    event.text = u8"토스";
    const luil::text_input_view narrower { luil::make_text_input_view(state, event, query_target) };
    REQUIRE(luil::search_query(narrower, any_match) == u8"토스");

    // 조합이 아니면 확정된 글 하나뿐이라 물러설 것이 없다.
    const luil::text_input_view settled { luil::make_text_input_view(state, std::nullopt, query_target) };
    REQUIRE(luil::search_query(settled, any_match) == u8"토");
}

TEST_CASE("A search field inside an open menu keeps the editing keys", "[ui][interaction][popup]")
{
    // 검색 칸이 달린 메뉴다: 같은 popup tree에 텍스트 박스와 메뉴가 함께 있다.
    // 메뉴 kind 짝은 recording_policy가 돌려주는 것이다.
    const luil::ui_element_id box_id { kind_query_input, u8"menu-search" };
    const luil::ui_element_id first_item { kind_menu_item, u8"first" };
    const luil::ui_element_id second_item { kind_menu_item, u8"second" };

    auto popup_root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root, u8"menu" }) };
    popup_root->arrange({ { 0.0f, 0.0f, 160.0f, 120.0f }, 1.0f });
    auto box { std::make_unique<test_text_input>(box_id, u8"abc", 3, 3) };
    box->arrange({ { 0.0f, 0.0f, 160.0f, 24.0f }, 1.0f });
    popup_root->add(std::move(box));

    // find_menu는 owner 없는 container id를 찾는다.
    auto menu { std::make_unique<test_panel>(luil::ui_element_id { kind_menu }) };
    menu->arrange({ { 0.0f, 24.0f, 160.0f, 48.0f }, 1.0f });
    for (const luil::ui_element_id& id : { first_item, second_item })
    {
        auto item { std::make_unique<test_panel>(id) };
        item->arrange({ { 0.0f, 24.0f, 160.0f, 24.0f }, 1.0f });
        menu->add(std::move(item));
    }
    popup_root->add(std::move(menu));

    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());
    luil::surface_tree_list popups {};
    popups.emplace_back(u8"menu", std::make_shared<const luil::ui_tree>(std::move(popup_root)));
    controller.set_surface_trees(std::move(popups));

    // 초점 없이도 ↑/↓는 메뉴의 것이다 (지금까지와 같다).
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::arrow_down }));
    REQUIRE(controller.snapshot().menu_highlight == first_item);

    luil::pointer_pressed_event press { 20.0f, 12.0f, luil::pointer_button::left, at(0) };
    press.surface = u8"menu";
    static_cast<void>(controller.process(press));
    REQUIRE(controller.snapshot().focused_input == box_id);

    // 초점이 생겨도 ↑/↓는 여전히 메뉴가 갖는다 — 한 줄 검색 칸은 세로 키를 쓰지 않는다.
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::arrow_down }));
    REQUIRE(controller.snapshot().menu_highlight == second_item);

    // 메뉴가 갖지 않는 편집 키는 그 안의 텍스트 박스로 간다.
    // 메뉴가 전부 삼키면 검색 칸에서 caret을 옮길 수도, 고를 수도 없다.
    policy.edit_count = 0;
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::arrow_left }));
    REQUIRE(policy.edit_count == 1);
    REQUIRE(policy.last_edit.target == query_target);
    REQUIRE(policy.last_edit.command == luil::text::text_edit_command::move_left);

    luil::key_pressed_event select_all { luil::key_code::key_a };
    select_all.control = true;
    static_cast<void>(controller.process(select_all));
    REQUIRE(policy.last_edit.command == luil::text::text_edit_command::select_all);
}

TEST_CASE("Menu keyboard navigation follows the menu into a popup tree", "[ui][interaction][popup]")
{
    // 정책이 예약 kind 짝을 돌려주고 메뉴는 popup tree에 있다.
    class popup_menu_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::menu_kinds> menu() const override
        {
            return luil::menu_kinds { luil::ui_element_kind::menu, luil::ui_element_kind::menu_item };
        }
    };

    popup_menu_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());

    luil::menu_config config {};
    config.items.push_back({ u8"first", u8"첫째" });
    luil::menu_item_config second {};
    second.key = u8"second";
    second.label = u8"둘째";
    second.enabled = false;
    config.items.push_back(std::move(second));
    config.items.push_back({ u8"third", u8"셋째" });
    config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { fake_intent { key } } }; };
    auto menu { std::make_unique<luil::menu_element>(config) };
    menu->arrange({ { 0.0f, 0.0f, 160.0f, luil::menu_element::height_for(config) }, 1.0f });
    luil::surface_tree_list popups {};
    popups.emplace_back(u8"context", std::make_shared<const luil::ui_tree>(std::move(menu)));
    controller.set_surface_trees(std::move(popups));

    // 첫 ↓는 첫 활성 항목을 고르고, 다음 ↓는 비활성 항목을 건너뛴다.
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::arrow_down }));
    REQUIRE(controller.snapshot().menu_highlight == luil::ui_element_id { luil::ui_element_kind::menu_item, u8"first" });
    // 강조는 그 메뉴가 사는 표면을 함께 남긴다 — 초점은 여기 없고(항목은 자리가
    // 아니다) 포인터도 손대지 않았다 (multi-window-design.md).
    REQUIRE(controller.snapshot().menu_surface == u8"context");
    REQUIRE(controller.snapshot().focused_surface.empty());
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::arrow_down }));
    REQUIRE(controller.snapshot().menu_highlight == luil::ui_element_id { luil::ui_element_kind::menu_item, u8"third" });
    REQUIRE(controller.snapshot().menu_surface == u8"context");

    // Enter는 강조 항목의 선택 메시지를 낸다.
    auto actions { controller.process(luil::key_pressed_event { luil::key_code::enter }) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0]) != nullptr);
    REQUIRE(intent_of(actions[0])->name == u8"third");

    // popup이 사라지면(메뉴가 닫혔다) 강조도 표식도 사라진다.
    controller.set_surface_trees({});
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::arrow_down }));
    REQUIRE(controller.snapshot().menu_highlight == luil::ui_element_id {});
    REQUIRE(controller.snapshot().menu_surface.empty());
}

TEST_CASE("Pressing a tab stop takes the keyboard focus without showing the ring", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());

    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 15.0f, luil::pointer_button::left, at(0) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });
    // 텍스트 박스가 아니므로 caret·문자의 초점은 비어 있다.
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id {});
    // 눌러서 잡은 초점에는 테를 그리지 않는다.
    REQUIRE(controller.snapshot().focus_visible == false);
    static_cast<void>(controller.process(luil::pointer_released_event { 20.0f, 15.0f, luil::pointer_button::left, at(20) }));

    // 자리가 아닌 곳을 누르면 거둔다 (텍스트 초점과 같은 규칙).
    static_cast<void>(controller.process(luil::pointer_pressed_event { 150.0f, 150.0f, luil::pointer_button::left, at(40) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});

    // tree에서 사라지면 초점도 거둬진다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 15.0f, luil::pointer_button::left, at(60) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });
    auto empty { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    empty->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(empty)));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
}

TEST_CASE("Tab walks the focus order and wraps at both ends", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    const auto add_stop = [&root](const std::u8string_view name, const float y) {
        auto stop { std::make_unique<test_panel>(luil::ui_element_id { kind_button, std::u8string { name } }) };
        stop->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        stop->arrange({ { 0.0f, y, 100.0f, 20.0f }, 1.0f });
        root->add(std::move(stop));
    };
    add_stop(u8"one", 0.0f);
    add_stop(u8"two", 30.0f);
    add_stop(u8"three", 60.0f);
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    const auto tab = [](const bool shift) { return luil::key_pressed_event { luil::key_code::tab, false, shift, false, false, at(0) }; };
    const auto focused = [&controller] { return controller.snapshot().focused; };

    // 초점이 없으면 첫 자리에서 시작한다. 키보드로 옮긴 초점이라 테를 그린다.
    REQUIRE(controller.process(tab(false)).empty());
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"one" });
    REQUIRE(controller.snapshot().focus_visible);

    static_cast<void>(controller.process(tab(false)));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"two" });
    static_cast<void>(controller.process(tab(false)));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"three" });
    // 끝에서 처음으로 돈다.
    static_cast<void>(controller.process(tab(false)));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"one" });
    // Shift+Tab은 반대로 돈다.
    static_cast<void>(controller.process(tab(true)));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"three" });
}

TEST_CASE("Tab into a text box arms the caret and out of it disarms", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto input { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input }, u8"abcdef", 2u, 2u) };
    input->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    root->add(std::move(input));
    auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"one" }) };
    button->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    button->arrange({ { 0.0f, 40.0f, 100.0f, 20.0f }, 1.0f });
    root->add(std::move(button));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    // 텍스트 박스로 들어가면 문자·caret의 초점도 함께 선다.
    // 시각은 이벤트가 실어 온 것이다 — caret 깜빡임의 위상 기준이라 없으면
    // caret이 아예 그려지지 않는다.
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(700) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_query_input });
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input });
    REQUIRE(controller.snapshot().focus_started_at == at(700));

    // 박스를 나가면 문자의 초점만 비고 초점 자체는 다음 자리에 있다.
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(800) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id {});
}

TEST_CASE("Tab flows to the policy when nothing can take focus", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    // 자리가 없으면 삼키지 않는다 — 앱이 자기 뜻으로 쓸 수 있다.
    const auto actions { controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }) };
    REQUIRE(policy.last_key == luil::key_code::tab);
    REQUIRE(actions.size() == 1u);
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
}

TEST_CASE("Space presses the focused control but stays a letter in text boxes", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    int clicks { 0 };
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"one" }) };
    button->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    button->set_action(luil::ui_trigger::left_click, [&clicks](const luil::ui_action_context&) -> std::vector<luil::input_action> {
        ++clicks;
        return { luil::input_action { luil::app_message { fake_intent { u8"click" } } } };
    });
    root->add(std::move(button));
    auto input { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input }, u8"abcdef", 2u, 2u) };
    input->arrange({ { 0.0f, 40.0f, 100.0f, 20.0f }, 1.0f });
    root->add(std::move(input));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    const auto space = [] { return luil::key_pressed_event { luil::key_code::space, false, false, false, false, at(0) }; };

    // 초점이 없으면 앱 단축키다.
    static_cast<void>(controller.process(space()));
    REQUIRE(policy.last_key == luil::key_code::space);
    REQUIRE(clicks == 0);

    // 초점을 가진 컨트롤은 클릭과 같은 경로로 실행된다.
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(10) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });
    const auto actions { controller.process(space()) };
    REQUIRE(clicks == 1);
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0]) != nullptr);
    REQUIRE(intent_of(actions[0])->name == u8"click");
    // 클릭과 같은 경로라 policy도 함께 안다.
    REQUIRE(policy.clicked.size() == 1u);
    REQUIRE(policy.clicked[0] == luil::ui_element_id { kind_button, u8"one" });

    // 텍스트 박스에서는 글자다. 문자 경로가 먹으므로 키는 여기서 끝난다 —
    // 앱 단축키로도 흐르지 않는다.
    policy.last_key = luil::key_code::none;
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(20) }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input });
    REQUIRE(controller.process(space()).empty());
    REQUIRE(clicks == 1);
    REQUIRE(policy.last_key == luil::key_code::none);
}

TEST_CASE("Enter presses the focused control and flows on inside a text box", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    int clicks { 0 };
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"one" }) };
    button->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    button->set_action(luil::ui_trigger::left_click, [&clicks](const luil::ui_action_context&) -> std::vector<luil::input_action> {
        ++clicks;
        return { luil::input_action { luil::app_message { fake_intent { u8"click" } } } };
    });
    root->add(std::move(button));
    auto input { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input }, u8"abcdef", 2u, 2u) };
    input->arrange({ { 0.0f, 40.0f, 100.0f, 20.0f }, 1.0f });
    root->add(std::move(input));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    const auto enter = [] { return luil::key_pressed_event { luil::key_code::enter, false, false, false, false, at(0) }; };
    const auto tab = [](const int milliseconds) { return luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(milliseconds) }; };

    // 초점이 없으면 앱의 키다 (기본 버튼이 없는 화면이다).
    static_cast<void>(controller.process(enter()));
    REQUIRE(policy.last_key == luil::key_code::enter);
    REQUIRE(clicks == 0);

    // 초점을 가진 자리에서는 Space와 같은 경로로 실행된다 — **초점이 이긴다.**
    static_cast<void>(controller.process(tab(10)));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });
    const auto actions { controller.process(enter()) };
    REQUIRE(clicks == 1);
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"click");
    REQUIRE(policy.clicked.size() == 1u);

    // 텍스트 칸에서는 Space와 갈린다. Enter는 글자가 아니라 삼키지 않고 흘러간다 —
    // 그 아래에 기본 버튼이 있으면 그것이 받는다 (여기서는 없으니 앱까지 간다).
    policy.last_key = luil::key_code::none;
    static_cast<void>(controller.process(tab(20)));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input });
    static_cast<void>(controller.process(enter()));
    REQUIRE(clicks == 1);
    REQUIRE(policy.last_key == luil::key_code::enter);

    // Ctrl·Alt와 함께면 실행이 아니라 앱 단축키다 (Space와 같은 판정).
    policy.last_key = luil::key_code::none;
    static_cast<void>(controller.process(tab(30)));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::enter, true, false, false, false, at(40) }));
    REQUIRE(clicks == 1);
    REQUIRE(policy.last_key == luil::key_code::enter);

    // 역할로 알린 주 단축키(macOS의 Command)도 같은 판정이다 — 물리 Control이 없어도
    // 초점 버튼을 누르지 않는다.
    policy.last_key = luil::key_code::none;
    luil::key_pressed_event command_enter { luil::key_code::enter, false, false, false, false, at(50) };
    command_enter.meta = true;
    command_enter.primary_shortcut = true;
    static_cast<void>(controller.process(command_enter));
    REQUIRE(clicks == 1);
    REQUIRE(policy.last_key == luil::key_code::enter);

    policy.last_key = luil::key_code::none;
    luil::key_pressed_event command_space { luil::key_code::space, false, false, false, false, at(60) };
    command_space.meta = true;
    command_space.primary_shortcut = true;
    static_cast<void>(controller.process(command_space));
    REQUIRE(clicks == 1);
    REQUIRE(policy.last_key == luil::key_code::space);
}

TEST_CASE("Enter that the focus did not take goes to the default button", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    int confirms { 0 };
    // 바깥에 기본 버튼이 하나, 가둠 안에 또 하나를 둘 수 있는 조립이다.
    const auto build = [&confirms](const bool modal, const bool inner_default, const bool enabled) {
        const auto make_button = [&confirms](const std::u8string_view name, const bool is_default, const bool button_enabled) {
            auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, std::u8string { name } }) };
            button->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
            button->set_enabled(button_enabled);
            button->set_default_button(is_default);
            button->set_action(luil::ui_trigger::left_click, [&confirms, name](const luil::ui_action_context&) -> std::vector<luil::input_action> {
                ++confirms;
                return { luil::input_action { luil::app_message { fake_intent { std::u8string { name } } } } };
            });
            return button;
        };

        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        root->add(make_button(u8"outside", true, true));
        if (modal)
        {
            auto trap { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"modal" }) };
            trap->arrange({ { 0.0f, 40.0f, 200.0f, 160.0f }, 1.0f });
            trap->set_focus_trap(true);
            trap->add(make_button(u8"inside", inner_default, enabled));
            root->add(std::move(trap));
        }
        return std::make_shared<const luil::ui_tree>(std::move(root));
    };

    const auto enter = [] { return luil::key_pressed_event { luil::key_code::enter, false, false, false, false, at(0) }; };

    SECTION("초점이 없으면 기본 버튼이 받는다")
    {
        controller.set_tree(build(false, false, true));
        const auto actions { controller.process(enter()) };
        REQUIRE(confirms == 1);
        REQUIRE(intent_of(actions[0])->name == u8"outside");
        // 클릭과 같은 경로라 policy도 함께 안다.
        REQUIRE(policy.clicked.size() == 1u);
        // 앱 정책까지 흐르지 않았다 — 그 Enter는 기본 버튼의 것이다.
        REQUIRE(policy.last_key == luil::key_code::none);
    }

    SECTION("가둠이 서면 밖의 기본 버튼은 없는 것이다")
    {
        // 가둠 안에 기본 버튼이 없으면 Enter는 앱으로 흐른다.
        controller.set_tree(build(true, false, true));
        static_cast<void>(controller.process(enter()));
        REQUIRE(confirms == 0);
        REQUIRE(policy.last_key == luil::key_code::enter);

        // 안에 있으면 그것이 받는다 (그리기 순서의 마지막이 임자이기도 하다).
        policy.last_key = luil::key_code::none;
        controller.set_tree(build(true, true, true));
        const auto actions { controller.process(enter()) };
        REQUIRE(confirms == 1);
        REQUIRE(intent_of(actions[0])->name == u8"inside");
    }

    SECTION("비활성 기본 버튼은 실행하지 않고 앱으로 흘린다")
    {
        controller.set_tree(build(true, true, false));
        static_cast<void>(controller.process(enter()));
        REQUIRE(confirms == 0);
        REQUIRE(policy.last_key == luil::key_code::enter);
    }

    SECTION("초점이 이긴다 — 기본 버튼은 그때 나서지 않는다")
    {
        controller.set_tree(build(false, false, true));
        static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(10) }));
        REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"outside" });
        // 여기서는 초점과 기본 버튼이 같은 자리라 실행은 한 번뿐이다.
        static_cast<void>(controller.process(enter()));
        REQUIRE(confirms == 1);
    }

    SECTION("Ctrl+Enter는 앱의 것이다")
    {
        controller.set_tree(build(false, false, true));
        static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::enter, true, false, false, false, at(0) }));
        REQUIRE(confirms == 0);
        REQUIRE(policy.last_key == luil::key_code::enter);
    }

    SECTION("역할로 Control을 끈 Ctrl+Enter도 앱의 것이다")
    {
        // 물리 수정키가 눌렸으면 편집 역할과 무관하게 앱 단축키다.
        controller.set_tree(build(false, false, true));
        luil::key_pressed_event control_enter { luil::key_code::enter, true, false, false, false, at(0) };
        control_enter.primary_shortcut = false;
        static_cast<void>(controller.process(control_enter));
        REQUIRE(confirms == 0);
        REQUIRE(policy.last_key == luil::key_code::enter);
    }

    SECTION("텍스트 칸에 커서를 둔 채 친 Enter가 기본 버튼까지 간다")
    {
        // 모든 폼이 하는 그 동작이다. Space는 글자라 삼켜지지만 Enter는 흘러간다.
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        auto input { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input }, u8"abcdef", 2u, 2u) };
        input->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
        root->add(std::move(input));
        auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"confirm" }) };
        button->arrange({ { 0.0f, 40.0f, 100.0f, 20.0f }, 1.0f });
        button->set_default_button(true);
        button->set_action(luil::ui_trigger::left_click, [&confirms](const luil::ui_action_context&) -> std::vector<luil::input_action> {
            ++confirms;
            return { luil::input_action { luil::app_message { fake_intent { u8"confirm" } } } };
        });
        root->add(std::move(button));
        controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

        static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(10) }));
        REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input });
        const auto actions { controller.process(enter()) };
        REQUIRE(confirms == 1);
        REQUIRE(intent_of(actions[0])->name == u8"confirm");
    }
}

TEST_CASE("Arrows walk a focus group and leave the other direction to the app", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto group { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"tabs" }) };
    group->arrange({ { 0.0f, 0.0f, 200.0f, 20.0f }, 1.0f });
    group->set_focus_group(luil::focus_axis::horizontal);
    for (const std::u8string_view name : { std::u8string_view { u8"one" }, std::u8string_view { u8"two" }, std::u8string_view { u8"three" } })
    {
        auto item { std::make_unique<test_panel>(luil::ui_element_id { kind_button, std::u8string { name } }) };
        item->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        item->arrange({ { 0.0f, 0.0f, 60.0f, 20.0f }, 1.0f });
        group->add(std::move(item));
    }
    root->add(std::move(group));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    const auto key = [](const luil::key_code code) { return luil::key_pressed_event { code, false, false, false, false, at(0) }; };
    const auto focused = [&controller] { return controller.snapshot().focused; };

    // 묶음은 Tab에서 한 자리다 — 들어가면 첫 항목이다.
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"one" });

    REQUIRE(controller.process(key(luil::key_code::arrow_right)).empty());
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"two" });
    static_cast<void>(controller.process(key(luil::key_code::arrow_right)));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"three" });
    // 끝에서 돈다.
    static_cast<void>(controller.process(key(luil::key_code::arrow_right)));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"one" });
    static_cast<void>(controller.process(key(luil::key_code::arrow_left)));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"three" });

    // 방향이 맞지 않는 화살표는 묶음의 것이 아니다.
    const auto actions { controller.process(key(luil::key_code::arrow_down)) };
    REQUIRE(policy.last_key == luil::key_code::arrow_down);
    REQUIRE(actions.size() == 1u);
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"three" });

    // Tab은 묶음을 통째로 지난다 — 자리가 하나뿐이라 제자리로 돌아온다.
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"one" });
}

TEST_CASE("Home and End jump to the ends of a focus group without wrapping", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    // 세로 묶음이다. 가로 화살표는 이 묶음의 것이 아니지만 Home/End는 방향이
    // 없으므로 축과 무관하게 묶음이 가진다.
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto group { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"rows" }) };
    group->arrange({ { 0.0f, 0.0f, 200.0f, 60.0f }, 1.0f });
    group->set_focus_group(luil::focus_axis::vertical);
    for (const std::u8string_view name : { std::u8string_view { u8"one" }, std::u8string_view { u8"two" }, std::u8string_view { u8"three" } })
    {
        auto item { std::make_unique<test_panel>(luil::ui_element_id { kind_button, std::u8string { name } }) };
        item->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        item->arrange({ { 0.0f, 0.0f, 200.0f, 20.0f }, 1.0f });
        group->add(std::move(item));
    }
    root->add(std::move(group));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    const auto key = [](const luil::key_code code) { return luil::key_pressed_event { code, false, false, false, false, at(0) }; };
    const auto focused = [&controller] { return controller.snapshot().focused; };

    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"one" });

    REQUIRE(controller.process(key(luil::key_code::end)).empty());
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"three" });

    // 돌지 않는다 — 끝에서 한 번 더 눌러도 제자리이고, 키는 그대로 묶음이 가진다.
    // (화살표였다면 여기서 처음으로 돌아갔다.)
    policy.last_key = luil::key_code::none;
    REQUIRE(controller.process(key(luil::key_code::end)).empty());
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"three" });
    REQUIRE(policy.last_key == luil::key_code::none);

    REQUIRE(controller.process(key(luil::key_code::home)).empty());
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"one" });
    REQUIRE(controller.process(key(luil::key_code::home)).empty());
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"one" });

    // 축이 맞지 않는 화살표는 여전히 앱의 것이다 — Home/End만 방향을 묻지 않는다.
    const auto actions { controller.process(key(luil::key_code::arrow_left)) };
    REQUIRE(policy.last_key == luil::key_code::arrow_left);
    REQUIRE(actions.size() == 1u);
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"one" });
}

TEST_CASE("A text box inside a group keeps the caret arrows", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto group { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"row" }) };
    group->arrange({ { 0.0f, 0.0f, 200.0f, 20.0f }, 1.0f });
    group->set_focus_group(luil::focus_axis::horizontal);
    auto input { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input }, u8"abcdef", 3u, 3u) };
    input->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    group->add(std::move(input));
    auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"one" }) };
    button->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    button->arrange({ { 100.0f, 0.0f, 60.0f, 20.0f }, 1.0f });
    group->add(std::move(button));
    root->add(std::move(group));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input });

    // ←는 caret의 것이다. 묶음이 먼저 가져가면 글 안을 돌아다닐 수 없다.
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::arrow_left, false, false, false, false, at(10) }));
    REQUIRE(policy.edit_count == 1);
    REQUIRE(policy.last_edit.command == luil::text::text_edit_command::move_left);
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_query_input });
}

TEST_CASE("An element that takes Tab keeps the focus and the key", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    // 들여쓰기를 받는 코드 칸이다. Tab이 초점을 옮기면 탭 문자를 칠 수 없다.
    auto code { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input }, u8"abcdef", 2u, 2u) };
    code->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    code->set_takes_tab(true);
    root->add(std::move(code));
    auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"one" }) };
    button->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    button->arrange({ { 0.0f, 40.0f, 100.0f, 20.0f }, 1.0f });
    root->add(std::move(button));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    const auto tab = [](const bool shift) { return luil::key_pressed_event { luil::key_code::tab, false, shift, false, false, at(0) }; };

    // 첫 Tab은 여느 때처럼 첫 자리로 간다 (그 자리가 코드 칸이다).
    static_cast<void>(controller.process(tab(false)));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_query_input });

    // 그다음 Tab은 초점을 옮기지 않고 키 경로로 흐른다 — 앱이 탭 문자를 넣는다.
    const auto actions { controller.process(tab(false)) };
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_query_input });
    REQUIRE(policy.last_key == luil::key_code::tab);
    REQUIRE(actions.size() == 1u);

    // Shift+Tab도 그 element의 것이다.
    policy.last_key = luil::key_code::none;
    static_cast<void>(controller.process(tab(true)));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_query_input });
    REQUIRE(policy.last_key == luil::key_code::tab);
}

TEST_CASE("The app can reorder and filter the tab order", "[ui][interaction][focus]")
{
    ordering_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    float y { 0.0f };
    for (const std::u8string_view name : { std::u8string_view { u8"one" }, std::u8string_view { u8"two" }, std::u8string_view { u8"three" } })
    {
        auto stop { std::make_unique<test_panel>(luil::ui_element_id { kind_button, std::u8string { name } }) };
        stop->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        stop->arrange({ { 0.0f, y, 100.0f, 20.0f }, 1.0f });
        root->add(std::move(stop));
        y += 30.0f;
    }
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    const auto tab = [] { return luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }; };

    // 그리기 순서는 one → two → three지만 정책이 뒤집고 two를 걸렀다.
    static_cast<void>(controller.process(tab()));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"three" });
    static_cast<void>(controller.process(tab()));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });
    // 걸러 낸 자리에는 Tab이 서지 않는다.
    static_cast<void>(controller.process(tab()));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"three" });
}

TEST_CASE("A focus trap drops the focus that stayed outside", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    int opens { 0 };
    const auto build = [&opens](const bool modal) {
        const auto make_stop = [](const std::u8string_view name, luil::ui_action action) {
            auto stop { std::make_unique<test_panel>(luil::ui_element_id { kind_button, std::u8string { name } }) };
            stop->set_action(luil::ui_trigger::left_click, std::move(action));
            stop->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
            return stop;
        };
        const auto noop = [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };

        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        root->add(make_stop(u8"open", [&opens](const luil::ui_action_context&) -> std::vector<luil::input_action> {
            ++opens;
            return {};
        }));
        if (modal)
        {
            // scrim처럼 클릭을 흡수하는 가둠이다.
            auto trap { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"modal" }) };
            trap->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
            trap->set_focus_trap(true);
            trap->add(make_stop(u8"cancel", noop));
            trap->add(make_stop(u8"confirm", noop));
            root->add(std::move(trap));
        }
        return std::make_shared<const luil::ui_tree>(std::move(root));
    };

    const auto tab = [] { return luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }; };
    const auto space = [] { return luil::key_pressed_event { luil::key_code::space, false, false, false, false, at(0) }; };
    const auto focused = [&controller] { return controller.snapshot().focused; };

    controller.set_tree(build(false));
    static_cast<void>(controller.process(tab()));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"open" });

    // dialog가 열리면 그 버튼은 scrim 뒤라 사라진 것과 같다.
    controller.set_tree(build(true));
    REQUIRE(focused() == luil::ui_element_id {});
    REQUIRE(controller.snapshot().focus_visible == false);

    // 그래서 Space가 뒤의 버튼을 다시 실행하지 않는다.
    // 초점이 없으면 Space는 앱의 키다.
    static_cast<void>(controller.process(space()));
    REQUIRE(opens == 0);
    REQUIRE(policy.last_key == luil::key_code::space);

    // 첫 Tab이 dialog의 첫 자리에 서고, 그 뒤로도 밖으로 나가지 않는다.
    static_cast<void>(controller.process(tab()));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"cancel" });
    static_cast<void>(controller.process(tab()));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"confirm" });
    static_cast<void>(controller.process(tab()));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"cancel" });

    // dialog가 닫히면 다시 밖을 돈다.
    controller.set_tree(build(false));
    REQUIRE(focused() == luil::ui_element_id {});
    static_cast<void>(controller.process(tab()));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"open" });
}

TEST_CASE("The focus leaves an element that the next tree hides or disables", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    int presses { 0 };
    // 같은 id의 버튼과 텍스트 칸을 싣되, 숨김·비활성을 인자로 갈아 끼운다.
    // id가 그대로라 `find`로는 갈리지 않는 tree들이다.
    enum class shape
    {
        plain,
        button_hidden,
        branch_hidden,
        input_disabled,
    };
    const auto build = [&presses](const shape variant) {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });

        auto branch { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"branch" }) };
        branch->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
        branch->set_visible(variant != shape::branch_hidden);
        auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"go" }) };
        button->set_action(luil::ui_trigger::left_click, [&presses](const luil::ui_action_context&) -> std::vector<luil::input_action> {
            ++presses;
            return {};
        });
        button->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
        button->set_visible(variant != shape::button_hidden);
        branch->add(std::move(button));
        root->add(std::move(branch));

        auto input { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input }, u8"abc", 1u, 1u) };
        input->arrange({ { 0.0f, 120.0f, 100.0f, 20.0f }, 1.0f });
        input->set_enabled(variant != shape::input_disabled);
        root->add(std::move(input));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    };

    const auto enter = [] { return luil::key_pressed_event { luil::key_code::enter, false, false, false, false, at(0) }; };
    const auto press_button = [&controller] {
        static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::left, at(0) }));
        static_cast<void>(controller.process(luil::pointer_released_event { 10.0f, 10.0f, luil::pointer_button::left, at(10) }));
    };

    SECTION("버튼 자신이 숨으면 초점이 거둬지고 Enter가 그것을 누르지 않는다")
    {
        controller.set_tree(build(shape::plain));
        press_button();
        REQUIRE(presses == 1);
        REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"go" });

        controller.set_tree(build(shape::button_hidden));
        REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
        static_cast<void>(controller.process(enter()));
        REQUIRE(presses == 1);
    }

    SECTION("조상이 숨어도 같다 — 버튼 자신은 보이는 채다")
    {
        controller.set_tree(build(shape::plain));
        press_button();
        REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"go" });

        controller.set_tree(build(shape::branch_hidden));
        REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
        static_cast<void>(controller.process(enter()));
        REQUIRE(presses == 1);
    }

    SECTION("비활성이 된 텍스트 칸에는 글자도 편집 명령도 가지 않는다")
    {
        controller.set_tree(build(shape::plain));
        static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 130.0f, luil::pointer_button::left, at(0) }));
        static_cast<void>(controller.process(luil::pointer_released_event { 10.0f, 130.0f, luil::pointer_button::left, at(10) }));
        REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input });
        const int edits_before { policy.edit_count };

        controller.set_tree(build(shape::input_disabled));
        REQUIRE(controller.snapshot().focused_input == luil::ui_element_id {});
        REQUIRE(controller.snapshot().focused == luil::ui_element_id {});

        // 글자는 정책의 키로 흘러가고 편집 요청은 나가지 않는다.
        static_cast<void>(controller.process(luil::character_typed_event { U'x' }));
        static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::arrow_left, false, false, false, false, at(20) }));
        static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::key_v, true, false }));
        REQUIRE(policy.edit_count == edits_before);
    }

    SECTION("다시 보이고 활성이 되어도 초점이 저절로 돌아오지는 않는다")
    {
        controller.set_tree(build(shape::plain));
        press_button();
        controller.set_tree(build(shape::button_hidden));
        controller.set_tree(build(shape::plain));
        REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
    }
}

TEST_CASE("A focus trap stands the focus on the entry it names", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    const auto stop_id = [](const std::u8string_view name) { return luil::ui_element_id { kind_button, std::u8string { name } }; };

    // 가둠이 이름 지은 자리를 인자로 받는다. 빈 id가 "자동 초점 없음"이다.
    int fired { 0 };
    const auto build = [&fired, &stop_id](const luil::ui_element_id& entry) {
        const auto make_stop = [&fired, &stop_id](const std::u8string_view name, const luil::rect_f& slot) {
            auto stop { std::make_unique<test_panel>(stop_id(name)) };
            stop->set_action(luil::ui_trigger::left_click, [&fired](const luil::ui_action_context&) -> std::vector<luil::input_action> {
                ++fired;
                return {};
            });
            stop->arrange({ slot, 1.0f });
            return stop;
        };

        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        root->add(make_stop(u8"open", { 0.0f, 0.0f, 100.0f, 20.0f }));

        auto trap { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"modal" }) };
        trap->arrange({ { 0.0f, 40.0f, 200.0f, 160.0f }, 1.0f });
        trap->set_focus_trap(true);
        trap->set_focus_entry(entry);
        trap->add(make_stop(u8"cancel", { 0.0f, 40.0f, 100.0f, 20.0f }));
        trap->add(make_stop(u8"confirm", { 0.0f, 70.0f, 100.0f, 20.0f }));
        root->add(std::move(trap));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    };

    const auto tab = [] { return luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }; };
    const auto focused = [&controller] { return controller.snapshot().focused; };

    SECTION("이름 짓지 않은 가둠은 초점을 세우지 않는다")
    {
        controller.set_tree(build({}));
        REQUIRE(focused() == luil::ui_element_id {});
        REQUIRE(controller.snapshot().focus_visible == false);
        // 그 화면에서는 예전 그대로 첫 Tab이 초점을 세운다.
        static_cast<void>(controller.process(tab()));
        REQUIRE(focused() == stop_id(u8"cancel"));
    }

    SECTION("이름 지은 자리에 뜨는 순간 선다")
    {
        controller.set_tree(build(stop_id(u8"confirm")));
        REQUIRE(focused() == stop_id(u8"confirm"));
        // 테는 그리지 않는다 — 계기가 키보드가 아니라 tree라, 테를 켜면 마우스로
        // 연 dialog에도 테가 미리 선다. 키는 테 없이도 이 초점으로 간다.
        REQUIRE(controller.snapshot().focus_visible == false);
        // 시각은 비운다. `set_tree`에는 시각 인자가 없고 controller는 시계를 조회하지 않는다.
        REQUIRE(controller.snapshot().focus_started_at.has_value() == false);
        // 세우기만 한다 — 자동 초점은 액션을 실행하지 않는다.
        REQUIRE(fired == 0);
        REQUIRE(policy.clicked.empty());
        // 첫 Tab부터가 키보드다 — 옮기면서 테가 켜진다.
        static_cast<void>(controller.process(tab()));
        REQUIRE(focused() == stop_id(u8"cancel"));
        REQUIRE(controller.snapshot().focus_visible);
    }

    SECTION("이름 지은 자리가 지금 자리가 아니면 첫 자리로 물러선다")
    {
        controller.set_tree(build(stop_id(u8"gone")));
        REQUIRE(focused() == stop_id(u8"cancel"));
    }

    SECTION("같은 tree가 다시 와도 초점이 튀지 않는다")
    {
        controller.set_tree(build(stop_id(u8"confirm")));
        static_cast<void>(controller.process(tab()));
        REQUIRE(focused() == stop_id(u8"cancel"));
        // 술어의 전제("초점이 없다")가 이미 거짓이라 되풀이 적용이 공짜로 풀린다.
        controller.set_tree(build(stop_id(u8"confirm")));
        REQUIRE(focused() == stop_id(u8"cancel"));
    }

    SECTION("사용자가 누른 자리가 이긴다")
    {
        controller.set_tree(build(stop_id(u8"cancel")));
        REQUIRE(focused() == stop_id(u8"cancel"));
        static_cast<void>(controller.process(luil::pointer_pressed_event { 50.0f, 75.0f, luil::pointer_button::left, at(0) }));
        REQUIRE(focused() == stop_id(u8"confirm"));
        // 눌러서 잡은 초점이라 테는 없고, 다음 tree가 와도 진입 자리로 되돌아가지 않는다.
        REQUIRE(controller.snapshot().focus_visible == false);
        controller.set_tree(build(stop_id(u8"cancel")));
        REQUIRE(focused() == stop_id(u8"confirm"));
    }
}

TEST_CASE("A focus trap returns the focus to the outside place it named", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    const auto stop_id = [](const std::u8string_view name) { return luil::ui_element_id { kind_button, std::u8string { name } }; };

    // 가둠의 유무·되돌아갈 자리·그 바깥 자리의 존재를 인자로 받는다.
    const auto build = [&stop_id](const bool modal, const luil::ui_element_id& back, const bool keep_open) {
        const auto make_stop = [&stop_id](const std::u8string_view name, const luil::rect_f& slot) {
            auto stop { std::make_unique<test_panel>(stop_id(name)) };
            stop->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
            stop->arrange({ slot, 1.0f });
            return stop;
        };

        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        if (keep_open)
            root->add(make_stop(u8"open", { 0.0f, 0.0f, 90.0f, 20.0f }));
        root->add(make_stop(u8"later", { 100.0f, 0.0f, 90.0f, 20.0f }));
        if (modal)
        {
            auto trap { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"modal" }) };
            trap->arrange({ { 0.0f, 40.0f, 200.0f, 160.0f }, 1.0f });
            trap->set_focus_trap(true);
            trap->set_focus_entry(stop_id(u8"cancel"));
            trap->set_focus_return(back);
            trap->add(make_stop(u8"cancel", { 0.0f, 40.0f, 100.0f, 20.0f }));
            root->add(std::move(trap));
        }
        return std::make_shared<const luil::ui_tree>(std::move(root));
    };

    const auto tab = [] { return luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }; };
    const auto focused = [&controller] { return controller.snapshot().focused; };

    SECTION("가둠이 사라지면 이름 지은 바깥 자리로 돌아간다")
    {
        controller.set_tree(build(true, stop_id(u8"open"), true));
        REQUIRE(focused() == stop_id(u8"cancel"));
        controller.set_tree(build(false, {}, true));
        REQUIRE(focused() == stop_id(u8"open"));
        // 되돌리기는 진입과 달리 테를 그린다. 진입 자리는 방금 뜬 dialog가
        // "키가 여기로 온다"를 화면으로 이미 말하지만, 돌아온 자리는 아무것도
        // 말하지 않는다 — 테가 없으면 Space가 어디를 다시 실행할지 근거가 없다.
        // 시각은 진입처럼 비운다.
        REQUIRE(controller.snapshot().focus_visible);
        REQUIRE(controller.snapshot().focus_started_at.has_value() == false);
        // 한 칸을 비웠다 — 그 뒤의 tree가 초점을 다시 끌어오지 않는다.
        static_cast<void>(controller.process(tab()));
        REQUIRE(focused() == stop_id(u8"later"));
        controller.set_tree(build(false, {}, true));
        REQUIRE(focused() == stop_id(u8"later"));
    }

    SECTION("되돌릴 자리를 이름 짓지 않으면 초점은 사라진 채로 남는다")
    {
        controller.set_tree(build(true, {}, true));
        REQUIRE(focused() == stop_id(u8"cancel"));
        controller.set_tree(build(false, {}, true));
        REQUIRE(focused() == luil::ui_element_id {});
        REQUIRE(controller.snapshot().focus_visible == false);
    }

    SECTION("되돌릴 자리가 사라졌으면 초점 없음으로 남는다")
    {
        controller.set_tree(build(true, stop_id(u8"open"), true));
        REQUIRE(focused() == stop_id(u8"cancel"));
        // 닫히는 사이에 화면이 바뀌어 그 자리가 없다.
        controller.set_tree(build(false, {}, false));
        REQUIRE(focused() == luil::ui_element_id {});
        // 낡은 이름을 들고 있지 않는다 — 그 자리가 다시 생겨도 끌어가지 않는다.
        controller.set_tree(build(false, {}, true));
        REQUIRE(focused() == luil::ui_element_id {});
    }
}

TEST_CASE("Nested traps hand the return slot to the outer one", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    const auto stop_id = [](const std::u8string_view name) { return luil::ui_element_id { kind_button, std::u8string { name } }; };

    // 0은 가둠 없음, 1은 바깥만, 2는 바깥 안에 안쪽까지다.
    // 임자는 "그리기 순서의 마지막"이라 자손인 안쪽이 이긴다.
    const auto build = [&stop_id](const int depth) {
        const auto make_stop = [&stop_id](const std::u8string_view name, const luil::rect_f& slot) {
            auto stop { std::make_unique<test_panel>(stop_id(name)) };
            stop->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
            stop->arrange({ slot, 1.0f });
            return stop;
        };

        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        root->add(make_stop(u8"open", { 0.0f, 0.0f, 100.0f, 20.0f }));
        if (depth >= 1)
        {
            auto outer { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"outer" }) };
            outer->arrange({ { 0.0f, 30.0f, 200.0f, 170.0f }, 1.0f });
            outer->set_focus_trap(true);
            outer->set_focus_entry(stop_id(u8"outer-cancel"));
            outer->set_focus_return(stop_id(u8"open"));
            outer->add(make_stop(u8"outer-cancel", { 0.0f, 30.0f, 100.0f, 20.0f }));
            // 바깥 안에서 안쪽을 여는 자리다. 안쪽이 되돌아갈 곳을 **바깥의 진입
            // 자리와 다른 id**로 갈라 두어야 아래 단정문이 두 경로를 가른다.
            outer->add(make_stop(u8"outer-more", { 100.0f, 30.0f, 100.0f, 20.0f }));
            if (depth >= 2)
            {
                auto inner { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"inner" }) };
                inner->arrange({ { 0.0f, 90.0f, 200.0f, 110.0f }, 1.0f });
                inner->set_focus_trap(true);
                inner->set_focus_entry(stop_id(u8"inner-ok"));
                inner->set_focus_return(stop_id(u8"outer-more"));
                inner->add(make_stop(u8"inner-ok", { 0.0f, 90.0f, 100.0f, 20.0f }));
                outer->add(std::move(inner));
            }
            root->add(std::move(outer));
        }
        return std::make_shared<const luil::ui_tree>(std::move(root));
    };

    const auto focused = [&controller] { return controller.snapshot().focused; };

    controller.set_tree(build(2));
    REQUIRE(focused() == stop_id(u8"inner-ok"));

    // 안쪽이 사라지면 **바깥의 `focus_entry`**에 선다. 안쪽이 이름 지은 되돌아갈
    // 자리(`outer-more`)는 소비되지 않는다 — 되돌리기 절의 전제가 "가둠이 하나도
    // 없다"인데 바깥 가둠이 아직 서 있어서다. 두 id를 갈라 둔 덕에 이 단정문이
    // "바깥 entry가 이긴다"만을 뜻한다 (focus-entry-design.md).
    //  - 한 칸은 그동안 바깥이 자기 값으로 덮는다. 그리기 순서의 마지막이 임자라는
    //    기존 규칙이 그대로 스택 노릇을 하는 것이라 controller에 쌓는 자리를 따로
    //    두지 않는다.
    controller.set_tree(build(1));
    REQUIRE(focused() == stop_id(u8"outer-cancel"));

    // 바깥까지 사라지면 그 바깥이 이름 지은 자리로 돌아간다.
    controller.set_tree(build(0));
    REQUIRE(focused() == stop_id(u8"open"));
}

TEST_CASE("Nested traps leave two paths that no design settled", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    const auto stop_id = [](const std::u8string_view name) { return luil::ui_element_id { kind_button, std::u8string { name } }; };

    // 위 test와 같은 조립인데 **바깥이 진입 자리를 이름 짓는지**까지 인자로 받는다.
    const auto build = [&stop_id](const int depth, const bool outer_names_entry) {
        const auto make_stop = [&stop_id](const std::u8string_view name, const luil::rect_f& slot) {
            auto stop { std::make_unique<test_panel>(stop_id(name)) };
            stop->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
            stop->arrange({ slot, 1.0f });
            return stop;
        };

        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        root->add(make_stop(u8"open", { 0.0f, 0.0f, 100.0f, 20.0f }));
        if (depth >= 1)
        {
            auto outer { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"outer" }) };
            outer->arrange({ { 0.0f, 30.0f, 200.0f, 170.0f }, 1.0f });
            outer->set_focus_trap(true);
            if (outer_names_entry)
                outer->set_focus_entry(stop_id(u8"outer-cancel"));
            outer->set_focus_return(stop_id(u8"open"));
            outer->add(make_stop(u8"outer-cancel", { 0.0f, 30.0f, 100.0f, 20.0f }));
            outer->add(make_stop(u8"outer-more", { 100.0f, 30.0f, 100.0f, 20.0f }));
            if (depth >= 2)
            {
                auto inner { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"inner" }) };
                inner->arrange({ { 0.0f, 90.0f, 200.0f, 110.0f }, 1.0f });
                inner->set_focus_trap(true);
                inner->set_focus_entry(stop_id(u8"inner-ok"));
                inner->set_focus_return(stop_id(u8"outer-more"));
                inner->add(make_stop(u8"inner-ok", { 0.0f, 90.0f, 100.0f, 20.0f }));
                outer->add(std::move(inner));
            }
            root->add(std::move(outer));
        }
        return std::make_shared<const luil::ui_tree>(std::move(root));
    };

    const auto tab = [] { return luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }; };
    const auto focused = [&controller] { return controller.snapshot().focused; };

    // 아래 둘은 **옳은 답이라서 잠그는 것이 아니다.** 어느 문서도 이 두 경로를
    // 의도라고 적은 적이 없다. 지금 동작이 바뀌면 알아채려고 잠근다 — 실제 중첩
    // modal 소비자가 생기면 가둠 사슬을 스택으로 만드는 것과 함께 다시 연다
    // (focus-entry-design.md).

    SECTION("바깥이 진입 자리를 이름 짓지 않으면 가둠 안에 초점이 없다")
    {
        // 안쪽은 이름 지었으니 안쪽이 임자인 동안은 거기 선다.
        controller.set_tree(build(2, false));
        REQUIRE(focused() == stop_id(u8"inner-ok"));

        // 안쪽 초점의 element가 사라져 초점을 거둔다.
        // 바깥 scope에 진입 대상이 없으면 trap은 유지되지만 키를 받을 초점은 비어 있다.
        controller.set_tree(build(1, false));
        REQUIRE(focused() == luil::ui_element_id {});
        REQUIRE(controller.snapshot().focus_visible == false);

        // 벗어나는 길은 사용자의 Tab뿐이다.
        static_cast<void>(controller.process(tab()));
        REQUIRE(focused() == stop_id(u8"outer-cancel"));
    }

    SECTION("둘이 한 frame에 함께 닫히면 초점이 통째로 사라진다")
    {
        controller.set_tree(build(2, true));
        REQUIRE(focused() == stop_id(u8"inner-ok"));

        // 한 칸에는 안쪽 값(`outer-more`)이 들어 있고 그 자리는 바깥과 함께
        // 사라졌다. 바깥이 이름 지은 `open`은 tree에 그대로 있는데도 한 칸이
        // 그것을 들고 있지 않아 초점이 아무 데도 서지 않는다.
        controller.set_tree(build(0, true));
        REQUIRE(focused() == luil::ui_element_id {});
        REQUIRE(controller.snapshot().focus_visible == false);

        // 한 칸은 어느 쪽이든 비운다 — 뒤에 오는 tree가 초점을 뒤늦게 끌어오지 않는다.
        controller.set_tree(build(0, true));
        REQUIRE(focused() == luil::ui_element_id {});
    }
}

TEST_CASE("Escape closes the trap that carries a dismiss action", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    // dismiss가 있는 dialog, 없는 dialog, 그리고 그 위에 열린 메뉴를 같은 조립으로 짓는다.
    const auto build = [](const bool closable, const bool menu_open) {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });

        auto trap { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"modal" }) };
        trap->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        trap->set_focus_trap(true);
        if (closable)
            trap->set_dismiss_action(
                [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_message { fake_intent { u8"close-dialog" } } } }; });
        if (menu_open)
        {
            auto menu { std::make_unique<test_panel>(luil::ui_element_id { kind_menu }) };
            menu->arrange({ { 0.0f, 0.0f, 80.0f, 40.0f }, 1.0f });
            trap->add(std::move(menu));
        }
        root->add(std::move(trap));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    };

    const auto escape = [] { return luil::key_pressed_event { luil::key_code::escape, false, false, false, false, at(0) }; };

    controller.set_tree(build(true, false));
    const auto closed { controller.process(escape()) };
    REQUIRE(closed.size() == 1u);
    REQUIRE(intent_of(closed[0]) != nullptr);
    REQUIRE(intent_of(closed[0])->name == u8"close-dialog");
    // 앱 정책까지 흐르지 않았다 — 그 Esc는 dialog의 것이다.
    REQUIRE(policy.last_key == luil::key_code::none);

    // 빈 액션이 "Esc로는 닫지 않는다"다. 키가 그대로 앱으로 흐른다.
    controller.set_tree(build(false, false));
    const auto ignored { controller.process(escape()) };
    REQUIRE(policy.last_key == luil::key_code::escape);
    REQUIRE(ignored.size() == 1u);
    REQUIRE(intent_of(ignored[0])->name == u8"key");

    // dialog 안에서 연 메뉴가 먼저다 — Esc는 메뉴만 닫고 dialog는 남는다.
    policy.last_key = luil::key_code::none;
    controller.set_tree(build(true, true));
    const auto menu_closed { controller.process(escape()) };
    REQUIRE(menu_closed.size() == 1u);
    REQUIRE(intent_of(menu_closed[0])->name == u8"close-menu");
    REQUIRE(policy.last_key == luil::key_code::none);
}

TEST_CASE("Tab from another surface starts in the surface that sent the key", "[ui][interaction][window]")
{
    // 초점이 없을 때의 시작 표면은 **키가 온 표면**이다.
    // "마지막으로 누른 표면"은 대체값이었고, 누름 없이 창이 활성화되는 길
    // (Alt+Tab·작업 표시줄·캡션 클릭)마다 틀렸다
    // (key-surface-routing-design.md).
    recording_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());
    luil::surface_tree_list surfaces {};
    surfaces.emplace_back(u8"tool", popup_item_tree());
    controller.set_surface_trees(std::move(surfaces));

    // 주 창을 눌러 "마지막으로 손댄 표면"을 주 창으로 만든다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 15.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_released_event { 20.0f, 15.0f, luil::pointer_button::left, at(20) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });

    // 빈 곳을 눌러 초점을 거둔다. 누름의 기억을 들었다면 그 값은 여전히 주 창이다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 150.0f, 150.0f, luil::pointer_button::left, at(40) }));
    static_cast<void>(controller.process(luil::pointer_released_event { 150.0f, 150.0f, luil::pointer_button::left, at(60) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});

    // 보조 창 표식의 Tab은 그 창의 자리를 잡는다.
    luil::key_pressed_event from_tool { luil::key_code::tab, false, false, false, false, at(80) };
    from_tool.surface = u8"tool";
    REQUIRE(controller.process(from_tool).empty());
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_popup_item, u8"first" });
    REQUIRE(controller.snapshot().focused_surface == u8"tool");

    // 초점이 선 뒤에는 그 초점의 표면이 이긴다 — 빈 표식(주 창)의 Tab도 보조
    // tree 안에서 돈다. popup이 사는 길이 이것이다: 앵커 창이 키를 나르므로
    // 표식과 초점의 표면이 다른 것이 정상이다 (key-surface-routing-design.md).
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(100) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_popup_item, u8"first" });
    REQUIRE(controller.snapshot().focused_surface == u8"tool");

    // 빈 표식은 여전히 주 창이다 — 초점을 거두고 나면 그리로 돌아간다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 150.0f, 150.0f, luil::pointer_button::left, at(120) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(140) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });
    REQUIRE(controller.snapshot().focused_surface.empty());
}

TEST_CASE("Enter and Escape from a secondary window do not reach the main window", "[ui][interaction][window]")
{
    // 주 창에 기본 버튼과 dismiss를 가진 가둠이 서 있고 보조 창에는 둘 다 없다.
    // 초점이 없으므로 두 키의 임자는 **키가 온 표면**이 정한다.
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    int confirms { 0 };
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto trap { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"modal" }) };
    trap->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    trap->set_focus_trap(true);
    trap->set_dismiss_action([](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_message { fake_intent { u8"close-dialog" } } } }; });
    // 가둠은 자리를 이름 짓지 않는다 — 자동 초점이 서면 "초점 없음"의 길을
    // 재지 못한다 (focus-entry-design.md).
    auto confirm { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"confirm" }) };
    confirm->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    confirm->set_default_button(true);
    confirm->set_action(luil::ui_trigger::left_click, [&confirms](const luil::ui_action_context&) -> std::vector<luil::input_action> {
        ++confirms;
        return { luil::input_action { luil::app_message { fake_intent { u8"confirm" } } } };
    });
    trap->add(std::move(confirm));
    root->add(std::move(trap));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    luil::surface_tree_list surfaces {};
    surfaces.emplace_back(u8"tool", popup_item_tree());
    controller.set_surface_trees(std::move(surfaces));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});

    const auto from_tool = [](const luil::key_code key, const int when) {
        luil::key_pressed_event event { key, false, false, false, false, at(when) };
        event.surface = u8"tool";
        return event;
    };

    // 보조 창의 Enter는 주 창의 기본 버튼을 실행하지 않는다.
    // 그 표면에는 기본 버튼이 없으므로 키는 앱 정책으로 흐른다.
    const auto entered { controller.process(from_tool(luil::key_code::enter, 0)) };
    REQUIRE(confirms == 0);
    REQUIRE(policy.last_key == luil::key_code::enter);
    REQUIRE(entered.size() == 1u);
    REQUIRE(intent_of(entered[0]) != nullptr);
    REQUIRE(intent_of(entered[0])->name == u8"key");

    // 보조 창의 Esc도 주 창의 가둠을 닫지 않는다.
    policy.last_key = luil::key_code::none;
    const auto escaped { controller.process(from_tool(luil::key_code::escape, 20)) };
    REQUIRE(policy.last_key == luil::key_code::escape);
    REQUIRE(escaped.size() == 1u);
    REQUIRE(intent_of(escaped[0])->name == u8"key");

    // 판정을 죽인 것이 아니다 — 주 창이 나른 같은 키는 그대로 닿는다.
    policy.last_key = luil::key_code::none;
    const auto confirmed { controller.process(luil::key_pressed_event { luil::key_code::enter, false, false, false, false, at(40) }) };
    REQUIRE(confirms == 1);
    REQUIRE(confirmed.size() == 1u);
    REQUIRE(intent_of(confirmed[0])->name == u8"confirm");
    REQUIRE(policy.last_key == luil::key_code::none);

    const auto closed { controller.process(luil::key_pressed_event { luil::key_code::escape, false, false, false, false, at(60) }) };
    REQUIRE(closed.size() == 1u);
    REQUIRE(intent_of(closed[0])->name == u8"close-dialog");
    REQUIRE(policy.last_key == luil::key_code::none);
}

namespace {
    // 자리 이름 앞에 표면의 머리를 붙인다.
    // 어느 tree의 자리에 초점이 섰는지를 이름만 보고 가리려는 것이다.
    [[nodiscard]] luil::ui_element_id surface_stop_id(const std::u8string_view surface, const std::u8string_view name)
    {
        std::u8string owner { surface };
        owner += u8'-';
        owner += name;
        return luil::ui_element_id { kind_button, std::move(owner) };
    }

    // 확인 dialog가 뜬 창의 tree다. 주 tree로도 보조 표면으로도 쓴다 —
    // controller에는 둘의 구분이 없다 (multi-window-design.md).
    //  - `trapped`가 거짓이면 dialog가 닫힌 뒤의 같은 화면이다. 가둠 밖의
    //    `open`은 그대로 남아 되돌아갈 자리가 된다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> dialog_tree(const std::u8string_view surface, const bool trapped)
    {
        const auto make_stop = [](const luil::ui_element_id& id, const luil::rect_f& slot) {
            auto stop { std::make_unique<test_panel>(id) };
            stop->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
            stop->arrange({ slot, 1.0f });
            return stop;
        };

        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root, std::u8string { surface } }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        root->add(make_stop(surface_stop_id(surface, u8"open"), { 0.0f, 0.0f, 100.0f, 20.0f }));
        if (trapped == false)
            return std::make_shared<const luil::ui_tree>(std::move(root));

        auto trap { std::make_unique<test_panel>(luil::ui_element_id { kind_card, std::u8string { surface } }) };
        trap->arrange({ { 0.0f, 40.0f, 200.0f, 160.0f }, 1.0f });
        trap->set_focus_trap(true);
        trap->set_focus_entry(surface_stop_id(surface, u8"confirm"));
        trap->set_focus_return(surface_stop_id(surface, u8"open"));
        trap->add(make_stop(surface_stop_id(surface, u8"confirm"), { 0.0f, 40.0f, 100.0f, 20.0f }));
        root->add(std::move(trap));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }
} // namespace

TEST_CASE("A focus trap enters in the surface that holds the keyboard", "[ui][interaction][window][focus]")
{
    // 가둠이 막는 것은 키보드이고 키보드는 활성 창에 있다. 그래서 활성 창이 아닌
    // 가둠은 지금 아무도 가두고 있지 않다 (active-surface-design.md).
    // focus-entry-design.md이 "표면 둘이 동시에 가두면 누가 임자인가"로
    // 미뤄 둔 자리의 답이다.
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    const auto focused = [&controller] { return controller.snapshot().focused; };
    const auto activate = [&controller](const std::u8string_view surface) {
        // 실물의 순서 그대로다 — Win32는 옛 창의 `WM_KILLFOCUS`를 새 창의
        // `WM_SETFOCUS`보다 **먼저** 보낸다.
        static_cast<void>(controller.process(luil::surface_focus_lost_event { controller.snapshot().focused_surface }));
        static_cast<void>(controller.process(luil::surface_focus_gained_event { std::u8string { surface } }));
    };

    SECTION("활성 표면이 주 창이면 예전 그대로 주 창의 가둠이 진입한다")
    {
        // 활성 표면의 기본값이 주 창이라, 알림을 한 번도 받지 못한 소비자도
        // 옛 동작 그대로 선다 (3.2).
        controller.set_tree(dialog_tree(u8"main", true));
        REQUIRE(focused() == surface_stop_id(u8"main", u8"confirm"));
        REQUIRE(controller.snapshot().focused_surface.empty());
    }

    SECTION("보조 창이 활성인 동안 주 창의 가둠은 진입하지 않는다")
    {
        luil::surface_tree_list surfaces {};
        surfaces.emplace_back(u8"tool", dialog_tree(u8"tool", false));
        controller.set_surface_trees(std::move(surfaces));
        activate(u8"tool");

        // 보고 있지 않은 창에 dialog가 떴다. 초점은 서지 않는다.
        controller.set_tree(dialog_tree(u8"main", true));
        REQUIRE(focused() == luil::ui_element_id {});

        // 그 창을 활성화하는 순간 그 자리에 선다 — 계기가 느는 것이 아니라
        // 진입의 전제가 하나 느는 것이다.
        activate({});
        REQUIRE(focused() == surface_stop_id(u8"main", u8"confirm"));
        REQUIRE(controller.snapshot().focused_surface.empty());
    }

    SECTION("보조 표면의 가둠이 그 표면의 진입 자리에 선다")
    {
        // 지금까지는 진입이 주 tree만 보아, 같은 앱 코드가 창에 따라 다르게 돌았다
        // (active-surface-design.md).
        controller.set_tree(single_button_tree());
        luil::surface_tree_list surfaces {};
        surfaces.emplace_back(u8"tool", dialog_tree(u8"tool", true));
        controller.set_surface_trees(std::move(surfaces));
        REQUIRE(focused() == luil::ui_element_id {});

        activate(u8"tool");
        REQUIRE(focused() == surface_stop_id(u8"tool", u8"confirm"));
        REQUIRE(controller.snapshot().focused_surface == u8"tool");
    }

    SECTION("되돌리기는 적어 둔 표면이 활성일 때만 발화한다")
    {
        controller.set_tree(dialog_tree(u8"main", true));
        luil::surface_tree_list surfaces {};
        surfaces.emplace_back(u8"tool", dialog_tree(u8"tool", false));
        controller.set_surface_trees(std::move(surfaces));
        REQUIRE(focused() == surface_stop_id(u8"main", u8"confirm"));

        // 보조 창으로 넘어간다. 주 창의 가둠은 아직 서 있다.
        activate(u8"tool");
        REQUIRE(focused() == luil::ui_element_id {});

        // 그 dialog가 보조 창이 활성인 동안 닫힌다.
        // 되돌리기가 여기서 터지면 보고 있지 않은 창으로 초점이 뛴다.
        controller.set_tree(dialog_tree(u8"main", false));
        REQUIRE(focused() == luil::ui_element_id {});

        // 돌아오면 그때 되돌아간다 — 한 칸은 표면을 함께 들고 기다렸다 (3.4).
        activate({});
        REQUIRE(focused() == surface_stop_id(u8"main", u8"open"));
        REQUIRE(controller.snapshot().focused_surface.empty());
    }

    SECTION("활성 표면이 닫혀 사라지면 진입이 멈춘다")
    {
        controller.set_tree(dialog_tree(u8"main", true));
        luil::surface_tree_list surfaces {};
        surfaces.emplace_back(u8"tool", dialog_tree(u8"tool", false));
        controller.set_surface_trees(std::move(surfaces));
        activate(u8"tool");
        REQUIRE(focused() == luil::ui_element_id {});

        // 표면이 없어지면 `surface_tree`가 nullptr로 답해 아무 tree도 고르지
        // 못한다. 활성 표면 한 칸은 낡은 채 남지만 그것으로 할 수 있는 일이 없다.
        controller.set_surface_trees({});
        REQUIRE(focused() == luil::ui_element_id {});

        // 실물에서는 그 창이 닫히며 주 창이 초점을 되받고, 그 알림이 진입을 깨운다.
        activate({});
        REQUIRE(focused() == surface_stop_id(u8"main", u8"confirm"));
    }
}

TEST_CASE("Pressing a part inside the focused text box keeps the caret", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    auto input { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input }, u8"abcdef", 0u, 0u) };
    input->arrange({ { 0.0f, 0.0f, 160.0f, 20.0f }, 1.0f });
    // 칸 안의 부품이다: 자리도 아니고(tab_stop false) 텍스트 대상도 아니다 —
    // 실물의 지우기 버튼이 정확히 이 꼴이다.
    auto part { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"clear" }) };
    part->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    part->set_tab_stop(false);
    part->arrange({ { 140.0f, 0.0f, 20.0f, 20.0f }, 1.0f });
    input->add(std::move(part));
    root->add(std::move(input));

    auto outside { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"outside" }) };
    outside->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    outside->set_tab_stop(false);
    outside->arrange({ { 0.0f, 40.0f, 40.0f, 20.0f }, 1.0f });
    root->add(std::move(outside));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    const auto press = [&controller](const float x, const float y) {
        static_cast<void>(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, at(0) }));
        static_cast<void>(controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, at(1) }));
    };

    // 칸을 누르면 텍스트 초점이 그 칸으로 간다.
    press(20.0f, 10.0f);
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input });

    // 칸 안의 부품을 눌러도 caret이 사라지지 않는다.
    // 초점이 부품으로 옮겨 가지도 않는다 — 글이 갈 곳은 여전히 칸이다.
    press(150.0f, 10.0f);
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_query_input });
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_query_input });

    // 칸 밖의 같은 성질(자리 아님·텍스트 아님)을 누르면 예전처럼 거둔다.
    press(20.0f, 50.0f);
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id {});
}
TEST_CASE("Typing letters walks a focus group by label", "[ui][interaction][focus]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto group { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"rows" }) };
    group->arrange({ { 0.0f, 0.0f, 200.0f, 80.0f }, 1.0f });
    group->set_focus_group(luil::focus_axis::vertical);
    const std::u8string_view labels[] { u8"apple", u8"apricot", u8"banana", u8"cherry" };
    for (const std::u8string_view label : labels)
    {
        auto item { std::make_unique<test_panel>(luil::ui_element_id { kind_button, std::u8string { label } }) };
        item->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        item->set_search_label(std::u8string { label });
        item->arrange({ { 0.0f, 0.0f, 200.0f, 20.0f }, 1.0f });
        group->add(std::move(item));
    }
    root->add(std::move(group));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    const auto focused = [&controller] { return controller.snapshot().focused; };
    const auto type
        = [&controller](const char32_t character, const std::chrono::milliseconds when) { return controller.process(luil::character_typed_event { character, at(static_cast<int>(when.count())) }); };

    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"apple" });

    // 첫 글자는 **다음** 항목부터 찾는다 — 같은 글자를 거듭 치면 그 글자로 시작하는
    // 항목들을 돈다. 글자는 묶음이 가지므로 앱으로 새지 않는다.
    REQUIRE(type(U'a', 100ms).empty());
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"apricot" });
    REQUIRE(type(U'a', 2000ms).empty());
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"apple" });

    // 이어 친 글자는 지금 항목부터 찾는다 — 글을 더 적은 것이지 다음으로 가자는
    // 뜻이 아니다. "ap"는 apple에 그대로 맞으므로 자리가 움직이지 않는다.
    static_cast<void>(type(U'p', 2100ms));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"apple" });
    // "apr"는 apricot이다.
    static_cast<void>(type(U'r', 2200ms));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"apricot" });

    // 시간이 끊기면 앞의 글자를 잊는다 — 'b'는 새 질의다.
    static_cast<void>(type(U'b', 5000ms));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"banana" });

    // 맞는 것이 없으면 자리는 그대로다.
    static_cast<void>(type(U'z', 5100ms));
    REQUIRE(focused() == luil::ui_element_id { kind_button, u8"banana" });
}

namespace {
    // 흘리는 창 노릇을 하는 test element다.
    // 실제 `scroll_view_element`를 쓰지 않는 이유는 이 test가 묻는 것이 라우팅이지
    // 스크롤 셈이 아니어서다 — 셈은 그 element의 test가 이미 잠근다.
    class reveal_probe final : public luil::ui_element
    {
    public:
        reveal_probe(const luil::ui_element_id id, const float delta)
            : ui_element { id }
            , delta_ { delta }
        {}

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}

        [[nodiscard]] float scroll_delta_to_reveal(const luil::rect_f&) const override
        {
            return delta_;
        }

    private:
        float delta_ { 0.0f };
    };

    // 되살리기 훅만 켠 정책이다.
    // `recording_policy`에 얹지 않는 것이 중요하다 — 얹으면 Tab·화살표가 액션을
    // 내지 않는 것을 잠근 기존 test 넷이 한꺼번에 깨진다.
    class revealing_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::vector<luil::input_action> on_focus_moved(const luil::ui_tree&, const luil::ui_element_id& focused) override
        {
            ++calls;
            last_focused = focused;
            return { luil::make_app_action(fake_intent { u8"reveal" }) };
        }

        int calls { 0 };
        luil::ui_element_id last_focused {};
    };
} // namespace

TEST_CASE("Reveal routing picks the first route that owns the focused element", "[ui][interaction][focus]")
{
    // 휠 라우팅의 거울이다. 좌표가 덮는가 대신 **품는가**를 묻는다.
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });

    auto scrolling { std::make_unique<reveal_probe>(luil::ui_element_id { kind_card, u8"list" }, 40.0f) };
    scrolling->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    auto row { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"row" }) };
    row->arrange({ { 0.0f, 300.0f, 200.0f, 20.0f }, 1.0f });
    scrolling->add(std::move(row));
    root->add(std::move(scrolling));

    // 이미 보이는 창이다 (델타 0).
    auto settled { std::make_unique<reveal_probe>(luil::ui_element_id { kind_card, u8"settled" }, 0.0f) };
    settled->arrange({ { 0.0f, 100.0f, 200.0f, 100.0f }, 1.0f });
    auto seen { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"seen" }) };
    seen->arrange({ { 0.0f, 110.0f, 200.0f, 20.0f }, 1.0f });
    settled->add(std::move(seen));
    root->add(std::move(settled));

    const luil::ui_tree tree { std::move(root) };
    const luil::scroll_route routes[] {
        { luil::ui_element_id { kind_card, u8"list" }, [](const float delta) { return luil::make_app_action(fake_intent { delta > 0.0f ? u8"list-down" : u8"list-up" }); } },
        { luil::ui_element_id { kind_card, u8"settled" }, [](const float) { return luil::make_app_action(fake_intent { u8"settled" }); } },
    };

    // 초점을 품은 첫 줄이 임자다.
    auto actions { luil::route_reveal(tree, luil::ui_element_id { kind_button, u8"row" }, routes) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"list-down");

    // 품었지만 이미 보이면 빈 목록이다 — 키마다 0짜리 메시지가 나가지 않게 하는
    // 방벽이다. 첫 줄이 품지 않았으므로 건너뛴 것도 함께 본다.
    REQUIRE(luil::route_reveal(tree, luil::ui_element_id { kind_button, u8"seen" }, routes).empty());

    // tree에 없는 초점이면 빈 목록이다.
    REQUIRE(luil::route_reveal(tree, luil::ui_element_id { kind_button, u8"gone" }, routes).empty());

    // 어느 줄도 품지 않으면 빈 목록이다 (창 밖의 자리).
    const luil::scroll_route narrow[] {
        { luil::ui_element_id { kind_card, u8"settled" }, [](const float) { return luil::make_app_action(fake_intent { u8"settled" }); } },
    };
    REQUIRE(luil::route_reveal(tree, luil::ui_element_id { kind_button, u8"row" }, narrow).empty());
}

TEST_CASE("Moving the focus by keyboard asks the policy to reveal it", "[ui][interaction][focus]")
{
    revealing_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());

    // Tab이 초점을 옮기면 그 한 번에 훅이 불린다.
    auto actions { controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }) };
    REQUIRE(policy.calls == 1);
    REQUIRE(policy.last_focused == luil::ui_element_id { kind_button, u8"one" });
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"reveal");

    // 자리가 하나뿐이라 다음 Tab은 같은 자리로 돈다 — 옮겨진 것이 없으므로
    // 묻지 않는다.
    REQUIRE(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(20) }).empty());
    REQUIRE(policy.calls == 1);
}

TEST_CASE("A shrunken surface asks to reveal the focus it holds", "[ui][interaction][focus]")
{
    revealing_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());

    // 초점이 없으면 드러낼 것이 없다.
    REQUIRE(controller.process(luil::focus_reveal_event {}).empty());
    REQUIRE(policy.calls == 0);

    // 초점이 서면 그 초점을 드러내 달라고 묻는다 (옮겨진 것이 아니어도).
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
    REQUIRE(policy.calls == 1);
    const auto actions { controller.process(luil::focus_reveal_event {}) };
    REQUIRE(policy.calls == 2);
    REQUIRE(policy.last_focused == luil::ui_element_id { kind_button, u8"one" });
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"reveal");

    // 다른 표면이 줄어든 것은 이 초점과 상관없다.
    REQUIRE(controller.process(luil::focus_reveal_event { u8"popup" }).empty());
    REQUIRE(policy.calls == 2);
}

TEST_CASE("An access focus request stands the focus and asks to reveal it", "[ui][interaction][focus][access]")
{
    revealing_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());

    // 보조 기술이 청한 초점이다 — 좌표 없이 요소를 이름으로 말하는 유일한 이벤트다.
    auto actions { controller.process(luil::access_focus_event { luil::ui_element_id { kind_button, u8"one" }, {}, at(0) }) };
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });
    // 키보드로 옮긴 것과 같은 부류라 테가 그려지고, 그래서 되살리기도 함께 선다.
    REQUIRE(controller.snapshot().focus_visible);
    REQUIRE(policy.calls == 1);
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"reveal");

    // 자리가 아닌 요소는 세우지 않는다 (root는 누를 수 없어 자리가 아니다).
    REQUIRE(controller.process(luil::access_focus_event { luil::ui_element_id { luil::ui_element_kind::root }, {}, at(20) }).empty());
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });
    // tree에 없는 요소도 마찬가지다.
    REQUIRE(controller.process(luil::access_focus_event { luil::ui_element_id { kind_button, u8"gone" }, {}, at(40) }).empty());
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });
    REQUIRE(policy.calls == 1);
}

TEST_CASE("An access focus request lands in the surface it names", "[ui][interaction][focus][access]")
{
    luil::interaction_controller controller {};
    controller.set_tree(single_button_tree());

    auto popup_root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    popup_root->arrange({ { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    auto item { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"item" }) };
    item->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    item->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    popup_root->add(std::move(item));
    controller.set_surface_trees({ { u8"menu", std::make_shared<const luil::ui_tree>(std::move(popup_root)) } });

    REQUIRE(controller.process(luil::access_focus_event { luil::ui_element_id { kind_button, u8"item" }, u8"menu", at(0) }).empty());
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"item" });
    REQUIRE(controller.snapshot().focused_surface == u8"menu");

    // 모르는 표면이면 아무것도 맞히지 않는다 (다른 이벤트와 같은 규칙).
    static_cast<void>(controller.process(luil::access_focus_event { luil::ui_element_id { kind_button, u8"one" }, u8"gone", at(20) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"item" });
}

TEST_CASE("A focus trap collects an access focus that landed outside it", "[ui][interaction][focus][access]")
{
    luil::interaction_controller controller {};

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto outside { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"outside" }) };
    outside->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    outside->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    root->add(std::move(outside));
    auto dialog { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"dialog" }) };
    dialog->set_focus_trap(true);
    dialog->arrange({ { 0.0f, 50.0f, 200.0f, 100.0f }, 1.0f });
    auto inside { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"inside" }) };
    inside->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    inside->arrange({ { 0.0f, 60.0f, 100.0f, 20.0f }, 1.0f });
    dialog->add(std::move(inside));
    root->add(std::move(dialog));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    // 가둠 안은 그대로 선다.
    static_cast<void>(controller.process(luil::access_focus_event { luil::ui_element_id { kind_button, u8"inside" }, {}, at(0) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"inside" });

    // 가둠 밖으로 청하면 다음 이벤트의 거둠이 되돌린다 — 막는 검사를 경로마다
    // 흩지 않고 한 곳에서 거두는 그 규칙이다 (modal-dialog-design.md).
    // 청하는 쪽(UIA provider)이 닿을 수 없는 자리를 미리 거절한다.
    static_cast<void>(controller.process(luil::access_focus_event { luil::ui_element_id { kind_button, u8"outside" }, {}, at(20) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"outside" });
    static_cast<void>(controller.process(luil::pointer_moved_event { 5.0f, 5.0f, at(40) }));
    // 가둠이 이름 지은 진입이 없으므로 거둔 자리는 "초점 없음"이다 — 그 아래가
    // 전부 이미 옳은 길을 탄다.
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
}

TEST_CASE("A pointer-driven focus asks for no reveal", "[ui][interaction][focus]")
{
    // 누른 자리는 이미 보인다. 마우스와 키보드를 가르는 값은 `focus_visible`이라
    // 새 상태를 세우지 않았다.
    revealing_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(single_button_tree());

    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 15.0f, luil::pointer_button::left, at(0) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_button, u8"one" });
    REQUIRE(controller.snapshot().focus_visible == false);
    REQUIRE(policy.calls == 0);
}

namespace {
    constexpr luil::ui_element_kind kind_value { luil::application_element_kind(6) };

    [[nodiscard]] std::u8string step_name(const luil::value_step step)
    {
        switch (step)
        {
        case luil::value_step::decrease:
            return u8"decrease";
        case luil::value_step::increase:
            return u8"increase";
        case luil::value_step::decrease_page:
            return u8"page-up";
        case luil::value_step::increase_page:
            return u8"page-down";
        case luil::value_step::minimum:
            return u8"minimum";
        default:
            return u8"maximum";
        }
    }

    // 초점을 가진 채 키로 값을 바꾸는 element다 (막대·스크롤 막대의 자리).
    //  - `answers_ends`가 거짓이면 Home/End에 답하지 않는다. 자기 범위를 모르는
    //    손잡이가 그 꼴이고, 그때 키는 그대로 흐른다.
    class step_probe final : public luil::ui_element
    {
    public:
        step_probe(const luil::ui_element_id id, const luil::focus_axis axis, const bool answers_ends)
            : ui_element { id }
        {
            set_tab_stop(true);
            luil::key_step_target steps {};
            steps.axis = axis;
            steps.on_step = [answers_ends](const luil::value_step step) -> std::optional<std::vector<luil::input_action>> {
                if (answers_ends == false && (step == luil::value_step::minimum || step == luil::value_step::maximum))
                    return std::nullopt;
                return std::vector<luil::input_action> { luil::make_app_action(fake_intent { step_name(step) }) };
            };
            set_key_step_target(std::move(steps));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };
} // namespace

TEST_CASE("Keys on a focused value control change the value and leave the focus in place", "[ui][interaction][focus]")
{
    // 묶음이 항목 **사이로** 초점을 옮기는 것과 갈리는 자리다 — 값은 초점이 선
    // 자리에서 바뀐다.
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto value { std::make_unique<step_probe>(luil::ui_element_id { kind_value, u8"volume" }, luil::focus_axis::horizontal, true) };
    value->arrange({ { 0.0f, 0.0f, 200.0f, 20.0f }, 1.0f });
    root->add(std::move(value));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_value, u8"volume" });

    const auto press = [&controller](const luil::key_code key, const int when) { return controller.process(luil::key_pressed_event { key, false, false, false, false, at(when) }); };

    auto actions { press(luil::key_code::arrow_right, 20) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"increase");
    // 초점은 제자리다.
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_value, u8"volume" });

    actions = press(luil::key_code::page_down, 40);
    REQUIRE(intent_of(actions[0])->name == u8"page-down");
    actions = press(luil::key_code::home, 60);
    REQUIRE(intent_of(actions[0])->name == u8"minimum");

    // 축이 맞지 않는 화살표는 이 element의 것이 아니라 앱 정책으로 흐른다.
    policy.last_key = luil::key_code::none;
    actions = press(luil::key_code::arrow_down, 80);
    REQUIRE(policy.last_key == luil::key_code::arrow_down);
    REQUIRE(intent_of(actions[0])->name == u8"key");

    // 수정자와 함께라면 앱 단축키다 (묶음과 같은 규칙).
    policy.last_key = luil::key_code::none;
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::arrow_right, true, false, false, false, at(100) }));
    REQUIRE(policy.last_key == luil::key_code::arrow_right);
}

TEST_CASE("A value control inside a group keeps the keys it answers", "[ui][interaction][focus]")
{
    // **묶음보다 앞이다.** 묶음은 축이 서 있기만 하면 Home/End를 자기 것으로
    // 보므로, 뒤에 두면 목록 안에 놓인 막대가 그 둘을 통째로 빼앗긴다.
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto group { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"mixer" }) };
    group->arrange({ { 0.0f, 0.0f, 200.0f, 80.0f }, 1.0f });
    group->set_focus_group(luil::focus_axis::vertical);
    // 손잡이처럼 Home/End에 답하지 않는 것 하나와, 답하는 것 하나를 나란히 둔다.
    auto first { std::make_unique<step_probe>(luil::ui_element_id { kind_value, u8"first" }, luil::focus_axis::horizontal, true) };
    first->arrange({ { 0.0f, 0.0f, 200.0f, 20.0f }, 1.0f });
    group->add(std::move(first));
    auto second { std::make_unique<step_probe>(luil::ui_element_id { kind_value, u8"second" }, luil::focus_axis::horizontal, true) };
    second->arrange({ { 0.0f, 30.0f, 200.0f, 20.0f }, 1.0f });
    group->add(std::move(second));
    root->add(std::move(group));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    // 묶음은 Tab의 한 자리다 — 첫 항목에 선다.
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_value, u8"first" });

    // 묶음의 축(세로)에 맞는 화살표는 묶음의 것이다 — 초점이 옮겨진다.
    REQUIRE(controller.process(luil::key_pressed_event { luil::key_code::arrow_down, false, false, false, false, at(20) }).empty());
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_value, u8"second" });

    // Home은 **값의 것**이다. 묶음이 가져가면 초점이 첫 항목으로 뛴다.
    auto actions { controller.process(luil::key_pressed_event { luil::key_code::home, false, false, false, false, at(40) }) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(intent_of(actions[0])->name == u8"minimum");
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_value, u8"second" });

    // 값이 답하지 않는 키는 묶음으로 이어진다 — 가로 화살표는 이 값의 것이다.
    actions = controller.process(luil::key_pressed_event { luil::key_code::arrow_left, false, false, false, false, at(60) });
    REQUIRE(intent_of(actions[0])->name == u8"decrease");
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_value, u8"second" });
}

TEST_CASE("Cancelling dropped gestures clears the press and the text drag", "[ui][interaction]")
{
    recording_policy policy {};
    luil::interaction_controller controller { &policy };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto input { std::make_unique<test_text_input>(luil::ui_element_id { kind_query_input }, u8"abcdef", 2u, 2u) };
    input->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    root->add(std::move(input));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    // 글 칸을 누르면 텍스트 끌기가 장전되고, 이동이 선택을 넓힌다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 30.0f, 10.0f, luil::pointer_button::left, at(0) }));
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id { kind_query_input });
    static_cast<void>(controller.process(luil::pointer_moved_event { 50.0f, 10.0f, at(20) }));
    const int edits_before_cancel { policy.edit_count };
    REQUIRE(edits_before_cancel >= 2);

    // 뗌이 유실된 것으로 판정되면 몸짓이 거둬진다.
    controller.cancel_dropped_gestures();
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});

    // 거둔 뒤의 이동은 더 이상 유령 선택 확장을 만들지 않는다.
    static_cast<void>(controller.process(luil::pointer_moved_event { 70.0f, 10.0f, at(40) }));
    REQUIRE(policy.edit_count == edits_before_cancel);
}

TEST_CASE("A queued quick tap is not promoted to a long press by the wall clock", "[ui][interaction][pump]")
{
    messaging::channel<luil::raw_input_event> input_inbox { messaging::channel_options { 8, messaging::overflow_policy::drop_oldest, {} } };
    messaging::channel<luil::app_message> app_inbox { messaging::channel_options { 8, messaging::overflow_policy::reject_newest, {} } };
    messaging::latest_slot<std::shared_ptr<const luil::ui_tree>> tree_slot {};
    messaging::latest_slot<luil::surface_tree_list> surface_tree_slot {};
    messaging::latest_slot<luil::interaction_snapshot> interaction_slot {};
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"one" }) };
    button->arrange({ { 10.0f, 10.0f, 40.0f, 20.0f }, 1.0f });
    for (const auto trigger : { luil::ui_trigger::left_click, luil::ui_trigger::right_click })
        button->set_action(trigger, [trigger](const luil::ui_action_context&) -> std::vector<luil::input_action> {
            return { luil::make_app_action(fake_intent { trigger == luil::ui_trigger::left_click ? u8"tap" : u8"long" }) };
        });
    root->add(std::move(button));
    static_cast<void>(tree_slot.publish(std::make_shared<const luil::ui_tree>(std::move(root))));
    const auto time { std::chrono::steady_clock::now() - 2s };
    luil::pointer_pressed_event down { 20.0f, 15.0f, luil::pointer_button::left, time };
    down.device = luil::pointer_device::touch;
    luil::pointer_released_event up { 20.0f, 15.0f, luil::pointer_button::left, time + 50ms };
    up.device = luil::pointer_device::touch;
    REQUIRE(input_inbox.post(down) == messaging::post_result::posted);
    REQUIRE(input_inbox.post(up) == messaging::post_result::posted);
    input_inbox.close();
    luil::run_ui_input_pump(input_inbox, tree_slot, surface_tree_slot, app_inbox, interaction_slot, {});
    messaging::envelope<luil::app_message> received {};
    REQUIRE(app_inbox.try_receive(received) == messaging::receive_status::received);
    REQUIRE(received.payload.get<fake_intent>()->name == u8"tap");
    REQUIRE(app_inbox.try_receive(received) != messaging::receive_status::received);
}

TEST_CASE("A queued move after the long press deadline does not swallow the menu", "[ui][interaction][pump]")
{
    messaging::channel<luil::raw_input_event> input_inbox { messaging::channel_options { 8, messaging::overflow_policy::drop_oldest, {} } };
    messaging::channel<luil::app_message> app_inbox { messaging::channel_options { 8, messaging::overflow_policy::reject_newest, {} } };
    messaging::latest_slot<std::shared_ptr<const luil::ui_tree>> tree_slot {};
    messaging::latest_slot<luil::surface_tree_list> surface_tree_slot {};
    messaging::latest_slot<luil::interaction_snapshot> interaction_slot {};
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"one" }) };
    button->arrange({ { 10.0f, 10.0f, 40.0f, 20.0f }, 1.0f });
    for (const auto trigger : { luil::ui_trigger::left_click, luil::ui_trigger::right_click })
        button->set_action(trigger, [trigger](const luil::ui_action_context&) -> std::vector<luil::input_action> {
            return { luil::make_app_action(fake_intent { trigger == luil::ui_trigger::left_click ? u8"tap" : u8"long" }) };
        });
    root->add(std::move(button));
    static_cast<void>(tree_slot.publish(std::make_shared<const luil::ui_tree>(std::move(root))));
    // 손가락은 기한 안에 움직이지 않았다. 이동은 기한이 지난 뒤에야 큐에서 나온다.
    const auto time { std::chrono::steady_clock::now() - 2s };
    luil::pointer_pressed_event down { 20.0f, 15.0f, luil::pointer_button::left, time };
    down.device = luil::pointer_device::touch;
    luil::pointer_moved_event moved { 120.0f, 15.0f, time + 650ms };
    moved.device = luil::pointer_device::touch;
    moved.in_contact = true;
    luil::pointer_released_event up { 120.0f, 15.0f, luil::pointer_button::left, time + 700ms };
    up.device = luil::pointer_device::touch;
    REQUIRE(input_inbox.post(down) == messaging::post_result::posted);
    REQUIRE(input_inbox.post(moved) == messaging::post_result::posted);
    REQUIRE(input_inbox.post(up) == messaging::post_result::posted);
    input_inbox.close();
    luil::run_ui_input_pump(input_inbox, tree_slot, surface_tree_slot, app_inbox, interaction_slot, {});
    messaging::envelope<luil::app_message> received {};
    REQUIRE(app_inbox.try_receive(received) == messaging::receive_status::received);
    REQUIRE(received.payload.get<fake_intent>()->name == u8"long");
    REQUIRE(app_inbox.try_receive(received) != messaging::receive_status::received);
}

TEST_CASE("A sequence gap in the raw input queue cancels the in-flight press", "[ui][interaction][pump]")
{
    // press는 소비됐고 release가 drop_oldest에 잘려 나간 상황을 결정적으로 만든다:
    // 정책의 app_message가 가득 찬 app inbox에 막혀 pump가 재시도 대기에 들어간
    // 사이에 raw 큐를 넘치게 한다.
    messaging::channel<luil::raw_input_event> input_inbox { messaging::channel_options { 4, messaging::overflow_policy::drop_oldest, {} } };
    messaging::channel<luil::app_message> app_inbox { messaging::channel_options { 1, messaging::overflow_policy::reject_newest, {} } };
    messaging::latest_slot<std::shared_ptr<const luil::ui_tree>> tree_slot {};
    messaging::latest_slot<luil::surface_tree_list> surface_tree_slot {};
    messaging::latest_slot<luil::interaction_snapshot> interaction_slot {};

    recording_policy policy {};
    REQUIRE(tree_slot.publish(single_button_tree()) == 1u);

    const auto pump_main = [&] { luil::run_ui_input_pump(input_inbox, tree_slot, surface_tree_slot, app_inbox, interaction_slot, {}, &policy); };
    std::thread pump { pump_main };

    const auto wait_until = [](const auto& condition) {
        const auto deadline { std::chrono::steady_clock::now() + 3s };
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (condition())
                return true;
            std::this_thread::sleep_for(1ms);
        }
        return condition();
    };
    std::uint64_t seen_version { 0 };
    luil::interaction_snapshot last_snapshot {};
    const auto take_snapshot = [&] {
        if (const auto newer { interaction_slot.take_newer(seen_version) }; newer.has_value())
        {
            seen_version = newer->version;
            last_snapshot = newer->value;
        }
    };

    // 1. 누름이 소비되어 pressed가 게시된다.
    REQUIRE(input_inbox.post(luil::pointer_pressed_event { 20.0f, 15.0f, luil::pointer_button::left, at(0) }) == messaging::post_result::posted);
    REQUIRE(wait_until([&] {
        take_snapshot();
        return last_snapshot.pressed == luil::ui_element_id { kind_button, u8"one" };
    }));

    // 2. app inbox를 미리 채운 뒤 정책이 app_message를 내는 키를 넣는다.
    //    pump는 post_with_retry의 재시도 대기에 붙잡힌다 — rejected가 그 증거다.
    REQUIRE(app_inbox.post(luil::app_message { fake_intent { u8"plug" } }) == messaging::post_result::posted);
    REQUIRE(input_inbox.post(luil::key_pressed_event { luil::key_code::f5 }) == messaging::post_result::posted);
    REQUIRE(wait_until([&] { return app_inbox.statistics().rejected >= 1; }));

    // 3. 붙잡힌 사이 raw 큐를 넘치게 한다 — 뗌이 가장 오래된 것으로 버려진다.
    REQUIRE(input_inbox.post(luil::pointer_released_event { 20.0f, 15.0f, luil::pointer_button::left, at(50) }) == messaging::post_result::posted);
    messaging::post_result flooded { messaging::post_result::posted };
    for (int index { 0 }; index < 4; ++index)
        flooded = input_inbox.post(luil::pointer_moved_event { 21.0f + static_cast<float>(index), 15.0f, at(60 + index) });
    REQUIRE(flooded == messaging::post_result::posted_after_drop);

    // 4. 마개를 빼면 pump가 풀리고, 접수 번호의 건너뜀이 몸짓을 거둔다.
    messaging::envelope<luil::app_message> unplugged {};
    REQUIRE(app_inbox.try_receive(unplugged) == messaging::receive_status::received);
    REQUIRE(wait_until([&] {
        take_snapshot();
        return last_snapshot.pressed == luil::ui_element_id {};
    }));

    input_inbox.close();
    pump.join();
}

TEST_CASE("A tooltip clock starts only when the pointer really moves onto an element", "[ui][interaction][hover]")
{
    // 같은 자리에 다른 버튼이 선 tree다. 클릭이나 스크롤 뒤 배치가 바뀌어 포인터 밑의 element가 바뀐 것과 같다.
    const auto other_button_tree = [] {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"two" }) };
        button->arrange({ { 10.0f, 10.0f, 40.0f, 20.0f }, 1.0f });
        button->set_tooltip(u8"two");
        root->add(std::move(button));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    };
    luil::interaction_controller controller {};
    controller.set_tree(single_button_tree());
    static_cast<void>(controller.process(luil::pointer_moved_event { 20.0f, 15.0f, at(100) }));
    REQUIRE(controller.snapshot().hover_started_at == at(100));

    // 포인터가 머문 채 밑의 element가 바뀌면 hover만 옮기고 시계는 세우지 않는다.
    // 옛 이동 시각으로 세우면 지연이 이미 지나 가리키지 않은 tooltip이 곧바로 선다.
    controller.set_tree(other_button_tree());
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id { kind_button, u8"two" });
    REQUIRE(controller.snapshot().hover_started_at.has_value() == false);

    // 같은 자리로 되풀이된 이동 메시지는 움직임이 아니다.
    static_cast<void>(controller.process(luil::pointer_moved_event { 20.0f, 15.0f, at(5000) }));
    REQUIRE(controller.snapshot().hover_started_at.has_value() == false);

    // 그 위에서 실제로 움직이면 그때부터 잰다.
    static_cast<void>(controller.process(luil::pointer_moved_event { 22.0f, 15.0f, at(6000) }));
    REQUIRE(controller.snapshot().hover_started_at == at(6000));

    // 같은 대상 안의 이동은 지연을 다시 세우지 않는다. 떠났다가 같은 자리로 들어오면 새 지연이다.
    static_cast<void>(controller.process(luil::pointer_moved_event { 23.0f, 15.0f, at(7000) }));
    REQUIRE(controller.snapshot().hover_started_at == at(6000));
    static_cast<void>(controller.process(luil::pointer_left_event {}));
    REQUIRE(controller.snapshot().hover_started_at.has_value() == false);
    static_cast<void>(controller.process(luil::pointer_moved_event { 23.0f, 15.0f, at(8000) }));
    REQUIRE(controller.snapshot().hover_started_at == at(8000));

    // 좌표가 같아도 표면을 옮기면 새 진입이다. 보조 표면의 tree 교체도 주 창과 같은 규칙이다.
    controller.set_surface_trees({ { u8"floating", single_button_tree() } });
    static_cast<void>(controller.process(luil::pointer_moved_event { .x = 23.0f, .y = 15.0f, .time = at(9000), .surface = u8"floating" }));
    REQUIRE(controller.snapshot().hover_started_at == at(9000));
    controller.set_surface_trees({ { u8"floating", other_button_tree() } });
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id { kind_button, u8"two" });
    REQUIRE(controller.snapshot().hovered_surface == u8"floating");
    REQUIRE(controller.snapshot().hover_started_at.has_value() == false);
    static_cast<void>(controller.process(luil::pointer_moved_event { .x = 23.0f, .y = 15.0f, .time = at(10000), .surface = u8"floating", .device = luil::pointer_device::pen }));
    REQUIRE(controller.snapshot().hover_started_at.has_value() == false);
    static_cast<void>(controller.process(luil::pointer_moved_event { .x = 24.0f, .y = 15.0f, .time = at(11000), .surface = u8"floating", .device = luil::pointer_device::pen }));
    REQUIRE(controller.snapshot().hover_started_at == at(11000));
}
