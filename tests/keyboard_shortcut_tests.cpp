#include "host/key_mapping.h"

#include "luil/ui/app_message.h"
#include "luil/ui/commands.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using luil::key_chord;
    using luil::key_code;

    constexpr luil::ui_element_kind kind_capture { luil::application_element_kind(1) };
    constexpr luil::ui_element_kind kind_button { luil::application_element_kind(2) };
    constexpr luil::ui_element_kind kind_input { luil::application_element_kind(3) };
    constexpr luil::text_input_target input_target { 4 };

    struct captured_intent
    {
        key_chord chord {};
    };

    struct policy_intent
    {};

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

    class test_text_input final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}

        [[nodiscard]] std::optional<std::size_t> offset_at(const float, const luil::text_measurer&) const override
        {
            return 0u;
        }

        [[nodiscard]] std::optional<luil::text_input_snapshot> text_input() const override
        {
            return luil::text_input_snapshot { u8"abc", 3u, 3u };
        }
    };

    class counting_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            return kind == kind_input ? std::optional<luil::text_input_target> { input_target } : std::nullopt;
        }

        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request&) const override
        {
            ++edits;
            return luil::input_action { luil::app_message { policy_intent {} } };
        }

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            ++keys;
            return { luil::input_action { luil::app_message { policy_intent {} } } };
        }

        mutable int edits { 0 };
        int keys { 0 };
    };

    [[nodiscard]] luil::ui_action no_op_action()
    {
        return [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
    }

    // root ⊃ 키 잡기 칸 · 단추 · 텍스트 칸. 키 잡기 칸이 Tab 차례의 처음이다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_capture_tree()
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 300.0f, 200.0f }, 1.0f });
        auto capture { std::make_unique<test_panel>(luil::ui_element_id { kind_capture, u8"capture" }) };
        capture->arrange({ { 0.0f, 0.0f, 200.0f, 28.0f }, 1.0f });
        capture->set_tab_stop(true);
        capture->set_key_capture(luil::key_capture_target {
            .on_key
            = [](const luil::key_pressed_event& event) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_message { captured_intent { luil::chord_of(event) } } } }; },
        });
        auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"button" }) };
        button->arrange({ { 0.0f, 40.0f, 80.0f, 28.0f }, 1.0f });
        button->set_action(luil::ui_trigger::left_click, no_op_action());
        auto input { std::make_unique<test_text_input>(luil::ui_element_id { kind_input, u8"input" }) };
        input->arrange({ { 0.0f, 80.0f, 200.0f, 28.0f }, 1.0f });
        input->set_action(luil::ui_trigger::left_click, no_op_action());
        root->add(std::move(capture));
        root->add(std::move(button));
        root->add(std::move(input));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    [[nodiscard]] std::optional<key_chord> captured_chord(const std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1)
            return std::nullopt;
        const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
        const auto* const intent { message != nullptr ? message->get<captured_intent>() : nullptr };
        return intent != nullptr ? std::optional<key_chord> { intent->chord } : std::nullopt;
    }

    [[nodiscard]] luil::key_pressed_event key(const key_code code, const bool control = false, const bool shift = false, const bool alt = false)
    {
        return luil::key_pressed_event { code, control, shift, alt };
    }
} // namespace

TEST_CASE("Every named key survives a label and parse round trip", "[shortcut]")
{
    // 라벨과 해석이 한 표를 쓴다. 표의 모든 키와 수정자 조합에서 왕복이 맞는지 본다.
    std::vector<key_code> keys {};
    for (std::uint32_t value { static_cast<std::uint32_t>(key_code::arrow_up) }; value < static_cast<std::uint32_t>(key_code::first_platform_key); ++value)
    {
        const auto code { static_cast<key_code>(value) };
        if (luil::shortcut_label(key_chord { code }).empty() == false)
            keys.push_back(code);
    }
    // 영문자 26 · 숫자 10 · F 키 12 · 탐색·편집 15 · 구두점 11 · 키패드 15다.
    REQUIRE(keys.size() == 89u);
    for (const key_code code : keys)
        for (int modifiers { 0 }; modifiers < 8; ++modifiers)
        {
            const key_chord chord { code, (modifiers & 1) != 0, (modifiers & 2) != 0, (modifiers & 4) != 0 };
            const std::u8string label { luil::shortcut_label(chord) };
            CAPTURE(std::string { label.begin(), label.end() });
            REQUIRE(luil::parse_shortcut(label) == chord);
        }
}

