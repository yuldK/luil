#include "luil/ui/message_box.h"

#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/text_block.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"
#include "ui/default_text.h"
#include "ui/message_box_element.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    // 코드포인트가 아니라 byte마다 글자 크기의 절반 폭이다 (12px 글이면 6px). 그래서 geometry를 보는 글은 ASCII로 쓴다.
    // 배율이 2여도 논리 폭은 같아 줄이 바뀌는 자리가 같다.
    [[nodiscard]] float half_size_width(const std::u8string_view text, const float pixel_size)
    {
        return static_cast<float>(text.size()) * pixel_size / 2.0f;
    }

    const luil::text_measurer half_size_measurer { half_size_width };

    struct result_intent
    {
        std::u8string key {};
    };

    struct toggle_intent
    {
    };

    struct flowed_intent
    {
    };

    // 눌림, Esc, 바깥이 낸 결과 key를 되읽는다.
    [[nodiscard]] luil::input_action make_result(const std::u8string_view key)
    {
        return luil::make_app_action(result_intent { std::u8string { key } });
    }

    [[nodiscard]] std::vector<std::u8string> result_keys(const std::vector<luil::input_action>& actions)
    {
        std::vector<std::u8string> keys {};
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr && message->get<result_intent>() != nullptr)
                keys.push_back(message->get<result_intent>()->key);
        return keys;
    }

    [[nodiscard]] std::vector<std::u8string> run_action(const luil::ui_action* const action)
    {
        REQUIRE(action != nullptr);
        return result_keys((*action)(luil::ui_action_context {}));
    }

    // 정책이 받은 키는 앱으로 흘러간 키다.
    class flowing_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            ++keys;
            return { luil::make_app_action(flowed_intent {}) };
        }

        int keys { 0 };
    };

    [[nodiscard]] float body_size()
    {
        return luil::measure_context {}.style_metrics().body_font_size;
    }

    [[nodiscard]] float title_line()
    {
        return luil::text_block_line_height(luil::text_block_style { .font = { .size = body_size() + 2.0f } });
    }

    [[nodiscard]] float body_line()
    {
        return luil::text_block_line_height(luil::text_block_style { .font = { .size = body_size() } });
    }

    constexpr float inner_padding { 20.0f };

    // 취소(거절)와 위험한 초기화다. 짧은 글 한 줄과 본문 한 줄이다.
    [[nodiscard]] luil::message_box_config sample()
    {
        luil::message_box_config config {};
        config.owner = u8"reset";
        config.title = u8"Reset?";
        config.body = u8"This clears the count.";
        config.buttons = { luil::message_box_cancel(u8"Cancel"), luil::message_box_button { .key = u8"reset", .text = u8"Reset", .role = luil::message_box_role::destructive } };
        config.result = make_result;
        return config;
    }

    // 취소(거절)와 저장(긍정)이다.
    [[nodiscard]] luil::message_box_config saving()
    {
        luil::message_box_config config { sample() };
        config.buttons = { luil::message_box_cancel(u8"Cancel"), luil::message_box_ok(u8"Save") };
        return config;
    }

    [[nodiscard]] luil::message_box_check ask_again()
    {
        return luil::message_box_check { .label = u8"Do not ask again", .checked = false, .toggle = luil::make_message_action(toggle_intent {}) };
    }

    [[nodiscard]] luil::ui_element_id box_id(const std::u8string_view owner = u8"reset")
    {
        return { luil::ui_element_kind::message_box, std::u8string { owner } };
    }

    // 측정기를 넣어 배치한 tree다. 자리와 배율은 물리 픽셀이다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> shown(luil::message_box_config config, const luil::rect_f& slot = { 0.0f, 0.0f, 800.0f, 600.0f }, const float scale = 1.0f)
    {
        std::unique_ptr<luil::modal_host_element> host { luil::make_message_box(std::move(config)) };
        host->arrange({ .slot = slot, .scale = scale, .measurer = &half_size_measurer });
        return std::make_shared<const luil::ui_tree>(std::move(host));
    }

    [[nodiscard]] const luil::ui_element& must_find(const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const found { tree.find(id) };
        REQUIRE(found != nullptr);
        return *found;
    }

    [[nodiscard]] luil::key_pressed_event key(const luil::key_code code)
    {
        return luil::key_pressed_event { code, false, false, false, false, std::chrono::steady_clock::time_point {} };
    }

    [[nodiscard]] luil::ui_element_id button(const std::u8string_view key_name)
    {
        return luil::message_box_button_id(u8"reset", key_name);
    }
}

