#include "luil/ui/dialog_elements.h"
#include "luil/ui/markdown_element.h"
#include "luil/ui/text_area_element.h"
#include "luil/ui/text_document.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_memo { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_line { luil::application_element_kind(1) };
    constexpr luil::text_input_target memo_target { 7 };
    constexpr luil::text_input_target line_target { 8 };

    struct edit_intent
    {
        luil::text_edit_request request {};
    };

    struct open_intent
    {
        std::u8string destination {};
    };

    // 글자 하나가 10 픽셀인 측정기다.
    [[nodiscard]] float fixed_width(const std::u8string_view text, float)
    {
        float width { 0.0f };
        for (const char8_t unit : text)
            if ((static_cast<unsigned int>(unit) & 0xC0u) != 0x80u)
                width += 10.0f;
        return width;
    }

    class link_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            if (kind == kind_memo)
                return memo_target;
            if (kind == kind_line)
                return line_target;
            return std::nullopt;
        }

        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request& request) const override
        {
            return luil::input_action { luil::app_message { edit_intent { request } } };
        }
    };

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] luil::text_input_view view_of(std::u8string text, const std::size_t caret)
    {
        luil::text_input_view view {};
        view.text = std::move(text);
        view.caret = caret;
        view.anchor = caret;
        return view;
    }

    // "see a.b and c.d"의 두 주소다. 글자 하나가 10 픽셀이고 글은 x = 7에서 시작한다.
    //  - 첫 링크 [4, 7)은 x 47..77, 둘째 [12, 15)는 x 127..157이다.
    [[nodiscard]] luil::text_area_config linked_config(const bool read_only)
    {
        luil::text_area_config config {};
        config.read_only = read_only;
        config.links = std::make_shared<const std::vector<luil::text_link>>(std::vector<luil::text_link> { { 4, 7, u8"https://a.b" }, { 12, 15, u8"https://c.d" } });
        config.activate_link = [](const std::u8string_view destination) { return luil::input_action { luil::app_message { open_intent { std::u8string { destination } } } }; };
        config.link_hint = u8"Ctrl+클릭하여 열기";
        return config;
    }

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> linked_tree(luil::text_input_view view, luil::text_area_config config)
    {
        auto memo { std::make_unique<luil::text_area_element>(luil::ui_element_id { kind_memo, u8"memo" }, std::move(view), std::move(config)) };
        const luil::text_measurer measurer { fixed_width };
        memo->arrange({ .slot = { 0.0f, 0.0f, 200.0f, 100.0f }, .scale = 1.0f, .measurer = &measurer });
        return std::make_shared<const luil::ui_tree>(std::move(memo));
    }

    [[nodiscard]] luil::interaction_config fixed_config()
    {
        luil::interaction_config config {};
        config.measure_text = fixed_width;
        return config;
    }

    [[nodiscard]] std::optional<std::u8string> opened(const std::vector<luil::input_action>& actions)
    {
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const open { message->get<open_intent>() }; open != nullptr)
                    return open->destination;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<luil::text_edit_request> edit_in(const std::vector<luil::input_action>& actions)
    {
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const edit { message->get<edit_intent>() }; edit != nullptr)
                    return edit->request;
        return std::nullopt;
    }

    [[nodiscard]] luil::pointer_pressed_event press(const float x, const float y, const int milliseconds, const bool control = false)
    {
        luil::pointer_pressed_event event { x, y, luil::pointer_button::left, at(milliseconds) };
        event.control = control;
        return event;
    }

    [[nodiscard]] luil::pointer_released_event release(const float x, const float y, const int milliseconds, const bool control = false)
    {
        luil::pointer_released_event event { x, y, luil::pointer_button::left, at(milliseconds) };
        event.control = control;
        return event;
    }

    constexpr float first_row { 5.0f + 9.0f };
}

