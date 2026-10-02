#include "luil/ui/ui_interaction.h"

#include "luil/ui/app_message.h"
#include "luil/ui/strip_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_area { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_card { luil::application_element_kind(1) };
    constexpr luil::ui_element_kind kind_input { luil::application_element_kind(2) };
    constexpr luil::ui_element_kind kind_scrim { luil::application_element_kind(3) };

    constexpr luil::text_input_target input_target { 3 };

    struct fake_intent
    {
        std::u8string name {};
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

        // 픽셀 10개가 글자 하나다 (텍스트 칸으로 쓸 때).
        [[nodiscard]] std::optional<std::size_t> offset_at(const float x, const luil::text_measurer&) const override
        {
            return static_cast<std::size_t>((x - bounds().x) / 10.0f);
        }
    };

    [[nodiscard]] luil::ui_action message_action(const std::u8string name)
    {
        return [name](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(fake_intent { name }) }; };
    }

    [[nodiscard]] const fake_intent* intent_of(const luil::input_action& action)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        return message != nullptr ? message->get<fake_intent>() : nullptr;
    }

    [[nodiscard]] std::vector<std::u8string> names_of(const std::vector<luil::input_action>& actions)
    {
        std::vector<std::u8string> names {};
        for (const luil::input_action& action : actions)
            if (const fake_intent* const intent { intent_of(action) }; intent != nullptr)
                names.push_back(intent->name);
        return names;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] luil::scroll_source recording_source(std::vector<float>* const scrolled, const std::u8string name, const float scale, const luil::scroll_axis axis)
    {
        luil::scroll_source source {};
        source.scroll = [scrolled, name](const float delta) {
            scrolled->push_back(delta);
            return luil::make_app_action(fake_intent { name });
        };
        source.scale = scale;
        source.axis = axis;
        return source;
    }

    struct card_options
    {
        luil::scroll_axis axis { luil::scroll_axis::vertical };
        float scale { 1.0f };
        bool draggable { false };
        bool handle { false };
        bool visible { true };
        bool enabled { true };
        bool covered { false };
    };

    // 흘리는 창 안에 클릭·우클릭·더블 클릭을 단 카드 하나를 둔다.
    // 창의 (300, 0)부터 오른쪽은 클릭 대상이 없는 여백이다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> card_tree(std::vector<float>* const scrolled, const card_options options = {})
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, options.scale });
        auto area { std::make_unique<test_panel>(luil::ui_element_id { kind_area, u8"area" }) };
        area->arrange({ { 0.0f, 0.0f, 400.0f, 300.0f }, options.scale });
        area->set_scroll_source(recording_source(scrolled, u8"scroll", options.scale, options.axis));
        auto card { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"card" }) };
        card->arrange({ { 20.0f, 20.0f, 200.0f, 200.0f }, options.scale });
        card->set_action(luil::ui_trigger::left_click, message_action(u8"click"));
        card->set_action(luil::ui_trigger::right_click, message_action(u8"menu"));
        card->set_action(luil::ui_trigger::double_click, message_action(u8"double"));
        card->set_visible(options.visible);
        card->set_enabled(options.enabled);
        if (options.draggable)
            card->set_drag_source(luil::drag_source { [](const luil::ui_action_context& context) { return luil::drag_payload { context.element, u8"card" }; } });
        if (options.handle)
        {
            luil::pointer_drag_target drag {};
            drag.on_move = [](const luil::ui_action_context&, const luil::ui_action_context& current) -> std::vector<luil::input_action> {
                if (current.x == 0.0f)
                    return {};
                return { luil::make_app_action(fake_intent { u8"move" }) };
            };
            card->set_pointer_drag_target(std::move(drag));
        }
        area->add(std::move(card));
        if (options.draggable)
        {
            auto bin { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"bin" }) };
            bin->arrange({ { 20.0f, 320.0f, 100.0f, 60.0f }, options.scale });
            bin->set_drop_target(luil::drop_target {
                [](const luil::drag_payload&) { return true; },
                [](const luil::drag_payload&, const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(fake_intent { u8"drop" }) }; },
            });
            root->add(std::move(bin));
        }
        root->add(std::move(area));
        if (options.covered)
        {
            auto scrim { std::make_unique<test_panel>(luil::ui_element_id { kind_scrim, u8"scrim" }) };
            scrim->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, options.scale });
            scrim->set_hit_opaque(true);
            root->add(std::move(scrim));
        }
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    [[nodiscard]] luil::pointer_pressed_event touch_press(const float x, const float y, const int time, const std::uint32_t id = 1, const float scale = 1.0f)
    {
        luil::pointer_pressed_event event { x, y, luil::pointer_button::left, at(time) };
        event.device = luil::pointer_device::touch;
        event.pointer_id = id;
        event.scale = scale;
        return event;
    }

    [[nodiscard]] luil::pointer_moved_event touch_move(const float x, const float y, const int time, const std::uint32_t id = 1)
    {
        luil::pointer_moved_event event { x, y, at(time) };
        event.device = luil::pointer_device::touch;
        event.pointer_id = id;
        event.in_contact = true;
        return event;
    }

    [[nodiscard]] luil::pointer_released_event touch_release(const float x, const float y, const int time, const std::uint32_t id = 1)
    {
        luil::pointer_released_event event { x, y, luil::pointer_button::left, at(time) };
        event.device = luil::pointer_device::touch;
        event.pointer_id = id;
        return event;
    }

    template<typename event_type>
    [[nodiscard]] event_type as_pen(event_type event)
    {
        event.device = luil::pointer_device::pen;
        event.pointer_id = 9;
        return event;
    }

    // 누름 관찰과 텍스트 칸을 아는 policy다.
    class touch_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            if (kind == kind_input)
                return input_target;
            return std::nullopt;
        }

        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request& request) const override
        {
            edits.push_back(request);
            return luil::make_app_action(fake_intent { u8"edit" });
        }

        [[nodiscard]] std::vector<luil::input_action> on_press(const luil::ui_tree&, const luil::ui_element& element, const luil::pointer_pressed_event& event) override
        {
            pressed.push_back(event.device);
            return { luil::make_app_action(fake_intent { u8"press:" + element.id().owner }) };
        }

        void on_click(const luil::ui_element& element) override
        {
            clicked.push_back(element.id());
        }

        mutable std::vector<luil::text_edit_request> edits {};
        std::vector<luil::pointer_device> pressed {};
        std::vector<luil::ui_element_id> clicked {};
    };
} // namespace