TEST_CASE("The standard buttons carry their key, role, and a default text the app can overwrite", "[ui][message-box]")
{
    const luil::message_box_button ok { luil::message_box_ok() };
    REQUIRE(ok.key == luil::message_box_ok_key);
    REQUIRE(ok.role == luil::message_box_role::accept);
    REQUIRE(ok.text == luil::default_text::message_box_ok_label());

    const luil::message_box_button cancel { luil::message_box_cancel() };
    REQUIRE(cancel.key == luil::message_box_cancel_key);
    REQUIRE(cancel.role == luil::message_box_role::reject);
    REQUIRE(cancel.text == luil::default_text::message_box_cancel_label());

    const luil::message_box_button yes { luil::message_box_yes() };
    REQUIRE(yes.key == luil::message_box_yes_key);
    REQUIRE(yes.role == luil::message_box_role::accept);
    REQUIRE(yes.text == luil::default_text::message_box_yes_label());

    const luil::message_box_button no { luil::message_box_no() };
    REQUIRE(no.key == luil::message_box_no_key);
    REQUIRE(no.role == luil::message_box_role::reject);
    REQUIRE(no.text == luil::default_text::message_box_no_label());

    // 앱이 준 글이 기본 문구를 덮는다. key와 역할은 그대로다.
    const luil::message_box_button english { luil::message_box_ok(u8"Got it") };
    REQUIRE(english.text == u8"Got it");
    REQUIRE(english.key == luil::message_box_ok_key);
    REQUIRE(english.role == luil::message_box_role::accept);

    // 네 key는 서로 겹치지 않는다. 한 상자에 함께 놓을 수 있다.
    const std::array<std::u8string_view, 4> keys { luil::message_box_ok_key, luil::message_box_cancel_key, luil::message_box_yes_key, luil::message_box_no_key };
    for (std::size_t first = 0; first < keys.size(); ++first)
        for (std::size_t second = first + 1u; second < keys.size(); ++second)
            REQUIRE(keys[first] != keys[second]);
}

TEST_CASE("Button and check ids join the owner and the key with the unit separator", "[ui][message-box]")
{
    const luil::ui_element_id id { luil::message_box_button_id(u8"reset", u8"ok") };
    REQUIRE(id.kind == luil::ui_element_kind::message_box_button);
    REQUIRE(id.owner == u8"reset\u001fok");
    // 같은 key라도 상자가 다르면 다른 단추다.
    REQUIRE(luil::message_box_button_id(u8"other", u8"ok") != id);

    const luil::ui_element_id check { luil::message_box_check_id(u8"reset") };
    REQUIRE(check.kind == luil::ui_element_kind::check);
    REQUIRE(check.owner == u8"reset\u001fcheck");
}

TEST_CASE("A message box stands as a modal host with one content element, its check, and its buttons in order", "[ui][message-box]")
{
    luil::message_box_config config { saving() };
    config.check = ask_again();
    const std::shared_ptr<const luil::ui_tree> tree { shown(std::move(config)) };

    const luil::ui_element& host { *tree->root() };
    REQUIRE(host.id() == luil::ui_element_id { luil::ui_element_kind::modal_host, u8"reset" });
    REQUIRE(host.focus_trap());

    // 내용은 비공개 element 하나이고 단추와 칸이 그 자식이다. 칸이 먼저, 단추는 준 순서다.
    const luil::ui_element& content { must_find(*tree, box_id()) };
    REQUIRE(content.children().size() == 3u);
    REQUIRE(content.children()[0]->id() == luil::message_box_check_id(u8"reset"));
    REQUIRE(content.children()[1]->id() == button(u8"cancel"));
    REQUIRE(content.children()[2]->id() == button(u8"ok"));
    REQUIRE(tree->focus_order() == std::vector<luil::ui_element_id> { luil::message_box_check_id(u8"reset"), button(u8"cancel"), button(u8"ok") });

    // 단추는 손 모양 커서이고 누를 수 있다.
    for (const std::size_t index : { 1u, 2u })
    {
        REQUIRE(content.children()[index]->cursor() == luil::ui_cursor::hand);
        REQUIRE(content.children()[index]->action(luil::ui_trigger::left_click) != nullptr);
    }

    // 상자의 빈 자리를 눌러도 뒤의 scrim으로 새지 않는다.
    REQUIRE(content.hit_opaque());
}

TEST_CASE("Every way to close a message box posts the one result function with the key", "[ui][message-box]")
{
    const std::shared_ptr<const luil::ui_tree> tree { shown(saving()) };
    const luil::ui_element& host { *tree->root() };
    const luil::ui_element& scrim { must_find(*tree, luil::ui_element_id { luil::ui_element_kind::modal_scrim, u8"reset" }) };

    // 단추 눌림은 자기 key를 한 번 낸다.
    REQUIRE(run_action(must_find(*tree, button(u8"cancel")).action(luil::ui_trigger::left_click)) == std::vector<std::u8string> { u8"cancel" });
    REQUIRE(run_action(must_find(*tree, button(u8"ok")).action(luil::ui_trigger::left_click)) == std::vector<std::u8string> { u8"ok" });
    // Esc와 바깥 누름은 거절의 key를 낸다. 뒤로 가기용 helper도 같은 결과다.
    REQUIRE(run_action(host.dismiss_action()) == std::vector<std::u8string> { u8"cancel" });
    REQUIRE(run_action(scrim.action(luil::ui_trigger::left_click)) == std::vector<std::u8string> { u8"cancel" });
    const std::optional<luil::input_action> back { luil::message_box_dismiss_message(saving()) };
    REQUIRE(back.has_value());
    REQUIRE(result_keys({ *back }) == std::vector<std::u8string> { u8"cancel" });
}

