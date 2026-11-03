#include "luil/ui/rich_text.h"

#include "luil/ui/app_message.h"
#include "luil/ui/rich_text_commands.h"
#include "luil/ui/text_area_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/undo_commands.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using luil::text::text_mark;

    constexpr luil::ui_element_kind kind_editor { luil::application_element_kind(0) };
    constexpr luil::text_input_target editor_target { 5 };

    struct format_intent
    {
        luil::rich_text_command command { luil::rich_text_command::bold };
    };

    // 칸이 키를 편집으로 썼다는 표시다. 서식 명령과 갈라 본다.
    struct edited_intent
    {
    };

    [[nodiscard]] luil::text::text_edit_state formatted(const std::u8string_view text, luil::text::text_format format)
    {
        luil::text::text_edit_state state {};
        luil::text::text_edit_set_formatted_text(state, text, std::make_shared<const luil::text::text_format>(std::move(format)));
        return state;
    }

    void select(luil::text::text_edit_state& state, const std::size_t anchor, const std::size_t caret)
    {
        luil::text::text_edit_place_caret(state, anchor, false);
        luil::text::text_edit_place_caret(state, caret, true);
    }

    [[nodiscard]] luil::key_pressed_event control(const luil::key_code key)
    {
        luil::key_pressed_event event { key };
        event.control = true;
        return event;
    }

    [[nodiscard]] const luil::app_command& command_with(const std::vector<luil::app_command>& commands, const std::u8string_view id)
    {
        const auto found { std::find_if(commands.begin(), commands.end(), [id](const luil::app_command& command) { return command.id == id; }) };
        REQUIRE(found != commands.end());
        return *found;
    }

    [[nodiscard]] luil::input_action format_action(const luil::rich_text_command command)
    {
        return luil::make_app_action(format_intent { command });
    }

    [[nodiscard]] std::optional<luil::rich_text_command> command_in(const std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1u)
            return std::nullopt;
        const auto* const message { std::get_if<luil::app_message>(&actions.front()) };
        const auto* const intent { message != nullptr ? message->get<format_intent>() : nullptr };
        if (intent == nullptr)
            return std::nullopt;
        return intent->command;
    }

    class editor_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            if (kind == kind_editor)
                return editor_target;
            return std::nullopt;
        }

        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request&) const override
        {
            return luil::input_action { luil::app_message { edited_intent {} } };
        }

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event& event, const luil::interaction_snapshot&) override
        {
            unhandled.push_back(event.key);
            return {};
        }

        std::vector<luil::key_code> unhandled {};
    };
}

TEST_CASE("The presenter hands back the same vectors while the format pointer stays the same", "[ui][rich-text]")
{
    luil::rich_text_presenter presenter {};
    luil::text::text_edit_state state {};
    presenter.update(state);
    REQUIRE(presenter.spans() == nullptr);
    REQUIRE(presenter.links() == nullptr);

    state = formatted(u8"bold link", { { { 0, 4, text_mark::bold | text_mark::underline }, { 5, 9, text_mark::code } }, { { 5, 9, u8"https://example.com/" } } });
    presenter.update(state);
    const auto spans { presenter.spans() };
    const auto links { presenter.links() };
    REQUIRE(spans != nullptr);
    REQUIRE(spans->size() == 2u);
    REQUIRE((*spans)[0].begin == 0u);
    REQUIRE((*spans)[0].end == 4u);
    REQUIRE((*spans)[0].bold);
    REQUIRE((*spans)[0].underline);
    REQUIRE((*spans)[0].italic == false);
    REQUIRE((*spans)[1].code);
    REQUIRE(links != nullptr);
    REQUIRE(*links == std::vector<luil::text_link> { { 5, 9, u8"https://example.com/" } });

    // 서식이 같은 포인터면 지난 vector를 그대로 준다. caret만 옮겨도 서식은 그대로다.
    luil::text::text_edit_place_caret(state, 2, false);
    presenter.update(state);
    REQUIRE(presenter.spans() == spans);
    REQUIRE(presenter.links() == links);

    // 편집이 서식을 바꾸면 새 vector다.
    luil::text::text_edit_insert(state, u8"x");
    presenter.update(state);
    REQUIRE(presenter.spans() != spans);
    REQUIRE(presenter.spans()->front().end == 5u);
}

