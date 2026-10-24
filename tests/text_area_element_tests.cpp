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
    //  - 측정기 없이 배치한다. Shaping 구성은 native 배치로 재고, core test에는 글꼴이 없어 글 폭이 0이다.
    //    글 안의 x를 묻는 시험은 고정 폭 측정기로 배치하는 `wrapped_tree`를 쓴다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> memo_tree(luil::text_input_view view, luil::text_area_config config = {})
    {
        return std::make_shared<const luil::ui_tree>(
            luil::make_arranged_tree(std::make_unique<luil::text_area_element>(luil::ui_element_id { kind_memo, u8"memo" }, std::move(view), std::move(config)), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f));
    }

    // 같은 칸을 고정 폭 측정기로 배치한다. 배치가 줄을 접는다 — core test의 기본 측정기는 폭 0을 재 접지 않는다.
    //  - 흘리지 않는 칸이면 접는 폭은 200 - 7·2 = 186, 곧 한 줄에 18글자다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> wrapped_tree(luil::text_input_view view, luil::text_area_config config = {})
    {
        auto memo { std::make_unique<luil::text_area_element>(luil::ui_element_id { kind_memo, u8"memo" }, std::move(view), std::move(config)) };
        const luil::text_measurer measurer { fixed_width };
        memo->arrange({ .slot = { 0.0f, 0.0f, 200.0f, 100.0f }, .scale = 1.0f, .measurer = &measurer });
        return std::make_shared<const luil::ui_tree>(std::move(memo));
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

TEST_CASE("A text area splits lines at line breaks", "[ui][text-area]")
{
    // 빈 글도 한 줄이다. 줄 바꿈 하나가 줄 하나를 더한다.
    using line = luil::text_area_element::line;
    REQUIRE(memo_of(*wrapped_tree(view_of(u8"", 0))).lines() == std::vector<line> { { 0, 0, false } });
    REQUIRE(memo_of(*wrapped_tree(view_of(u8"a\nb\n", 0))).lines() == std::vector<line> { { 0, 1, false }, { 2, 3, false }, { 4, 4, false } });
    REQUIRE(memo_of(*wrapped_tree(view_of(u8"a\nb\n", 0))).area().metrics().content_height == 3.0f * luil::text_area_line_height + 2.0f * luil::text_area_padding_y);
}

TEST_CASE("A text area wraps long lines after a space at its width", "[ui][text-area]")
{
    using line = luil::text_area_element::line;
    // 열여덟 글자가 드는 칸이다. "dddd"는 걸쳐 넘치므로 앞의 공백 뒤에서 접고, 공백은 윗줄에 매달린다.
    const auto words { wrapped_tree(view_of(u8"aaaa bbbb cccc dddd eeee", 0)) };
    REQUIRE(memo_of(*words).lines() == std::vector<line> { { 0, 15, true }, { 15, 24, false } });
    REQUIRE(memo_of(*words).area().metrics().content_height == 2.0f * luil::text_area_line_height + 2.0f * luil::text_area_padding_y);

    // 칸보다 긴 낱말은 글자 사이에서 접는다.
    REQUIRE(memo_of(*wrapped_tree(view_of(u8"abcdefghijklmnopqrstuvwxyz", 0))).lines() == std::vector<line> { { 0, 18, true }, { 18, 26, false } });
    // 바로 넘치는 자리가 공백이면 그 공백까지 윗줄이다. 접은 뒤의 줄 바꿈은 지금처럼 나눈다.
    REQUIRE(memo_of(*wrapped_tree(view_of(u8"aaaaaaaaaaaaaaaaaa bb\ncc", 0))).lines() == std::vector<line> { { 0, 19, true }, { 19, 21, false }, { 22, 24, false } });
    // 끝에 매달린 공백만으로는 줄을 더하지 않는다.
    REQUIRE(memo_of(*wrapped_tree(view_of(u8"aaaaaaaaaaaaaaaaaa   ", 0))).lines() == std::vector<line> { { 0, 21, false } });

    // 흘리는 칸은 막대 자리를 늘 빼고 접는다 — 넘치지 않아 막대가 없어도 같은 폭이다.
    const auto scrolled { wrapped_tree(view_of(u8"aaaa bbbb cccc ddd", 0), scrolling_config()) };
    REQUIRE(memo_of(*scrolled).area().metrics().overflowing() == false);
    REQUIRE(memo_of(*scrolled).lines().size() == 2u);
}

TEST_CASE("A wrapped text area places points and spans on screen lines", "[ui][text-area]")
{
    const std::u8string text { u8"aaaa bbbb cccc dddd eeee" };
    const auto tree { wrapped_tree(view_of(text, 0)) };
    const luil::text_area_element& memo { memo_of(*tree) };
    // 둘째 화면 줄 "dddd eeee"는 offset 15에서 시작한다.
    REQUIRE(memo.offset_at_point(7.0f + 22.0f, row_middle(1), fixed_width) == 17u);
    REQUIRE(memo.offset_at_point(-50.0f, row_middle(1), fixed_width) == 15u);
    // 접은 줄의 오른쪽 끝은 접은 자리(15)이고, 누른 윗줄에 붙는다. 아랫줄의 처음도 같은 offset이지만 아랫줄 쪽이다.
    REQUIRE(memo.caret_at_point(7.0f + 5000.0f, row_middle(0), fixed_width) == luil::text_caret_position { 15, true });
    REQUIRE(memo.caret_at_point(-50.0f, row_middle(1), fixed_width) == luil::text_caret_position { 15, false });
    REQUIRE(memo.offset_at_point(7.0f + 5000.0f, row_middle(0), fixed_width) == 15u);
    // 접지 않은 줄의 끝은 아랫줄 쪽이 따로 없다.
    REQUIRE(memo.caret_at_point(7.0f + 5000.0f, row_middle(1), fixed_width) == luil::text_caret_position { 24, false });
    // affinity가 없는 caret은 접은 자리에서 아랫줄의 처음이다.
    const auto at_break { memo.text_span_bounds({ text, 15, 15, 15 }, fixed_width) };
    REQUIRE(at_break->x == 7.0f);
    REQUIRE(at_break->y == 5.0f + 18.0f);
    // 접은 자리를 넘는 구간은 시작한 화면 줄의 끝까지다.
    const auto across { memo.text_span_bounds({ text, 12, 12, 17 }, fixed_width) };
    REQUIRE(across->x == 7.0f + 120.0f);
    REQUIRE(across->width == 30.0f);
    REQUIRE(across->y == 5.0f);
}

TEST_CASE("A composing text area wraps the displayed text and rewraps the committed text", "[ui][text-area]")
{
    // 확정 글은 한 줄에 들지만, 조합 글이 섞인 표시 문서는 넘쳐 두 줄로 접힌다.
    luil::text_input_view view { view_of(u8"aaaa bbbb cccc ", 15) };
    view.composing = true;
    view.composition_text = u8"aaaa bbbb cccc dddd eeee";
    view.composition_caret = 20;
    view.composing_begin = 15;
    view.composing_end = 24;
    const auto tree { wrapped_tree(view) };
    const luil::text_area_element& memo { memo_of(*tree) };
    REQUIRE(memo.lines().size() == 2u);

    // IME는 표시 문서로 묻는다 — 배치가 접은 그 줄이다. offset 20은 둘째 화면 줄의 다섯 글자 뒤다.
    const auto caret { memo.text_span_bounds({ view.composition_text, 20, 20, 20 }, fixed_width) };
    REQUIRE(caret->x == 7.0f + 50.0f);
    REQUIRE(caret->y == 5.0f + 18.0f);
    // 확정 글로 묻는 구간은 그 글을 같은 폭으로 다시 접어 답한다. 확정 글은 한 줄이라 끝(15)도 첫 줄에 선다.
    const auto committed_end { memo.text_span_bounds({ view.text, 15, 15, 15 }, fixed_width) };
    REQUIRE(committed_end->x == 7.0f + 150.0f);
    REQUIRE(committed_end->y == 5.0f);
}

TEST_CASE("A composing text area places the pointer on the displayed text", "[ui][text-area]")
{
    // 누름은 host가 조합을 확정한 뒤에 온다. 확정된 글이 곧 표시 문서라 포인터는 표시 문서에서 잰다.
    luil::text_input_view view { view_of(u8"aaaa bbbb cccc ", 15) };
    view.composing = true;
    view.composition_text = u8"aaaa bbbb cccc dddd eeee";
    view.composition_caret = 20;
    view.composing_begin = 15;
    view.composing_end = 24;
    const auto tree { wrapped_tree(view) };
    const luil::text_area_element& memo { memo_of(*tree) };
    // 조합 글이 넘겨 접은 둘째 화면 줄을 누르면 그 줄의 자리다. 확정 글로 재면 글 끝(15)으로 뭉개진다.
    REQUIRE(memo.offset_at_point(7.0f + 22.0f, row_middle(1), fixed_width) == 17u);
    REQUIRE(memo.offset_at_point(7.0f + 22.0f, row_middle(0), fixed_width) == 2u);
    // 마지막 줄 아래는 표시 문서의 끝이다.
    REQUIRE(memo.offset_at_point(7.0f, row_middle(4), fixed_width) == 24u);
    // y 없이 묻는 자리는 조합 caret이 선 줄(둘째 줄)에서 판정한다.
    REQUIRE(memo.offset_at(7.0f + 22.0f, fixed_width) == 17u);
}

TEST_CASE("A press on a composing text area places the caret where the displayed text was pressed", "[ui][text-area][interaction]")
{
    // 조합 중에 눌러도 controller는 caret 편집을 낸다 — 확정은 host가 누름보다 먼저 앱에 보낸다.
    // 그 편집의 offset은 확정 뒤의 글(= 표시 문서) 위의 자리여야 한다.
    luil::text_input_view view { view_of(u8"aaaa bbbb cccc ", 15) };
    view.composing = true;
    view.composition_text = u8"aaaa bbbb cccc dddd eeee";
    view.composition_caret = 20;
    view.composing_begin = 15;
    view.composing_end = 24;
    memo_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    controller.set_tree(wrapped_tree(view));
    const auto pressed { edit_in(controller.process(luil::pointer_pressed_event { 7.0f + 22.0f, row_middle(1), luil::pointer_button::left, at(0) })) };
    REQUIRE(pressed.has_value());
    REQUIRE(pressed->command == luil::text::text_edit_command::place_caret);
    REQUIRE(pressed->offset == 17u);
    REQUIRE(pressed->extend == false);

    // 누른 채 끌면 같은 글 위에서 선택을 늘린다. 첫 줄의 둘째 글자 뒤까지다.
    const auto dragged { edit_in(controller.process(luil::pointer_moved_event { 7.0f + 22.0f, row_middle(0), at(20) })) };
    REQUIRE(dragged.has_value());
    REQUIRE(dragged->command == luil::text::text_edit_command::place_caret);
    REQUIRE(dragged->offset == 2u);
    REQUIRE(dragged->extend);
}

TEST_CASE("A tap on a composing text area places the caret where the displayed text was tapped", "[ui][text-area][interaction][touch]")
{
    // 터치는 뗄 때 caret을 놓는다. Android host도 누름에서 조합을 먼저 확정하므로 같은 규칙이다.
    luil::text_input_view view { view_of(u8"aaaa bbbb cccc ", 15) };
    view.composing = true;
    view.composition_text = u8"aaaa bbbb cccc dddd eeee";
    view.composition_caret = 20;
    view.composing_begin = 15;
    view.composing_end = 24;
    memo_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    controller.set_tree(wrapped_tree(view));
    luil::pointer_pressed_event press { 7.0f + 22.0f, row_middle(1), luil::pointer_button::left, at(0) };
    press.device = luil::pointer_device::touch;
    press.pointer_id = 1;
    static_cast<void>(controller.process(press));
    luil::pointer_released_event release { 7.0f + 22.0f, row_middle(1), luil::pointer_button::left, at(40) };
    release.device = luil::pointer_device::touch;
    release.pointer_id = 1;
    const auto tapped { edit_in(controller.process(release)) };
    REQUIRE(tapped.has_value());
    REQUIRE(tapped->command == luil::text::text_edit_command::place_caret);
    REQUIRE(tapped->offset == 17u);
}

TEST_CASE("Line keys follow the screen lines of a wrapped text area", "[ui][text-area][interaction]")
{
    memo_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    const std::u8string text { u8"aaaa bbbb cccc dddd eeee" };
    controller.set_tree(wrapped_tree(view_of(text, 2)));
    focus(controller);
    controller.set_tree(wrapped_tree(view_of(text, 2)));
    // ↓는 같은 줄 바꿈 안의 다음 화면 줄로 간다.
    REQUIRE(edit_in(controller.process(key(luil::key_code::arrow_down)))->offset == 17u);
    // End는 caret이 선 화면 줄의 끝이다. 접은 자리라 윗줄에 붙는다.
    const auto end { edit_in(controller.process(key(luil::key_code::end))) };
    REQUIRE(end->offset == 15u);
    REQUIRE(end->upstream);
    // ↓·↑는 caret을 놓되 접은 자리가 아니면 붙지 않는다.
    REQUIRE(edit_in(controller.process(key(luil::key_code::arrow_down)))->upstream == false);

    controller.set_tree(wrapped_tree(view_of(text, 20)));
    REQUIRE(edit_in(controller.process(key(luil::key_code::home)))->offset == 15u);
    REQUIRE(edit_in(controller.process(key(luil::key_code::arrow_up)))->offset == 5u);
    // Ctrl+End는 글의 끝이다.
    REQUIRE(edit_in(controller.process(key(luil::key_code::end, false, true)))->command == luil::text::text_edit_command::move_line_end);
}

TEST_CASE("A text area maps a point to the line and the column under it", "[ui][text-area]")
{
    const auto tree { wrapped_tree(view_of(u8"abc\ndefgh\nij", 0)) };
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
    const auto tree { wrapped_tree(view_of(text, 7)) };
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
    controller.set_tree(wrapped_tree(view_of(u8"abc\ndefgh\nij", 7)));
    focus(controller);
    controller.set_tree(wrapped_tree(view_of(u8"abc\ndefgh\nij", 7)));

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
    controller.set_tree(wrapped_tree(view_of(u8"abc\ndefgh\nij", 2)));
    REQUIRE(edit_in(controller.process(key(luil::key_code::arrow_up)))->offset == 0u);
    controller.set_tree(wrapped_tree(view_of(u8"abc\ndefgh\nij", 11)));
    REQUIRE(edit_in(controller.process(key(luil::key_code::arrow_down)))->offset == 12u);
}

TEST_CASE("Page keys move a text area caret by a window of lines", "[ui][text-area][interaction]")
{
    memo_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    controller.set_tree(wrapped_tree(view_of(twenty_lines(), 0), scrolling_config()));
    focus(controller);
    controller.set_tree(wrapped_tree(view_of(twenty_lines(), 0), scrolling_config()));
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

TEST_CASE("A caret held at a wrap point stays on the upper line until another edit moves it", "[ui][text-area][interaction]")
{
    const std::u8string text { u8"aaaa bbbb cccc dddd eeee" };
    // 편집 상태: place_caret만 affinity를 세우고, 다른 명령은 지운다.
    luil::text::text_edit_state state { .text = text, .caret = 2, .anchor = 2 };
    luil::text_edit_request place {};
    place.command = luil::text::text_edit_command::place_caret;
    place.offset = 15;
    place.upstream = true;
    luil::apply_text_edit(state, place);
    REQUIRE(state.caret == 15u);
    REQUIRE(state.caret_upstream);
    luil::text_edit_request left {};
    left.command = luil::text::text_edit_command::move_left;
    luil::apply_text_edit(state, left);
    REQUIRE(state.caret_upstream == false);
    luil::apply_text_edit(state, place);
    luil::text_edit_request insert {};
    insert.command = luil::text::text_edit_command::insert;
    insert.text = u8"#";
    luil::apply_text_edit(state, insert);
    REQUIRE(state.caret_upstream == false);
    // 다듬어져 다른 자리에 놓인 caret은 붙지 않는다.
    place.offset = 999;
    luil::apply_text_edit(state, place);
    REQUIRE(state.caret_upstream == false);

    // 칸: 붙은 caret은 윗줄 끝에 서고, 그 자리를 묻는 질의(IME 후보 창·보조 기술)도 윗줄을 답한다.
    luil::text_input_view held { view_of(text, 15) };
    held.caret_upstream = true;
    const auto tree { wrapped_tree(held) };
    const luil::text_area_element& memo { memo_of(*tree) };
    REQUIRE(memo.text_input()->caret_upstream);
    const auto caret { memo.text_span_bounds({ text, 15, 15, 15 }, fixed_width) };
    REQUIRE(caret->x == 7.0f + 150.0f);
    REQUIRE(caret->y == 5.0f);
    // 같은 offset의 다른 질의(caret이 아닌 자리)는 아랫줄의 처음이다.
    REQUIRE(memo.text_span_bounds({ text, 0, 15, 15 }, fixed_width)->y == 5.0f + 18.0f);

    // 붙은 caret에서 Home은 윗줄의 처음, ↓는 같은 가로 자리의 아랫줄(그 줄의 끝)이다.
    memo_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    controller.set_tree(tree);
    focus(controller);
    controller.set_tree(wrapped_tree(held));
    REQUIRE(edit_in(controller.process(key(luil::key_code::home)))->offset == 0u);
    REQUIRE(edit_in(controller.process(key(luil::key_code::arrow_down)))->offset == 24u);
}
TEST_CASE("A wrap cache gives the same lines and measures only changed paragraphs", "[ui][text-area]")
{
    // 잰 횟수를 세는 고정 폭 측정기다.
    std::size_t measured { 0 };
    const auto count = [&measured](const std::u8string_view value, const float size) {
        ++measured;
        return fixed_width(value, size);
    };
    const luil::text_measurer counting { count };
    const auto arrange = [&counting](const std::u8string& text, std::shared_ptr<luil::text_area_wrap_cache> cache, const float width = 200.0f) {
        luil::text_area_config config {};
        config.wrap_cache = std::move(cache);
        auto memo { std::make_unique<luil::text_area_element>(luil::ui_element_id { kind_memo, u8"memo" }, view_of(text, 0), std::move(config)) };
        memo->arrange({ .slot = { 0.0f, 0.0f, width, 100.0f }, .scale = 1.0f, .measurer = &counting });
        return memo->lines();
    };

    // 캐시가 있든 없든 같은 줄이다 — 공백 뒤 접기, 매달린 공백, 긴 낱말, 한글, 빈 문단.
    for (const std::u8string& text : {
             std::u8string { u8"aaaa bbbb cccc dddd eeee" },
             std::u8string { u8"aaaaaaaaaaaaaaaaaa   \nabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz" },
             std::u8string { u8"한글 낱말은 사이에서 접히고 아주 긴 문장은 여러 줄이 된다\n\n끝" },
         })
        REQUIRE(arrange(text, std::make_shared<luil::text_area_wrap_cache>()) == arrange(text, nullptr));

    const std::u8string first { u8"aaaa bbbb cccc dddd eeee\nffff gggg hhhh iiii jjjj\nkk" };
    const auto cache { std::make_shared<luil::text_area_wrap_cache>() };
    static_cast<void>(arrange(first, cache));
    REQUIRE(cache->size() == 3u);
    // 같은 글은 다시 재지 않는다 — 표본 글 하나만 잰다.
    measured = 0;
    static_cast<void>(arrange(first, cache));
    REQUIRE(measured == 1u);

    // 한 문단만 바꾸면 그 문단만 잰다. 쓰지 않게 된 문단은 버린다.
    measured = 0;
    static_cast<void>(arrange(u8"new paragraph here", nullptr));
    const std::size_t new_paragraph_alone { measured };
    const std::u8string edited { u8"aaaa bbbb cccc dddd eeee\nffff gggg hhhh iiii jjjj!\nkk" };
    measured = 0;
    REQUIRE(arrange(edited, cache) == arrange(edited, nullptr));
    REQUIRE(cache->size() == 3u);
    measured = 0;
    static_cast<void>(arrange(u8"aaaa bbbb cccc dddd eeee\nffff gggg hhhh iiii jjjj!\nkk\nnew paragraph here", cache));
    // 잰 것은 표본 글과 새 문단뿐이다.
    REQUIRE(measured == new_paragraph_alone + 1u);
    REQUIRE(cache->size() == 4u);
    static_cast<void>(arrange(u8"kk", cache));
    REQUIRE(cache->size() == 1u);

    // 폭이 바뀌면 통째로 다시 잰다.
    REQUIRE(arrange(first, cache, 120.0f) == arrange(first, nullptr, 120.0f));
}

TEST_CASE("A long paragraph wraps by measuring about one line at a time", "[ui][text-area]")
{
    // 줄 바꿈 없는 문단을 줄마다 남은 문단 전체로 재면 잰 글자 수가 문단 길이의 제곱으로 는다.
    std::size_t measured_units { 0 };
    const auto count = [&measured_units](const std::u8string_view value, const float size) {
        measured_units += value.size();
        return fixed_width(value, size);
    };
    const luil::text_measurer counting { count };
    std::u8string text {};
    for (int word = 0; word < 2000; ++word)
        text += u8"word ";
    auto memo { std::make_unique<luil::text_area_element>(luil::ui_element_id { kind_memo, u8"memo" }, view_of(text, 0), luil::text_area_config {}) };
    memo->arrange({ .slot = { 0.0f, 0.0f, 200.0f, 100.0f }, .scale = 1.0f, .measurer = &counting });
    // 18글자 칸에 "word " 세 개(15글자)씩 접힌다.
    REQUIRE(memo->lines().size() == 667u);
    // 줄마다 그 줄의 몇 배만 잰다 — 문단 전체(10,000바이트)를 줄 수만큼 재면 수백만이다.
    REQUIRE(measured_units < 50u * text.size());
}
TEST_CASE("Read only documents keep selection and copy while suppressing edits", "[ui][text-area][read-only]")
{
    memo_policy policy {};
    luil::interaction_controller controller { &policy, fixed_config() };
    auto view { view_of(u8"abc", 3) };
    view.anchor = 0;
    luil::text_area_config config {};
    config.read_only = true;
    const auto tree { wrapped_tree(view, config) };
    controller.set_tree(tree);
    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 20.0f, luil::pointer_button::left }));
    static_cast<void>(controller.process(luil::pointer_released_event { 20.0f, 20.0f, luil::pointer_button::left }));
    REQUIRE(memo_of(*tree).text_input()->read_only);
    REQUIRE(controller.process(luil::character_typed_event { U'z' }).empty());
    REQUIRE(controller.process(luil::character_typed_event { U'\b' }).empty());
    for (const auto key : { luil::key_code::key_x, luil::key_code::key_v, luil::key_code::key_z, luil::key_code::key_y })
        REQUIRE(controller.process(luil::key_pressed_event { key, true }).empty());
    const auto copied { controller.process(luil::key_pressed_event { luil::key_code::key_c, true }) };
    REQUIRE(copied.size() == 1u);
    REQUIRE(std::get<luil::clipboard_copy_request>(copied.front()).text == u8"abc");
    REQUIRE(controller.process(luil::key_pressed_event { luil::key_code::key_a, true }).size() == 1u);
    // 글을 바꾸는 나머지 키도 삼킨다 — Delete, Shift+Delete(잘라내기), Enter, Shift+Insert(붙여넣기),
    // Ctrl+Backspace(WM_CHAR의 U+007F).
    for (const luil::key_pressed_event& event : { key(luil::key_code::delete_forward), key(luil::key_code::delete_forward, true), key(luil::key_code::enter), key(luil::key_code::insert, true) })
        REQUIRE(controller.process(event).empty());
    REQUIRE(controller.process(luil::character_typed_event { U'\x7F' }).empty());
    // Ctrl+Insert는 복사라 남는다.
    REQUIRE(controller.process(key(luil::key_code::insert, false, true)).size() == 1u);
}