TEST_CASE("Without a result function nothing posts, and a box that cannot dismiss lets Esc flow to the app", "[ui][message-box]")
{
    luil::message_box_config config { saving() };
    config.result = nullptr;
    const std::shared_ptr<const luil::ui_tree> tree { shown(config) };
    const luil::ui_element& host { *tree->root() };

    // 단추는 서 있지만 눌러도 메시지가 없다. Esc도 바깥도 닫는 길이 아니다.
    REQUIRE(run_action(must_find(*tree, button(u8"ok")).action(luil::ui_trigger::left_click)).empty());
    REQUIRE(host.dismiss_action() == nullptr);
    REQUIRE(must_find(*tree, luil::ui_element_id { luil::ui_element_kind::modal_scrim, u8"reset" }).action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(luil::message_box_dismiss_message(config).has_value() == false);

    // 거절도 단추 하나도 아니면 응할 단추가 없다.
    luil::message_box_config unclear { saving() };
    unclear.buttons = { luil::message_box_button { .key = u8"a", .text = u8"A" }, luil::message_box_button { .key = u8"b", .text = u8"B", .role = luil::message_box_role::accept } };
    REQUIRE(luil::message_box_dismiss_message(unclear).has_value() == false);
    REQUIRE(shown(unclear)->root()->dismiss_action() == nullptr);

    // 단추가 하나면 그 단추가 닫는 길이다 (거절이 아니어도).
    luil::message_box_config single { saving() };
    single.buttons = { luil::message_box_ok(u8"Got it") };
    REQUIRE(result_keys({ *luil::message_box_dismiss_message(single) }) == std::vector<std::u8string> { u8"ok" });
    REQUIRE(run_action(shown(single)->root()->dismiss_action()) == std::vector<std::u8string> { u8"ok" });

    // 거절이 여럿이면 마지막이다.
    luil::message_box_config two_rejects { saving() };
    two_rejects.buttons = { luil::message_box_no(), luil::message_box_cancel(), luil::message_box_yes() };
    REQUIRE(result_keys({ *luil::message_box_dismiss_message(two_rejects) }) == std::vector<std::u8string> { u8"cancel" });
}

TEST_CASE("Outside presses dismiss only when allowed, and are absorbed either way", "[ui][message-box]")
{
    luil::message_box_config config { saving() };
    config.outside_dismisses = false;
    const std::shared_ptr<const luil::ui_tree> tree { shown(config) };
    const luil::ui_element& scrim { must_find(*tree, luil::ui_element_id { luil::ui_element_kind::modal_scrim, u8"reset" }) };

    // 바깥 누름은 흡수만 한다. 그래도 Esc는 닫는다.
    REQUIRE(scrim.action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(scrim.hit_opaque());
    REQUIRE(tree->root()->dismiss_action() != nullptr);
}

TEST_CASE("Roles become the look and the default button of each button", "[ui][message-box]")
{
    luil::message_box_config config { sample() };
    config.buttons = {
        luil::message_box_button { .key = u8"a", .text = u8"First", .role = luil::message_box_role::accept },
        luil::message_box_button { .key = u8"n", .text = u8"Plain", .role = luil::message_box_role::normal },
        luil::message_box_button { .key = u8"r", .text = u8"Reject", .role = luil::message_box_role::reject },
        luil::message_box_button { .key = u8"d", .text = u8"Delete", .role = luil::message_box_role::destructive },
        luil::message_box_button { .key = u8"b", .text = u8"Second", .role = luil::message_box_role::accept },
    };
    const std::shared_ptr<const luil::ui_tree> tree { shown(config) };

    // 마지막 accept만 Enter의 대상이다. 채운 상자가 둘이면 어느 쪽이 Enter의 자리인지 사라진다.
    REQUIRE(must_find(*tree, button(u8"a")).default_button() == false);
    REQUIRE(must_find(*tree, button(u8"n")).default_button() == false);
    REQUIRE(must_find(*tree, button(u8"r")).default_button() == false);
    REQUIRE(must_find(*tree, button(u8"d")).default_button() == false);
    REQUIRE(must_find(*tree, button(u8"b")).default_button());
    REQUIRE(tree->default_button() != nullptr);
    REQUIRE(tree->default_button()->id() == button(u8"b"));

    // 모양은 순수 함수가 정한다. 마지막 accept만 강조색 채움이고 destructive는 기본이 아니라 오류색 바탕이다.
    const auto fill_of = [](const luil::message_box_role role, const bool is_default) {
        luil::text_button_config look {};
        look.visual = role == luil::message_box_role::accept ? luil::text_button_visual::accent : (role == luil::message_box_role::destructive ? luil::text_button_visual::danger : luil::text_button_visual::normal);
        look.default_button = is_default;
        return luil::text_button_fill_for(look, true);
    };
    REQUIRE(fill_of(luil::message_box_role::accept, true) == luil::text_button_fill::solid_accent);
    REQUIRE(fill_of(luil::message_box_role::accept, false) == luil::text_button_fill::soft_accent);
    REQUIRE(fill_of(luil::message_box_role::destructive, false) == luil::text_button_fill::soft_danger);

    // 역할 규칙은 단추가 없는 경우까지 순수하다.
    const std::array<luil::message_box_button, 0> none {};
    REQUIRE(luil::message_box_default_index(none).has_value() == false);
    REQUIRE(luil::message_box_entry_index(none).has_value() == false);
    REQUIRE(luil::message_box_dismiss_index(none).has_value() == false);
}

TEST_CASE("The first focus stands where Enter goes, and Enter presses the focused button", "[ui][message-box][focus]")
{
    flowing_policy policy {};
    luil::interaction_controller controller { &policy };
    const auto focused = [&controller] { return controller.snapshot().focused; };

    SECTION("긍정이 있으면 마지막 긍정에 서고 Enter가 그 단추를 누른다")
    {
        controller.set_tree(shown(saving()));
        REQUIRE(focused() == button(u8"ok"));
        const std::vector<luil::input_action> pressed { controller.process(key(luil::key_code::enter)) };
        REQUIRE(result_keys(pressed) == std::vector<std::u8string> { u8"ok" });
        REQUIRE(policy.keys == 0);
    }

    SECTION("위험한 단추만 있으면 거절에 선다 — 되돌릴 수 없는 일은 Enter의 대상이 아니다")
    {
        // 기본 버튼이 없으므로 Enter는 초점이 선 취소를 누른다. 초기화는 누르지 않는다.
        const std::shared_ptr<const luil::ui_tree> tree { shown(sample()) };
        REQUIRE(tree->default_button() == nullptr);
        controller.set_tree(tree);
        REQUIRE(focused() == button(u8"cancel"));
        const std::vector<luil::input_action> pressed { controller.process(key(luil::key_code::enter)) };
        REQUIRE(result_keys(pressed) == std::vector<std::u8string> { u8"cancel" });
    }

    SECTION("역할이 모두 보통이면 첫 단추에 선다")
    {
        luil::message_box_config config { sample() };
        config.buttons = { luil::message_box_button { .key = u8"left", .text = u8"Left" }, luil::message_box_button { .key = u8"right", .text = u8"Right" } };
        controller.set_tree(shown(config));
        REQUIRE(focused() == button(u8"left"));
        // 거절도 하나뿐인 단추도 아니므로 Esc는 앱으로 흐른다.
        static_cast<void>(controller.process(key(luil::key_code::escape)));
        REQUIRE(policy.keys == 1);
    }

    SECTION("테는 첫 Tab에서 선다")
    {
        controller.set_tree(shown(saving()));
        REQUIRE(controller.snapshot().focus_visible == false);
        static_cast<void>(controller.process(key(luil::key_code::tab)));
        REQUIRE(controller.snapshot().focus_visible);
    }
}

TEST_CASE("Esc closes a message box with the reject result, or flows to the app when nothing can close it", "[ui][message-box][focus]")
{
    flowing_policy policy {};
    luil::interaction_controller controller { &policy };

    controller.set_tree(shown(saving()));
    const std::vector<luil::input_action> escaped { controller.process(key(luil::key_code::escape)) };
    REQUIRE(result_keys(escaped) == std::vector<std::u8string> { u8"cancel" });
    // helper가 낸 결과와 같다. 앱이 뒤로 가기에 단 메시지가 Esc와 어긋나지 않는다.
    REQUIRE(result_keys({ *luil::message_box_dismiss_message(saving()) }) == result_keys(escaped));
    REQUIRE(policy.keys == 0);

    luil::message_box_config silent { saving() };
    silent.result = nullptr;
    controller.set_tree(shown(silent));
    static_cast<void>(controller.process(key(luil::key_code::escape)));
    REQUIRE(policy.keys == 1);
}

TEST_CASE("Tab stays inside the box and visits the check, then the buttons in the given order", "[ui][message-box][focus]")
{
    flowing_policy policy {};
    luil::interaction_controller controller { &policy };
    luil::message_box_config config { saving() };
    config.check = ask_again();
    controller.set_tree(shown(std::move(config)));
    const auto focused = [&controller] { return controller.snapshot().focused; };
    const luil::ui_element_id check { luil::message_box_check_id(u8"reset") };

    // 뜨면 저장에 서 있고 Tab은 칸으로 돌아 나간다.
    REQUIRE(focused() == button(u8"ok"));
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == check);
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == button(u8"cancel"));
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == button(u8"ok"));

    // 칸에 초점이 있으면 Enter는 칸을 켜고 끈다 (초점이 이긴다). 기본 버튼은 나서지 않는다.
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == check);
    const std::vector<luil::input_action> toggled { controller.process(key(luil::key_code::enter)) };
    REQUIRE(toggled.size() == 1u);
    REQUIRE(std::get<luil::app_message>(toggled[0]).get<toggle_intent>() != nullptr);
    REQUIRE(policy.keys == 0);
}