TEST_CASE("A format command toggles the selection as one step or flips the typing marks", "[ui][rich-text]")
{
    luil::text::text_edit_state state { formatted(u8"abcdef", { { { 0, 3, text_mark::bold } }, {} }) };

    // 고른 구간이 모두 굵지 않으면 켠다. 한 걸음이다.
    select(state, 1, 5);
    REQUIRE(luil::rich_text_state_of(state).bold == false);
    REQUIRE(luil::apply_rich_text_command(state, luil::rich_text_command::bold));
    REQUIRE(state.format->runs == std::vector<luil::text::text_mark_run> { { 0, 5, text_mark::bold } });
    REQUIRE(state.undo.size() == 1u);
    REQUIRE(luil::rich_text_state_of(state).bold);
    // 모두 굵으면 끈다.
    REQUIRE(luil::apply_rich_text_command(state, luil::rich_text_command::bold));
    REQUIRE(state.format->runs == std::vector<luil::text::text_mark_run> { { 0, 1, text_mark::bold } });
    REQUIRE(state.undo.size() == 2u);

    // 코드는 다른 서식과 함께 걸리고, 서식 지우기는 고른 구간의 서식만 지운다.
    select(state, 0, 2);
    REQUIRE(luil::apply_rich_text_command(state, luil::rich_text_command::code));
    REQUIRE(state.format->runs == std::vector<luil::text::text_mark_run> { { 0, 1, text_mark::bold | text_mark::code }, { 1, 2, text_mark::code } });
    REQUIRE(luil::apply_rich_text_command(state, luil::rich_text_command::clear_format));
    REQUIRE(state.format->runs.empty());
    // 지울 것이 없으면 걸음이 아니다.
    const std::size_t steps { state.undo.size() };
    REQUIRE(luil::apply_rich_text_command(state, luil::rich_text_command::clear_format) == false);
    REQUIRE(state.undo.size() == steps);

    // 고른 구간이 없으면 다음 글자 서식만 뒤집는다. 걸음이 아니다.
    luil::text::text_edit_place_caret(state, 6, false);
    REQUIRE(luil::apply_rich_text_command(state, luil::rich_text_command::italic));
    REQUIRE(state.typing_marks == text_mark::italic);
    REQUIRE(state.undo.size() == steps);
    REQUIRE(luil::rich_text_state_of(state).italic);
    REQUIRE(luil::apply_rich_text_command(state, luil::rich_text_command::italic));
    REQUIRE(state.typing_marks == text_mark::none);
    REQUIRE(luil::rich_text_state_of(state).italic == false);

    // 링크·클립보드 명령은 이 함수의 몫이 아니다.
    for (const auto command : { luil::rich_text_command::link, luil::rich_text_command::copy, luil::rich_text_command::cut, luil::rich_text_command::paste, luil::rich_text_command::paste_plain })
        REQUIRE(luil::apply_rich_text_command(state, command) == false);
}

TEST_CASE("A link is set on the selection and removed from the link at the caret", "[ui][rich-text]")
{
    luil::text::text_edit_state state { formatted(u8"see the docs", {}) };
    luil::text::text_edit_place_caret(state, 3, false);
    // 고른 구간도 링크도 없으면 할 일이 없다.
    REQUIRE(luil::set_rich_text_link(state, u8"https://example.com/") == false);

    select(state, 8, 12);
    REQUIRE(luil::set_rich_text_link(state, u8"https://example.com/"));
    REQUIRE(state.format->links == std::vector<luil::text::text_format_link> { { 8, 12, u8"https://example.com/" } });
    REQUIRE(luil::rich_text_state_of(state).link);

    // 고른 구간 없이 링크 바로 뒤에서 빈 주소를 주면 그 링크를 걷는다.
    luil::text::text_edit_place_caret(state, 12, false);
    REQUIRE(luil::rich_text_state_of(state).link);
    REQUIRE(luil::set_rich_text_link(state, {}));
    REQUIRE(state.format->links.empty());
    REQUIRE(luil::rich_text_state_of(state).link == false);
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.format->links.size() == 1u);
}

