#include "luil/ui/check_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    struct toggle_intent
    {
        std::u8string owner {};
    };

    [[nodiscard]] luil::ui_action make_toggle(std::u8string owner)
    {
        return [owner = std::move(owner)](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(toggle_intent { owner }) }; };
    }
} // namespace

TEST_CASE("A check control fills the slot width and its configured row height", "[ui][check]")
{
    luil::check_config config {};
    config.owner = u8"wrap";
    config.label = u8"줄 바꿈";
    config.toggle = make_toggle(u8"wrap");
    REQUIRE(luil::check_element::height_for(config) == luil::check_row_height);

    luil::check_element control { config };
    control.arrange({ { 10.0f, 20.0f, 200.0f, 999.0f }, 2.0f });

    // 높이는 설정이 정하고 폭은 slot을 다 쓴다 — 라벨까지 줄 전체가 눌린다.
    REQUIRE(control.bounds().x == 10.0f);
    REQUIRE(control.bounds().y == 20.0f);
    REQUIRE(control.bounds().width == 200.0f);
    REQUIRE(control.bounds().height == luil::check_row_height * 2.0f);
}

TEST_CASE("A check control without a toggle action is not interactive", "[ui][check]")
{
    // "없는 것은 두지 않는다": 바꿀 수 없는 자리에 바꾸는 시늉을 두지 않는다.
    luil::check_config plain {};
    plain.owner = u8"readonly";
    plain.label = u8"읽기 전용";
    plain.tooltip = u8"바꿀 수 없다";
    const luil::check_element quiet { plain };
    REQUIRE(quiet.interactive() == false);
    REQUIRE(quiet.cursor() == luil::ui_cursor::inherit);
    REQUIRE(quiet.tooltip().empty());

    luil::check_config live { plain };
    live.toggle = make_toggle(u8"readonly");
    const luil::check_element active { live };
    REQUIRE(active.interactive());
    REQUIRE(active.cursor() == luil::ui_cursor::hand);
    REQUIRE(active.tooltip() == u8"바꿀 수 없다");
}

TEST_CASE("A check control emits the app action when clicked", "[ui][check]")
{
    luil::check_config config {};
    config.owner = u8"wrap";
    config.checked = true;
    config.toggle = make_toggle(u8"wrap");

    auto control { std::make_unique<luil::check_element>(config) };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(control), { 0.0f, 0.0f, 200.0f, 24.0f }, 1.0f) };

    // 상태는 앱이 갖고 element는 바꾸자는 메시지만 낸다 — 스스로 뒤집지 않는다.
    const luil::ui_element* const hit { tree.hit_test(100.0f, 12.0f) };
    REQUIRE(hit != nullptr);
    REQUIRE(hit->id() == luil::ui_element_id { luil::ui_element_kind::check, u8"wrap" });
    const luil::ui_action* const action { hit->action(luil::ui_trigger::left_click) };
    REQUIRE(action != nullptr);
    const std::vector<luil::input_action> actions { (*action)(luil::ui_action_context {}) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<toggle_intent>() != nullptr);
    REQUIRE(message->get<toggle_intent>()->owner == u8"wrap");
}

TEST_CASE("The indicator width follows the style", "[ui][check]")
{
    // 라벨 없이 여럿을 줄 맞춰 놓을 때 담는 쪽이 이 값을 쓴다.
    luil::check_config box {};
    REQUIRE(luil::check_element::indicator_width_for(box) == luil::check_box_size);

    luil::check_config radio {};
    radio.style = luil::check_style::radio;
    REQUIRE(luil::check_element::indicator_width_for(radio) == luil::check_box_size);

    luil::check_config knob {};
    knob.style = luil::check_style::toggle_switch;
    REQUIRE(luil::check_element::indicator_width_for(knob) == luil::check_switch_width);
}
