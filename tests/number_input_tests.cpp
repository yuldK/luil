#include "luil/ui/number_input.h"

#include "luil/ui/app_message.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 숫자 칸은 글을 읽고 재는 순수 함수와, 그것으로 증감·오류를 보이는 칸으로 나뉜다 (number-input-design.md).

namespace {
    using luil::number_input_problem;
    using luil::value_step;

    constexpr luil::ui_element_kind kind_number { luil::application_element_kind(0) };
    constexpr luil::text_input_target number_target { 7 };

    struct number_text_intent
    {
        std::u8string text {};
    };

    struct edit_intent
    {};

    [[nodiscard]] luil::number_input_range seconds_range()
    {
        return { .minimum = 0.0, .maximum = 3600.0, .step = 5.0 };
    }

    [[nodiscard]] std::optional<std::u8string> stepped(const std::u8string_view text, const luil::number_input_range& range, const value_step step)
    {
        return luil::stepped_number_input(text, range, step);
    }

    class number_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            return kind == kind_number ? std::optional<luil::text_input_target> { number_target } : std::nullopt;
        }

        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request&) const override
        {
            return luil::input_action { luil::app_message { edit_intent {} } };
        }

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            ++keys;
            return {};
        }

        int keys { 0 };
    };

    [[nodiscard]] luil::text_input_config number_config(const bool spin)
    {
        luil::text_input_config config {};
        config.number = luil::text_input_number {
            .range = seconds_range(),
            .change_to = [](std::u8string text) { return luil::input_action { luil::app_message { number_text_intent { std::move(text) } } }; },
            .spin_buttons = spin,
            .increase_tooltip = u8"늘리기",
            .decrease_tooltip = u8"줄이기",
        };
        return config;
    }

    [[nodiscard]] std::unique_ptr<luil::text_input_element> make_number_input(std::u8string text, luil::text_input_config config)
    {
        luil::text_input_view view {};
        view.caret = text.size();
        view.anchor = text.size();
        view.text = std::move(text);
        auto input { std::make_unique<luil::text_input_element>(luil::ui_element_id { kind_number, u8"seconds" }, std::move(view), std::move(config)) };
        input->arrange({ { 0.0f, 0.0f, 160.0f, 28.0f + luil::text_input_element::error_height_for(input->config()) }, 1.0f });
        return input;
    }

    [[nodiscard]] std::optional<std::u8string> stepped_text(const std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1)
            return std::nullopt;
        const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
        const auto* const intent { message != nullptr ? message->get<number_text_intent>() : nullptr };
        return intent != nullptr ? std::optional<std::u8string> { intent->text } : std::nullopt;
    }

    [[nodiscard]] bool is_edit(const std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1)
            return false;
        const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
        return message != nullptr && message->get<edit_intent>() != nullptr;
    }

    // 칸을 눌러 초점을 준 controller다.
    struct focused_number
    {
        number_policy policy {};
        luil::interaction_controller controller { &policy };

        explicit focused_number(std::unique_ptr<luil::text_input_element> input)
        {
            controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(input)));
            static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 14.0f, luil::pointer_button::left }));
            static_cast<void>(controller.process(luil::pointer_released_event { 10.0f, 14.0f, luil::pointer_button::left }));
        }

        [[nodiscard]] std::vector<luil::input_action> press(const luil::key_code key, const bool control = false, const bool shift = false)
        {
            return controller.process(luil::key_pressed_event { key, control, shift });
        }
    };
} // namespace