TEST_CASE("A text area without native layout ends a CRLF line before its CR", "[ui][text-area]")
{
    // LF만 나누는 길에서도 CR은 줄의 글이 아니다. 남기면 글리프로 그려지고 줄 끝 누름이 CR과 LF 사이에 선다.
    using line = luil::text_area_element::line;
    const auto tree { wrapped_tree(view_of(u8"ab\r\ncd\r\n", 0)) };
    const luil::text_area_element& memo { memo_of(*tree) };
    REQUIRE(memo.lines() == std::vector<line> { { 0, 2, false }, { 4, 6, false }, { 8, 8, false } });
    REQUIRE(memo.caret_at_point(7.0f + 5000.0f, row_middle(0), fixed_width) == luil::text_caret_position { 2, false });
    REQUIRE(memo.caret_at_point(7.0f + 5000.0f, row_middle(1), fixed_width) == luil::text_caret_position { 6, false });
}

TEST_CASE("Horizontal caret following keeps the caret inside the text padding", "[ui][text-area]")
{
    const std::u8string text { u8"012345678901234567890123456789" };
    const auto config_at = [](const float scroll_x) {
        auto config { scrolling_config() };
        config.wrap = false;
        config.scroll_horizontal = config.scroll;
        config.scroll_x = scroll_x;
        return config;
    };
    // 글 폭 300에 좌우 여백 7씩이라 내용 폭은 314다.
    const float viewport { memo_of(*wrapped_tree(view_of(text, 0), config_at(0.0f))).area().viewport().width };
    const float limit { 314.0f - viewport };
    REQUIRE(limit > 7.0f);
    // Home: 여백만큼 흘린 자리(7)에서 처음으로 돌아간다. 여백을 빼지 않고 들이면 7에 멈춘다.
    REQUIRE(scroll_in(memo_of(*wrapped_tree(view_of(text, 0), config_at(7.0f))).scroll_to_offset(0)) == -7.0f);
    // End: 글 끝이 자르는 끝에 딱 닿은 자리에서는 오른쪽 여백만큼 더 흘린다. 폭 0으로 들이면 caret이 자르는 끝 밖에 남는다.
    REQUIRE(scroll_in(memo_of(*wrapped_tree(view_of(text, 30), config_at(limit - 7.0f))).scroll_to_offset(30)) == 7.0f);
    // 끝까지 흘린 자리의 글 끝 caret은 이미 보인다. 더 흘릴 수 없는 delta를 내지 않는다.
    REQUIRE(scroll_in(memo_of(*wrapped_tree(view_of(text, 30), config_at(limit))).scroll_to_offset(30)).has_value() == false);
}