TEST_CASE("A message box is as tall as its title, body, and buttons need", "[ui][message-box][size]")
{
    const std::shared_ptr<const luil::ui_tree> tree { shown(sample()) };
    const luil::ui_element& content { must_find(*tree, box_id()) };

    // 20 + 머리(제목 + 8 + 본문) + 20 + 단추 + 20이다. 폭은 설정 그대로고 자리 가운데에 선다.
    const float expected { inner_padding + title_line() + 8.0f + body_line() + inner_padding + luil::text_button_height + inner_padding };
    REQUIRE(content.bounds().width == Catch::Approx(luil::message_box_default_width));
    REQUIRE(content.bounds().height == Catch::Approx(expected));
    REQUIRE(content.bounds().x == Catch::Approx((800.0f - luil::message_box_default_width) / 2.0f));
    REQUIRE(content.bounds().y == Catch::Approx((600.0f - expected) / 2.0f));

    SECTION("본문 줄이 늘면 높이가 그 줄 높이만큼 는다")
    {
        luil::message_box_config config { sample() };
        config.body = u8"line one\nline two\nline three";
        const std::shared_ptr<const luil::ui_tree> taller { shown(std::move(config)) };
        REQUIRE(must_find(*taller, box_id()).bounds().height == Catch::Approx(expected + 2.0f * body_line()));
    }

    SECTION("긴 줄은 상자 폭에서 바뀐다")
    {
        // 글 폭은 320이다. 6px 글자가 60자이면 두 줄이다.
        luil::message_box_config config { sample() };
        config.body = std::u8string(60, u8'x') + u8" " + std::u8string(10, u8'y');
        REQUIRE(must_find(*shown(std::move(config)), box_id()).bounds().height == Catch::Approx(expected + body_line()));
    }

    SECTION("제목이나 본문이 없으면 그 몫과 사이 간격이 빠진다")
    {
        luil::message_box_config titled { sample() };
        titled.body = {};
        REQUIRE(must_find(*shown(std::move(titled)), box_id()).bounds().height == Catch::Approx(inner_padding + title_line() + inner_padding + luil::text_button_height + inner_padding));

        luil::message_box_config bodied { sample() };
        bodied.title = {};
        REQUIRE(must_find(*shown(std::move(bodied)), box_id()).bounds().height == Catch::Approx(inner_padding + body_line() + inner_padding + luil::text_button_height + inner_padding));
    }

    SECTION("확인 칸은 본문과 12 떨어지고 높이를 더한다")
    {
        luil::message_box_config config { sample() };
        config.check = ask_again();
        const std::shared_ptr<const luil::ui_tree> checked { shown(std::move(config)) };
        const luil::ui_element& box { must_find(*checked, box_id()) };
        const float check_height { 24.0f };
        REQUIRE(box.bounds().height == Catch::Approx(expected + 12.0f + check_height));
        const luil::ui_element& check { must_find(*checked, luil::message_box_check_id(u8"reset")) };
        REQUIRE(check.bounds().y == Catch::Approx(box.bounds().y + inner_padding + title_line() + 8.0f + body_line() + 12.0f));
        REQUIRE(check.bounds().x == Catch::Approx(box.bounds().x + inner_padding));
        REQUIRE(check.bounds().height == Catch::Approx(check_height));
    }

    SECTION("단추 높이는 control_size를 따르고 확인 칸도 그만큼 키운다")
    {
        luil::message_box_config config { sample() };
        config.control_size = 44.0f;
        config.check = ask_again();
        const std::shared_ptr<const luil::ui_tree> touch { shown(std::move(config)) };
        const luil::ui_element& box { must_find(*touch, box_id()) };
        REQUIRE(box.bounds().height == Catch::Approx(expected - luil::text_button_height + 44.0f + 12.0f + 44.0f));
        REQUIRE(must_find(*touch, button(u8"reset")).bounds().height == Catch::Approx(44.0f));
        // 칸 자신은 24이고 44의 줄 가운데에 선다.
        const luil::ui_element& check { must_find(*touch, luil::message_box_check_id(u8"reset")) };
        REQUIRE(check.bounds().height == Catch::Approx(24.0f));
        REQUIRE(check.bounds().y == Catch::Approx(box.bounds().y + inner_padding + title_line() + 8.0f + body_line() + 12.0f + 10.0f));
    }

    SECTION("글자 크기를 키우면 높이가 따라온다")
    {
        luil::ui_metrics metrics { luil::default_ui_style().metrics };
        metrics.body_font_size += 4.0f;
        std::unique_ptr<luil::modal_host_element> host { luil::make_message_box(sample()) };
        host->arrange({ .slot = { 0.0f, 0.0f, 800.0f, 600.0f }, .scale = 1.0f, .measurer = &half_size_measurer, .metrics = &metrics });
        const luil::ui_tree large { std::move(host) };
        const float larger_title { luil::text_block_line_height(luil::text_block_style { .font = { .size = metrics.body_font_size + 2.0f } }) };
        const float larger_body { luil::text_block_line_height(luil::text_block_style { .font = { .size = metrics.body_font_size } }) };
        const float bigger { inner_padding + larger_title + 8.0f + larger_body + inner_padding + luil::text_button_height + inner_padding };
        REQUIRE(must_find(large, box_id()).bounds().height == Catch::Approx(bigger));
        REQUIRE(bigger > expected);
    }
}

