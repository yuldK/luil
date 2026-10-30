#include "raster_probe.h"

#include "ui/badge_paint.h"

#include "luil/theme/ui_style.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/tab_bar_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"
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
    constexpr float raster_scale { 2.0f };

    struct select_intent
    {
        std::u8string key {};
    };

    struct close_intent
    {
        std::u8string key {};
    };

    struct scroll_intent
    {
        float delta { 0.0f };
    };

    // 막대 끝의 꼬리다 (새 탭 단추 자리).
    class trailing_button final : public luil::ui_element
    {
    public:
        trailing_button()
            : ui_element { luil::ui_element_id { luil::ui_element_kind::app_bar_button, u8"new-tab" } }
        {
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
    };

    [[nodiscard]] luil::tab_bar_config make_config(const std::size_t count)
    {
        luil::tab_bar_config config {};
        config.owner = u8"tabs";
        for (std::size_t index = 0; index < count; ++index)
        {
            luil::tab_item item {};
            item.key = u8"tab-" + std::u8string { static_cast<char8_t>(u8'a' + index) };
            config.items.push_back(std::move(item));
        }
        return config;
    }

    void with_messages(luil::tab_bar_config& config)
    {
        config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { select_intent { key } } }; };
        config.close = [](const std::u8string& key) { return luil::input_action { luil::app_message { close_intent { key } } }; };
        config.scroll = [](const float delta) { return luil::input_action { luil::app_message { scroll_intent { delta } } }; };
    }

    [[nodiscard]] luil::tab_bar_style style_of(const luil::tab_bar_variant variant)
    {
        luil::tab_bar_style style {};
        style.variant = variant;
        return style;
    }

    [[nodiscard]] const luil::ui_element& tab_lane(const luil::tab_bar_element& bar)
    {
        return *bar.children()[0]->children()[0];
    }

    [[nodiscard]] const luil::ui_element& tab_at(const luil::tab_bar_element& bar, const std::size_t index)
    {
        return *tab_lane(bar).children()[index];
    }

    template<typename T>
    [[nodiscard]] const T* single_message(std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1)
            return nullptr;
        auto* const message { std::get_if<luil::app_message>(&actions[0]) };
        return message != nullptr ? message->get<T>() : nullptr;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    // 막대 하나를 그 자리에 배치한 tree다. 막대는 tree 안에서 root다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> bar_tree(luil::tab_bar_config config, const float width, const float scale = 1.0f, const luil::ui_style* style = nullptr)
    {
        return std::make_shared<const luil::ui_tree>(
            luil::make_arranged_tree(std::make_unique<luil::tab_bar_element>(std::move(config)), { 0.0f, 0.0f, width * scale, 36.0f * scale }, scale, {}, nullptr, style));
    }

    [[nodiscard]] const luil::tab_bar_element& bar_of(const luil::ui_tree& tree)
    {
        return static_cast<const luil::tab_bar_element&>(*tree.root());
    }

    // 래스터 막대다: 네 탭(160 논리 픽셀), 둘째가 선택, 배율 2라 탭 하나가 물리 320 폭이고 막대는 72 높이다.
    [[nodiscard]] luil::testing::raster_frame draw_bar(const luil::color_theme theme, const luil::tab_bar_variant variant, const luil::interaction_snapshot& interaction = {})
    {
        luil::tab_bar_config config { make_config(4) };
        config.selected = u8"tab-b";
        config.style = style_of(variant);
        const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::tab_bar_element>(std::move(config)), { 0.0f, 0.0f, 1280.0f, 72.0f }, raster_scale) };
        luil::testing::raster_frame frame { 1280, 72, luil::color_palette_for(theme), raster_scale };
        frame.draw(tree, interaction);
        return frame;
    }

    // 막대 바닥선 위쪽 띠에서 그 색의 픽셀 수다 — 바닥선은 모든 탭 밑을 지나므로 탭끼리 견줄 때 뺀다.
    [[nodiscard]] int count_above_floor(const luil::testing::raster_frame& frame, const float left, const luil::ui_color color)
    {
        return frame.count_color({ left, 0.0f, 320.0f, 68.0f }, color);
    }
}