TEST_CASE("Unwrapped documents measure the longest line and expose horizontal scrolling", "[ui][text-area]")
{
    auto config { scrolling_config() };
    config.wrap = false;
    config.scroll_x = 30.0f;
    config.scroll_horizontal = config.scroll;
    config.font_size = 22.0f;
    const auto tree { wrapped_tree(view_of(u8"012345678901234567890123456789\nx", 0), config) };
    const auto& memo { memo_of(*tree) };
    REQUIRE(memo.lines().size() == 2u);
    REQUIRE(memo.content_width() == 314.0f);
    REQUIRE(memo.scroll() != nullptr);
    REQUIRE(memo.scroll()->axis == luil::scroll_axis::horizontal);
    REQUIRE(memo.area().metrics().content_height == 82.0f);
}

TEST_CASE("Document gutters stay outside text offsets and do not invalidate cached wrapping", "[ui][text-area][document]")
{
    auto config { scrolling_config() };
    config.gutters.push_back({ luil::gutter_kind::line_number, {}, 40.0f });
    config.wrap_cache = std::make_shared<luil::text_area_wrap_cache>();
    const auto tree { wrapped_tree(view_of(u8"first\nsecond", 0), config) };
    const auto& memo { memo_of(*tree) };
    REQUIRE(memo.line_rect(0)->x == 47.0f);
    REQUIRE(memo.line_rect(1)->y == 23.0f);
    REQUIRE(memo.line_rect(2).has_value() == false);
    REQUIRE(memo.offset_at(luil::zoom_point { 1.0f, 14.0f }) == 0u);
    REQUIRE(memo.text_input()->text == u8"first\nsecond");
    REQUIRE(config.wrap_cache->size() == 2u);
    config.spans = std::make_shared<const std::vector<luil::text_span>>(std::vector<luil::text_span> { { 0, 5, luil::text_color_role::accent } });
    static_cast<void>(wrapped_tree(view_of(u8"first\nsecond", 0), config));
    REQUIRE(config.wrap_cache->size() == 2u);
}