TEST_CASE("The format commands carry ids, shortcuts, labels and checks", "[ui][rich-text][commands]")
{
    const std::vector<luil::app_command> plain { luil::make_rich_text_commands({}) };
    REQUIRE(plain.size() == 6u);
    for (const luil::app_command& command : plain)
    {
        REQUIRE(command.scope == luil::command_scope::focused);
        REQUIRE(command.enabled);
        REQUIRE(command.checked == false);
        REQUIRE(luil::rich_text_command_of(command.id).has_value());
    }
    REQUIRE(command_with(plain, luil::rich_text_bold_command_id).shortcut == luil::key_chord { .key = luil::key_code::key_b, .control = true });
    REQUIRE(command_with(plain, luil::rich_text_italic_command_id).shortcut == luil::key_chord { .key = luil::key_code::key_i, .control = true });
    REQUIRE(command_with(plain, luil::rich_text_underline_command_id).shortcut == luil::key_chord { .key = luil::key_code::key_u, .control = true });
    REQUIRE(command_with(plain, luil::rich_text_link_command_id).shortcut == luil::key_chord { .key = luil::key_code::key_k, .control = true });
    // 코드와 서식 지우기는 기본 단축키가 없다.
    REQUIRE(luil::effective_shortcuts(command_with(plain, luil::rich_text_code_command_id)).empty());
    REQUIRE(luil::effective_shortcuts(command_with(plain, luil::rich_text_clear_format_command_id)).empty());
    REQUIRE(command_with(plain, luil::rich_text_clear_format_command_id).checkable == false);
    // 앱이 이름을 주지 않으면 기본 문구다.
    REQUIRE(command_with(plain, luil::rich_text_bold_command_id).label == u8"굵게");
    REQUIRE(command_with(plain, luil::rich_text_clear_format_command_id).label == u8"서식 지우기");

    // 메뉴에 보이는 표는 편집 상태의 켜짐을 체크로 싣고 앱이 준 이름을 쓴다.
    const std::vector<luil::app_command> shown { luil::make_rich_text_commands({ .bold = true, .link = true }, { .bold = u8"Bold" }) };
    REQUIRE(command_with(shown, luil::rich_text_bold_command_id).checked);
    REQUIRE(command_with(shown, luil::rich_text_bold_command_id).label == u8"Bold");
    REQUIRE(command_with(shown, luil::rich_text_italic_command_id).checked == false);
    REQUIRE(command_with(shown, luil::rich_text_link_command_id).checked);
    REQUIRE(luil::rich_text_command_of(u8"edit.undo").has_value() == false);
    REQUIRE(luil::rich_text_command_of(luil::rich_text_code_command_id) == luil::rich_text_command::code);
}

TEST_CASE("Ctrl+B, I, U and K match the format commands without clashing with undo", "[ui][rich-text][commands]")
{
    const std::vector<luil::app_command> commands { luil::make_rich_text_commands({}) };
    REQUIRE(luil::match_command(commands, control(luil::key_code::key_b), luil::command_scope::focused) == std::u8string { luil::rich_text_bold_command_id });
    REQUIRE(luil::match_command(commands, control(luil::key_code::key_i), luil::command_scope::focused) == std::u8string { luil::rich_text_italic_command_id });
    REQUIRE(luil::match_command(commands, control(luil::key_code::key_u), luil::command_scope::focused) == std::u8string { luil::rich_text_underline_command_id });
    REQUIRE(luil::match_command(commands, control(luil::key_code::key_k), luil::command_scope::focused) == std::u8string { luil::rich_text_link_command_id });

    std::vector<luil::app_command> together { commands };
    for (luil::app_command& command : luil::make_undo_commands({}))
        together.push_back(std::move(command));
    REQUIRE(luil::find_conflicts(together).empty());

    // 처리기는 맞는 키에 앱 메시지 하나를 내고, 맞지 않는 키는 소비하지 않는다.
    const luil::key_handler handler { luil::make_rich_text_key_handler(commands, format_action) };
    REQUIRE(handler != nullptr);
    const auto bold { handler(control(luil::key_code::key_b)) };
    REQUIRE(bold.has_value());
    REQUIRE(command_in(*bold) == luil::rich_text_command::bold);
    REQUIRE(handler(control(luil::key_code::key_q)).has_value() == false);
    REQUIRE(handler(luil::key_pressed_event { luil::key_code::key_b }).has_value() == false);
    // 지을 메시지가 없으면 처리기도 없다.
    REQUIRE(luil::make_rich_text_key_handler(commands, {}) == nullptr);
}