TEST_CASE("The fitted tab width shrinks evenly down to the minimum", "[ui][tab][layout]")
{
    // 다 들어가면 원래 폭이다.
    REQUIRE(luil::fitted_tab_width(4, 160.0f, 100.0f, 1000.0f) == 160.0f);
    // 모자라면 같은 폭으로 나눈다.
    REQUIRE(luil::fitted_tab_width(10, 160.0f, 100.0f, 1200.0f) == 120.0f);
    // 최소 밑으로는 줄이지 않는다.
    REQUIRE(luil::fitted_tab_width(10, 160.0f, 100.0f, 500.0f) == 100.0f);
    // 최소가 0이거나 원래 폭 이상이면 줄이지 않는다 (처음부터의 동작).
    REQUIRE(luil::fitted_tab_width(10, 160.0f, 0.0f, 500.0f) == 160.0f);
    REQUIRE(luil::fitted_tab_width(10, 160.0f, 200.0f, 500.0f) == 160.0f);
}

TEST_CASE("A step reveals one whole tab at the edge it moves toward", "[ui][tab][layout]")
{
    // 탭 100, 띠 250, 최대 750 (탭 열 개).
    REQUIRE(luil::tab_step_offset(0.0f, 100.0f, 250.0f, 750.0f, 1) == 50.0f);
    REQUIRE(luil::tab_step_offset(50.0f, 100.0f, 250.0f, 750.0f, 1) == 150.0f);
    REQUIRE(luil::tab_step_offset(150.0f, 100.0f, 250.0f, 750.0f, -1) == 100.0f);
    REQUIRE(luil::tab_step_offset(100.0f, 100.0f, 250.0f, 750.0f, -1) == 0.0f);
    // 두 칸은 경계 둘이다.
    REQUIRE(luil::tab_step_offset(0.0f, 100.0f, 250.0f, 750.0f, 2) == 150.0f);
    // 끝에서는 더 가지 않는다.
    REQUIRE(luil::tab_step_offset(750.0f, 100.0f, 250.0f, 750.0f, 1) == 750.0f);
    REQUIRE(luil::tab_step_offset(0.0f, 100.0f, 250.0f, 750.0f, -1) == 0.0f);
    // 끝에서 뒤로 가면 왼쪽에 걸친 탭이 온전히 드러난다.
    REQUIRE(luil::tab_step_offset(750.0f, 100.0f, 250.0f, 750.0f, -1) == 700.0f);
}

TEST_CASE("A tab bar shrinks its tabs before it overflows and reports the width", "[ui][tab][layout]")
{
    luil::tab_bar_config config { make_config(10) };
    config.tab_width = 160.0f;
    config.min_tab_width = 100.0f;
    luil::tab_bar_element wide { config };
    wide.arrange({ { 0.0f, 0.0f, 1200.0f, 36.0f }, 1.0f });
    REQUIRE(wide.tab_width() == 120.0f);
    REQUIRE(wide.overflowing() == false);
    REQUIRE(wide.maximum_scroll() == 0.0f);
    REQUIRE(tab_at(wide, 9).bounds().x == 1080.0f);
    REQUIRE(tab_at(wide, 9).bounds().width == 120.0f);

    luil::tab_bar_element narrow { config };
    narrow.arrange({ { 0.0f, 0.0f, 900.0f, 36.0f }, 1.0f });
    REQUIRE(narrow.tab_width() == 100.0f);
    REQUIRE(narrow.overflowing());
    REQUIRE(narrow.maximum_scroll() == 100.0f);

    // 최소 폭이 없으면 처음부터의 동작이다 — 줄지 않고 넘친다.
    luil::tab_bar_config fixed { make_config(10) };
    luil::tab_bar_element unchanged { std::move(fixed) };
    unchanged.arrange({ { 0.0f, 0.0f, 1200.0f, 36.0f }, 1.0f });
    REQUIRE(unchanged.tab_width() == 160.0f);
    REQUIRE(unchanged.overflowing());
    REQUIRE(unchanged.maximum_scroll() == 400.0f);
}