TEST_CASE("A message box narrows to the room and keeps its margin on a small surface", "[ui][message-box][size]")
{
    // 자리 폭 300에서 여백 16을 양쪽에 남기면 268이다. 폭 360의 설정을 따르지 않는다.
    const std::shared_ptr<const luil::ui_tree> narrow { shown(sample(), { 0.0f, 0.0f, 300.0f, 600.0f }) };
    const luil::ui_element& content { must_find(*narrow, box_id()) };
    REQUIRE(content.bounds().width == Catch::Approx(300.0f - 2.0f * luil::message_box_screen_margin));
    REQUIRE(content.bounds().x == Catch::Approx(luil::message_box_screen_margin));

    // 폭이 좁아지면 글 폭도 좁아져 줄이 는다. 글 폭은 268 - 40 = 228이다. 6px 글자가 40자이면 240이라 두 줄이다.
    luil::message_box_config config { sample() };
    config.body = std::u8string(25, u8'a') + u8" " + std::u8string(14, u8'b');
    const float one_line { must_find(*shown(config), box_id()).bounds().height };
    const float two_lines { must_find(*shown(config, { 0.0f, 0.0f, 300.0f, 600.0f }), box_id()).bounds().height };
    REQUIRE(two_lines == Catch::Approx(one_line + body_line()));

    // 폭이 설정보다 좁은 상자는 설정 폭을 넘지 않는다.
    luil::message_box_config slim { sample() };
    slim.width = 200.0f;
    REQUIRE(must_find(*shown(std::move(slim)), box_id()).bounds().width == Catch::Approx(200.0f));
}