TEST_CASE("Only touch turns a quick swipe into a scroll", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled));

    // 마우스로 같은 손짓을 하면 지금처럼 클릭이다 — 마우스에는 몸짓이 없다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 60.0f, 120.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_moved_event { 60.0f, 90.0f, at(40) }));
    auto actions { controller.process(luil::pointer_released_event { 60.0f, 90.0f, luil::pointer_button::left, at(80) }) };
    REQUIRE(scrolled.empty());
    REQUIRE(names_of(actions) == std::vector<std::u8string> { u8"click" });

    // 펜도 같다. 펜촉의 쓸기는 선택·끌기의 몫이다.
    static_cast<void>(controller.process(as_pen(luil::pointer_pressed_event { 60.0f, 120.0f, luil::pointer_button::left, at(1000) })));
    static_cast<void>(controller.process(as_pen(luil::pointer_moved_event { 60.0f, 90.0f, at(1040) })));
    actions = controller.process(as_pen(luil::pointer_released_event { 60.0f, 90.0f, luil::pointer_button::left, at(1080) }));
    REQUIRE(scrolled.empty());
    REQUIRE(names_of(actions) == std::vector<std::u8string> { u8"click" });

    // 터치만 흘린다. 떼는 것은 클릭이 아니다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 2000)));
    actions = controller.process(touch_move(60.0f, 90.0f, 2040));
    REQUIRE(names_of(actions) == std::vector<std::u8string> { u8"scroll" });
    REQUIRE(scrolled == std::vector<float> { 30.0f });
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});
    REQUIRE(controller.process(touch_release(60.0f, 90.0f, 2080)).empty());
}

TEST_CASE("A swipe scrolls in logical pixels and follows the finger", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled, { .scale = 2.0f }));

    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0, 1, 2.0f)));
    // 허용치 안의 떨림은 아무것도 흘리지 않는다 — 물리 20픽셀은 배율 2에서 논리 10이다.
    REQUIRE(controller.process(touch_move(60.0f, 100.0f, 40)).empty());
    // 시작할 때는 누른 자리부터 쌓인 이동을 한 번에 흘린다 (물리 30 → 논리 15).
    REQUIRE(names_of(controller.process(touch_move(60.0f, 90.0f, 80))) == std::vector<std::u8string> { u8"scroll" });
    REQUIRE(scrolled == std::vector<float> { 15.0f });
    // 이어지는 이동은 지난 자리와의 차이만 흘린다. 내리면 거꾸로다.
    static_cast<void>(controller.process(touch_move(64.0f, 130.0f, 120)));
    REQUIRE(scrolled.back() == -20.0f);
    // 정상적인 뗌의 마지막 변화도 흘린다.
    REQUIRE(names_of(controller.process(touch_release(64.0f, 120.0f, 160))) == std::vector<std::u8string> { u8"scroll" });
    REQUIRE(scrolled.back() == 5.0f);
}

TEST_CASE("How a swipe was sampled does not change whether it scrolls", "[ui][interaction][touch]")
{
    std::vector<float> stepped {};
    luil::interaction_controller first {};
    first.set_tree(card_tree(&stepped));
    static_cast<void>(first.process(touch_press(60.0f, 120.0f, 0)));
    static_cast<void>(first.process(touch_move(60.0f, 112.0f, 20)));
    static_cast<void>(first.process(touch_move(60.0f, 100.0f, 40)));

    std::vector<float> single {};
    luil::interaction_controller second {};
    second.set_tree(card_tree(&single));
    static_cast<void>(second.process(touch_press(60.0f, 120.0f, 0)));
    static_cast<void>(second.process(touch_move(60.0f, 100.0f, 40)));

    REQUIRE(stepped == std::vector<float> { 20.0f });
    REQUIRE(single == stepped);
}