TEST_CASE("A trailing element follows the last tab and pins to the end when tabs overflow", "[ui][tab][layout]")
{
    const auto make = [](const std::size_t count) {
        luil::tab_bar_config config { make_config(count) };
        config.tab_width = 100.0f;
        config.trailing_width = 30.0f;
        config.trailing = [] { return std::unique_ptr<luil::ui_element> { std::make_unique<trailing_button>() }; };
        return config;
    };

    // 탭이 적으면 마지막 탭 바로 뒤다.
    luil::tab_bar_element few { make(3) };
    few.arrange({ { 0.0f, 0.0f, 800.0f, 36.0f }, 1.0f });
    const luil::ui_element& short_tail { *few.children().back() };
    REQUIRE(short_tail.id().owner == u8"new-tab");
    REQUIRE(short_tail.bounds().x == 300.0f);
    REQUIRE(few.overflowing() == false);

    // 꼬리까지 들어가지 않으면 넘침이고, 꼬리는 오른쪽 끝에 고정된다.
    luil::tab_bar_element many { make(8) };
    many.arrange({ { 0.0f, 0.0f, 800.0f, 36.0f }, 1.0f });
    REQUIRE(many.overflowing());
    REQUIRE(many.children().back()->bounds().x == 770.0f);
    // 띠는 꼬리 밑으로 지나가지 않는다.
    REQUIRE(many.children()[0]->bounds().width == 770.0f);
}

TEST_CASE("Step buttons sit at both ends and move one tab at a time", "[ui][tab][overflow]")
{
    luil::tab_bar_config config { make_config(10) };
    with_messages(config);
    config.tab_width = 100.0f;
    config.overflow_mode = luil::tab_overflow_mode::step;
    config.step_back_tooltip = u8"Previous tabs";
    config.step_forward_tooltip = u8"Next tabs";
    config.overflow = [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; };
    config.overflow_tooltip = u8"All tabs";
    luil::tab_bar_element bar { std::move(config) };
    bar.arrange({ { 0.0f, 0.0f, 400.0f, 36.0f }, 1.0f });

    // [앞] 띠 [뒤] [넘침 메뉴] 순이다.
    const luil::ui_element* back { nullptr };
    const luil::ui_element* forward { nullptr };
    const luil::ui_element* menu { nullptr };
    for (const auto& child : bar.children())
        if (child->id().kind == luil::ui_element_kind::tab_step_back)
            back = child.get();
        else if (child->id().kind == luil::ui_element_kind::tab_step_forward)
            forward = child.get();
        else if (child->id().kind == luil::ui_element_kind::tab_overflow)
            menu = child.get();
    REQUIRE(back != nullptr);
    REQUIRE(forward != nullptr);
    REQUIRE(menu != nullptr);
    REQUIRE(back->visible());
    REQUIRE(back->bounds().x == 0.0f);
    REQUIRE(bar.children()[0]->bounds().x == 28.0f);
    REQUIRE(forward->bounds().x == 400.0f - 28.0f * 2.0f);
    REQUIRE(menu->bounds().x == 400.0f - 28.0f);
    // 처음이라 앞 단추는 비활성이다.
    REQUIRE(back->enabled() == false);
    REQUIRE(forward->enabled());
    // 단추는 Tab 자리가 아니다. 이름은 tooltip이다.
    REQUIRE(back->tab_stop() == false);
    REQUIRE(forward->access_name() == u8"Next tabs");

    // 띠 폭 316 — 오른쪽에 걸친 넷째 탭이 온전히 드러나는 자리(400 - 316 = 84)로 간다.
    auto stepped { (*forward->action(luil::ui_trigger::left_click))({ forward->id(), 0.0f, 0.0f }) };
    const auto* const scrolled { single_message<scroll_intent>(stepped) };
    REQUIRE(scrolled != nullptr);
    REQUIRE(scrolled->delta == 84.0f);

    // 넘치지 않으면 단추가 없다.
    luil::tab_bar_config few { make_config(2) };
    with_messages(few);
    few.overflow_mode = luil::tab_overflow_mode::step;
    luil::tab_bar_element calm { std::move(few) };
    calm.arrange({ { 0.0f, 0.0f, 400.0f, 36.0f }, 1.0f });
    for (const auto& child : calm.children())
        if (child->id().kind == luil::ui_element_kind::tab_step_back || child->id().kind == luil::ui_element_kind::tab_step_forward)
            REQUIRE(child->visible() == false);
}