TEST_CASE("A severity adds a glyph cell that narrows the text and names the host", "[ui][message-box][size]")
{
    // 글리프 칸은 24 + 12다. 글 폭이 320에서 284가 된다. 6px 글자가 50자이면 300이라 한 줄에서 두 줄이 된다.
    luil::message_box_config plain { sample() };
    plain.body = std::u8string(25, u8'a') + u8" " + std::u8string(24, u8'b');
    luil::message_box_config marked { plain };
    marked.severity = luil::ui_severity::warning;
    const float without_glyph { must_find(*shown(plain), box_id()).bounds().height };
    const float with_glyph { must_find(*shown(marked), box_id()).bounds().height };
    REQUIRE(with_glyph == Catch::Approx(without_glyph + body_line()));

    // 머리는 글리프 칸(24)과 글 높이 중 큰 쪽이다. 글이 한 줄 제목뿐이어도 글리프 칸보다 낮아지지 않는다.
    luil::message_box_config bare { sample() };
    bare.body = {};
    bare.severity = luil::ui_severity::info;
    const float head { std::max(24.0f, title_line()) };
    REQUIRE(must_find(*shown(std::move(bare)), box_id()).bounds().height == Catch::Approx(inner_padding + head + inner_padding + luil::text_button_height + inner_padding));
}