TEST_CASE("Shortcut text parses punctuation, keypad and aliases", "[shortcut]")
{
    REQUIRE(luil::parse_shortcut(u8"Ctrl+Shift+P") == key_chord { key_code::key_p, true, true });
    REQUIRE(luil::parse_shortcut(u8"ctrl+-") == key_chord { key_code::minus, true });
    REQUIRE(luil::parse_shortcut(u8"Ctrl+=") == key_chord { key_code::equal, true });
    REQUIRE(luil::parse_shortcut(u8"Alt+Num+") == key_chord { key_code::numpad_add, false, false, true });
    REQUIRE(luil::parse_shortcut(u8"Num-") == key_chord { key_code::numpad_subtract });
    REQUIRE(luil::parse_shortcut(u8"Control+Escape") == key_chord { key_code::escape, true });
    REQUIRE(luil::parse_shortcut(u8"  Shift+Del ") == key_chord { key_code::delete_forward, false, true });
    REQUIRE(luil::parse_shortcut(u8"Ctrl+\\") == key_chord { key_code::backslash, true });
    REQUIRE(luil::parse_shortcut(u8"Backspace") == key_chord { key_code::backspace });
    // 모르는 이름·키 없는 글·같은 수정자를 두 번 쓴 글은 없다.
    REQUIRE(luil::parse_shortcut(u8"Ctrl+Hyper").has_value() == false);
    REQUIRE(luil::parse_shortcut(u8"Ctrl+").has_value() == false);
    REQUIRE(luil::parse_shortcut(u8"Ctrl+Ctrl+A").has_value() == false);
    REQUIRE(luil::parse_shortcut(u8"Ctrl++").has_value() == false);
    REQUIRE(luil::parse_shortcut(u8"").has_value() == false);
    // 라벨은 언제나 Ctrl·Shift·Alt 차례다. 해석은 차례를 가리지 않는다.
    REQUIRE(luil::parse_shortcut(u8"Alt+Shift+Ctrl+K") == key_chord { key_code::key_k, true, true, true });
    REQUIRE(luil::shortcut_label({ key_code::key_k, true, true, true }) == u8"Ctrl+Shift+Alt+K");
}

TEST_CASE("Both platforms map punctuation and keypad keys to the same names", "[shortcut]")
{
    // Win32 VK_*와 Android AKEYCODE_* 값이다. 두 표가 같은 이름으로 모인다.
    struct pair_case
    {
        std::uint32_t win32 { 0 };
        std::int32_t android { 0 };
        key_code key { key_code::none };
    };
    const pair_case cases[] {
        { 0x08, 67, key_code::backspace },
        { 0xBD, 69, key_code::minus },
        { 0xBB, 70, key_code::equal },
        { 0xBC, 55, key_code::comma },
        { 0xBE, 56, key_code::period },
        { 0xBF, 76, key_code::slash },
        { 0xBA, 74, key_code::semicolon },
        { 0xDE, 75, key_code::quote },
        { 0xDB, 71, key_code::bracket_left },
        { 0xDD, 72, key_code::bracket_right },
        { 0xDC, 73, key_code::backslash },
        { 0xC0, 68, key_code::grave },
        { 0x60, 144, key_code::numpad_0 },
        { 0x65, 149, key_code::numpad_5 },
        { 0x69, 153, key_code::numpad_9 },
        { 0x6B, 157, key_code::numpad_add },
        { 0x6D, 156, key_code::numpad_subtract },
        { 0x6A, 155, key_code::numpad_multiply },
        { 0x6F, 154, key_code::numpad_divide },
        { 0x6E, 158, key_code::numpad_decimal },
    };
    for (const pair_case& value : cases)
    {
        REQUIRE(luil::named_key_from_win32_virtual(value.win32) == value.key);
        REQUIRE(luil::named_key_from_android(value.android) == value.key);
    }
    // 이름이 없는 키는 none이다 (Pause·SysRq).
    REQUIRE(luil::named_key_from_win32_virtual(0x13) == key_code::none);
    REQUIRE(luil::named_key_from_android(120) == key_code::none);
}

TEST_CASE("A command answers to every effective shortcut", "[shortcut][commands]")
{
    luil::app_command redo { u8"redo", u8"다시 실행", { key_code::key_y, true } };
    redo.extra_shortcuts = { { key_code::key_z, true, true }, { key_code::key_y, true } };
    // 겹친 단축키는 한 번만 센다.
    REQUIRE(luil::effective_shortcuts(redo).size() == 2u);
    const std::vector<luil::app_command> commands { redo };
    REQUIRE(luil::match_command(commands, key(key_code::key_z, true, true), luil::command_scope::window) == u8"redo");
    REQUIRE(luil::match_command(commands, key(key_code::key_y, true), luil::command_scope::window) == u8"redo");

    // 사용자가 다시 정하면 그것만 쓴다. 빈 목록은 단축키가 없다는 뜻이다.
    luil::app_command overridden { redo };
    overridden.user_shortcuts = std::vector<key_chord> { { key_code::f9 } };
    const std::vector<luil::app_command> custom { overridden };
    REQUIRE(luil::match_command(custom, key(key_code::key_y, true), luil::command_scope::window).has_value() == false);
    REQUIRE(luil::match_command(custom, key(key_code::f9), luil::command_scope::window) == u8"redo");
    REQUIRE(luil::menu_item_from(overridden).shortcut_text == u8"F9");
    overridden.user_shortcuts = std::vector<key_chord> {};
    REQUIRE(luil::effective_shortcuts(overridden).empty());
    REQUIRE(luil::menu_item_from(overridden).shortcut_text.empty());
}

