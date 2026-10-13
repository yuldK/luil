#include "luil/ui/ui_interaction.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

// 끄는 동안의 가장자리 자동 스크롤이다 (`drag_auto_scroll_config`).
// 시계는 보지 않는다 — 시각은 test가 `advance`로 정한다.
namespace {
    constexpr luil::ui_element_kind kind_list { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_row { luil::application_element_kind(1) };

    // 흘리는 창이 낸 스크롤이다 (휠·막대와 같은 메시지).
    struct scrolled
    {
        luil::ui_element_id window {};
        float delta { 0.0f };
    };

    // 손잡이가 받은 이동이다.
    struct moved
    {
        float previous_x { 0.0f };
        float previous_y { 0.0f };
        float x { 0.0f };
        float y { 0.0f };
    };

    class panel final : public luil::ui_element
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

    struct list_state
    {
        bool at_start { false };
        bool at_end { false };
        bool auto_scroll { true };
        luil::scroll_axis axis { luil::scroll_axis::vertical };
    };

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    // 띠 20, 최대 1000/초, 간격 10ms다 — 변화량을 손으로 셀 수 있다.
    [[nodiscard]] luil::interaction_config config()
    {
        luil::interaction_config value {};
        value.auto_scroll.edge_band = 20.0f;
        value.auto_scroll.maximum_speed = 1000.0f;
        value.auto_scroll.interval = std::chrono::milliseconds { 10 };
        return value;
    }

    // 목록 (0,100)–(200,300) 안에 끌기 손잡이 행(위)과 끌 수 있는 행(아래)이 있다. 목록 밖 오른쪽 (300,100)–(400,300)은
    // 두 번째 목록이다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_tree(const list_state state = {})
    {
        auto root { std::make_unique<panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
        const auto make_list = [&state](std::u8string owner, const luil::rect_f& box) {
            auto list { std::make_unique<panel>(luil::ui_element_id { kind_list, owner }) };
            list->arrange({ box, 1.0f });
            luil::scroll_source source {};
            source.scroll = [id = list->id()](const float delta) { return luil::make_app_action(scrolled { id, delta }); };
            source.axis = state.axis;
            source.at_start = state.at_start;
            source.at_end = state.at_end;
            list->set_scroll_source(std::move(source));
            return list;
        };

        auto list { make_list(u8"left", { 0.0f, 100.0f, 200.0f, 200.0f }) };
        auto handle_row { std::make_unique<panel>(luil::ui_element_id { kind_row, u8"handle" }) };
        handle_row->arrange({ { 10.0f, 150.0f, 180.0f, 30.0f }, 1.0f });
        luil::pointer_drag_target target {};
        target.auto_scroll = state.auto_scroll;
        target.on_move = [](const luil::ui_action_context& previous, const luil::ui_action_context& current) -> std::vector<luil::input_action> {
            return { luil::make_app_action(moved { previous.x, previous.y, current.x, current.y }) };
        };
        handle_row->set_pointer_drag_target(std::move(target));
        list->add(std::move(handle_row));

        auto item_row { std::make_unique<panel>(luil::ui_element_id { kind_row, u8"item" }) };
        item_row->arrange({ { 10.0f, 200.0f, 180.0f, 30.0f }, 1.0f });
        luil::drag_source source {};
        source.make_payload = [](const luil::ui_action_context& context) {
            luil::drag_payload payload {};
            payload.source = context.element;
            payload.dragged_owner = u8"item";
            return payload;
        };
        item_row->set_drag_source(std::move(source));
        list->add(std::move(item_row));
        root->add(std::move(list));
        root->add(make_list(u8"right", { 300.0f, 100.0f, 100.0f, 200.0f }));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    [[nodiscard]] luil::pointer_moved_event move(const float x, const float y, const int time)
    {
        return luil::pointer_moved_event { x, y, at(time) };
    }

    [[nodiscard]] const scrolled* scroll_of(const luil::input_action& action)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        return message != nullptr ? message->get<scrolled>() : nullptr;
    }

    [[nodiscard]] const moved* move_of(const luil::input_action& action)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        return message != nullptr ? message->get<moved>() : nullptr;
    }

    // 손잡이가 끝을 들었다.
    struct released
    {
        float y { 0.0f };
    };

    [[nodiscard]] const released* release_of(const luil::input_action& action)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        return message != nullptr ? message->get<released>() : nullptr;
    }