TEST_CASE("A late or sideways touch drag neither scrolls nor clicks", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled));

    // 잠시 잡고 있다 끈 것은 손짓이 아니다. 허용치를 넘었으므로 클릭으로도 되돌리지 않는다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0)));
    REQUIRE(controller.process(touch_move(60.0f, 90.0f, 450)).empty());
    REQUIRE(controller.process(touch_release(60.0f, 90.0f, 500)).empty());
    REQUIRE(scrolled.empty());

    // 세로 창을 옆으로 쓸면 흘리지 않는다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 1000)));
    REQUIRE(controller.process(touch_move(90.0f, 110.0f, 1050)).empty());
    REQUIRE(controller.process(touch_release(90.0f, 110.0f, 1100)).empty());
    REQUIRE(scrolled.empty());
}

TEST_CASE("A swipe picks the innermost container that flows along its axis", "[ui][interaction][touch]")
{
    // 세로 화면 안에 넘친 가로 띠가 있다.
    std::vector<float> page {};
    std::vector<float> strip {};
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    auto area { std::make_unique<test_panel>(luil::ui_element_id { kind_area, u8"page" }) };
    area->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    area->set_scroll_source(recording_source(&page, u8"page", 1.0f, luil::scroll_axis::vertical));
    auto band { std::make_unique<test_panel>(luil::ui_element_id { kind_area, u8"band" }) };
    band->arrange({ { 0.0f, 100.0f, 400.0f, 80.0f }, 1.0f });
    band->set_scroll_source(recording_source(&strip, u8"band", 1.0f, luil::scroll_axis::horizontal));
    area->add(std::move(band));
    root->add(std::move(area));
    luil::interaction_controller controller {};
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    // 띠 위에서 옆으로 밀면 띠가 흐른다 — 왼쪽으로 밀면 오른쪽 내용이 들어온다.
    static_cast<void>(controller.process(touch_press(200.0f, 140.0f, 0)));
    REQUIRE(names_of(controller.process(touch_move(170.0f, 142.0f, 40))) == std::vector<std::u8string> { u8"band" });
    REQUIRE(strip == std::vector<float> { 30.0f });
    static_cast<void>(controller.process(touch_release(170.0f, 142.0f, 80)));

    // 같은 띠 위에서 위로 쓸면 가로 띠를 지나쳐 화면이 흐른다.
    static_cast<void>(controller.process(touch_press(200.0f, 140.0f, 1000)));
    REQUIRE(names_of(controller.process(touch_move(202.0f, 110.0f, 1040))) == std::vector<std::u8string> { u8"page" });
    REQUIRE(page == std::vector<float> { 30.0f });
    REQUIRE(strip.size() == 1u);
}

TEST_CASE("Empty margins scroll without a made up press target", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    touch_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(card_tree(&scrolled));

    // 클릭 대상이 없는 여백에서도 흐른다. 누름 관찰은 가짜 대상으로 부르지 않는다.
    static_cast<void>(controller.process(touch_press(350.0f, 150.0f, 0)));
    REQUIRE(policy.pressed.empty());
    REQUIRE(names_of(controller.process(touch_move(350.0f, 120.0f, 40))) == std::vector<std::u8string> { u8"scroll" });
    static_cast<void>(controller.process(touch_release(350.0f, 120.0f, 80)));
    REQUIRE(policy.clicked.empty());
}

TEST_CASE("A long touch press opens the menu and a short one clicks", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    touch_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(card_tree(&scrolled));

    // 누름 관찰은 터치도 누르는 순간 듣고, 장치를 안다.
    auto actions { controller.process(touch_press(60.0f, 120.0f, 0)) };
    REQUIRE(names_of(actions) == std::vector<std::u8string> { u8"press:card" });
    REQUIRE(policy.pressed == std::vector<luil::pointer_device> { luil::pointer_device::touch });
    // 손가락의 떨림은 봐준다.
    static_cast<void>(controller.process(touch_move(66.0f, 125.0f, 300)));
    actions = controller.process(touch_release(66.0f, 125.0f, 700));
    REQUIRE(names_of(actions) == std::vector<std::u8string> { u8"menu" });
    REQUIRE(policy.clicked.size() == 1u);

    // 메뉴를 연 누름은 연속 탭의 첫 번째가 아니다. 다음 짧은 탭은 그냥 클릭이다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 800, 2)));
    REQUIRE(names_of(controller.process(touch_release(60.0f, 120.0f, 850, 2))) == std::vector<std::u8string> { u8"click" });

    // 길게 누르기를 끄면 오래 누른 것도 탭이다.
    luil::touch_gesture_config config {};
    config.long_press_enabled = false;
    REQUIRE(controller.set_touch_config(config));
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 3000)));
    REQUIRE(names_of(controller.process(touch_release(60.0f, 120.0f, 4000))) == std::vector<std::u8string> { u8"click" });
}

TEST_CASE("A long press without a right click action is a tap", "[ui][interaction][touch]")
{
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto button { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"one" }) };
    button->arrange({ { 10.0f, 10.0f, 80.0f, 40.0f }, 1.0f });
    button->set_action(luil::ui_trigger::left_click, message_action(u8"click"));
    root->add(std::move(button));
    luil::interaction_controller controller {};
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    static_cast<void>(controller.process(touch_press(30.0f, 20.0f, 0)));
    REQUIRE(names_of(controller.process(touch_release(30.0f, 20.0f, 1000))) == std::vector<std::u8string> { u8"click" });
}