TEST_CASE("The wheel moves one tab per notch and gathers fine scrolling", "[ui][tab][overflow][input]")
{
    luil::tab_bar_config config { make_config(10) };
    with_messages(config);
    config.tab_width = 100.0f;
    config.wheel_steps = true;
    luil::interaction_controller controller {};
    controller.set_tree(bar_tree(std::move(config), 250.0f));

    // 아래로 한 칸이면 오른쪽에 걸친 탭이 온전히 드러난다 (띠 250 → 50).
    auto notch { controller.process(luil::mouse_wheel_event { .x = 100.0f, .y = 18.0f, .delta = -120.0f, .time = at(0) }) };
    const auto* const first { single_message<scroll_intent>(notch) };
    REQUIRE(first != nullptr);
    REQUIRE(first->delta == 50.0f);

    // 잘게 오는 휠은 한 칸이 찰 때까지 아무것도 내지 않는다.
    REQUIRE(controller.process(luil::mouse_wheel_event { .x = 100.0f, .y = 18.0f, .delta = -40.0f, .time = at(10) }).empty());
    REQUIRE(controller.process(luil::mouse_wheel_event { .x = 100.0f, .y = 18.0f, .delta = -40.0f, .time = at(20) }).empty());
    auto filled { controller.process(luil::mouse_wheel_event { .x = 100.0f, .y = 18.0f, .delta = -40.0f, .time = at(30) }) };
    REQUIRE(single_message<scroll_intent>(filled) != nullptr);

    // 가로 휠도 한 칸에 탭 하나다 (오른쪽이 앞).
    auto sideways { controller.process(luil::mouse_wheel_event { .x = 100.0f, .y = 18.0f, .delta = 120.0f, .time = at(40), .horizontal = true }) };
    const auto* const horizontal { single_message<scroll_intent>(sideways) };
    REQUIRE(horizontal != nullptr);
    REQUIRE(horizontal->delta == 50.0f);
}

TEST_CASE("A middle click closes the tab under it without selecting it", "[ui][tab][input]")
{
    luil::tab_bar_config config { make_config(3) };
    with_messages(config);
    config.selected = u8"tab-a";
    for (luil::tab_item& item : config.items)
        item.closable = true;
    luil::interaction_controller controller {};
    controller.set_tree(bar_tree(config, 600.0f));

    SECTION("press and release on the same tab")
    {
        static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 200.0f, .y = 18.0f, .button = luil::pointer_button::middle, .time = at(0) }));
        auto actions { controller.process(luil::pointer_released_event { .x = 202.0f, .y = 18.0f, .button = luil::pointer_button::middle, .time = at(50) }) };
        const auto* const closed { single_message<close_intent>(actions) };
        REQUIRE(closed != nullptr);
        REQUIRE(closed->key == u8"tab-b");
        // 선택·초점·눌림은 그대로다.
        REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
        REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});
    }

    SECTION("on the close button of that tab")
    {
        // 둘째 탭(160..320)의 닫기 단추는 오른쪽에서 6 들어온 18 폭이다.
        static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 305.0f, .y = 18.0f, .button = luil::pointer_button::middle, .time = at(0) }));
        auto actions { controller.process(luil::pointer_released_event { .x = 305.0f, .y = 18.0f, .button = luil::pointer_button::middle, .time = at(50) }) };
        const auto* const closed { single_message<close_intent>(actions) };
        REQUIRE(closed != nullptr);
        REQUIRE(closed->key == u8"tab-b");
    }

    SECTION("moving past the drag threshold cancels it")
    {
        static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 200.0f, .y = 18.0f, .button = luil::pointer_button::middle, .time = at(0) }));
        static_cast<void>(controller.process(luil::pointer_moved_event { .x = 240.0f, .y = 18.0f, .time = at(20) }));
        static_cast<void>(controller.process(luil::pointer_moved_event { .x = 200.0f, .y = 18.0f, .time = at(40) }));
        REQUIRE(controller.process(luil::pointer_released_event { .x = 200.0f, .y = 18.0f, .button = luil::pointer_button::middle, .time = at(60) }).empty());
    }

    SECTION("releasing over another tab is not a click")
    {
        static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 200.0f, .y = 18.0f, .button = luil::pointer_button::middle, .time = at(0) }));
        REQUIRE(controller.process(luil::pointer_released_event { .x = 340.0f, .y = 18.0f, .button = luil::pointer_button::middle, .time = at(50) }).empty());
    }

    SECTION("the bar can turn it off")
    {
        config.middle_click_close = false;
        controller.set_tree(bar_tree(config, 600.0f));
        static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 200.0f, .y = 18.0f, .button = luil::pointer_button::middle, .time = at(0) }));
        REQUIRE(controller.process(luil::pointer_released_event { .x = 200.0f, .y = 18.0f, .button = luil::pointer_button::middle, .time = at(50) }).empty());
    }
}