    [[nodiscard]] std::unique_ptr<panel> make_window(const luil::ui_element_id& id, const luil::rect_f& box)
    {
        auto window { std::make_unique<panel>(id) };
        window->arrange({ box, 1.0f });
        luil::scroll_source source {};
        source.scroll = [id](const float delta) { return luil::make_app_action(scrolled { id, delta }); };
        window->set_scroll_source(std::move(source));
        return window;
    }

    // 목록 (0,100)–(200,300) 안의 손잡이 행이 내용과 함께 흐른 자리다. `top`이 없으면 창 밖으로 흘러 지어지지 않았다
    // (가상 목록). 누른 자리 (20,160)에는 그 사이 다른 행이 와 있다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_flowing_tree(const std::optional<float> top)
    {
        auto root { std::make_unique<panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
        auto list { make_window({ kind_list, u8"left" }, { 0.0f, 100.0f, 200.0f, 200.0f }) };
        if (top.has_value())
        {
            auto handle_row { std::make_unique<panel>(luil::ui_element_id { kind_row, u8"handle" }) };
            handle_row->arrange({ { 10.0f, *top, 180.0f, 30.0f }, 1.0f });
            luil::pointer_drag_target target {};
            target.auto_scroll = true;
            target.on_move = [](const luil::ui_action_context& previous, const luil::ui_action_context& current) -> std::vector<luil::input_action> {
                return { luil::make_app_action(moved { previous.x, previous.y, current.x, current.y }) };
            };
            target.on_release = [](const luil::ui_action_context& context, const luil::drag_velocity&) -> std::vector<luil::input_action> { return { luil::make_app_action(released { context.y }) }; };
            handle_row->set_pointer_drag_target(std::move(target));
            list->add(std::move(handle_row));
        }
        // 흐른 내용의 다른 행이다. 누르면 고르는 행이라 손잡이가 떠난 누른 자리를 덮는다.
        if (top != 150.0f)
        {
            auto other_row { std::make_unique<panel>(luil::ui_element_id { kind_row, u8"other" }) };
            other_row->arrange({ { 10.0f, 150.0f, 180.0f, 30.0f }, 1.0f });
            other_row->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
            list->add(std::move(other_row));
        }
        root->add(std::move(list));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    // 손잡이 행을 누르고 (x, y)로 옮긴다 (시각 0과 10).
    void grab_handle(luil::interaction_controller& controller, const float x, const float y)
    {
        static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 160.0f, luil::pointer_button::left, at(0) }));
        static_cast<void>(controller.process(move(x, y, 10)));
    }
} // namespace

