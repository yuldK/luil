#include "luil/ui/text_area_element.h"

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

    struct scroll_intent
    {
        float delta { 0.0f };
    };

    // 글자 하나가 10 픽셀인 측정기다. 글꼴 없이 자리 계산을 잠근다.
    [[nodiscard]] float fixed_width(const std::u8string_view text, float)
    {
        float width { 0.0f };
        for (const char8_t unit : text)
            if ((static_cast<unsigned int>(unit) & 0xC0u) != 0x80u)
                width += 10.0f;
        return width;
    }

    class memo_policy final : public luil::interaction_policy
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

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event& event, const luil::interaction_snapshot&) override
        {
            unhandled.push_back(event.key);
            return {};
        }

        std::vector<luil::key_code> unhandled {};
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

    [[nodiscard]] luil::text_area_config scrolling_config(const float offset = 0.0f)
    {
        luil::text_area_config config {};
        config.placeholder = u8"메모";
        config.scroll_offset = offset;
        config.scroll = [](const float delta) { return luil::input_action { luil::app_message { scroll_intent { delta } } }; };
        return config;
    }

    // 200×100 칸 하나다 (배율 1). 줄 높이 18, 위 여백 5, 왼쪽 여백 7이라 n번 줄의 가운데는 y = 5 + 18n + 9다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> memo_tree(luil::text_input_view view, luil::text_area_config config = {})
    {
        return std::make_shared<const luil::ui_tree>(
            luil::make_arranged_tree(std::make_unique<luil::text_area_element>(luil::ui_element_id { kind_memo, u8"memo" }, std::move(view), std::move(config)), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    }

    [[nodiscard]] const luil::text_area_element& memo_of(const luil::ui_tree& tree)
    {
        return static_cast<const luil::text_area_element&>(*tree.root());
    }

    [[nodiscard]] float row_middle(const int row)
    {
        return 5.0f + 18.0f * static_cast<float>(row) + 9.0f;
    }

    [[nodiscard]] luil::interaction_config fixed_config()
    {
        luil::interaction_config config {};
        config.measure_text = fixed_width;
        return config;
    }

    // 칸을 눌러 초점을 준다. 누름이 낸 caret 편집은 버린다.
    void focus(luil::interaction_controller& controller)
    {
        static_cast<void>(controller.process(luil::pointer_pressed_event { 100.0f, row_middle(0), luil::pointer_button::left, at(0) }));
        static_cast<void>(controller.process(luil::pointer_released_event { 100.0f, row_middle(0), luil::pointer_button::left, at(10) }));
    }

    [[nodiscard]] std::optional<luil::text_edit_request> edit_in(const std::vector<luil::input_action>& actions)
    {
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const edit { message->get<edit_intent>() }; edit != nullptr)
                    return edit->request;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<float> scroll_in(const std::vector<luil::input_action>& actions)
    {
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const scroll { message->get<scroll_intent>() }; scroll != nullptr)
                    return scroll->delta;
        return std::nullopt;
    }

    [[nodiscard]] luil::key_pressed_event key(const luil::key_code code, const bool shift = false, const bool control = false)
    {
        return luil::key_pressed_event { code, control, shift, false, false, at(20) };
    }

    [[nodiscard]] std::u8string twenty_lines()
    {
        std::u8string text {};
        for (int line = 0; line < 20; ++line)
        {
            if (line > 0)
                text += u8"\n";
            text += u8"line";
        }
        return text;
    }
} // namespace

TEST_CASE("A text area counts lines by line breaks only", "[ui][text-area]")
{
    // 배치에는 글꼴이 없어 넘겨 접은 줄을 셀 수 없다 — 줄 수와 내용 높이는 글만의 함수다.
    REQUIRE(luil::text_area_line_count(u8"") == 1u);
    REQUIRE(luil::text_area_line_count(u8"one line, however long it is") == 1u);
    REQUIRE(luil::text_area_line_count(u8"a\nb\n") == 3u);
    REQUIRE(luil::text_area_content_height(u8"") == luil::text_area_line_height + 2.0f * luil::text_area_padding_y);
    REQUIRE(luil::text_area_content_height(u8"a\nb\n") == 3.0f * luil::text_area_line_height + 2.0f * luil::text_area_padding_y);
}