TEST_CASE("A tab that cannot close ignores the middle button", "[ui][tab][input]")
{
    luil::tab_bar_config config { make_config(2) };
    with_messages(config);
    luil::interaction_controller controller {};
    controller.set_tree(bar_tree(std::move(config), 400.0f));
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 50.0f, .y = 18.0f, .button = luil::pointer_button::middle, .time = at(0) }));
    REQUIRE(controller.process(luil::pointer_released_event { .x = 50.0f, .y = 18.0f, .button = luil::pointer_button::middle, .time = at(50) }).empty());
}

TEST_CASE("A hidden close button lets a touch tap select its tab", "[ui][tab][touch]")
{
    luil::tab_bar_config config { make_config(2) };
    with_messages(config);
    config.selected = u8"tab-a";
    for (luil::tab_item& item : config.items)
        item.closable = true;
    luil::tab_bar_style style {};
    style.close_visibility = luil::tab_close_visibility::selected_or_hovered;
    config.style = style;
    const auto tree { bar_tree(config, 400.0f) };

    // 고르지 않은 탭의 단추만 숨을 수 있다. 숨어도 element는 그대로 있어 동작과 자리가 지금과 같다.
    const luil::ui_element& selected_close { *tab_at(bar_of(*tree), 0).children()[0] };
    const luil::ui_element& hidden_close { *tab_at(bar_of(*tree), 1).children()[0] };
    REQUIRE(selected_close.hover_only() == false);
    REQUIRE(hidden_close.hover_only());
    REQUIRE(hidden_close.visible());
    REQUIRE(hidden_close.action(luil::ui_trigger::left_click) != nullptr);

    luil::interaction_controller controller {};
    controller.set_tree(tree);
    const float x { hidden_close.bounds().x + 9.0f };
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = x, .y = 18.0f, .time = at(0), .device = luil::pointer_device::touch, .pointer_id = 7 }));
    auto actions { controller.process(luil::pointer_released_event { .x = x, .y = 18.0f, .time = at(60), .device = luil::pointer_device::touch, .pointer_id = 7 }) };
    const auto* const selected { single_message<select_intent>(actions) };
    REQUIRE(selected != nullptr);
    REQUIRE(selected->key == u8"tab-b");

    // 늘 보이는 단추는 터치로도 닫는다 (처음부터의 동작).
    config.style.reset();
    controller.set_tree(bar_tree(config, 400.0f));
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = x, .y = 18.0f, .time = at(1000), .device = luil::pointer_device::touch, .pointer_id = 8 }));
    auto closing { controller.process(luil::pointer_released_event { .x = x, .y = 18.0f, .time = at(1060), .device = luil::pointer_device::touch, .pointer_id = 8 }) };
    const auto* const closed { single_message<close_intent>(closing) };
    REQUIRE(closed != nullptr);
    REQUIRE(closed->key == u8"tab-b");
}

TEST_CASE("The app style reaches every tab bar through the arrange context", "[ui][tab][style]")
{
    // 공용 스타일: 앱이 스타일에 한 번 적으면 config에 적지 않은 막대가 그 모양이다. 막대의 config가 덮으면 그 막대만 다르다.
    luil::ui_style style { luil::default_ui_style() };
    style.tab_bar.close_visibility = luil::tab_close_visibility::selected_or_hovered;

    luil::tab_bar_config config { make_config(2) };
    with_messages(config);
    for (luil::tab_item& item : config.items)
        item.closable = true;
    const auto styled { bar_tree(config, 400.0f, 1.0f, &style) };
    REQUIRE(tab_at(bar_of(*styled), 1).children()[0]->hover_only());

    const auto plain { bar_tree(config, 400.0f) };
    REQUIRE(tab_at(bar_of(*plain), 1).children()[0]->hover_only() == false);

    config.style = luil::tab_bar_style {};
    const auto overridden { bar_tree(config, 400.0f, 1.0f, &style) };
    REQUIRE(tab_at(bar_of(*overridden), 1).children()[0]->hover_only() == false);
}