TEST_CASE("A contact that wandered off loses the long press for good", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled));

    // 시간 창이 지난 뒤 멀리 갔다 돌아온 접촉이다. 스크롤도 끌기도 아니고,
    // 한 번 허용치를 넘었으므로 메뉴도 클릭도 아니다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0)));
    static_cast<void>(controller.process(touch_move(100.0f, 120.0f, 500)));
    static_cast<void>(controller.process(touch_move(60.0f, 120.0f, 550)));
    REQUIRE(controller.process(touch_release(60.0f, 120.0f, 900)).empty());
    REQUIRE(scrolled.empty());
}

TEST_CASE("A touch on a drag handle starts at once and long presses only in place", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled, { .handle = true }));

    // 손잡이는 스크롤 후보가 아니다. 빠르게 쓸어도 손잡이가 움직인다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0)));
    REQUIRE(names_of(controller.process(touch_move(60.0f, 90.0f, 40))) == std::vector<std::u8string> { u8"move" });
    REQUIRE(scrolled.empty());
    // 실제로 옮긴 뒤에는 오래 있다 떼어도 메뉴가 아니다.
    static_cast<void>(controller.process(touch_move(60.0f, 120.0f, 80)));
    REQUIRE(controller.process(touch_release(60.0f, 120.0f, 1000)).empty());

    // 움직이지 않고 오래 누르면 메뉴다. 짧게 누르면 아무 일도 없다 (마우스와 같다).
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 2000)));
    REQUIRE(controller.process(touch_release(60.0f, 120.0f, 2100)).empty());
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 3000)));
    REQUIRE(names_of(controller.process(touch_release(62.0f, 121.0f, 3700))) == std::vector<std::u8string> { u8"menu" });
}

TEST_CASE("A held touch drags and drops after the pan window closes", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled, { .draggable = true }));

    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0)));
    static_cast<void>(controller.process(touch_move(60.0f, 200.0f, 500)));
    REQUIRE(controller.snapshot().drag.has_value());
    static_cast<void>(controller.process(touch_move(60.0f, 340.0f, 550)));
    REQUIRE(controller.snapshot().drag->hovered_drop_target.owner == u8"bin");
    REQUIRE(names_of(controller.process(touch_release(60.0f, 340.0f, 600))) == std::vector<std::u8string> { u8"drop" });
    REQUIRE(scrolled.empty());
    REQUIRE(controller.snapshot().drag.has_value() == false);
}

TEST_CASE("Touch taps make double clicks only with other taps", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled));

    // 접촉마다 포인터 id가 달라도 연속 탭이다. 거리 한계는 터치 허용치다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0, 4)));
    REQUIRE(names_of(controller.process(touch_release(60.0f, 120.0f, 50, 4))) == std::vector<std::u8string> { u8"click" });
    static_cast<void>(controller.process(touch_press(68.0f, 126.0f, 150, 5)));
    REQUIRE(names_of(controller.process(touch_release(68.0f, 126.0f, 200, 5))) == std::vector<std::u8string> { u8"double" });

    // 마우스 클릭 뒤의 탭은 더블 클릭이 아니다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 60.0f, 120.0f, luil::pointer_button::left, at(1000) }));
    REQUIRE(names_of(controller.process(luil::pointer_released_event { 60.0f, 120.0f, luil::pointer_button::left, at(1020) })) == std::vector<std::u8string> { u8"click" });
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 1100, 6)));
    REQUIRE(names_of(controller.process(touch_release(60.0f, 120.0f, 1150, 6))) == std::vector<std::u8string> { u8"click" });
    // 탭 뒤의 펜 접촉도 마찬가지다.
    static_cast<void>(controller.process(as_pen(luil::pointer_pressed_event { 60.0f, 120.0f, luil::pointer_button::left, at(1200) })));
    REQUIRE(names_of(controller.process(as_pen(luil::pointer_released_event { 60.0f, 120.0f, luil::pointer_button::left, at(1220) }))) == std::vector<std::u8string> { u8"click" });
}

TEST_CASE("One contact owns the controls and other fingers are swallowed", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled));

    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0, 1)));
    // 두 번째 손가락은 컨트롤 액션을 내지 않는다.
    REQUIRE(controller.process(touch_press(100.0f, 120.0f, 20, 2)).empty());
    REQUIRE(controller.process(touch_move(100.0f, 60.0f, 40, 2)).empty());
    REQUIRE(scrolled.empty());
    REQUIRE(controller.process(touch_release(100.0f, 60.0f, 60, 2)).empty());
    // 첫 손가락의 탭은 그대로다.
    REQUIRE(names_of(controller.process(touch_release(60.0f, 120.0f, 80, 1))) == std::vector<std::u8string> { u8"click" });

    // 첫 손가락을 뗀 뒤에도 남은 손가락은 새 누름이 되지 않는다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 1000, 3)));
    static_cast<void>(controller.process(touch_press(100.0f, 120.0f, 1010, 4)));
    REQUIRE(names_of(controller.process(touch_release(60.0f, 120.0f, 1020, 3))) == std::vector<std::u8string> { u8"click" });
    REQUIRE(controller.process(touch_move(100.0f, 60.0f, 1040, 4)).empty());
    REQUIRE(controller.process(touch_release(100.0f, 60.0f, 1060, 4)).empty());
    REQUIRE(scrolled.empty());
}