TEST_CASE("Address detection finds web, www and mail links and trims sentence punctuation", "[ui][document][link]")
{
    const auto found = [](const std::u8string_view text) {
        std::vector<std::u8string> result {};
        for (const luil::text_link& link : luil::detect_text_links(text))
            result.push_back(std::u8string { text.substr(link.begin, link.end - link.begin) } + u8" -> " + link.destination);
        return result;
    };
    REQUIRE(found(u8"See https://example.com/docs.") == std::vector<std::u8string> { u8"https://example.com/docs -> https://example.com/docs" });
    REQUIRE(found(u8"HTTP://A.B/x?y=1&z=2, then") == std::vector<std::u8string> { u8"HTTP://A.B/x?y=1&z=2 -> HTTP://A.B/x?y=1&z=2" });
    REQUIRE(found(u8"go www.lusain.dev!") == std::vector<std::u8string> { u8"www.lusain.dev -> https://www.lusain.dev" });
    REQUIRE(found(u8"mail me@lusain.dev or mailto:a@b.co") == std::vector<std::u8string> { u8"me@lusain.dev -> mailto:me@lusain.dev", u8"mailto:a@b.co -> mailto:a@b.co" });
    // 한국어 조사와 괄호. 주소 안에 짝이 있는 괄호는 남는다.
    REQUIRE(found(u8"주소는 https://example.com에서 본다") == std::vector<std::u8string> { u8"https://example.com -> https://example.com" });
    REQUIRE(found(u8"(https://a.b/x_(y)) 끝") == std::vector<std::u8string> { u8"https://a.b/x_(y) -> https://a.b/x_(y)" });
    REQUIRE(found(u8"[see https://a.b/c]") == std::vector<std::u8string> { u8"https://a.b/c -> https://a.b/c" });
    // 낱말이나 다른 주소의 한가운데에서는 시작하지 않는다. 주소가 아닌 것도 링크가 아니다.
    REQUIRE(found(u8"xhttp://a.b a.www.b.c https:// www. user@host @x.co a@b.c1").empty());
    // `https://user@host`의 @는 메일 주소가 아니다.
    REQUIRE(found(u8"https://user@host.dev/p") == std::vector<std::u8string> { u8"https://user@host.dev/p -> https://user@host.dev/p" });
    REQUIRE(luil::detect_text_links({}).empty());
}

TEST_CASE("Link queries find the link under an offset and the links of a range", "[ui][document][link]")
{
    const std::vector<luil::text_link> links { { 2, 5, u8"a" }, { 8, 9, u8"b" } };
    REQUIRE(luil::text_link_at(links, 1) == nullptr);
    REQUIRE(luil::text_link_at(links, 2)->destination == u8"a");
    REQUIRE(luil::text_link_at(links, 4)->destination == u8"a");
    REQUIRE(luil::text_link_at(links, 5) == nullptr);
    REQUIRE(luil::text_link_at(links, 8)->destination == u8"b");
    REQUIRE(luil::text_links_in_range(links, 0, 9).size() == 2u);
    REQUIRE(luil::text_links_in_range(links, 5, 8).empty());
}

TEST_CASE("A text area places a hyperlink child over each link it draws", "[ui][text-area][link]")
{
    const auto tree { linked_tree(view_of(u8"see a.b and c.d", 0), linked_config(true)) };
    const luil::ui_element* const first { tree->hit_test(50.0f, first_row) };
    REQUIRE(first != nullptr);
    REQUIRE(first->id().kind == luil::ui_element_kind::text_link);
    REQUIRE(first->inline_link());
    REQUIRE(first->tab_stop() == false);
    const luil::access_info info { first->accessibility() };
    REQUIRE(info.role == luil::access_role::link);
    REQUIRE(info.name == u8"a.b");
    REQUIRE(info.value == u8"https://a.b");
    // 읽기 전용 칸의 tooltip은 주소만이다. 안내는 편집 칸의 것이다.
    REQUIRE(first->tooltip() == u8"https://a.b");
    REQUIRE(first->bounds().x == 47.0f);
    REQUIRE(first->bounds().width == 30.0f);
    // 링크 사이의 글은 칸의 것이다.
    REQUIRE(tree->hit_test(100.0f, first_row)->id().kind == kind_memo);
    REQUIRE(tree->hit_test(140.0f, first_row)->accessibility().value == u8"https://c.d");
    // 둘째 줄 자리(글이 없는 곳)는 링크가 아니다.
    REQUIRE(tree->hit_test(50.0f, first_row + 18.0f)->id().kind == kind_memo);
    REQUIRE(first->cursor_at(50.0f, first_row, {}) == luil::ui_cursor::hand);
}