TEST_CASE("A text area maps a point to the line and the column under it", "[ui][text-area]")
{
    const auto tree { memo_tree(view_of(u8"abc\ndefgh\nij", 0)) };
    const luil::text_area_element& memo { memo_of(*tree) };
    // 둘째 줄 "defgh"는 offset 4에서 시작한다. x = 7 + 22는 두 글자 뒤가 가깝다.
    REQUIRE(memo.offset_at_point(7.0f + 22.0f, row_middle(1), fixed_width) == 6u);
    REQUIRE(memo.offset_at_point(7.0f + 5000.0f, row_middle(1), fixed_width) == 9u);
    REQUIRE(memo.offset_at_point(-50.0f, row_middle(2), fixed_width) == 10u);
    // 첫 줄 위는 글의 처음, 마지막 줄 아래는 글의 끝이다.
    REQUIRE(memo.offset_at_point(50.0f, 1.0f, fixed_width) == 0u);
    REQUIRE(memo.offset_at_point(50.0f, 95.0f, fixed_width) == 12u);
}

TEST_CASE("A text area answers the caret span on its own line", "[ui][text-area]")
{
    const std::u8string text { u8"abc\ndefgh\nij" };
    const auto tree { memo_tree(view_of(text, 7)) };
    const auto span { memo_of(*tree).text_span_bounds({ text, 7, 7, 7 }, fixed_width) };
    REQUIRE(span.has_value());
    // offset 7은 둘째 줄의 세 글자 뒤다.
    REQUIRE(span->x == 7.0f + 30.0f);
    REQUIRE(span->y == 5.0f + 18.0f);
    REQUIRE(span->width == 0.0f);
    REQUIRE(span->height == 18.0f);

    // 줄에 걸친 구간은 시작한 줄의 끝까지다 — IME 후보 창이 조합 글자 밑에 앉는다.
    const auto across { memo_of(*tree).text_span_bounds({ text, 7, 2, 6 }, fixed_width) };
    REQUIRE(across->x == 7.0f + 20.0f);
    REQUIRE(across->width == 10.0f);
    REQUIRE(across->y == 5.0f);
}

TEST_CASE("A text area says it takes several lines and reads as an edit", "[ui][text-area]")
{
    const auto tree { memo_tree(view_of(u8"a\nb", 1), scrolling_config()) };
    const auto snapshot { memo_of(*tree).text_input() };
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->multiline);
    REQUIRE(snapshot->text == u8"a\nb");
    const luil::access_info info { memo_of(*tree).accessibility() };
    REQUIRE(info.role == luil::access_role::edit);
    REQUIRE(info.name == u8"메모");
    REQUIRE(info.value == u8"a\nb");
}