TEST_CASE("A number field reads ASCII, full-width and minus-sign digits", "[number][input]")
{
    const luil::number_input_range range { .minimum = -100.0, .maximum = 100.0, .step = 1.0 };
    REQUIRE(luil::check_number_input(u8"42", range) == luil::number_input_check { number_input_problem::none, 42.0 });
    // 붙여넣은 글의 앞뒤 공백·줄바꿈은 무시한다.
    REQUIRE(luil::check_number_input(u8"  42\r\n", range).value == 42.0);
    // IME가 전각 모드로 확정한 숫자와 문서에서 복사한 수학 빼기다.
    REQUIRE(luil::check_number_input(u8"４２", range).value == 42.0);
    REQUIRE(luil::check_number_input(u8"−12", range).value == -12.0);
    REQUIRE(luil::check_number_input(u8"－7", range).value == -7.0);
    REQUIRE(luil::check_number_input(u8"+3", range).value == 3.0);
    REQUIRE(luil::check_number_input(u8"", range).problem == number_input_problem::empty);
    REQUIRE(luil::check_number_input(u8"   ", range).problem == number_input_problem::empty);
}

TEST_CASE("A number field rejects grouping, stray characters and too many decimals", "[number][input]")
{
    const luil::number_input_range integers { .minimum = 0.0, .maximum = 10000.0 };
    // 천 단위 쉼표는 소수점 쉼표와 섞이면 값이 천 배가 되므로 받지 않는다.
    REQUIRE(luil::check_number_input(u8"1,200", integers).problem == number_input_problem::not_a_number);
    REQUIRE(luil::check_number_input(u8"12a", integers).problem == number_input_problem::not_a_number);
    REQUIRE(luil::check_number_input(u8"1 2", integers).problem == number_input_problem::not_a_number);
    REQUIRE(luil::check_number_input(u8"-", integers).problem == number_input_problem::not_a_number);
    REQUIRE(luil::check_number_input(u8"1..2", integers).problem == number_input_problem::not_a_number);
    // 정수 칸의 소수는 자릿수 초과다. 끝의 0은 자릿수로 세지 않는다.
    REQUIRE(luil::check_number_input(u8"1.5", integers).problem == number_input_problem::too_precise);
    REQUIRE(luil::check_number_input(u8"7.00", integers) == luil::number_input_check { number_input_problem::none, 7.0 });

    const luil::number_input_range tenths { .minimum = 0.0, .maximum = 10.0, .step = 0.1, .decimals = 1 };
    REQUIRE(luil::check_number_input(u8"1.50", tenths).value == 1.5);
    REQUIRE(luil::check_number_input(u8".5", tenths).value == 0.5);
    REQUIRE(luil::check_number_input(u8"2.", tenths).value == 2.0);
    REQUIRE(luil::check_number_input(u8"1.25", tenths).problem == number_input_problem::too_precise);
}

TEST_CASE("A number field reports out-of-range values with the value it read", "[number][input]")
{
    const luil::number_input_range range { seconds_range() };
    REQUIRE(luil::check_number_input(u8"3600", range).valid());
    REQUIRE(luil::check_number_input(u8"0", range).valid());
    REQUIRE(luil::check_number_input(u8"3601", range) == luil::number_input_check { number_input_problem::above_maximum, 3601.0 });
    REQUIRE(luil::check_number_input(u8"-1", range) == luil::number_input_check { number_input_problem::below_minimum, -1.0 });
    // 정수로 담을 수 없는 크기는 부호에 따라 범위 밖이다.
    REQUIRE(luil::check_number_input(u8"99999999999999999999", range).problem == number_input_problem::above_maximum);
    REQUIRE(luil::check_number_input(u8"-99999999999999999999", luil::number_input_range {}).problem == number_input_problem::below_minimum);
    // 범위가 없으면 담을 수 있는 크기 안에서는 모두 받는다.
    REQUIRE(luil::check_number_input(u8"-123456", luil::number_input_range {}).valid());
}