TEST_CASE("Buttons sit in one row at the right edge until their widths no longer fit, then stack at full width", "[ui][message-box][size]")
{
    // 경계는 순수 함수가 정한다. 합이 안쪽 폭과 같으면 한 행이다.
    REQUIRE(luil::message_box_stacks_buttons(std::array<float, 2> { 160.0f, 152.0f }, 320.0f) == false);
    REQUIRE(luil::message_box_stacks_buttons(std::array<float, 2> { 160.5f, 152.0f }, 320.0f));
    REQUIRE(luil::message_box_stacks_buttons(std::array<float, 1> { 320.0f }, 320.0f) == false);
    REQUIRE(luil::message_box_stacks_buttons(std::array<float, 1> { 321.0f }, 320.0f));
    REQUIRE(luil::message_box_stacks_buttons(std::span<const float> {}, 0.0f) == false);

    // 글 22자는 단추 폭 156(= 6 * 22 + 24)이다. 둘이면 156 + 156 + 8 = 320이 안쪽 폭과 같다.
    luil::message_box_config config { sample() };
    config.buttons = { luil::message_box_button { .key = u8"a", .text = std::u8string(22, u8'A') }, luil::message_box_button { .key = u8"b", .text = std::u8string(22, u8'B') } };
    const std::shared_ptr<const luil::ui_tree> row { shown(config) };
    const luil::ui_element& row_box { must_find(*row, box_id()) };
    const luil::ui_element& row_first { must_find(*row, button(u8"a")) };
    const luil::ui_element& row_second { must_find(*row, button(u8"b")) };
    REQUIRE(row_first.bounds().width == Catch::Approx(156.0f));
    REQUIRE(row_second.bounds().width == Catch::Approx(156.0f));
    REQUIRE(row_first.bounds().y == Catch::Approx(row_second.bounds().y));
    REQUIRE(row_first.bounds().x == Catch::Approx(row_box.bounds().x + inner_padding));
    REQUIRE(row_second.bounds().x == Catch::Approx(row_first.bounds().x + 156.0f + 8.0f));
    // 오른쪽 끝에 붙는다.
    REQUIRE(row_second.bounds().x + row_second.bounds().width == Catch::Approx(row_box.bounds().x + row_box.bounds().width - inner_padding));

    // 한 글자를 더하면 합이 넘쳐 쌓인다. 순서는 그대로 위에서 아래이고 모두 안쪽 폭 전체다.
    config.buttons[1].text = std::u8string(23, u8'B');
    const std::shared_ptr<const luil::ui_tree> stacked { shown(config) };
    const luil::ui_element& box { must_find(*stacked, box_id()) };
    const luil::ui_element& first { must_find(*stacked, button(u8"a")) };
    const luil::ui_element& second { must_find(*stacked, button(u8"b")) };
    REQUIRE(first.bounds().width == Catch::Approx(320.0f));
    REQUIRE(second.bounds().width == Catch::Approx(320.0f));
    REQUIRE(first.bounds().x == Catch::Approx(box.bounds().x + inner_padding));
    REQUIRE(second.bounds().x == Catch::Approx(box.bounds().x + inner_padding));
    REQUIRE(second.bounds().y == Catch::Approx(first.bounds().y + luil::text_button_height + 8.0f));
    REQUIRE(box.bounds().height == Catch::Approx(row_box.bounds().height + luil::text_button_height + 8.0f));
    // 맨 아래 단추 아래가 안쪽 여백이다.
    REQUIRE(second.bounds().y + second.bounds().height == Catch::Approx(box.bounds().y + box.bounds().height - inner_padding));
}

TEST_CASE("Short buttons are as wide as the minimum, long ones as wide as their text", "[ui][message-box][size]")
{
    const std::shared_ptr<const luil::ui_tree> tree { shown(sample()) };
    // "Cancel"과 "Reset"은 단추 최소 폭(64)보다 좁은 글이라 88이다.
    REQUIRE(must_find(*tree, button(u8"cancel")).bounds().width == Catch::Approx(luil::message_box_button_minimum_width));
    REQUIRE(must_find(*tree, button(u8"reset")).bounds().width == Catch::Approx(luil::message_box_button_minimum_width));

    luil::message_box_config config { sample() };
    config.buttons = { luil::message_box_button { .key = u8"long", .text = std::u8string(20, u8'L') } };
    // 6 * 20 + 24 = 144다.
    REQUIRE(must_find(*shown(std::move(config)), button(u8"long")).bounds().width == Catch::Approx(144.0f));
}

TEST_CASE("A short surface trims the body lines and keeps the title, check, and buttons", "[ui][message-box][size]")
{
    luil::message_box_config config { sample() };
    config.body = u8"1\n2\n3\n4\n5\n6\n7\n8\n9\n10";
    const std::shared_ptr<const luil::ui_tree> natural { shown(config) };
    const float full { must_find(*natural, box_id()).bounds().height };
    REQUIRE(full == Catch::Approx(inner_padding + title_line() + 8.0f + 10.0f * body_line() + inner_padding + luil::text_button_height + inner_padding));

    // 호스트가 높이를 자리 - 32로 자른다. 본문 3.5줄이 모자라게 자리를 주면 4줄이 빠져 6줄이 남는다.
    const float room { full - 3.5f * body_line() };
    const std::shared_ptr<const luil::ui_tree> trimmed { shown(config, { 0.0f, 0.0f, 800.0f, room + 2.0f * luil::message_box_screen_margin }) };
    const luil::ui_element& box { must_find(*trimmed, box_id()) };
    REQUIRE(box.bounds().height == Catch::Approx(room));
    // 단추는 상자 안에 서고, 위치는 본문 6줄 아래다.
    const luil::ui_element& cancel { must_find(*trimmed, button(u8"cancel")) };
    REQUIRE(cancel.bounds().y == Catch::Approx(box.bounds().y + inner_padding + title_line() + 8.0f + 6.0f * body_line() + inner_padding));
    REQUIRE(cancel.bounds().y + cancel.bounds().height == Catch::Approx(box.bounds().y + (full - 4.0f * body_line()) - inner_padding));
    REQUIRE(cancel.bounds().y + cancel.bounds().height <= box.bounds().y + box.bounds().height);
    // 접근 이름은 줄인 것과 상관없이 전체 본문이다.
    REQUIRE(box.accessibility().name == config.body);

    // 한 줄로도 모자라면 아래가 넘쳐 그려진다. 단추는 줄이지 않는다.
    const std::shared_ptr<const luil::ui_tree> tiny { shown(config, { 0.0f, 0.0f, 800.0f, 100.0f }) };
    const luil::ui_element& small_box { must_find(*tiny, box_id()) };
    REQUIRE(small_box.bounds().height == Catch::Approx(100.0f - 2.0f * luil::message_box_screen_margin));
    const luil::ui_element& overflowing { must_find(*tiny, button(u8"cancel")) };
    REQUIRE(overflowing.bounds().y == Catch::Approx(small_box.bounds().y + inner_padding + title_line() + 8.0f + body_line() + inner_padding));
    REQUIRE(overflowing.bounds().y + overflowing.bounds().height > small_box.bounds().y + small_box.bounds().height);
    REQUIRE(overflowing.bounds().height == Catch::Approx(luil::text_button_height));
}