TEST_CASE("Ctrl+B in a focused text area reaches the key handler on the area", "[ui][rich-text][interaction]")
{
    // 칸은 Ctrl+B를 쓰지 않아 8단계의 처리기에 닿는다. controller는 서식을 모른다.
    auto area { std::make_unique<luil::text_area_element>(luil::ui_element_id { kind_editor, u8"editor" }, luil::text_input_view { .text = u8"abc" }, luil::text_area_config {}) };
    area->set_key_handler(luil::make_rich_text_key_handler(luil::make_rich_text_commands({}), format_action));
    editor_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(area), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f)));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 50.0f, 14.0f, luil::pointer_button::left }));
    static_cast<void>(controller.process(luil::pointer_released_event { 50.0f, 14.0f, luil::pointer_button::left }));
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_editor, u8"editor" });

    REQUIRE(command_in(controller.process(control(luil::key_code::key_b))) == luil::rich_text_command::bold);
    REQUIRE(command_in(controller.process(control(luil::key_code::key_k))) == luil::rich_text_command::link);
    REQUIRE(policy.unhandled.empty());
    // 맞지 않는 Ctrl 조합은 처리기를 지나 앱 on_key로 흐른다.
    static_cast<void>(controller.process(control(luil::key_code::key_q)));
    REQUIRE(policy.unhandled == std::vector<luil::key_code> { luil::key_code::key_q });
}

TEST_CASE("A rich text copy request carries the selected text and its HTML", "[ui][rich-text][clipboard]")
{
    // "ab cd"의 "b c"를 고른다. 굵게는 [0, 2)라 조각의 첫 글자만 굵다.
    luil::text::text_format format {};
    format.runs = { { 0, 2, text_mark::bold } };
    format.links = { { 3, 5, u8"https://example.com/" } };
    luil::text::text_edit_state state { formatted(u8"ab cd", format) };
    select(state, 1, 4);
    const luil::clipboard_data_copy_request request { luil::rich_text_copy_request(state) };
    REQUIRE(request.text == u8"b c");
    REQUIRE(request.html == u8"<span style=\"white-space: pre-wrap;\"><b>b</b> <a href=\"https://example.com/\">c</a></span>");
    REQUIRE(request.image.valid() == false);
    REQUIRE(request.png.empty());
    const luil::rich_text_fragment back { luil::rich_text_from_html(request.html) };
    REQUIRE(back.text == u8"b c");
    REQUIRE(back.format.runs == std::vector<luil::text::text_mark_run> { { 0, 1, text_mark::bold } });
    REQUIRE(back.format.links == std::vector<luil::text::text_format_link> { { 2, 3, u8"https://example.com/" } });

    // 고른 구간이 없으면 빈 요청이다. 서식 없는 글도 HTML을 싣는다.
    luil::text::text_edit_place_caret(state, 2, false);
    REQUIRE(luil::rich_text_copy_request(state) == luil::clipboard_data_copy_request {});
    luil::text::text_edit_state plain {};
    luil::text::text_edit_set_text(plain, u8"x  y", false);
    luil::text::text_edit_select_all(plain);
    REQUIRE(luil::rich_text_copy_request(plain).html == u8"<span style=\"white-space: pre-wrap;\">x  y</span>");
}

