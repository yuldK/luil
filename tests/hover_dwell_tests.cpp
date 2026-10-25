#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    constexpr luil::ui_element_kind kind_panel { luil::application_element_kind(0) };

    struct said_intent
    {
        std::u8string what {};
    };

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

    [[nodiscard]] luil::ui_action say(std::u8string what)
    {
        return [what = std::move(what)](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_message { said_intent { what } } } }; };
    }

    [[nodiscard]] std::function<luil::input_action()> say_once(std::u8string what)
    {
        return [what = std::move(what)] { return luil::input_action { luil::app_message { said_intent { what } } }; };
    }

    [[nodiscard]] std::vector<std::u8string> said(const std::vector<luil::input_action>& actions)
    {
        std::vector<std::u8string> result {};
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const intent { message->get<said_intent>() }; intent != nullptr)
                    result.push_back(intent->what);
        return result;
    }

    [[nodiscard]] std::unique_ptr<test_panel> make_panel(const char8_t* key, const luil::rect_f& slot)
    {
        auto panel { std::make_unique<test_panel>(luil::ui_element_id { kind_panel, key }) };
        panel->arrange({ slot, 1.0f });
        return panel;
    }

    // root(300 × 200) ⊃ 카드(0..100, 0..100) ⊃ 제목(10..90, 10..30) · 단추(200..280, 0..40).
    // 카드에 머무름을, 위쪽 띠(0..300, 150..200)에 hover 영역을 둔다. 띠 아래에는 단추(0..80, 150..190)가 있다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_tree(const bool covered = false)
    {
        auto root { make_panel(u8"root", { 0.0f, 0.0f, 300.0f, 200.0f }) };
        auto card { make_panel(u8"card", { 0.0f, 0.0f, 100.0f, 100.0f }) };
        card->set_action(luil::ui_trigger::left_click, say(u8"card-click"));
        card->set_hover_dwell(luil::hover_dwell_target { .delay = 350ms, .on_dwell = say(u8"preview"), .on_end = say(u8"preview-end") });
        auto title { make_panel(u8"title", { 10.0f, 10.0f, 80.0f, 20.0f }) };
        title->set_action(luil::ui_trigger::left_click, say(u8"title-click"));
        card->add(std::move(title));
        root->add(std::move(card));
        auto button { make_panel(u8"button", { 200.0f, 0.0f, 80.0f, 40.0f }) };
        button->set_action(luil::ui_trigger::left_click, say(u8"button-click"));
        root->add(std::move(button));
        auto under { make_panel(u8"under", { 0.0f, 150.0f, 80.0f, 40.0f }) };
        under->set_action(luil::ui_trigger::left_click, say(u8"under-click"));
        root->add(std::move(under));
        // hit를 받지 않는 띠다 — 액션이 없다.
        auto strip { make_panel(u8"strip", { 0.0f, 150.0f, 300.0f, 50.0f }) };
        strip->set_hover_region(luil::hover_region_target { .on_enter = say_once(u8"reveal"), .on_leave = say_once(u8"hide") });
        root->add(std::move(strip));
        if (covered)
        {
            // 화면 전체를 덮어 hit를 삼키는 scrim이다.
            auto scrim { make_panel(u8"scrim", { 0.0f, 0.0f, 300.0f, 200.0f }) };
            scrim->set_hit_opaque(true);
            root->add(std::move(scrim));
        }
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    [[nodiscard]] luil::pointer_moved_event move_to(const float x, const float y, const std::chrono::milliseconds at, const luil::pointer_device device = luil::pointer_device::mouse)
    {
        luil::pointer_moved_event event { x, y, std::chrono::steady_clock::time_point {} + at };
        event.device = device;
        return event;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const std::chrono::milliseconds value)
    {
        return std::chrono::steady_clock::time_point {} + value;
    }
} // namespace

TEST_CASE("Resting over an element fires its dwell once after the delay", "[hover][dwell]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree());
    REQUIRE(said(controller.process(move_to(20.0f, 20.0f, 0ms))).empty());
    REQUIRE(controller.next_deadline() == at(350ms));
    REQUIRE(said(controller.advance(at(349ms))).empty());
    // 안쪽 자식 사이를 옮겨 다녀도 임자(카드)가 같아 시계가 이어진다.
    REQUIRE(said(controller.process(move_to(50.0f, 60.0f, 200ms))).empty());
    REQUIRE(controller.next_deadline() == at(350ms));
    REQUIRE(said(controller.advance(at(350ms))) == std::vector<std::u8string> { u8"preview" });
    // 한 번만 낸다.
    REQUIRE(controller.next_deadline().has_value() == false);
    REQUIRE(said(controller.advance(at(900ms))).empty());
    // 떠나면 끝이다.
    REQUIRE(said(controller.process(move_to(150.0f, 120.0f, 1s))) == std::vector<std::u8string> { u8"preview-end" });
}

TEST_CASE("A card with only a dwell still takes the pointer and fires", "[hover][dwell]")
{
    // 실제 데모에서 잡힌 결함이다: 액션 없이 머무름만 단 카드는 hit 대상이 아니어서 hover가 서지 않았다.
    auto root { make_panel(u8"root", { 0.0f, 0.0f, 300.0f, 200.0f }) };
    auto card { make_panel(u8"card", { 0.0f, 0.0f, 100.0f, 100.0f }) };
    card->set_hover_dwell(luil::hover_dwell_target { .delay = 350ms, .on_dwell = say(u8"preview"), .on_end = say(u8"preview-end") });
    REQUIRE(card->interactive());
    root->add(std::move(card));
    luil::interaction_controller controller {};
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));
    static_cast<void>(controller.process(move_to(20.0f, 20.0f, 0ms)));
    REQUIRE(controller.next_deadline() == at(350ms));
    REQUIRE(said(controller.advance(at(350ms))) == std::vector<std::u8string> { u8"preview" });
    REQUIRE(said(controller.process(move_to(150.0f, 120.0f, 1s))) == std::vector<std::u8string> { u8"preview-end" });
}