TEST_CASE("A number field steps on the grid counted from the minimum", "[number][input]")
{
    const luil::number_input_range range { seconds_range() };
    REQUIRE(stepped(u8"10", range, value_step::increase) == u8"15");
    REQUIRE(stepped(u8"10", range, value_step::decrease) == u8"5");
    // 눈금 사이에서는 가까운 위·아래 눈금으로 간다.
    REQUIRE(stepped(u8"12", range, value_step::increase) == u8"15");
    REQUIRE(stepped(u8"12", range, value_step::decrease) == u8"10");
    // 쪽 걸음은 정하지 않으면 걸음의 10배다.
    REQUIRE(stepped(u8"10", range, value_step::increase_page) == u8"60");
    REQUIRE(stepped(u8"100", range, value_step::decrease_page) == u8"50");
    // 범위로 접고, 끝에 닿은 걸음은 없다.
    REQUIRE(stepped(u8"3598", range, value_step::increase) == u8"3600");
    REQUIRE(stepped(u8"3600", range, value_step::increase).has_value() == false);
    REQUIRE(stepped(u8"0", range, value_step::decrease).has_value() == false);
    REQUIRE(stepped(u8"9000", range, value_step::increase) == u8"3600");
    REQUIRE(stepped(u8"-40", range, value_step::decrease) == u8"0");
    REQUIRE(stepped(u8"10", range, value_step::minimum) == u8"0");
    REQUIRE(stepped(u8"10", range, value_step::maximum) == u8"3600");
    // 비었거나 읽을 수 없는 글은 0을 범위로 접은 값이 된다.
    REQUIRE(stepped(u8"", range, value_step::increase) == u8"0");
    REQUIRE(stepped(u8"abc", luil::number_input_range { .minimum = 10.0, .maximum = 20.0 }, value_step::decrease) == u8"10");
    // 끝이 정해지지 않은 범위에는 Home·End의 자리가 없다.
    REQUIRE(stepped(u8"10", luil::number_input_range {}, value_step::maximum).has_value() == false);
    // 전각으로 적힌 글도 읽고 ASCII로 다시 쓴다.
    REQUIRE(stepped(u8"１０", range, value_step::increase) == u8"15");
}

TEST_CASE("A number field steps decimals exactly and writes the configured places", "[number][input]")
{
    const luil::number_input_range range { .minimum = -1.0, .maximum = 1.0, .step = 0.1, .decimals = 2 };
    // 0.1을 세 번 더해도 0.30000000000000004가 되지 않는다.
    std::u8string text { u8"0" };
    for (int index { 0 }; index < 3; ++index)
        text = *stepped(text, range, value_step::increase);
    REQUIRE(text == u8"0.30");
    REQUIRE(stepped(u8"0.05", range, value_step::increase) == u8"0.10");
    REQUIRE(stepped(u8"-0.05", range, value_step::decrease) == u8"-0.10");
    REQUIRE(stepped(u8"-0.9", range, value_step::decrease_page) == u8"-1.00");
    REQUIRE(luil::format_number_input(0.5, range) == u8"0.50");
    REQUIRE(luil::format_number_input(-3.0, luil::number_input_range {}) == u8"-3");
    REQUIRE(luil::format_number_input(0.004, range) == u8"0.00");
}

TEST_CASE("A number filter keeps only what a number of the range can contain", "[number][input]")
{
    // 붙여넣은 금액과 IME 전각 확정 조각이다.
    const luil::text_insert_filter positive_integers { luil::number_input_filter(seconds_range()) };
    REQUIRE(positive_integers(u8"1 200 원") == u8"1200");
    REQUIRE(positive_integers(u8"３０") == u8"30");
    REQUIRE(positive_integers(u8"-1.5") == u8"15");

    const luil::text_insert_filter signed_decimals { luil::number_input_filter({ .minimum = -10.0, .maximum = 10.0, .decimals = 1 }) };
    REQUIRE(signed_decimals(u8"−1.5") == u8"-1.5");
    REQUIRE(signed_decimals(u8"．5") == u8".5");
    // 조각만 보므로 이어 붙여 틀린 글은 검사가 잡는다.
    REQUIRE(signed_decimals(u8"1..2") == u8"1..2");
    REQUIRE(luil::check_number_input(u8"1..2", { .minimum = -10.0, .maximum = 10.0, .decimals = 1 }).problem == number_input_problem::not_a_number);

    // 편집 다리에 그대로 꽂힌다.
    luil::text::text_edit_state state {};
    luil::apply_text_edit(state, luil::text_edit_request { .target = number_target, .command = luil::text::text_edit_command::insert, .text = u8"12초" }, positive_integers);
    REQUIRE(state.text == u8"12");
}