TEST_CASE("A tab indicator speaks after the badge and spins only when motion is allowed", "[ui][tab][accessibility]")
{
    luil::tab_bar_config config { make_config(2) };
    config.items[0].label = u8"Report";
    config.items[0].badge = luil::tab_badge { .text = u8"3", .description = u8"3 unread" };
    config.items[0].indicator = luil::tab_indicator { .kind = luil::tab_indicator_kind::busy, .description = u8"Loading" };
    config.items[1].label = u8"Notes";
    config.items[1].indicator = luil::tab_indicator { .kind = luil::tab_indicator_kind::dot };
    const auto tree { bar_tree(std::move(config), 400.0f) };

    REQUIRE(tab_at(bar_of(*tree), 0).accessibility().name == u8"Report, 3 unread, Loading");
    // 말이 없는 표시는 이름을 바꾸지 않는다.
    REQUIRE(tab_at(bar_of(*tree), 1).accessibility().name == u8"Notes");

    // 진행 중인 탭만 다음 frame을 청한다. 움직임 줄이기면 멈춘 그림이라 청하지 않는다.
    const luil::update_context moving { .now = at(1000) };
    const luil::update_context still { .now = at(1000), .reduce_motion = true };
    REQUIRE(tree->next_update(moving, {}).has_value());
    REQUIRE(tree->next_update(still, {}).has_value() == false);
    REQUIRE(tab_at(bar_of(*tree), 1).next_update(moving, {}).has_value() == false);
}

TEST_CASE("A preview bar is a picture with one name and no input", "[ui][tab][preview]")
{
    luil::tab_bar_config config { make_config(3) };
    with_messages(config);
    config.selected = u8"tab-a";
    config.items[1].closable = true;
    config.overflow = [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; };
    config.preview = luil::tab_bar_preview { .name = u8"Connected tabs sample", .hovered = u8"tab-b" };
    const auto tree { bar_tree(std::move(config), 200.0f) };
    const luil::tab_bar_element& bar { bar_of(*tree) };

    // 누를 자리도, 초점 자리도, 놓을 자리도 없다.
    REQUIRE(tree->hit_test(30.0f, 18.0f) == nullptr);
    REQUIRE(tree->hit_test(150.0f, 18.0f) == nullptr);
    REQUIRE(tree->focus_order().empty());
    REQUIRE(bar.drop() == nullptr);
    // 견본의 탭은 탭 kind가 아니다 — 탭을 kind로 찾는 앱 코드가 견본을 집지 않고, 견본 막대가 여럿이어도 id가 겹치지 않는다.
    REQUIRE(tree->ids_of_kind(luil::ui_element_kind::tab).empty());
    REQUIRE(tab_at(bar, 0).id() == luil::ui_element_id { luil::ui_element_kind::tab_sample, u8"tabs/tab-a" });
    REQUIRE(tree->duplicate_ids().empty());
    // 닫기 단추 element도 넘침 버튼도 짓지 않는다.
    REQUIRE(tab_at(bar, 1).children().empty());
    REQUIRE(bar.children().size() == 1);
    // 접근 tree에는 앱이 붙인 이름 하나로 선다.
    REQUIRE(bar.accessibility().role == luil::access_role::image);
    REQUIRE(bar.accessibility().name == u8"Connected tabs sample");
    REQUIRE(luil::access_children(bar).empty());
}