TEST_CASE("A message box at scale 2 lays out the same logical sizes in physical pixels", "[ui][message-box][size]")
{
    luil::message_box_config config { sample() };
    config.severity = luil::ui_severity::error;
    config.check = ask_again();
    const std::shared_ptr<const luil::ui_tree> one { shown(config, { 0.0f, 0.0f, 800.0f, 600.0f }, 1.0f) };
    const std::shared_ptr<const luil::ui_tree> two { shown(config, { 0.0f, 0.0f, 1600.0f, 1200.0f }, 2.0f) };

    const std::array<luil::ui_element_id, 4> ids { box_id(), luil::message_box_check_id(u8"reset"), button(u8"cancel"), button(u8"reset") };
    for (const luil::ui_element_id& id : ids)
    {
        const luil::rect_f small { must_find(*one, id).bounds() };
        const luil::rect_f large { must_find(*two, id).bounds() };
        REQUIRE(large.x == Catch::Approx(small.x * 2.0f));
        REQUIRE(large.y == Catch::Approx(small.y * 2.0f));
        REQUIRE(large.width == Catch::Approx(small.width * 2.0f));
        REQUIRE(large.height == Catch::Approx(small.height * 2.0f));
    }
}

TEST_CASE("A message box tells the screen reader its host name, body, buttons, and check", "[ui][message-box][access]")
{
    luil::message_box_config config { saving() };
    config.check = ask_again();
    config.severity = luil::ui_severity::warning;
    const std::shared_ptr<const luil::ui_tree> tree { shown(config) };

    // host는 dialog이고 이름은 심각도 낱말과 제목이다. 화면 읽기가 뜨는 순간 말하는 이름이다.
    const luil::access_info host { tree->root()->accessibility() };
    REQUIRE(host.role == luil::access_role::dialog);
    REQUIRE(host.name == std::u8string { luil::default_text::severity_name(luil::ui_severity::warning) } + u8" Reset?");

    // 본문은 static_text이고 이름이 전체 본문이다. 단추와 칸이 그 아래 있다.
    const luil::access_info body { must_find(*tree, box_id()).accessibility() };
    REQUIRE(body.role == luil::access_role::static_text);
    REQUIRE(body.name == u8"This clears the count.");
    const luil::access_info save { must_find(*tree, button(u8"ok")).accessibility() };
    REQUIRE(save.role == luil::access_role::button);
    REQUIRE(save.name == u8"Save");
    const luil::access_info check { must_find(*tree, luil::message_box_check_id(u8"reset")).accessibility() };
    REQUIRE(check.role == luil::access_role::check_box);
    REQUIRE(check.name == u8"Do not ask again");

    // 앱이 준 심각도 낱말이 기본 문구를 덮는다.
    config.severity_name = u8"Warning";
    REQUIRE(shown(config)->root()->accessibility().name == u8"Warning Reset?");
    // 심각도가 없으면 제목뿐이다.
    config.severity.reset();
    REQUIRE(shown(config)->root()->accessibility().name == u8"Reset?");
    // 본문이 없으면 내용은 구조다.
    config.body = {};
    const std::shared_ptr<const luil::ui_tree> bodiless { shown(config) };
    REQUIRE(must_find(*bodiless, box_id()).accessibility().role == luil::access_role::none);
}

TEST_CASE("A message box hands the host its entry, return slot, and fit settings", "[ui][message-box]")
{
    luil::message_box_config config { saving() };
    config.focus_return = luil::ui_element_id { luil::application_element_kind(0), u8"open" };
    const std::unique_ptr<luil::modal_host_element> host { luil::make_message_box(config) };
    REQUIRE(host->id() == luil::ui_element_id { luil::ui_element_kind::modal_host, u8"reset" });
    // 뜨는 순간 초점은 마지막 긍정이고 닫히면 연 자리로 돌아간다.
    REQUIRE(host->focus_entry() == button(u8"ok"));
    REQUIRE(host->focus_return() == config.focus_return);
    // 크기는 내용에서 나온다. host는 둘레에 16의 여백을 남긴다.
    REQUIRE(host->children().size() == 2u);

    // 폭이 잘못된 값이면 기본 폭이다.
    luil::message_box_config broken { saving() };
    broken.width = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(must_find(*shown(std::move(broken)), box_id()).bounds().width == Catch::Approx(luil::message_box_default_width));
}