TEST_CASE("A real mouse press takes over from a touch contact", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled));

    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0)));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 60.0f, 120.0f, luil::pointer_button::left, at(20) }));
    // 취소된 터치의 나머지는 삼켜진다.
    REQUIRE(controller.process(touch_move(60.0f, 80.0f, 30)).empty());
    REQUIRE(controller.process(touch_release(60.0f, 80.0f, 40)).empty());
    REQUIRE(scrolled.empty());
    REQUIRE(names_of(controller.process(luil::pointer_released_event { 60.0f, 120.0f, luil::pointer_button::left, at(60) })) == std::vector<std::u8string> { u8"click" });
}

TEST_CASE("A cancelled contact runs no click, menu or drop", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled, { .draggable = true }));

    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0)));
    static_cast<void>(controller.process(luil::pointer_cancelled_event { luil::pointer_device::touch, 1, {}, at(700) }));
    REQUIRE(controller.process(touch_release(60.0f, 120.0f, 800)).empty());

    // 끌던 것도 놓지 않고 거둔다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 1000)));
    static_cast<void>(controller.process(touch_move(60.0f, 340.0f, 1500)));
    REQUIRE(controller.snapshot().drag.has_value());
    static_cast<void>(controller.process(luil::pointer_cancelled_event { luil::pointer_device::touch, 1, {}, at(1550) }));
    REQUIRE(controller.snapshot().drag.has_value() == false);
    REQUIRE(controller.process(touch_release(60.0f, 340.0f, 1600)).empty());

    // 다른 id의 취소는 쥔 접촉을 건드리지 않는다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 2000)));
    static_cast<void>(controller.process(luil::pointer_cancelled_event { luil::pointer_device::touch, 8, {}, at(2010) }));
    REQUIRE(names_of(controller.process(touch_release(60.0f, 120.0f, 2050))) == std::vector<std::u8string> { u8"click" });
}

TEST_CASE("A pen barrel switch cancels the left press before the right one", "[ui][interaction][pen]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled));

    // 어댑터는 버튼이 바뀌면 취소 뒤 새 버튼으로 누른다. 우클릭만 나간다.
    static_cast<void>(controller.process(as_pen(luil::pointer_pressed_event { 60.0f, 120.0f, luil::pointer_button::left, at(0) })));
    static_cast<void>(controller.process(luil::pointer_cancelled_event { luil::pointer_device::pen, 9, {}, at(30) }));
    static_cast<void>(controller.process(as_pen(luil::pointer_pressed_event { 60.0f, 120.0f, luil::pointer_button::right, at(30) })));
    const auto actions { controller.process(as_pen(luil::pointer_released_event { 60.0f, 120.0f, luil::pointer_button::right, at(60) })) };
    REQUIRE(names_of(actions) == std::vector<std::u8string> { u8"menu" });
}

TEST_CASE("Pen hover moves the hover without a press", "[ui][interaction][pen]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled));

    static_cast<void>(controller.process(as_pen(luil::pointer_moved_event { 60.0f, 120.0f, at(0) })));
    REQUIRE(controller.snapshot().hovered.owner == u8"card");
    static_cast<void>(controller.process(luil::pointer_left_event { {}, luil::pointer_device::pen }));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id {});

    // 터치는 hover를 남기지 않는다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 100)));
    static_cast<void>(controller.process(touch_release(60.0f, 120.0f, 150)));
    REQUIRE(controller.snapshot().hovered == luil::ui_element_id {});
}

TEST_CASE("A tree change under a contact cancels it without actions", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled));

    // 흘리던 창이 사라지면 그 접촉은 취소된다. 뗌은 클릭이 아니다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0)));
    static_cast<void>(controller.process(touch_move(60.0f, 90.0f, 40)));
    REQUIRE(scrolled.size() == 1u);
    auto bare { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    bare->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    auto card { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"card" }) };
    card->arrange({ { 20.0f, 20.0f, 200.0f, 200.0f }, 1.0f });
    card->set_action(luil::ui_trigger::left_click, message_action(u8"click"));
    bare->add(std::move(card));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(bare)));
    REQUIRE(controller.process(touch_move(60.0f, 60.0f, 80)).empty());
    REQUIRE(controller.process(touch_release(60.0f, 60.0f, 120)).empty());
    REQUIRE(scrolled.size() == 1u);

    // 탭하던 대상을 새 modal이 덮으면 뗌은 클릭이 아니다.
    controller.set_tree(card_tree(&scrolled));
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 1000)));
    auto covered { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    covered->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    auto under { std::make_unique<test_panel>(luil::ui_element_id { kind_card, u8"card" }) };
    under->arrange({ { 20.0f, 20.0f, 200.0f, 200.0f }, 1.0f });
    under->set_action(luil::ui_trigger::left_click, message_action(u8"click"));
    covered->add(std::move(under));
    auto scrim { std::make_unique<test_panel>(luil::ui_element_id { kind_scrim, u8"scrim" }) };
    scrim->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    scrim->set_hit_opaque(true);
    covered->add(std::move(scrim));
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(covered)));
    REQUIRE(controller.process(touch_release(60.0f, 120.0f, 1050)).empty());
}

