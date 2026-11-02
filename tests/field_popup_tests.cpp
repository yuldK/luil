#include "luil/ui/field_popup.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <optional>
#include <variant>
#include <vector>

namespace {
    struct open_intent
    {
        bool open { false };
    };

    // 필드는 (100, 50) 80x30이고 창은 800x600이다 (물리 픽셀, 배율 1).
    [[nodiscard]] luil::field_popup_anchor anchor_in_window()
    {
        return { .field = { 100.0f, 50.0f, 80.0f, 30.0f }, .limit = { 0.0f, 0.0f, 800.0f, 600.0f } };
    }

    [[nodiscard]] luil::key_pressed_event key(const luil::key_code code)
    {
        luil::key_pressed_event event {};
        event.key = code;
        return event;
    }

    [[nodiscard]] luil::key_pressed_event alt_down()
    {
        luil::key_pressed_event event { key(luil::key_code::arrow_down) };
        event.alt = true;
        return event;
    }

    [[nodiscard]] luil::input_action open_action(const bool open)
    {
        return luil::make_app_action(open_intent { open });
    }

    // 처리기가 낸 메시지의 열림 목표다. 메시지가 아니거나 개수가 맞지 않으면 비어 있다.
    [[nodiscard]] std::optional<bool> opened_to(const std::optional<std::vector<luil::input_action>>& answer)
    {
        if (answer.has_value() == false || answer->size() != 1u)
            return std::nullopt;
        const auto* const message { std::get_if<luil::app_message>(&(*answer)[0]) };
        if (message == nullptr || message->get<open_intent>() == nullptr)
            return std::nullopt;
        return message->get<open_intent>()->open;
    }
}

TEST_CASE("A field popup sits under the field by the gap and aligns to its left edge", "[ui][field-popup]")
{
    const luil::field_popup_position position { luil::place_field_popup(anchor_in_window(), { 120.0f, 100.0f }, 1.0f) };
    REQUIRE(position.x == 100.0f);
    // 필드 아래(50 + 30)에서 기본 간격 2다.
    REQUIRE(position.y == 82.0f);
    REQUIRE(position.above == false);
}

TEST_CASE("A field popup flips above only when below overflows and above fits", "[ui][field-popup]")
{
    luil::field_popup_anchor anchor { anchor_in_window() };
    anchor.field = { 100.0f, 550.0f, 80.0f, 30.0f };

    // 아래(582 + 100)가 창 아래(600)를 넘고 위(548 - 100)는 들어간다.
    const luil::field_popup_position flipped { luil::place_field_popup(anchor, { 120.0f, 100.0f }, 1.0f) };
    REQUIRE(flipped.above);
    REQUIRE(flipped.y == 448.0f);

    // 아래에 딱 들어가면 뒤집지 않는다. 경계는 포함이다.
    const luil::field_popup_position fits { luil::place_field_popup(anchor, { 120.0f, 18.0f }, 1.0f) };
    REQUIRE(fits.above == false);
    REQUIRE(fits.y == 582.0f);

    // 위도 안 들어가면 아래에 둔다. 나머지는 host가 민다.
    anchor.field = { 100.0f, 300.0f, 80.0f, 30.0f };
    const luil::field_popup_position neither { luil::place_field_popup(anchor, { 120.0f, 700.0f }, 1.0f) };
    REQUIRE(neither.above == false);
    REQUIRE(neither.y == 332.0f);
}

TEST_CASE("A field popup aligns to the end and is pushed inside the limit", "[ui][field-popup]")
{
    luil::field_popup_anchor anchor { anchor_in_window() };
    anchor.alignment = luil::field_popup_alignment::end;
    // popup 오른쪽(180)이 필드 오른쪽에 맞는다.
    REQUIRE(luil::place_field_popup(anchor, { 120.0f, 100.0f }, 1.0f).x == 60.0f);

    // 왼쪽으로 삐져나가면 limit의 왼쪽으로 민다.
    anchor.field = { 10.0f, 50.0f, 40.0f, 30.0f };
    REQUIRE(luil::place_field_popup(anchor, { 200.0f, 100.0f }, 1.0f).x == 0.0f);

    // 오른쪽으로 삐져나가면 limit의 오른쪽 끝에 붙는다.
    anchor.alignment = luil::field_popup_alignment::start;
    anchor.field = { 700.0f, 50.0f, 80.0f, 30.0f };
    REQUIRE(luil::place_field_popup(anchor, { 200.0f, 100.0f }, 1.0f).x == 600.0f);

    // limit보다 넓은 popup은 왼쪽 가장자리를 지킨다.
    anchor.limit = { 20.0f, 0.0f, 800.0f, 600.0f };
    REQUIRE(luil::place_field_popup(anchor, { 900.0f, 100.0f }, 1.0f).x == 20.0f);
}