TEST_CASE("A number field steps with up and down arrows and keeps left and right for the caret", "[number][input]")
{
    focused_number field { make_number_input(u8"10", number_config(false)) };
    REQUIRE(field.controller.snapshot().focused_input == luil::ui_element_id { kind_number, u8"seconds" });

    // ↑가 늘린다 — 막대·목록과 반대 방향이다.
    REQUIRE(stepped_text(field.press(luil::key_code::arrow_up)) == u8"15");
    REQUIRE(stepped_text(field.press(luil::key_code::arrow_down)) == u8"5");
    REQUIRE(stepped_text(field.press(luil::key_code::page_up)) == u8"60");
    REQUIRE(stepped_text(field.press(luil::key_code::page_down)) == u8"0");
    // ←→·Home/End는 caret 편집이다.
    REQUIRE(is_edit(field.press(luil::key_code::arrow_left)));
    REQUIRE(is_edit(field.press(luil::key_code::arrow_right)));
    REQUIRE(is_edit(field.press(luil::key_code::home)));
    REQUIRE(is_edit(field.press(luil::key_code::end)));
    // 수정자와 함께인 화살표는 앱 단축키다.
    static_cast<void>(field.press(luil::key_code::arrow_up, true));
    REQUIRE(field.policy.keys == 1);
}

TEST_CASE("A number field swallows steps at its end and while composing", "[number][input]")
{
    focused_number at_end { make_number_input(u8"3600", number_config(false)) };
    // 끝에 닿은 걸음은 삼킨다 — 바깥 스크롤로 새지 않는다.
    REQUIRE(at_end.press(luil::key_code::arrow_up).empty());
    REQUIRE(at_end.press(luil::key_code::page_up).empty());
    REQUIRE(at_end.policy.keys == 0);

    luil::text_input_view composing {};
    composing.text = u8"10";
    composing.composing = true;
    composing.composition_text = u8"10５";
    const luil::text_input_element input { { kind_number, u8"seconds" }, std::move(composing), number_config(false) };
    REQUIRE(input.key_step() != nullptr);
    REQUIRE(input.key_step()->up_increases);
    REQUIRE(input.key_step()->on_step(value_step::increase)->empty());
}

TEST_CASE("A plain text field keeps up and down away from number steps", "[number][input]")
{
    // 증감 메시지가 없으면 숫자 범위만으로는 걸음을 받지 않는다.
    luil::text_input_config config {};
    config.number = luil::text_input_number { .range = seconds_range() };
    const auto input { make_number_input(u8"10", std::move(config)) };
    REQUIRE(input->key_step() == nullptr);
    REQUIRE(input->children().empty());
}