TEST_CASE("A link that wraps gives one hit fragment on each screen line", "[ui][text-area][link]")
{
    // 18글자 폭에서 접힌다. "aaaaaaaaa " 뒤의 19글자 낱말은 둘째 줄 18글자와 셋째 줄 한 글자로 나뉜다.
    luil::text_area_config config { linked_config(true) };
    config.links = std::make_shared<const std::vector<luil::text_link>>(std::vector<luil::text_link> { { 10, 29, u8"https://wrap.test" } });
    const auto tree { linked_tree(view_of(u8"aaaaaaaaa bbbbbbbbbbbbbbbbbbb", 0), config) };
    REQUIRE(static_cast<const luil::text_area_element&>(*tree->root()).lines().size() == 3u);
    const luil::ui_element* const second { tree->hit_test(7.0f + 15.0f, first_row + 18.0f) };
    REQUIRE(second->id().kind == luil::ui_element_kind::text_link);
    REQUIRE(tree->hit_test(7.0f + 5.0f, first_row + 36.0f) == second);
    REQUIRE(tree->hit_test(7.0f + 15.0f, first_row + 36.0f)->id().kind == kind_memo);
    REQUIRE(tree->hit_test(7.0f + 15.0f, first_row)->id().kind == kind_memo);
}

TEST_CASE("An editable text area places the caret on a plain click and opens a link on Ctrl+click", "[ui][text-area][link][interaction]")
{
    link_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    const auto tree { linked_tree(view_of(u8"see a.b and c.d", 0), linked_config(false)) };
    controller.set_tree(tree);

    // 그냥 누름은 칸의 것이다. 링크 안에 caret이 서고 아무것도 열지 않는다.
    const auto caret { edit_in(controller.process(press(57.0f, first_row, 0))) };
    REQUIRE(caret.has_value());
    REQUIRE(caret->command == luil::text::text_edit_command::place_caret);
    REQUIRE(caret->offset == 5u);
    REQUIRE(opened(controller.process(release(57.0f, first_row, 10))).has_value() == false);

    // Ctrl+클릭은 링크를 연다. caret은 움직이지 않는다.
    REQUIRE(edit_in(controller.process(press(57.0f, first_row, 1000, true))).has_value() == false);
    REQUIRE(opened(controller.process(release(57.0f, first_row, 1010, true))) == u8"https://a.b");
    // 칸 안의 링크를 눌렀으므로 초점은 칸에 남는다 — 연 뒤에 이어 칠 수 있다.
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_memo, u8"memo" });

    // 편집 칸의 링크는 Ctrl을 누르고 있을 때만 손 모양이고 굵은 밑줄 대상이다.
    const luil::ui_element* const link { tree->hit_test(57.0f, first_row) };
    REQUIRE(link->tooltip() == u8"https://a.b · Ctrl+클릭하여 열기");
    static_cast<void>(controller.process(luil::pointer_moved_event { 58.0f, first_row, at(2000) }));
    REQUIRE(controller.snapshot().pointer_control == false);
    REQUIRE(link->cursor_at(58.0f, first_row, controller.snapshot()) == luil::ui_cursor::text);
    luil::pointer_moved_event held { 59.0f, first_row, at(2010) };
    held.control = true;
    static_cast<void>(controller.process(held));
    REQUIRE(controller.snapshot().pointer_control);
    REQUIRE(controller.snapshot().hovered == link->id());
    REQUIRE(link->cursor_at(59.0f, first_row, controller.snapshot()) == luil::ui_cursor::hand);
}