TEST_CASE("Each tab style reads in light, dark and high contrast", "[ui][raster][tab][style]")
{
    for (const luil::color_theme theme : { luil::color_theme::dark, luil::color_theme::light, luil::color_theme::high_contrast })
    {
        const luil::ui_color_palette palette { luil::color_palette_for(theme) };
        CAPTURE(static_cast<int>(theme));

        SECTION("underline")
        {
            const luil::testing::raster_frame frame { draw_bar(theme, luil::tab_bar_variant::underline) };
            REQUIRE(frame.pixel_at(480, 30) == palette.surface_background);
            REQUIRE(frame.pixel_at(480, 71) == palette.accent);
            // 고르지 않은 탭은 막대 바탕(지운 창 바탕) 그대로다.
            REQUIRE(frame.pixel_at(160, 30) == palette.window_background);
        }

        SECTION("connected")
        {
            const luil::testing::raster_frame frame { draw_bar(theme, luil::tab_bar_variant::connected) };
            // 선택된 탭은 내용 바탕으로 칠해 막대 바닥선을 덮는다 — 바닥선이 그 밑에서 끊긴다.
            REQUIRE(frame.pixel_at(480, 40) == palette.surface_background);
            REQUIRE(frame.pixel_at(480, 71) == palette.surface_background);
            REQUIRE(frame.pixel_at(160, 71) != palette.window_background);
            // 위 틈은 막대 바탕이다.
            REQUIRE(frame.pixel_at(480, 2) == palette.window_background);
            // 선택된 탭에 맞닿은 칸막이는 숨는다. 고르지 않은 두 탭 사이에는 선다.
            REQUIRE(frame.pixel_at(319, 36) == palette.window_background);
            REQUIRE(frame.pixel_at(959, 36) != palette.window_background);
            if (theme == luil::color_theme::high_contrast)
            {
                // 바탕이 같아도 선택은 테로 읽힌다 — 같은 자리의 고르지 않은 탭에는 테가 없다.
                REQUIRE(palette.surface_background == palette.window_background);
                REQUIRE(frame.pixel_at(329, 40) == palette.divider);
                REQUIRE(count_above_floor(frame, 320.0f, palette.divider) > count_above_floor(frame, 0.0f, palette.divider) + 50);
            }
        }

        SECTION("flat")
        {
            const luil::testing::raster_frame frame { draw_bar(theme, luil::tab_bar_variant::flat) };
            REQUIRE(frame.pixel_at(480, 40) == palette.surface_background);
            REQUIRE(frame.pixel_at(480, 1) == palette.accent);
            REQUIRE(frame.pixel_at(480, 71) == palette.surface_background);
            // 고르지 않은 칸은 위 강조선이 없다.
            REQUIRE(frame.pixel_at(160, 1) != palette.accent);
            REQUIRE(frame.pixel_at(319, 30) != palette.window_background);
            if (theme == luil::color_theme::high_contrast)
                REQUIRE(frame.pixel_at(320, 30) == palette.divider);
        }
    }
}

TEST_CASE("The app picks the bar and selected backgrounds from palette roles", "[ui][raster][tab][style]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::tab_bar_config config { make_config(4) };
    config.selected = u8"tab-b";
    luil::tab_bar_style style { style_of(luil::tab_bar_variant::flat) };
    style.bar_background = luil::palette_role::caption_background;
    style.selected_background = luil::palette_role::window_background;
    config.style = style;
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::tab_bar_element>(std::move(config)), { 0.0f, 0.0f, 1280.0f, 72.0f }, raster_scale) };
    luil::testing::raster_frame frame { 1280, 72, palette, raster_scale };
    frame.draw(tree, {});
    REQUIRE(frame.pixel_at(160, 30) == palette.caption.background);
    REQUIRE(frame.pixel_at(480, 30) == palette.window_background);
}