TEST_CASE("The edge speed ramps through the band and keeps growing outside the window", "[ui][interaction][auto-scroll]")
{
    luil::drag_auto_scroll_config value {};
    value.edge_band = 20.0f;
    value.maximum_speed = 1000.0f;
    // 창 100–300이다. 띠의 안쪽 끝은 0, 가장자리는 절반, 띠 폭만큼 밖이면 최대다.
    REQUIRE(luil::drag_auto_scroll_speed(200.0f, 100.0f, 300.0f, 1.0f, value) == 0.0f);
    REQUIRE(luil::drag_auto_scroll_speed(120.0f, 100.0f, 300.0f, 1.0f, value) == 0.0f);
    REQUIRE(luil::drag_auto_scroll_speed(110.0f, 100.0f, 300.0f, 1.0f, value) == Catch::Approx(-250.0f));
    REQUIRE(luil::drag_auto_scroll_speed(100.0f, 100.0f, 300.0f, 1.0f, value) == Catch::Approx(-500.0f));
    REQUIRE(luil::drag_auto_scroll_speed(90.0f, 100.0f, 300.0f, 1.0f, value) == Catch::Approx(-750.0f));
    REQUIRE(luil::drag_auto_scroll_speed(80.0f, 100.0f, 300.0f, 1.0f, value) == Catch::Approx(-1000.0f));
    REQUIRE(luil::drag_auto_scroll_speed(-500.0f, 100.0f, 300.0f, 1.0f, value) == Catch::Approx(-1000.0f));
    REQUIRE(luil::drag_auto_scroll_speed(290.0f, 100.0f, 300.0f, 1.0f, value) == Catch::Approx(250.0f));
    REQUIRE(luil::drag_auto_scroll_speed(320.0f, 100.0f, 300.0f, 1.0f, value) == Catch::Approx(1000.0f));
    // 띠는 논리 픽셀이다 — 배율 2면 물리 40px다.
    REQUIRE(luil::drag_auto_scroll_speed(120.0f, 100.0f, 300.0f, 2.0f, value) == Catch::Approx(-250.0f));
    // 띠 둘보다 좁은 창은 반씩 나눈다. 가운데는 흐르지 않는다.
    REQUIRE(luil::drag_auto_scroll_speed(115.0f, 100.0f, 130.0f, 1.0f, value) == 0.0f);
    REQUIRE(luil::drag_auto_scroll_speed(100.0f, 100.0f, 130.0f, 1.0f, value) < 0.0f);
}

TEST_CASE("A held handle in the bottom band scrolls on ticks and moves again in place", "[ui][interaction][auto-scroll]")
{
    luil::interaction_controller controller { nullptr, config() };
    controller.set_tree(make_tree());
    grab_handle(controller, 20.0f, 290.0f);

    // 띠 안 절반 깊이 = 250/초다. 다음 흐름은 이동 뒤 10ms다.
    REQUIRE(controller.next_deadline() == at(20));
    REQUIRE(controller.advance(at(15)).empty());
    auto actions { controller.advance(at(20)) };
    REQUIRE(actions.size() == 2u);
    // 스크롤이 먼저 나가고, 같은 표면 좌표의 이동이 뒤따른다.
    REQUIRE(scroll_of(actions[0]) != nullptr);
    REQUIRE(scroll_of(actions[0])->window == luil::ui_element_id { kind_list, u8"left" });
    REQUIRE(scroll_of(actions[0])->delta == Catch::Approx(2.5f));
    REQUIRE(move_of(actions[1]) != nullptr);
    REQUIRE(move_of(actions[1])->previous_y == 290.0f);
    REQUIRE(move_of(actions[1])->y == 290.0f);

    // 늦게 깨어나면 흐른 시간만큼 한 번에 낸다.
    actions = controller.advance(at(50));
    REQUIRE(scroll_of(actions[0])->delta == Catch::Approx(7.5f));
    REQUIRE(controller.next_deadline() == at(60));
}

TEST_CASE("The top band scrolls back and outside the window scrolls at full speed", "[ui][interaction][auto-scroll]")
{
    luil::interaction_controller controller { nullptr, config() };
    controller.set_tree(make_tree());
    grab_handle(controller, 20.0f, 105.0f);
    auto actions { controller.advance(at(20)) };
    // 띠 안 3/4 깊이 = 375/초, 위쪽이라 음수다.
    REQUIRE(scroll_of(actions[0])->delta == Catch::Approx(-3.75f));

    // 창 밖으로 띠 폭 넘게 나가면 최대 속도다. 이동 중에도 시계는 이어진다.
    static_cast<void>(controller.process(move(20.0f, 40.0f, 25)));
    actions = controller.advance(at(30));
    REQUIRE(scroll_of(actions[0])->delta == Catch::Approx(-10.0f));
    REQUIRE(move_of(actions[1])->y == 40.0f);

    // 띠 밖(창 가운데)으로 돌아오면 멈춘다.
    static_cast<void>(controller.process(move(20.0f, 200.0f, 35)));
    REQUIRE(controller.next_deadline().has_value() == false);
    REQUIRE(controller.advance(at(100)).empty());
}

