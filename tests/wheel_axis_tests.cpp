#include "luil/ui/ui_interaction.h"

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
    constexpr luil::ui_element_kind kind_box { luil::application_element_kind(0) };

    struct scroll_intent
    {
        std::u8string owner {};
        float delta { 0.0f };
    };

    // 한 축으로 흘리는 상자다. 자식은 같은 자리에 선다.
    class scroll_box final : public luil::ui_element
    {
    public:
        scroll_box(std::u8string owner, const luil::scroll_axis axis)
            : ui_element { luil::ui_element_id { kind_box, owner } }
        {
            luil::scroll_source source {};
            source.axis = axis;
            source.scroll = [owner = std::move(owner)](const float delta) { return luil::make_app_action(scroll_intent { owner, delta }); };
            set_scroll_source(std::move(source));
        }

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
            for (const std::unique_ptr<ui_element>& child : children())
                child->arrange(context);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
    };

    [[nodiscard]] const scroll_intent* single_scroll(const std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1u)
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
        return message != nullptr ? message->get<scroll_intent>() : nullptr;
    }

    // 가로 띠 안에 세로 목록이 같은 자리로 선다.
    [[nodiscard]] luil::ui_tree strip_with_list()
    {
        auto strip { std::make_unique<scroll_box>(u8"strip", luil::scroll_axis::horizontal) };
        strip->add(std::make_unique<scroll_box>(u8"list", luil::scroll_axis::vertical));
        return luil::make_arranged_tree(std::move(strip), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f);
    }

    // 컨트롤러가 하는 대로 휠 눈금을 세로 휠 부호의 논리 픽셀로 바꾼다 (위로 굴림 = 음수).
    [[nodiscard]] float scroll_delta_of(const luil::mouse_wheel_event& event)
    {
        return -(event.delta / 120.0f) * luil::input_wheel_scroll_step;
    }
}

TEST_CASE("A vertical wheel goes to the innermost scroll container whatever its axis", "[ui][scroll][wheel]")
{
    const luil::ui_tree tree { strip_with_list() };
    const luil::mouse_wheel_event down { .x = 50.0f, .y = 50.0f, .delta = -120.0f };
    // 돌려받은 동작을 붙들어야 그 안을 가리키는 포인터가 산다.
    const std::vector<luil::input_action> actions { luil::route_wheel(tree, down, scroll_delta_of(down)) };
    const scroll_intent* const message { single_scroll(actions) };
    REQUIRE(message != nullptr);
    REQUIRE(message->owner == u8"list");
    REQUIRE(message->delta == luil::input_wheel_scroll_step);

    // 가로 띠뿐인 자리에서도 세로 휠은 그 띠를 흘린다 — 축을 묻지 않는 지금까지의 동작이다.
    const luil::ui_tree strip_only { luil::make_arranged_tree(std::make_unique<scroll_box>(u8"strip", luil::scroll_axis::horizontal), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };
    const std::vector<luil::input_action> strip_actions { luil::route_wheel(strip_only, down, scroll_delta_of(down)) };
    const scroll_intent* const strip { single_scroll(strip_actions) };
    REQUIRE(strip != nullptr);
    REQUIRE(strip->owner == u8"strip");
}

TEST_CASE("A horizontal wheel passes a vertical container and scrolls the horizontal one around it", "[ui][scroll][wheel]")
{
    const luil::ui_tree tree { strip_with_list() };
    // 오른쪽으로 기울이면 offset이 는다 (내용이 왼쪽으로 간다).
    const luil::mouse_wheel_event right { .x = 50.0f, .y = 50.0f, .delta = 120.0f, .horizontal = true };
    const std::vector<luil::input_action> actions { luil::route_wheel(tree, right, scroll_delta_of(right)) };
    const scroll_intent* const message { single_scroll(actions) };
    REQUIRE(message != nullptr);
    REQUIRE(message->owner == u8"strip");
    REQUIRE(message->delta == luil::input_wheel_scroll_step);

    const luil::mouse_wheel_event left { .x = 50.0f, .y = 50.0f, .delta = -120.0f, .horizontal = true };
    const std::vector<luil::input_action> back_actions { luil::route_wheel(tree, left, scroll_delta_of(left)) };
    const scroll_intent* const back { single_scroll(back_actions) };
    REQUIRE(back != nullptr);
    REQUIRE(back->delta == -luil::input_wheel_scroll_step);
}

TEST_CASE("A horizontal wheel over only vertical containers scrolls nothing", "[ui][scroll][wheel]")
{
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<scroll_box>(u8"list", luil::scroll_axis::vertical), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };
    const luil::mouse_wheel_event right { .x = 50.0f, .y = 50.0f, .delta = 120.0f, .horizontal = true };
    REQUIRE(luil::route_wheel(tree, right, scroll_delta_of(right)).empty());

    // 정책 없는 컨트롤러도 같은 길을 탄다.
    luil::interaction_controller controller {};
    controller.set_tree(std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::make_unique<scroll_box>(u8"list", luil::scroll_axis::vertical), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f)));
    REQUIRE(controller.process(right).empty());
}