TEST_CASE("Dragging across a link in an editable text area selects text", "[ui][text-area][link][interaction]")
{
    link_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    controller.set_tree(linked_tree(view_of(u8"see a.b and c.d", 0), linked_config(false)));
    static_cast<void>(controller.process(press(57.0f, first_row, 0)));
    const auto dragged { edit_in(controller.process(luil::pointer_moved_event { 137.0f, first_row, at(20) })) };
    REQUIRE(dragged.has_value());
    REQUIRE(dragged->extend);
    REQUIRE(dragged->offset == 13u);
    REQUIRE(opened(controller.process(release(137.0f, first_row, 30))).has_value() == false);
}

TEST_CASE("A read only text area opens a link on a plain click", "[ui][text-area][link][interaction]")
{
    link_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    const auto tree { linked_tree(view_of(u8"see a.b and c.d", 0), linked_config(true)) };
    controller.set_tree(tree);
    static_cast<void>(controller.process(press(137.0f, first_row, 0)));
    REQUIRE(opened(controller.process(release(137.0f, first_row, 10))) == u8"https://c.d");
    // 보조 기술의 Invoke도 같은 클릭 액션이다.
    REQUIRE(tree->hit_test(137.0f, first_row)->action(luil::ui_trigger::left_click) != nullptr);
}

TEST_CASE("Touch taps edit a link in an editable area and a long press opens the text menu", "[ui][text-area][link][touch]")
{
    link_policy policy {};
    luil::interaction_config config { fixed_config() };
    config.text_edit_menu = true;
    luil::interaction_controller controller { &policy, config };
    const auto touch_press = [](const float x, const int milliseconds) {
        luil::pointer_pressed_event event { x, first_row, luil::pointer_button::left, at(milliseconds) };
        event.device = luil::pointer_device::touch;
        event.pointer_id = 1;
        return event;
    };
    const auto touch_release = [](const float x, const int milliseconds) {
        luil::pointer_released_event event { x, first_row, luil::pointer_button::left, at(milliseconds) };
        event.device = luil::pointer_device::touch;
        event.pointer_id = 1;
        return event;
    };
    SECTION("A tap on an editable link places the caret")
    {
        controller.set_tree(linked_tree(view_of(u8"see a.b and c.d", 0), linked_config(false)));
        static_cast<void>(controller.process(touch_press(57.0f, 0)));
        const auto tapped { controller.process(touch_release(57.0f, 40)) };
        REQUIRE(opened(tapped).has_value() == false);
        REQUIRE(edit_in(tapped)->offset == 5u);
    }
    SECTION("A tap on a read only link opens it")
    {
        controller.set_tree(linked_tree(view_of(u8"see a.b and c.d", 0), linked_config(true)));
        static_cast<void>(controller.process(touch_press(57.0f, 0)));
        REQUIRE(opened(controller.process(touch_release(57.0f, 40))) == u8"https://a.b");
    }
    SECTION("A long press on a read only link selects in its area and asks for the menu")
    {
        controller.set_tree(linked_tree(view_of(u8"see a.b and c.d", 0), linked_config(true)));
        static_cast<void>(controller.process(touch_press(57.0f, 0)));
        const auto actions { controller.advance(at(600)) };
        REQUIRE(edit_in(actions)->command == luil::text::text_edit_command::select_word);
        REQUIRE(std::get<luil::ui_command>(actions.back()) == luil::ui_command::text_edit_menu);
        REQUIRE(controller.snapshot().focused_input.kind == kind_memo);
    }
}

TEST_CASE("The text input snapshot names the link under or right after the caret", "[ui][text-area][link]")
{
    const luil::text_area_config config { linked_config(false) };
    const auto link_at = [&config](const std::size_t caret, const std::size_t anchor) {
        luil::text_input_view view { view_of(u8"see a.b and c.d", caret) };
        view.anchor = anchor;
        const luil::text_area_element area { { kind_memo, u8"memo" }, std::move(view), config };
        const auto value { area.text_input() };
        REQUIRE((value->link.empty() || value->activate_link != nullptr));
        return std::u8string { value->link };
    };
    REQUIRE(link_at(5, 5) == u8"https://a.b");
    REQUIRE(link_at(7, 7) == u8"https://a.b");
    REQUIRE(link_at(8, 8).empty());
    REQUIRE(link_at(4, 7) == u8"https://a.b");
    REQUIRE(link_at(4, 13).empty());
}