TEST_CASE("A window already at its end does not scroll further", "[ui][interaction][auto-scroll]")
{
    luil::interaction_controller controller { nullptr, config() };
    list_state state {};
    state.at_end = true;
    controller.set_tree(make_tree(state));
    grab_handle(controller, 20.0f, 295.0f);
    REQUIRE(controller.next_deadline().has_value() == false);
    REQUIRE(controller.advance(at(100)).empty());

    // 처음 쪽은 아직 흐른다.
    static_cast<void>(controller.process(move(20.0f, 100.0f, 110)));
    REQUIRE(scroll_of(controller.advance(at(120))[0])->delta < 0.0f);
}

TEST_CASE("Scrolling stops when a new tree says the end was reached", "[ui][interaction][auto-scroll]")
{
    luil::interaction_controller controller { nullptr, config() };
    controller.set_tree(make_tree());
    grab_handle(controller, 20.0f, 295.0f);
    REQUIRE(controller.advance(at(20)).size() == 2u);

    list_state state {};
    state.at_end = true;
    controller.set_tree(make_tree(state));
    REQUIRE(controller.advance(at(30)).empty());
    REQUIRE(controller.next_deadline().has_value() == false);
}

TEST_CASE("Releasing or cancelling the drag stops the flow", "[ui][interaction][auto-scroll]")
{
    luil::interaction_controller controller { nullptr, config() };
    controller.set_tree(make_tree());
    grab_handle(controller, 20.0f, 295.0f);
    REQUIRE(controller.next_deadline().has_value());
    static_cast<void>(controller.process(luil::pointer_released_event { 20.0f, 295.0f, luil::pointer_button::left, at(15) }));
    REQUIRE(controller.next_deadline().has_value() == false);
    REQUIRE(controller.advance(at(100)).empty());

    grab_handle(controller, 20.0f, 295.0f);
    REQUIRE(controller.next_deadline().has_value());
    static_cast<void>(controller.cancel_dropped_gestures());
    REQUIRE(controller.next_deadline().has_value() == false);
}

TEST_CASE("Handles without auto scroll never flow", "[ui][interaction][auto-scroll]")
{
    luil::interaction_controller controller { nullptr, config() };
    list_state state {};
    state.auto_scroll = false;
    controller.set_tree(make_tree(state));
    grab_handle(controller, 20.0f, 295.0f);
    REQUIRE(controller.next_deadline().has_value() == false);
    REQUIRE(controller.advance(at(100)).empty());
}

TEST_CASE("Drag and drop scrolls the window under the pointer and keeps it past its edge", "[ui][interaction][auto-scroll]")
{
    luil::interaction_controller controller { nullptr, config() };
    controller.set_tree(make_tree());
    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 210.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(move(20.0f, 230.0f, 10)));
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(controller.next_deadline().has_value() == false);

    // 끌기에는 손잡이의 이동이 없다 — 스크롤만 나간다.
    static_cast<void>(controller.process(move(20.0f, 290.0f, 20)));
    auto actions { controller.advance(at(30)) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(scroll_of(actions[0])->window == luil::ui_element_id { kind_list, u8"left" });
    REQUIRE(scroll_of(actions[0])->delta == Catch::Approx(2.5f));

    // 목록 아래로 나가도 그 목록이 이어서 흐른다.
    static_cast<void>(controller.process(move(20.0f, 340.0f, 35)));
    actions = controller.advance(at(40));
    REQUIRE(scroll_of(actions[0])->window == luil::ui_element_id { kind_list, u8"left" });
    REQUIRE(scroll_of(actions[0])->delta == Catch::Approx(10.0f));

    // 다른 목록의 위쪽 띠로 옮기면 그 목록이 흐른다.
    static_cast<void>(controller.process(move(350.0f, 110.0f, 45)));
    actions = controller.advance(at(50));
    REQUIRE(scroll_of(actions[0])->window == luil::ui_element_id { kind_list, u8"right" });
    REQUIRE(scroll_of(actions[0])->delta < 0.0f);

    // 놓으면 멈춘다.
    static_cast<void>(controller.process(luil::pointer_released_event { 350.0f, 110.0f, luil::pointer_button::left, at(55) }));
    REQUIRE(controller.next_deadline().has_value() == false);
    REQUIRE(controller.advance(at(200)).empty());
}