TEST_CASE("Enter breaks the line in a text area and stays a default key in a single line field", "[ui][text-area][interaction]")
{
    memo_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    controller.set_tree(memo_tree(view_of(u8"abc", 3)));
    focus(controller);

    const auto broken { edit_in(controller.process(key(luil::key_code::enter))) };
    REQUIRE(broken.has_value());
    REQUIRE(broken->command == luil::text::text_edit_command::insert);
    REQUIRE(broken->text == u8"\n");
    REQUIRE(broken->target == memo_target);
    // Shift+Enter도 줄 바꿈이다.
    REQUIRE(edit_in(controller.process(key(luil::key_code::enter, true)))->text == u8"\n");
    // Ctrl+Enter는 앱 단축키로 흐른다.
    REQUIRE(edit_in(controller.process(key(luil::key_code::enter, false, true))).has_value() == false);
    REQUIRE(policy.unhandled == std::vector<luil::key_code> { luil::key_code::enter });

    // 한 줄 칸의 Enter는 지금처럼 글을 바꾸지 않는다.
    memo_policy single_policy {};
    luil::interaction_controller single { &single_policy, fixed_config() };
    luil::text_input_view view { view_of(u8"abc", 3) };
    single.set_tree(std::make_shared<const luil::ui_tree>(
        luil::make_arranged_tree(std::make_unique<luil::text_input_element>(luil::ui_element_id { kind_line, u8"line" }, view, luil::text_input_config {}), { 0.0f, 0.0f, 200.0f, 28.0f }, 1.0f)));
    static_cast<void>(single.process(luil::pointer_pressed_event { 50.0f, 14.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(single.process(luil::pointer_released_event { 50.0f, 14.0f, luil::pointer_button::left, at(10) }));
    REQUIRE(edit_in(single.process(key(luil::key_code::enter))).has_value() == false);
    REQUIRE(single_policy.unhandled == std::vector<luil::key_code> { luil::key_code::enter });
}

TEST_CASE("Arrow keys move the caret between lines of a text area", "[ui][text-area][interaction]")
{
    memo_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    // caret은 둘째 줄 "defgh"의 세 글자 뒤(offset 7)다.
    controller.set_tree(memo_tree(view_of(u8"abc\ndefgh\nij", 7)));
    focus(controller);
    controller.set_tree(memo_tree(view_of(u8"abc\ndefgh\nij", 7)));

    const auto up { edit_in(controller.process(key(luil::key_code::arrow_up))) };
    REQUIRE(up->command == luil::text::text_edit_command::place_caret);
    REQUIRE(up->offset == 3u);
    REQUIRE(up->extend == false);
    // 셋째 줄은 두 글자뿐이라 그 끝으로 간다.
    REQUIRE(edit_in(controller.process(key(luil::key_code::arrow_down)))->offset == 12u);
    // Shift는 고르며 옮긴다.
    REQUIRE(edit_in(controller.process(key(luil::key_code::arrow_up, true)))->extend);
    // Home·End는 caret이 선 줄의 처음과 끝이다.
    REQUIRE(edit_in(controller.process(key(luil::key_code::home)))->offset == 4u);
    REQUIRE(edit_in(controller.process(key(luil::key_code::end)))->offset == 9u);
    // Ctrl+Home·End는 글의 처음과 끝이다 (한 줄 칸과 같은 명령).
    REQUIRE(edit_in(controller.process(key(luil::key_code::home, false, true)))->command == luil::text::text_edit_command::move_line_start);
    REQUIRE(edit_in(controller.process(key(luil::key_code::end, false, true)))->command == luil::text::text_edit_command::move_line_end);

    // 첫 줄에서 ↑는 글의 처음, 마지막 줄에서 ↓는 글의 끝이다.
    controller.set_tree(memo_tree(view_of(u8"abc\ndefgh\nij", 2)));
    REQUIRE(edit_in(controller.process(key(luil::key_code::arrow_up)))->offset == 0u);
    controller.set_tree(memo_tree(view_of(u8"abc\ndefgh\nij", 11)));
    REQUIRE(edit_in(controller.process(key(luil::key_code::arrow_down)))->offset == 12u);
}

TEST_CASE("Page keys move a text area caret by a window of lines", "[ui][text-area][interaction]")
{
    memo_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    controller.set_tree(memo_tree(view_of(twenty_lines(), 0), scrolling_config()));
    focus(controller);
    controller.set_tree(memo_tree(view_of(twenty_lines(), 0), scrolling_config()));
    // 높이 100에 줄 18이면 다섯 줄이 들고, 한 줄을 남겨 네 줄을 넘긴다. 줄은 "line" + 줄 바꿈이라 다섯 바이트씩이다.
    const auto paged { edit_in(controller.process(key(luil::key_code::page_down))) };
    REQUIRE(paged->command == luil::text::text_edit_command::place_caret);
    REQUIRE(paged->offset == 4u * 5u);
}

TEST_CASE("A text area follows its caret only when the caret or the text changes", "[ui][text-area][interaction]")
{
    memo_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    const std::u8string text { twenty_lines() };
    controller.set_tree(memo_tree(view_of(text, 0), scrolling_config()));
    focus(controller);
    // 처음 본 caret은 이미 보인다.
    REQUIRE(scroll_in(controller.follow_text_caret()).has_value() == false);

    // caret이 마지막 줄로 갔다. 그 줄을 창 안으로 들인다.
    controller.set_tree(memo_tree(view_of(text, text.size()), scrolling_config()));
    const std::optional<float> revealed { scroll_in(controller.follow_text_caret()) };
    REQUIRE(revealed.has_value());
    REQUIRE(*revealed > 0.0f);
    // 같은 tree를 다시 물어도 또 내지 않는다.
    REQUIRE(controller.follow_text_caret().empty());

    // 휠로 caret에서 멀리 흘렸다 — caret도 글도 그대로라 되돌리지 않는다.
    controller.set_tree(memo_tree(view_of(text, text.size()), scrolling_config(0.0f)));
    REQUIRE(controller.follow_text_caret().empty());

    // 글이 바뀌면 다시 들인다.
    controller.set_tree(memo_tree(view_of(text + u8"\nmore", text.size() + 5u), scrolling_config(0.0f)));
    REQUIRE(scroll_in(controller.follow_text_caret()).has_value());
}

TEST_CASE("A text area without a scroll factory does not follow its caret", "[ui][text-area][interaction]")
{
    memo_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    const std::u8string text { twenty_lines() };
    controller.set_tree(memo_tree(view_of(text, 0)));
    focus(controller);
    controller.set_tree(memo_tree(view_of(text, text.size())));
    REQUIRE(controller.follow_text_caret().empty());
}