TEST_CASE("A touch tap places the caret on release and a swipe leaves the focus alone", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    auto area { std::make_unique<test_panel>(luil::ui_element_id { kind_area, u8"area" }) };
    area->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    area->set_scroll_source(recording_source(&scrolled, u8"scroll", 1.0f, luil::scroll_axis::vertical));
    auto input { std::make_unique<test_panel>(luil::ui_element_id { kind_input, u8"query" }) };
    input->arrange({ { 0.0f, 100.0f, 300.0f, 40.0f }, 1.0f });
    input->set_action(luil::ui_trigger::left_click, message_action(u8"unused"));
    area->add(std::move(input));
    root->add(std::move(area));
    touch_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(std::make_shared<const luil::ui_tree>(std::move(root)));

    // 칸을 쓸면 부모가 흐른다. 초점도 caret도 움직이지 않는다.
    static_cast<void>(controller.process(touch_press(50.0f, 120.0f, 0)));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
    REQUIRE(names_of(controller.process(touch_move(50.0f, 90.0f, 40))) == std::vector<std::u8string> { u8"scroll" });
    static_cast<void>(controller.process(touch_release(50.0f, 90.0f, 80)));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
    REQUIRE(policy.edits.empty());

    // 탭은 뗄 때 초점을 주고 그 자리에 caret을 놓는다. 클릭 액션은 아니다.
    static_cast<void>(controller.process(touch_press(52.0f, 120.0f, 1000)));
    REQUIRE(policy.edits.empty());
    const auto actions { controller.process(touch_release(52.0f, 120.0f, 1050)) };
    REQUIRE(names_of(actions) == std::vector<std::u8string> { u8"edit" });
    REQUIRE(controller.snapshot().focused_input.owner == u8"query");
    REQUIRE(policy.edits.back().command == luil::text::text_edit_command::place_caret);
    REQUIRE(policy.edits.back().offset == 5u);
}

TEST_CASE("Touch settings change at run time and reject bad values", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled));

    // 잘못된 값은 거절되고 직전 값이 남는다.
    luil::touch_gesture_config invalid {};
    invalid.pan_start_time = std::chrono::milliseconds { 700 };
    REQUIRE(controller.set_touch_config(invalid) == false);
    invalid = {};
    invalid.press_move_tolerance = 0.0f;
    REQUIRE(controller.set_touch_config(invalid) == false);

    // 스크롤을 끄면 대기 중인 접촉이 취소되고 남은 이벤트는 삼켜진다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0)));
    luil::touch_gesture_config off {};
    off.pan_enabled = false;
    REQUIRE(controller.set_touch_config(off));
    REQUIRE(controller.process(touch_move(60.0f, 80.0f, 40)).empty());
    // 다시 켜도 아직 닿아 있는 손가락은 새 접촉이 아니다.
    REQUIRE(controller.set_touch_config({}));
    REQUIRE(controller.process(touch_move(60.0f, 40.0f, 60)).empty());
    REQUIRE(controller.process(touch_release(60.0f, 40.0f, 80)).empty());
    REQUIRE(scrolled.empty());

    // 스크롤이 꺼져 있으면 빠른 쓸기도 흘리지 않는다.
    REQUIRE(controller.set_touch_config(off));
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 1000)));
    REQUIRE(controller.process(touch_move(60.0f, 80.0f, 1040)).empty());
    REQUIRE(scrolled.empty());
}

TEST_CASE("A gap in the input queue drops the touch contact", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled));

    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0)));
    controller.cancel_dropped_gestures();
    REQUIRE(controller.process(touch_move(60.0f, 80.0f, 40)).empty());
    REQUIRE(controller.process(touch_release(60.0f, 80.0f, 80)).empty());
    REQUIRE(scrolled.empty());
}