TEST_CASE("A close slot indicator turns into the close button under the pointer", "[ui][raster][tab][indicator]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    const auto draw = [&palette](const luil::interaction_snapshot& interaction, const luil::tab_indicator_placement placement) {
        luil::tab_bar_config config { make_config(2) };
        with_messages(config);
        config.items[0].closable = true;
        config.items[0].icon = U'x';
        config.items[0].indicator = luil::tab_indicator {};
        luil::tab_bar_style style {};
        style.indicator_placement = placement;
        config.style = style;
        const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::tab_bar_element>(std::move(config)), { 0.0f, 0.0f, 640.0f, 72.0f }, raster_scale) };
        luil::testing::raster_frame frame { 640, 72, palette, raster_scale };
        frame.draw(tree, interaction);
        return frame;
    };

    // 닫기 자리는 탭 오른쪽에서 6 들어온 18 폭이다 — 물리 272..308, 가운데 290.
    const luil::testing::raster_frame resting { draw({}, luil::tab_indicator_placement::close_slot) };
    REQUIRE(resting.pixel_at(290, 36) == palette.accent);

    luil::interaction_snapshot over_close {};
    over_close.hovered = luil::ui_element_id { luil::ui_element_kind::tab_close, u8"tab-a" };
    const luil::testing::raster_frame hovered { draw(over_close, luil::tab_indicator_placement::close_slot) };
    REQUIRE(hovered.pixel_at(290, 36) != palette.accent);
    REQUIRE(hovered.pixel_at(276, 36) != palette.window_background);

    // 아이콘 모서리: 아이콘 칸(물리 20..52)의 오른쪽 아래에 점이 선다.
    const luil::testing::raster_frame corner { draw({}, luil::tab_indicator_placement::icon_corner) };
    REQUIRE(corner.pixel_at(46, 43) == palette.accent);
    REQUIRE(corner.pixel_at(290, 36) != palette.accent);
}

TEST_CASE("A neutral or accent badge makes room for the app's glyph and severity keeps its own", "[ui][tab][badge]")
{
    const float plain { luil::badge_pill_width_for(20.0f, true, luil::badge_tone::neutral, 10.0f) };
    // 글리프(글자 크기)와 글자 사이(4)만큼 넓다.
    REQUIRE(luil::badge_pill_width_for(20.0f, true, luil::badge_tone::neutral, 10.0f, true) == plain + 14.0f);
    REQUIRE(luil::badge_pill_width_for(20.0f, true, luil::badge_tone::accent, 10.0f, true) == plain + 14.0f);
    // 심각도는 이미 자기 글리프 자리가 있다 — 앱의 글리프가 하나 더 붙지 않는다.
    REQUIRE(luil::badge_pill_width_for(20.0f, true, luil::badge_tone::error, 10.0f, true) == luil::badge_pill_width_for(20.0f, true, luil::badge_tone::error, 10.0f));

    // 도는 배지 글리프도 다음 frame을 청한다. 심각도 배지는 돌지 않는다.
    luil::tab_bar_config config { make_config(2) };
    config.items[0].badge = luil::tab_badge { .text = u8"2", .tone = luil::badge_tone::neutral, .glyph = U'x', .spinning = true };
    config.items[1].badge = luil::tab_badge { .tone = luil::badge_tone::warning, .glyph = U'x', .spinning = true };
    const auto tree { bar_tree(std::move(config), 400.0f) };
    const luil::update_context moving { .now = at(1000) };
    REQUIRE(tab_at(bar_of(*tree), 0).next_update(moving, {}).has_value());
    REQUIRE(tab_at(bar_of(*tree), 1).next_update(moving, {}).has_value() == false);
}

TEST_CASE("A preview bar paints its pinned hover", "[ui][raster][tab][preview]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::tab_bar_config config { make_config(3) };
    config.selected = u8"tab-a";
    config.preview = luil::tab_bar_preview { .name = u8"sample", .hovered = u8"tab-b" };
    const luil::ui_tree tree { luil::make_arranged_tree(std::make_unique<luil::tab_bar_element>(std::move(config)), { 0.0f, 0.0f, 960.0f, 72.0f }, raster_scale) };
    luil::testing::raster_frame frame { 960, 72, palette, raster_scale };
    frame.draw(tree, {});
    REQUIRE(frame.pixel_at(480, 30) != palette.window_background);
    REQUIRE(frame.pixel_at(800, 30) == palette.window_background);
}

TEST_CASE("A dragged connected tab fades as a whole", "[ui][raster][tab][drag]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    luil::interaction_snapshot dragging {};
    dragging.drag = luil::drag_visual { .payload = luil::drag_payload { .dragged_owner = u8"tab-b" }, .x = 700.0f, .y = 30.0f };
    const luil::testing::raster_frame frame { draw_bar(luil::color_theme::dark, luil::tab_bar_variant::connected, dragging) };
    // 흐린 탭은 내용 바탕도 막대 바탕도 아닌 둘 사이다.
    REQUIRE(frame.pixel_at(480, 40) != palette.surface_background);
    REQUIRE(frame.pixel_at(480, 40) != palette.window_background);
}