TEST_CASE("Drag and drop auto scroll can be turned off", "[ui][interaction][auto-scroll]")
{
    luil::interaction_config value { config() };
    value.auto_scroll.drag_and_drop = false;
    luil::interaction_controller controller { nullptr, value };
    controller.set_tree(make_tree());
    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 210.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(move(20.0f, 290.0f, 10)));
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(controller.next_deadline().has_value() == false);
}

TEST_CASE("Horizontal windows flow along their own axis", "[ui][interaction][auto-scroll]")
{
    luil::interaction_controller controller { nullptr, config() };
    list_state state {};
    state.axis = luil::scroll_axis::horizontal;
    controller.set_tree(make_tree(state));
    // 세로 띠에 있어도 가로 창은 흐르지 않는다.
    grab_handle(controller, 100.0f, 295.0f);
    REQUIRE(controller.next_deadline().has_value() == false);
    // 오른쪽 띠다 (창 0–200).
    static_cast<void>(controller.process(move(195.0f, 200.0f, 20)));
    const auto actions { controller.advance(at(30)) };
    REQUIRE(scroll_of(actions[0])->delta == Catch::Approx(3.75f));
}

TEST_CASE("A touch handle drag scrolls at the edge as well", "[ui][interaction][auto-scroll][touch]")
{
    luil::interaction_controller controller { nullptr, config() };
    controller.set_tree(make_tree());
    luil::pointer_pressed_event down { 20.0f, 160.0f, luil::pointer_button::left, at(0) };
    down.device = luil::pointer_device::touch;
    static_cast<void>(controller.process(down));
    luil::pointer_moved_event drag { 20.0f, 290.0f, at(10) };
    drag.device = luil::pointer_device::touch;
    drag.in_contact = true;
    static_cast<void>(controller.process(drag));
    const auto actions { controller.advance(at(20)) };
    REQUIRE(actions.size() == 2u);
    REQUIRE(scroll_of(actions[0])->delta == Catch::Approx(2.5f));
    REQUIRE(move_of(actions[1])->y == 290.0f);
}

TEST_CASE("A handle keeps flowing after its content scrolls away from the pressed point", "[ui][interaction][auto-scroll]")
{
    // 흘리는 창 안의 손잡이는 내용과 함께 흐른다. 누른 자리를 덮은 것으로 손잡이의 생사를 보면 한 행만 흘러도
    // 끌기가 끊긴다.
    luil::interaction_controller controller { nullptr, config() };
    controller.set_tree(make_flowing_tree(150.0f));
    grab_handle(controller, 20.0f, 290.0f);
    auto actions { controller.advance(at(20)) };
    REQUIRE(actions.size() == 2u);
    REQUIRE(scroll_of(actions[0]) != nullptr);
    REQUIRE(move_of(actions[1]) != nullptr);

    // 한 행 넘게 흘렀다. 누른 자리에는 다른 행이 있다.
    controller.set_tree(make_flowing_tree(100.0f));
    actions = controller.advance(at(30));
    REQUIRE(actions.size() == 2u);
    REQUIRE(scroll_of(actions[0])->window == luil::ui_element_id { kind_list, u8"left" });
    REQUIRE(move_of(actions[1])->y == 290.0f);
    // 포인터를 움직여도 끊기지 않는다.
    actions = controller.process(move(20.0f, 295.0f, 35));
    REQUIRE(actions.size() == 1u);
    REQUIRE(move_of(actions[0])->y == 295.0f);

    // 창 밖으로 흘러 지어지지 않았다. 창이 남아 있으므로 끌기를 시작할 때 쥔 handler로 잇는다.
    controller.set_tree(make_flowing_tree(std::nullopt));
    actions = controller.advance(at(45));
    REQUIRE(actions.size() == 2u);
    REQUIRE(scroll_of(actions[0])->window == luil::ui_element_id { kind_list, u8"left" });
    REQUIRE(move_of(actions[1])->y == 295.0f);
    actions = controller.process(move(20.0f, 298.0f, 50));
    REQUIRE(actions.size() == 1u);
    REQUIRE(move_of(actions[0])->y == 298.0f);

    // 놓으면 끝을 듣는다 — 앱의 범위 상태가 끝난 줄 안다.
    actions = controller.process(luil::pointer_released_event { 20.0f, 298.0f, luil::pointer_button::left, at(55) });
    REQUIRE(actions.size() == 1u);
    REQUIRE(release_of(actions[0]) != nullptr);
    REQUIRE(release_of(actions[0])->y == 298.0f);
    REQUIRE(controller.next_deadline().has_value() == false);
}