TEST_CASE("Pan routing stops at opaque barriers and disabled branches", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    auto area { std::make_unique<test_panel>(luil::ui_element_id { kind_area, u8"area" }) };
    area->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });
    area->set_scroll_source(recording_source(&scrolled, u8"scroll", 1.5f, luil::scroll_axis::vertical));
    auto disabled { std::make_unique<test_panel>(luil::ui_element_id { kind_area, u8"disabled" }) };
    disabled->arrange({ { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    disabled->set_scroll_source(recording_source(&scrolled, u8"disabled", 1.0f, luil::scroll_axis::vertical));
    disabled->set_enabled(false);
    area->add(std::move(disabled));
    root->add(std::move(area));
    auto scrim { std::make_unique<test_panel>(luil::ui_element_id { kind_scrim, u8"scrim" }) };
    scrim->arrange({ { 200.0f, 200.0f, 200.0f, 200.0f }, 1.0f });
    scrim->set_hit_opaque(true);
    root->add(std::move(scrim));
    const luil::ui_tree tree { std::move(root) };

    const std::optional<luil::pan_target> open { luil::route_pan(tree, 150.0f, 50.0f, luil::scroll_axis::vertical) };
    REQUIRE(open.has_value());
    REQUIRE(open->id.owner == u8"area");
    REQUIRE(open->scale == 1.5f);
    // 비활성 가지는 지나쳐 바깥 창이 받는다.
    REQUIRE(luil::route_pan(tree, 50.0f, 50.0f, luil::scroll_axis::vertical)->id.owner == u8"area");
    // 막는 것이 먼저 걸리면 없다.
    REQUIRE(luil::route_pan(tree, 300.0f, 300.0f, luil::scroll_axis::vertical).has_value() == false);
    // 축이 맞는 것이 없으면 없다.
    REQUIRE(luil::route_pan(tree, 150.0f, 50.0f, luil::scroll_axis::horizontal).has_value() == false);

    // 표로 이름 대는 짝도 같은 규칙이다.
    const luil::pan_route routes[] { { luil::ui_element_id { kind_area, u8"area" }, luil::scroll_axis::horizontal, 2.0f, [](float) { return luil::input_action {}; } } };
    REQUIRE(luil::route_pan(tree, 150.0f, 50.0f, luil::scroll_axis::horizontal, routes)->scale == 2.0f);
    REQUIRE(luil::route_pan(tree, 150.0f, 50.0f, luil::scroll_axis::vertical, routes).has_value() == false);
    REQUIRE(luil::route_pan(tree, 300.0f, 300.0f, luil::scroll_axis::horizontal, routes).has_value() == false);
}

TEST_CASE("Touch settings are validated", "[ui][interaction][touch]")
{
    REQUIRE(luil::valid_touch_gesture_config({}));
    luil::touch_gesture_config config {};
    config.pan_start_distance = std::numeric_limits<float>::infinity();
    REQUIRE(luil::valid_touch_gesture_config(config) == false);
    config = {};
    config.long_press_time = std::chrono::milliseconds { 0 };
    REQUIRE(luil::valid_touch_gesture_config(config) == false);
    config = {};
    config.pan_start_time = config.long_press_time;
    REQUIRE(luil::valid_touch_gesture_config(config) == false);
}

TEST_CASE("An overflowing strip with a scroll factory flows horizontally", "[ui][strip][touch]")
{
    std::vector<float> scrolled {};
    luil::strip_config config {};
    config.content_width = 400.0f;
    config.scroll = [&scrolled](const float delta) {
        scrolled.push_back(delta);
        return luil::input_action {};
    };
    luil::strip_element strip { luil::ui_element_id { kind_area, u8"strip" }, config };
    strip.arrange({ { 0.0f, 0.0f, 300.0f, 40.0f }, 1.5f });
    const luil::scroll_source* const source { strip.scroll() };
    REQUIRE(source != nullptr);
    REQUIRE(source->axis == luil::scroll_axis::horizontal);
    REQUIRE(source->scale == 1.5f);

    // 넘치지 않으면 흘릴 것이 없다.
    strip.arrange({ { 0.0f, 0.0f, 900.0f, 40.0f }, 1.5f });
    REQUIRE(strip.scroll() == nullptr);
}

TEST_CASE("A contact on a surface that goes away is cancelled", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    std::vector<float> popup_scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled));
    controller.set_surface_trees({ { u8"popup", card_tree(&popup_scrolled) } });

    // popup에서 흘리던 접촉이다. 누름은 스크롤이 서며 이미 거뒀으므로 접촉만 남아 있다.
    luil::pointer_pressed_event press { touch_press(60.0f, 120.0f, 0) };
    press.surface = u8"popup";
    static_cast<void>(controller.process(press));
    luil::pointer_moved_event move { touch_move(60.0f, 90.0f, 40) };
    move.surface = u8"popup";
    static_cast<void>(controller.process(move));
    REQUIRE(popup_scrolled.size() == 1u);

    // 표면이 사라지면 그 접촉도 거둔다. 늦게 온 뗌은 아무것도 내지 않는다.
    controller.set_surface_trees({});
    luil::pointer_released_event release { touch_release(60.0f, 90.0f, 80) };
    release.surface = u8"popup";
    REQUIRE(controller.process(release).empty());

    // 쥔 접촉이 남지 않았으므로 주 창의 새 접촉이 곧바로 선다.
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 1000, 2)));
    REQUIRE(names_of(controller.process(touch_release(60.0f, 120.0f, 1050, 2))) == std::vector<std::u8string> { u8"click" });
}

TEST_CASE("Mouse handles reject pen hover and unrelated releases", "[ui][interaction][pen]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled, { .handle = true }));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 60.0f, 120.0f, luil::pointer_button::left, at(0) }));
    REQUIRE(controller.process(as_pen(luil::pointer_moved_event { 60.0f, 90.0f, at(20) })).empty());
    REQUIRE(controller.process(as_pen(luil::pointer_released_event { 60.0f, 90.0f, luil::pointer_button::left, at(30) })).empty());
    REQUIRE(controller.process(luil::pointer_released_event { 60.0f, 90.0f, luil::pointer_button::right, at(40) }).empty());
    luil::pointer_moved_event elsewhere { 60.0f, 80.0f, at(50) };
    elsewhere.surface = u8"other";
    REQUIRE(controller.process(elsewhere).empty());
    REQUIRE(names_of(controller.process(luil::pointer_moved_event { 60.0f, 90.0f, at(60) })) == std::vector<std::u8string> { u8"move" });
    static_cast<void>(controller.process(luil::pointer_released_event { 60.0f, 90.0f, luil::pointer_button::left, at(70) }));
    REQUIRE(controller.process(luil::pointer_moved_event { 60.0f, 80.0f, at(80) }).empty());
}