TEST_CASE("Rich text paste inserts the filtered HTML, the plain text or nothing", "[ui][rich-text][clipboard]")
{
    luil::clipboard_data data {};
    data.text = u8"bold and link";
    data.html = u8"<b>bold</b> and <a href=\"https://example.com/\">link</a><script>x</script>";

    // HTML은 걸러 자기 서식째 넣는다. 한 걸음으로 되돌아간다.
    luil::text::text_edit_state state { formatted(u8"[]", {}) };
    luil::text::text_edit_place_caret(state, 1, false);
    REQUIRE(luil::rich_text_paste(state, data));
    REQUIRE(state.text == u8"[bold and link]");
    REQUIRE(state.caret == 14);
    REQUIRE(state.format != nullptr);
    REQUIRE(state.format->runs == std::vector<luil::text::text_mark_run> { { 1, 5, text_mark::bold } });
    REQUIRE(state.format->links == std::vector<luil::text::text_format_link> { { 10, 14, u8"https://example.com/" } });
    REQUIRE(luil::text::text_edit_undo(state));
    REQUIRE(state.text == u8"[]");
    REQUIRE((state.format == nullptr || *state.format == luil::text::text_format {}));

    // 서식 없이 붙이면 글을 넣는다. 넣은 글은 앞 글자의 서식을 이어받는다.
    luil::text::text_format bold {};
    bold.runs = { { 0, 1, text_mark::bold } };
    luil::text::text_edit_state typed { formatted(u8"B", bold) };
    luil::text::text_edit_place_caret(typed, 1, false);
    REQUIRE(luil::rich_text_paste(typed, data, true));
    REQUIRE(typed.text == u8"Bbold and link");
    REQUIRE(typed.format->runs == std::vector<luil::text::text_mark_run> { { 0, 14, text_mark::bold } });
    REQUIRE(typed.format->links.empty());
    REQUIRE(luil::text::text_edit_undo(typed));
    REQUIRE(typed.text == u8"B");

    // HTML이 없거나 거른 조각이 비면 글로 물러선다.
    luil::clipboard_data text_only {};
    text_only.text = u8"line\r\nnext";
    luil::text::text_edit_state fallback {};
    REQUIRE(luil::rich_text_paste(fallback, text_only));
    REQUIRE(fallback.text == u8"line\r\nnext");
    luil::clipboard_data broken { text_only };
    broken.html = std::u8string { u8"<b>" } + char8_t { 0xFF } + u8"</b>";
    luil::text::text_edit_state broken_state {};
    REQUIRE(luil::rich_text_paste(broken_state, broken));
    REQUIRE(broken_state.text == u8"line\r\nnext");
    REQUIRE((broken_state.format == nullptr || broken_state.format->runs.empty()));

    // 그림만 든 답은 아무것도 넣지 않는다. 그림을 감싼 HTML만 있어도 같다.
    luil::clipboard_data image {};
    image.image_format = luil::clipboard_image_format::encoded;
    image.image = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t> { 1, 2, 3 });
    image.html = u8"<img src=\"x.png\">";
    luil::text::text_edit_state untouched { formatted(u8"keep", {}) };
    REQUIRE(luil::rich_text_paste(untouched, image) == false);
    REQUIRE(untouched.text == u8"keep");
    REQUIRE(untouched.undo.empty());
    // 글과 그림이 함께 오면 글만 넣는다.
    image.text = u8"caption";
    REQUIRE(luil::rich_text_paste(untouched, image));
    REQUIRE(untouched.text == u8"captionkeep");
}

TEST_CASE("The rich text key handler maps the standard clipboard keys before the command table", "[ui][rich-text][clipboard]")
{
    const luil::key_handler handler { luil::make_rich_text_key_handler(luil::make_rich_text_commands({}), format_action) };
    REQUIRE(handler != nullptr);
    const auto command_of = [&handler](const luil::key_code key, const bool control, const bool shift) -> std::optional<luil::rich_text_command> {
        luil::key_pressed_event event { key };
        event.control = control;
        event.shift = shift;
        const auto actions { handler(event) };
        if (actions.has_value() == false)
            return std::nullopt;
        return command_in(*actions);
    };
    REQUIRE(command_of(luil::key_code::key_c, true, false) == luil::rich_text_command::copy);
    REQUIRE(command_of(luil::key_code::key_c, true, true) == luil::rich_text_command::copy);
    REQUIRE(command_of(luil::key_code::insert, true, false) == luil::rich_text_command::copy);
    REQUIRE(command_of(luil::key_code::key_x, true, false) == luil::rich_text_command::cut);
    REQUIRE(command_of(luil::key_code::key_x, true, true) == luil::rich_text_command::cut);
    REQUIRE(command_of(luil::key_code::delete_forward, false, true) == luil::rich_text_command::cut);
    REQUIRE(command_of(luil::key_code::key_v, true, false) == luil::rich_text_command::paste);
    REQUIRE(command_of(luil::key_code::insert, false, true) == luil::rich_text_command::paste);
    REQUIRE(command_of(luil::key_code::key_v, true, true) == luil::rich_text_command::paste_plain);
    // 수정자가 다르면 편집 키가 아니다. Ctrl+Delete는 낱말 지우기라 칸의 것이다.
    REQUIRE(command_of(luil::key_code::key_c, false, false).has_value() == false);
    REQUIRE(command_of(luil::key_code::delete_forward, true, true).has_value() == false);
    REQUIRE(command_of(luil::key_code::insert, true, true).has_value() == false);
    luil::key_pressed_event alt_copy { luil::key_code::key_c };
    alt_copy.control = true;
    alt_copy.alt = true;
    REQUIRE(handler(alt_copy).has_value() == false);
    // 명령표의 키는 그대로다.
    REQUIRE(command_of(luil::key_code::key_b, true, false) == luil::rich_text_command::bold);
}