TEST_CASE("A handle without auto scroll still ends when the pressed point is covered", "[ui][interaction][auto-scroll]")
{
    // 제자리의 손잡이(막대·나눔 손잡이)는 지금처럼 누른 자리를 덮은 것이 끊는다.
    luil::interaction_controller controller { nullptr, config() };
    list_state state {};
    state.auto_scroll = false;
    controller.set_tree(make_tree(state));
    grab_handle(controller, 20.0f, 250.0f);
    controller.set_tree(make_flowing_tree(100.0f));
    REQUIRE(controller.process(move(20.0f, 255.0f, 20)).empty());
    REQUIRE(controller.process(move(20.0f, 260.0f, 30)).empty());
}

TEST_CASE("Dragging out of a list into the page that holds it keeps the list flowing", "[ui][interaction][auto-scroll]")
{
    // 판(0,0)–(400,400)이 흐르는 창이고 그 안에 목록 둘이 있다. 목록 위로 끌고 나가 판 안에 머물러도 목록이 흐른다.
    auto root { std::make_unique<panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    auto page { make_window({ kind_list, u8"page" }, { 0.0f, 0.0f, 400.0f, 400.0f }) };
    auto inner { make_window({ kind_list, u8"inner" }, { 0.0f, 100.0f, 200.0f, 200.0f }) };
    auto item_row { std::make_unique<panel>(luil::ui_element_id { kind_row, u8"item" }) };
    item_row->arrange({ { 10.0f, 200.0f, 180.0f, 30.0f }, 1.0f });
    luil::drag_source source {};
    source.make_payload = [](const luil::ui_action_context& context) {
        luil::drag_payload payload {};
        payload.source = context.element;
        payload.dragged_owner = u8"item";
        return payload;
    };
    item_row->set_drag_source(std::move(source));
    inner->add(std::move(item_row));
    page->add(std::move(inner));
    page->add(make_window({ kind_list, u8"right" }, { 250.0f, 100.0f, 100.0f, 200.0f }));
    root->add(std::move(page));

    luil::interaction_controller controller { nullptr, config() };
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 20.0f, 210.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(move(20.0f, 230.0f, 10)));
    REQUIRE(controller.snapshot().drag.has_value());

    // 목록 위쪽(판 안, 판의 띠 밖)이다. 목록이 위로 흐른다.
    static_cast<void>(controller.process(move(20.0f, 60.0f, 20)));
    auto actions { controller.advance(at(30)) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(scroll_of(actions[0])->window == luil::ui_element_id { kind_list, u8"inner" });
    REQUIRE(scroll_of(actions[0])->delta < 0.0f);

    // 판과 남남인 옆 목록에 들어가면 그 목록으로 옮겨 간다.
    static_cast<void>(controller.process(move(300.0f, 290.0f, 35)));
    actions = controller.advance(at(40));
    REQUIRE(actions.size() == 1u);
    REQUIRE(scroll_of(actions[0])->window == luil::ui_element_id { kind_list, u8"right" });
    REQUIRE(scroll_of(actions[0])->delta > 0.0f);
}