TEST_CASE("A single line input draws links and opens them on Ctrl+click", "[ui][text-input][link][interaction]")
{
    luil::text_input_config config {};
    config.links = std::make_shared<const std::vector<luil::text_link>>(std::vector<luil::text_link> { { 3, 6, u8"https://a.b" } });
    config.activate_link = [](const std::u8string_view destination) { return luil::input_action { luil::app_message { open_intent { std::u8string { destination } } } }; };
    auto input { std::make_unique<luil::text_input_element>(luil::ui_element_id { kind_line, u8"line" }, view_of(u8"go a.b now", 0), std::move(config)) };
    const luil::text_measurer measurer { fixed_width };
    input->arrange({ .slot = { 0.0f, 0.0f, 200.0f, 24.0f }, .scale = 1.0f, .measurer = &measurer });
    const auto tree { std::make_shared<const luil::ui_tree>(std::move(input)) };
    link_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    controller.set_tree(tree);

    // 글은 x = 7에서 시작한다. 링크 [3, 6)은 x 37..67이다.
    const luil::ui_element* const link { tree->hit_test(50.0f, 12.0f) };
    REQUIRE(link != nullptr);
    REQUIRE(link->id().kind == luil::ui_element_kind::text_link);
    REQUIRE(link->bounds().x == 37.0f);
    REQUIRE(tree->hit_test(80.0f, 12.0f)->id().kind == kind_line);
    REQUIRE(edit_in(controller.process(press(50.0f, 12.0f, 0)))->command == luil::text::text_edit_command::place_caret);
    REQUIRE(opened(controller.process(release(50.0f, 12.0f, 10))).has_value() == false);
    static_cast<void>(controller.process(press(50.0f, 12.0f, 1000, true)));
    REQUIRE(opened(controller.process(release(50.0f, 12.0f, 1010, true))) == u8"https://a.b");

    // 암호 칸은 링크를 세우지 않는다.
    luil::text_input_config secret {};
    secret.secret = true;
    secret.links = std::make_shared<const std::vector<luil::text_link>>(std::vector<luil::text_link> { { 0, 3, u8"https://a.b" } });
    const luil::text_input_element hidden { { kind_line, u8"secret" }, view_of(u8"a.b", 1), std::move(secret) };
    REQUIRE(hidden.children().empty());
    REQUIRE(hidden.text_input()->link.empty());
}

TEST_CASE("Markdown links that open take a Tab stop and open on Enter or Space", "[ui][markdown][link][keyboard]")
{
    luil::markdown_config config { .text = u8"[docs](https://a.b) and [shown](https://c.d)" };
    config.activate_link = [](const std::u8string_view destination) { return luil::input_action { luil::app_message { open_intent { std::u8string { destination } } } }; };
    auto markdown { std::make_unique<luil::markdown_element>(luil::ui_element_id { luil::ui_element_kind::root, u8"help" }, std::move(config)) };
    const luil::text_measurer measurer { fixed_width };
    markdown->arrange({ .slot = { 0.0f, 0.0f, 400.0f, 60.0f }, .scale = 1.0f, .measurer = &measurer });
    REQUIRE(markdown->children().front()->focusable());
    link_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(markdown)));
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab }));
    REQUIRE(controller.snapshot().focus_visible);
    REQUIRE(opened(controller.process(luil::key_pressed_event { luil::key_code::enter })) == u8"https://a.b");
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab }));
    REQUIRE(opened(controller.process(luil::key_pressed_event { luil::key_code::space })) == u8"https://c.d");

    // 표시만 하는 링크는 자리가 없다.
    luil::markdown_element shown { { luil::ui_element_kind::root, u8"plain" }, { .text = u8"[docs](https://a.b)" } };
    shown.arrange({ .slot = { 0.0f, 0.0f, 400.0f, 60.0f }, .scale = 1.0f, .measurer = &measurer });
    REQUIRE(shown.children().front()->focusable() == false);
}