TEST_CASE("A field popup reads the field in physical pixels and answers in logical ones", "[ui][field-popup]")
{
    // 배율 2에서 논리 (100, 50) 80x30인 필드다. limit은 1600x1200이고 간격은 논리 2다.
    luil::field_popup_anchor anchor {};
    anchor.field = { 200.0f, 100.0f, 160.0f, 60.0f };
    anchor.limit = { 0.0f, 0.0f, 1600.0f, 1200.0f };

    const luil::field_popup_position below { luil::place_field_popup(anchor, { 120.0f, 100.0f }, 2.0f) };
    REQUIRE(below.x == 100.0f);
    REQUIRE(below.y == 82.0f);
    REQUIRE(below.above == false);

    // 뒤집을 때도 popup 높이와 간격을 물리로 바꿔 견주고 논리로 돌려준다.
    anchor.field = { 200.0f, 1100.0f, 160.0f, 60.0f };
    const luil::field_popup_position above { luil::place_field_popup(anchor, { 120.0f, 100.0f }, 2.0f) };
    REQUIRE(above.above);
    REQUIRE(above.y == 448.0f);

    anchor.alignment = luil::field_popup_alignment::end;
    REQUIRE(luil::place_field_popup(anchor, { 120.0f, 100.0f }, 2.0f).x == 60.0f);

    // limit의 원점이 0이 아니어도 그 안에서만 움직인다. 원점은 물리 좌표다.
    anchor.alignment = luil::field_popup_alignment::start;
    anchor.field = { 30.0f, 100.0f, 160.0f, 60.0f };
    anchor.limit = { 100.0f, 50.0f, 1600.0f, 1200.0f };
    REQUIRE(luil::place_field_popup(anchor, { 120.0f, 100.0f }, 2.0f).x == 50.0f);
}

TEST_CASE("A field popup treats a bad scale as 1", "[ui][field-popup]")
{
    for (const float scale : { 0.0f, -2.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        CAPTURE(scale);
        const luil::field_popup_position position { luil::place_field_popup(anchor_in_window(), { 120.0f, 100.0f }, scale) };
        REQUIRE(position.x == 100.0f);
        REQUIRE(position.y == 82.0f);
    }
}

TEST_CASE("The open key handler opens a closed field on Alt+Down and F4", "[ui][field-popup][keys]")
{
    const luil::key_handler handler { luil::field_popup_key_handler(false, open_action, false) };
    REQUIRE(handler != nullptr);

    REQUIRE(opened_to(handler(alt_down())) == true);
    REQUIRE(opened_to(handler(key(luil::key_code::f4))) == true);

    // 수식 키 없는 ↓는 달리 쓰이는 자리라 열지 않는다.
    REQUIRE(handler(key(luil::key_code::arrow_down)).has_value() == false);
    // 그 밖의 키는 소비하지 않는다.
    REQUIRE(handler(key(luil::key_code::enter)).has_value() == false);
    REQUIRE(handler(key(luil::key_code::arrow_up)).has_value() == false);
    REQUIRE(handler(key(luil::key_code::space)).has_value() == false);
}

TEST_CASE("The open key handler opens on a bare Down only when the field asks for it", "[ui][field-popup][keys]")
{
    const luil::key_handler handler { luil::field_popup_key_handler(false, open_action, true) };
    REQUIRE(opened_to(handler(key(luil::key_code::arrow_down))) == true);
    REQUIRE(opened_to(handler(alt_down())) == true);

    // Shift·Ctrl이 함께면 ↓는 앱 단축키다.
    luil::key_pressed_event shifted { key(luil::key_code::arrow_down) };
    shifted.shift = true;
    REQUIRE(handler(shifted).has_value() == false);
    luil::key_pressed_event controlled { key(luil::key_code::arrow_down) };
    controlled.control = true;
    REQUIRE(handler(controlled).has_value() == false);
}

TEST_CASE("The open key handler refuses a chord with anything beside Alt", "[ui][field-popup][keys]")
{
    const luil::key_handler handler { luil::field_popup_key_handler(false, open_action, true) };
    for (const char modifier : { 'c', 's', 'm' })
    {
        CAPTURE(modifier);
        luil::key_pressed_event down { alt_down() };
        luil::key_pressed_event f4 { key(luil::key_code::f4) };
        down.control = modifier == 'c';
        down.shift = modifier == 's';
        down.meta = modifier == 'm';
        f4.control = modifier == 'c';
        f4.shift = modifier == 's';
        f4.meta = modifier == 'm';
        REQUIRE(handler(down).has_value() == false);
        REQUIRE(handler(f4).has_value() == false);
    }

    // Alt가 함께인 F4는 시스템 몫(창 닫기)이라 처리기가 받지 않는다.
    luil::key_pressed_event alt_f4 { key(luil::key_code::f4) };
    alt_f4.alt = true;
    REQUIRE(handler(alt_f4).has_value() == false);
    // 플랫폼이 주 단축키 역할을 켠 ↓도 앱 단축키다.
    luil::key_pressed_event primary { alt_down() };
    primary.primary_shortcut = true;
    REQUIRE(handler(primary).has_value() == false);
}

TEST_CASE("An open field swallows the open keys so they do not reach app shortcuts", "[ui][field-popup][keys]")
{
    const luil::key_handler handler { luil::field_popup_key_handler(true, open_action, true) };
    for (const luil::key_pressed_event& event : { alt_down(), key(luil::key_code::f4), key(luil::key_code::arrow_down) })
    {
        const std::optional<std::vector<luil::input_action>> answer { handler(event) };
        // 빈 목록은 소비했고 낼 메시지가 없다는 뜻이다.
        REQUIRE(answer.has_value());
        REQUIRE(answer->empty());
    }
    REQUIRE(handler(key(luil::key_code::enter)).has_value() == false);
}

TEST_CASE("Without a message factory there is no open key handler", "[ui][field-popup][keys]")
{
    REQUIRE(luil::field_popup_key_handler(false, nullptr, true) == nullptr);
}