TEST_CASE("Conflicts name the shared chord across extra and user shortcuts", "[shortcut][commands]")
{
    luil::app_command close { u8"close", u8"닫기", { key_code::key_w, true } };
    luil::app_command quit { u8"quit", u8"끝내기", { key_code::key_q, true } };
    quit.extra_shortcuts = { { key_code::key_w, true } };
    luil::app_command other_scope { u8"panel", u8"판 닫기", { key_code::key_w, true }, luil::command_scope::focused };
    const std::vector<luil::app_command> commands { close, quit, other_scope };
    const std::vector<luil::chord_conflict> conflicts { luil::find_conflicts(commands) };
    REQUIRE(conflicts.size() == 1u);
    REQUIRE(conflicts[0].first == u8"close");
    REQUIRE(conflicts[0].second == u8"quit");
    REQUIRE(conflicts[0].chord == key_chord { key_code::key_w, true });

    // 사용자가 끝내기를 다시 정하면 충돌이 사라진다.
    quit.user_shortcuts = std::vector<key_chord> { { key_code::key_q, true } };
    const std::vector<luil::app_command> fixed { close, quit, other_scope };
    REQUIRE(luil::find_conflicts(fixed).empty());
}

TEST_CASE("Tooltips carry the shortcut in parentheses", "[shortcut][commands]")
{
    REQUIRE(luil::label_with_shortcut(u8"닫기", { key_code::key_w, true }) == u8"닫기 (Ctrl+W)");
    REQUIRE(luil::label_with_shortcut(u8"닫기", {}) == u8"닫기");
    luil::app_command zoom { u8"zoom-in", u8"확대", { key_code::equal, true } };
    REQUIRE(luil::command_tooltip(zoom) == u8"확대 (Ctrl+=)");
}

TEST_CASE("System reserved chords are recognised", "[shortcut]")
{
    REQUIRE(luil::system_reserved_chord({ key_code::f4, false, false, true }));
    REQUIRE(luil::system_reserved_chord({ key_code::tab, false, false, true }));
    REQUIRE(luil::system_reserved_chord({ key_code::space, false, false, true }));
    REQUIRE(luil::system_reserved_chord({ key_code::escape, true }));
    REQUIRE(luil::system_reserved_chord({ key_code::delete_forward, true, false, true }));
    REQUIRE(luil::system_reserved_chord({ key_code::f4, true }) == false);
    REQUIRE(luil::system_reserved_chord({ key_code::key_w, true }) == false);
}

TEST_CASE("A capturing element receives Tab, Esc and shortcuts before anything else", "[shortcut][capture]")
{
    counting_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(make_capture_tree());
    // Tab으로 키 잡기 칸에 초점을 준다. 아직 잡기 전이라 Tab은 초점을 옮긴다.
    static_cast<void>(controller.process(key(key_code::tab)));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_capture, u8"capture" });
    REQUIRE(controller.snapshot().key_capture);

    // 이제 Tab도 잡는 칸의 것이다 — 초점이 옮지 않는다.
    REQUIRE(captured_chord(controller.process(key(key_code::tab))) == key_chord { key_code::tab });
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_capture, u8"capture" });
    REQUIRE(captured_chord(controller.process(key(key_code::escape))) == key_chord { key_code::escape });
    REQUIRE(captured_chord(controller.process(key(key_code::key_p, true, true))) == key_chord { key_code::key_p, true, true });
    REQUIRE(captured_chord(controller.process(key(key_code::f4, false, false, true))) == key_chord { key_code::f4, false, false, true });
    // 앱 단축키(정책)는 실행되지 않는다.
    REQUIRE(policy.keys == 0);
    // 잡는 동안의 글자는 버린다.
    REQUIRE(controller.process(luil::character_typed_event { U'p' }).empty());
    REQUIRE(policy.edits == 0);
}

TEST_CASE("Leaving the capturing element ends the capture", "[shortcut][capture]")
{
    counting_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(make_capture_tree());
    // 단추를 누르면 초점이 단추로 가고 잡기가 끝난다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 50.0f, luil::pointer_button::left }));
    static_cast<void>(controller.process(luil::pointer_released_event { 10.0f, 50.0f, luil::pointer_button::left }));
    REQUIRE(controller.snapshot().key_capture == false);
    static_cast<void>(controller.process(key(key_code::key_p, true, true)));
    REQUIRE(policy.keys == 1);
}

TEST_CASE("Backspace in a text field edits through the character and never reaches app shortcuts", "[shortcut][text]")
{
    counting_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(make_capture_tree());
    static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 90.0f, luil::pointer_button::left }));
    static_cast<void>(controller.process(luil::pointer_released_event { 10.0f, 90.0f, luil::pointer_button::left }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_input, u8"input" });
    // 누른 자리로 caret을 옮긴 편집은 세지 않는다.
    policy.edits = 0;

    // 키 쪽은 칸에서 끝난다. 앱의 "뒤로" 단축키로 새지 않는다.
    REQUIRE(controller.process(key(key_code::backspace)).empty());
    REQUIRE(controller.process(key(key_code::backspace, true)).empty());
    REQUIRE(policy.keys == 0);
    // 지우기는 글자 쪽이 한다.
    static_cast<void>(controller.process(luil::character_typed_event { U'\b' }));
    REQUIRE(policy.edits == 1);
}