TEST_CASE("A pen owns its drag until the matching pointer ends it", "[ui][interaction][pen]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled, { .draggable = true }));
    controller.set_surface_trees({ { u8"other", card_tree(&scrolled, { .draggable = true }) } });
    static_cast<void>(controller.process(as_pen(luil::pointer_pressed_event { 60.0f, 120.0f, luil::pointer_button::left, at(0) })));
    static_cast<void>(controller.process(luil::pointer_moved_event { 60.0f, 340.0f, at(20) }));
    REQUIRE(controller.snapshot().drag.has_value() == false);
    auto other { as_pen(luil::pointer_moved_event { 60.0f, 340.0f, at(30) }) };
    other.pointer_id = 10;
    static_cast<void>(controller.process(other));
    REQUIRE(controller.snapshot().drag.has_value() == false);
    static_cast<void>(controller.process(as_pen(luil::pointer_moved_event { 60.0f, 340.0f, at(40) })));
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(controller.process(luil::pointer_released_event { 60.0f, 340.0f, luil::pointer_button::left, at(50) }).empty());
    auto other_release { as_pen(luil::pointer_released_event { 60.0f, 340.0f, luil::pointer_button::left, at(55) }) };
    other_release.surface = u8"other";
    REQUIRE(controller.process(other_release).empty());
    static_cast<void>(controller.process(luil::pointer_cancelled_event { luil::pointer_device::pen, 10, {}, at(60) }));
    static_cast<void>(controller.process(luil::pointer_cancelled_event { luil::pointer_device::pen, 9, u8"other", at(70) }));
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(names_of(controller.process(as_pen(luil::pointer_released_event { 60.0f, 340.0f, luil::pointer_button::left, at(80) }))) == std::vector<std::u8string> { u8"drop" });
}

TEST_CASE("A new device press cancels the old handle before taking over", "[ui][interaction][pen]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled, { .handle = true }));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 60.0f, 120.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(as_pen(luil::pointer_pressed_event { 60.0f, 120.0f, luil::pointer_button::right, at(20) })));
    REQUIRE(controller.process(luil::pointer_released_event { 60.0f, 120.0f, luil::pointer_button::left, at(30) }).empty());
    REQUIRE(controller.process(as_pen(luil::pointer_moved_event { 60.0f, 90.0f, at(40) })).empty());
    REQUIRE(names_of(controller.process(as_pen(luil::pointer_released_event { 60.0f, 90.0f, luil::pointer_button::right, at(50) }))) == std::vector<std::u8string> { u8"menu" });
}

TEST_CASE("Touch handles stop when hidden disabled or covered", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled, { .handle = true }));
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0)));
    card_options changed { .handle = true };
    SECTION("hidden")
    {
        changed.visible = false;
    }
    SECTION("disabled")
    {
        changed.enabled = false;
    }
    SECTION("modal")
    {
        changed.covered = true;
    }
    controller.set_tree(card_tree(&scrolled, changed));
    REQUIRE(controller.process(touch_move(60.0f, 90.0f, 40)).empty());
    // 같은 id가 돌아와도 취소된 접촉을 되살리지 않는다.
    controller.set_tree(card_tree(&scrolled, { .handle = true }));
    REQUIRE(controller.process(touch_move(60.0f, 90.0f, 80)).empty());
    REQUIRE(controller.process(touch_release(60.0f, 90.0f, 700)).empty());
}

TEST_CASE("A hidden touch drag source cannot start or drop", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled, { .draggable = true }));
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0)));
    SECTION("before drag")
    {
        controller.set_tree(card_tree(&scrolled, { .draggable = true, .visible = false }));
        static_cast<void>(controller.process(touch_move(60.0f, 340.0f, 500)));
        REQUIRE(controller.snapshot().drag.has_value() == false);
        controller.set_tree(card_tree(&scrolled, { .draggable = true }));
        static_cast<void>(controller.process(touch_move(60.0f, 340.0f, 550)));
        REQUIRE(controller.snapshot().drag.has_value() == false);
    }
    SECTION("during drag")
    {
        static_cast<void>(controller.process(touch_move(60.0f, 340.0f, 500)));
        REQUIRE(controller.snapshot().drag.has_value());
        controller.set_tree(card_tree(&scrolled, { .draggable = true, .visible = false }));
    }
    REQUIRE(controller.process(touch_release(60.0f, 340.0f, 600)).empty());
    REQUIRE(controller.snapshot().drag.has_value() == false);
}

TEST_CASE("Disabling a handle long press preserves dragging but removes the menu", "[ui][interaction][touch]")
{
    std::vector<float> scrolled {};
    luil::interaction_controller controller {};
    controller.set_tree(card_tree(&scrolled, { .handle = true }));
    static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 0)));
    luil::touch_gesture_config off {};
    off.long_press_enabled = false;
    REQUIRE(controller.set_touch_config(off));
    SECTION("still held after reenabling")
    {
        REQUIRE(controller.set_touch_config({}));
        REQUIRE(controller.process(touch_release(60.0f, 120.0f, 700)).empty());
        static_cast<void>(controller.process(touch_press(60.0f, 120.0f, 1000)));
        REQUIRE(names_of(controller.process(touch_release(60.0f, 120.0f, 1700))) == std::vector<std::u8string> { u8"menu" });
    }
    SECTION("continue dragging")
    {
        REQUIRE(names_of(controller.process(touch_move(60.0f, 90.0f, 40))) == std::vector<std::u8string> { u8"move" });
        REQUIRE(controller.process(touch_release(60.0f, 90.0f, 700)).empty());
    }
}