TEST_CASE("Leaving before the delay fires nothing", "[hover][dwell]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree());
    static_cast<void>(controller.process(move_to(20.0f, 20.0f, 0ms)));
    REQUIRE(said(controller.process(move_to(150.0f, 120.0f, 100ms))).empty());
    REQUIRE(controller.next_deadline().has_value() == false);
    REQUIRE(said(controller.advance(at(1s))).empty());
}

TEST_CASE("A press cancels a pending dwell and ends a fired one", "[hover][dwell]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree());
    static_cast<void>(controller.process(move_to(20.0f, 20.0f, 0ms)));
    // 차기 전의 누름은 취소다. 그 자리에 머물러도 다시 재지 않는다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 20.0f, luil::pointer_button::left, at(100ms) }));
    static_cast<void>(controller.process(luil::pointer_released_event { 20.0f, 20.0f, luil::pointer_button::left, at(120ms) }));
    REQUIRE(controller.next_deadline().has_value() == false);
    REQUIRE(said(controller.advance(at(1s))).empty());

    // 떠났다 돌아오면 다시 잰다. 찬 뒤의 누름은 끝을 알린다.
    static_cast<void>(controller.process(move_to(150.0f, 120.0f, 1100ms)));
    static_cast<void>(controller.process(move_to(20.0f, 20.0f, 1200ms)));
    REQUIRE(said(controller.advance(at(1550ms))) == std::vector<std::u8string> { u8"preview" });
    const std::vector<std::u8string> pressed { said(controller.process(luil::pointer_pressed_event { 20.0f, 20.0f, luil::pointer_button::right, at(1600ms) })) };
    REQUIRE(pressed == std::vector<std::u8string> { u8"preview-end" });
}

TEST_CASE("Touch has no dwell", "[hover][dwell]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree());
    static_cast<void>(controller.process(move_to(20.0f, 20.0f, 0ms, luil::pointer_device::touch)));
    REQUIRE(controller.next_deadline().has_value() == false);
}

TEST_CASE("A cover that takes the pointer ends a fired dwell", "[hover][dwell]")
{
    // 포인터가 머문 채 scrim(가둠·대화 상자)이 서면 hover가 그쪽으로 옮고 머무름은 끝난다.
    luil::interaction_controller controller {};
    controller.set_tree(make_tree());
    static_cast<void>(controller.process(move_to(20.0f, 20.0f, 0ms)));
    REQUIRE(said(controller.advance(at(400ms))) == std::vector<std::u8string> { u8"preview" });
    controller.set_tree(make_tree(true));
    REQUIRE(said(controller.refresh_hover_tracking()) == std::vector<std::u8string> { u8"preview-end" });
}

TEST_CASE("A hover region reports entry and exit without taking hits", "[hover][region]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree());
    REQUIRE(said(controller.process(move_to(40.0f, 170.0f, 0ms))) == std::vector<std::u8string> { u8"reveal" });
    // 영역 안을 움직여도 다시 알리지 않는다.
    REQUIRE(said(controller.process(move_to(250.0f, 180.0f, 10ms))).empty());
    // 영역은 hit를 받지 않는다 — 그 자리의 단추가 눌린다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 40.0f, 170.0f, luil::pointer_button::left, at(20ms) }));
    REQUIRE(said(controller.process(luil::pointer_released_event { 40.0f, 170.0f, luil::pointer_button::left, at(30ms) })) == std::vector<std::u8string> { u8"under-click" });
    REQUIRE(said(controller.process(move_to(40.0f, 120.0f, 40ms))) == std::vector<std::u8string> { u8"hide" });
    // 창을 떠나도 나감이다.
    static_cast<void>(controller.process(move_to(40.0f, 170.0f, 50ms)));
    REQUIRE(said(controller.process(luil::pointer_left_event {})) == std::vector<std::u8string> { u8"hide" });
}

TEST_CASE("A hover region holds while the pointer is captured", "[hover][region]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree());
    static_cast<void>(controller.process(move_to(40.0f, 170.0f, 0ms)));
    // 영역 안의 단추를 눌러 쥔 채 밖으로 끌어 나가도 뗄 때까지 숨기지 않는다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 40.0f, 170.0f, luil::pointer_button::left, at(10ms) }));
    REQUIRE(said(controller.process(move_to(40.0f, 60.0f, 20ms))).empty());
    REQUIRE(said(controller.process(luil::pointer_released_event { 40.0f, 60.0f, luil::pointer_button::left, at(30ms) })) == std::vector<std::u8string> { u8"hide" });
}

TEST_CASE("A hover region that appears under a resting pointer reports entry", "[hover][region]")
{
    luil::interaction_controller controller {};
    auto root { make_panel(u8"root", { 0.0f, 0.0f, 300.0f, 200.0f }) };
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));
    static_cast<void>(controller.process(move_to(40.0f, 170.0f, 0ms)));
    controller.set_tree(make_tree());
    REQUIRE(said(controller.refresh_hover_tracking()) == std::vector<std::u8string> { u8"reveal" });
}