TEST_CASE("Spin buttons step the number and stay out of the tab order", "[number][input]")
{
    const auto input { make_number_input(u8"10", number_config(true)) };
    REQUIRE(input->children().size() == 2u);
    const luil::ui_element& increase { *input->children()[0] };
    const luil::ui_element& decrease { *input->children()[1] };
    REQUIRE(increase.id() == luil::ui_element_id { luil::ui_element_kind::text_input_increase, u8"seconds" });
    REQUIRE(decrease.id() == luil::ui_element_id { luil::ui_element_kind::text_input_decrease, u8"seconds" });
    REQUIRE(increase.tab_stop() == false);
    REQUIRE(increase.tooltip() == u8"늘리기");
    // 위 단추가 위 반, 아래 단추가 아래 반이고 둘 다 오른쪽 안쪽이다.
    REQUIRE(increase.bounds().y < decrease.bounds().y);
    REQUIRE(increase.bounds().x + increase.bounds().width <= 160.0f);
    REQUIRE(input->hit_test(increase.bounds().x + 2.0f, increase.bounds().y + 2.0f) == &increase);

    const luil::ui_action* const up { increase.action(luil::ui_trigger::left_click) };
    const luil::ui_action* const down { decrease.action(luil::ui_trigger::left_click) };
    REQUIRE(stepped_text((*up)(luil::ui_action_context { increase.id() })) == u8"15");
    REQUIRE(stepped_text((*down)(luil::ui_action_context { decrease.id() })) == u8"5");
    // 글이 차지하는 폭에서 스핀 기둥을 뺀다.
    REQUIRE(luil::text_input_element::trailing_width_for(input->config()) == luil::text_input_spin_width + luil::text_input_icon_gap);
}

TEST_CASE("An error line sits under the box and the field reads as invalid", "[number][input][accessibility]")
{
    REQUIRE(luil::text_input_element::error_height_for(luil::text_input_config {}) == 0.0f);
    luil::text_input_config config { number_config(true) };
    config.error = u8"0에서 3600 사이로 적어 주세요";
    REQUIRE(luil::text_input_element::error_height_for(config) == luil::text_input_error_height);
    auto input { make_number_input(u8"9000", std::move(config)) };
    input->set_access_name(u8"제한 시간(초)");
    input->arrange({ { 0.0f, 0.0f, 160.0f, 28.0f + luil::text_input_error_height }, 1.0f });

    // 스핀 단추는 상자 안이다 — 오류 줄까지 내려가지 않는다.
    REQUIRE(input->children()[1]->bounds().y + input->children()[1]->bounds().height <= 28.0f);
    // IME 후보 창이 붙는 글 구간도 상자 높이다.
    const auto span { input->text_span_bounds({ u8"9000", 4u, 0u, 4u }, [](const std::u8string_view text, float) { return static_cast<float>(text.size()) * 7.0f; }) };
    REQUIRE(span.has_value());
    REQUIRE(span->height == 28.0f);

    const luil::access_info info { input->accessibility() };
    REQUIRE(info.role == luil::access_role::edit);
    REQUIRE(info.name == u8"제한 시간(초)");
    REQUIRE(info.value == u8"9000");
    REQUIRE(info.invalid);
    REQUIRE(info.error == u8"0에서 3600 사이로 적어 주세요");
    REQUIRE(input->invalid());
}

TEST_CASE("A field without an access name falls back to its placeholder", "[number][input][accessibility]")
{
    luil::text_input_config config {};
    config.placeholder = u8"초";
    config.invalid = true;
    const luil::text_input_element input { { kind_number, u8"seconds" }, luil::text_input_view {}, std::move(config) };
    const luil::access_info info { input.accessibility() };
    REQUIRE(info.name == u8"초");
    // 오류 글 없이 상태만 알릴 수도 있다.
    REQUIRE(info.invalid);
    REQUIRE(info.error.empty());
    REQUIRE(luil::text_input_element::error_height_for(input.config()) == 0.0f);
}

TEST_CASE("A number field asks for a number keyboard unless the app chose one", "[number][input]")
{
    const auto integers { make_number_input(u8"1", number_config(false)) };
    REQUIRE(integers->text_input()->options.kind == luil::text_input_kind::number);

    luil::text_input_config decimals { number_config(false) };
    decimals.number->range.decimals = 1;
    REQUIRE(make_number_input(u8"1", std::move(decimals))->text_input()->options.kind == luil::text_input_kind::decimal);

    luil::text_input_config phone { number_config(false) };
    phone.input.kind = luil::text_input_kind::phone;
    REQUIRE(make_number_input(u8"1", std::move(phone))->text_input()->options.kind == luil::text_input_kind::phone);
}
