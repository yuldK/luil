#include "luil/ui/sheet_host_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_test { luil::application_element_kind(0) };

    // 시트가 낸 메시지다. 끈 양·멈춤점·닫힘 계기·안쪽 목록의 스크롤 중 하나다.
    struct sheet_event
    {
        std::optional<float> drag {};
        std::optional<luil::sheet_detent> settle {};
        std::optional<luil::sheet_dismiss_reason> dismiss {};
        std::optional<float> scroll {};
        std::optional<std::u8string> click {};
    };

    [[nodiscard]] std::vector<sheet_event> events_in(const std::vector<luil::input_action>& actions)
    {
        std::vector<sheet_event> events {};
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const event { message->get<sheet_event>() }; event != nullptr)
                    events.push_back(*event);
        return events;
    }

    [[nodiscard]] luil::sheet_host_config sheet_config(const luil::sheet_kind kind = luil::sheet_kind::bottom, const bool modal = true)
    {
        luil::sheet_host_config config {};
        config.owner = u8"sheet";
        config.name = u8"Sheet";
        config.kind = kind;
        config.modal = modal;
        config.dismiss = [](const luil::sheet_dismiss_reason reason) { return luil::make_app_action(sheet_event { .dismiss = reason }); };
        config.drag = [](const float delta) { return luil::make_app_action(sheet_event { .drag = delta }); };
        config.settle = [](const luil::sheet_detent detent) { return luil::make_app_action(sheet_event { .settle = detent }); };
        return config;
    }

    // 자식을 자기 자리 그대로 겹쳐 두는 test 컨테이너다 (뒤 화면 + 시트).
    class test_shell final : public luil::ui_element
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
            for (const std::unique_ptr<ui_element>& child : children())
                child->arrange(context);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    // 시트 안의 세로 목록이다. 자리를 통째로 받고, 처음에 있는지는 앱이 싣는다.
    class test_list final : public luil::ui_element
    {
    public:
        explicit test_list(const bool at_start)
            : ui_element { { kind_test, u8"list" } }
        {
            set_scroll_source(luil::scroll_source {
                .scroll = [](const float delta) { return luil::make_app_action(sheet_event { .scroll = delta }); },
                .at_start = at_start,
            });
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    // 뒤 화면(누르면 "behind")과 400×800 자리의 시트를 짓는다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> sheet_tree(const luil::sheet_host_config& config, std::unique_ptr<luil::ui_element> content = {}, const float width = 400.0f,
        const float height = 800.0f, const luil::rect_f& surface = {}, const float scale = 1.0f, const int unavailable = 0)
    {
        auto shell { std::make_unique<test_shell>(luil::ui_element_id { luil::ui_element_kind::root }) };
        auto behind { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_test, u8"behind" }, luil::panel_config {}) };
        behind->set_action(luil::ui_trigger::left_click,
            [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(sheet_event { .click = std::u8string { u8"behind" } }) }; });
        shell->add(std::move(behind));
        auto host { std::make_unique<luil::sheet_host_element>(config) };
        host->set_visible(unavailable != 1);
        if (unavailable == 2)
            for (const auto& child : host->children())
                for (const auto& grandchild : child->children())
                    grandchild->set_enabled(false);
        if (content != nullptr)
            host->set_content(std::move(content));
        shell->add(std::move(host));
        if (unavailable == 3)
        {
            auto cover { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_test, u8"cover" }, luil::panel_config {}) };
            cover->set_focus_trap(true);
            cover->set_hit_opaque(true);
            shell->add(std::move(cover));
        }
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(shell), { 0.0f, 0.0f, width, height }, scale, surface));
    }

    [[nodiscard]] const luil::sheet_host_element& host_of(const luil::ui_tree& tree)
    {
        return *static_cast<const luil::sheet_host_element*>(tree.find(luil::ui_element_id { luil::ui_element_kind::sheet_host, u8"sheet" }));
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] luil::pointer_pressed_event touch_press(const float x, const float y, const int time)
    {
        return { .x = x, .y = y, .time = at(time), .device = luil::pointer_device::touch, .pointer_id = 1 };
    }

    [[nodiscard]] luil::pointer_moved_event touch_move(const float x, const float y, const int time)
    {
        return { .x = x, .y = y, .time = at(time), .device = luil::pointer_device::touch, .pointer_id = 1, .in_contact = true };
    }

    [[nodiscard]] luil::pointer_released_event touch_release(const float x, const float y, const int time)
    {
        return { .x = x, .y = y, .time = at(time), .device = luil::pointer_device::touch, .pointer_id = 1 };
    }

    // 사각형 넷의 값이 같은가다 (`rect_f`에는 비교가 없다).
    [[nodiscard]] bool same(const luil::rect_f& left, const luil::rect_f& right) noexcept
    {
        return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
    }

    // 끈 양을 모두 더한다.
    [[nodiscard]] float dragged(const std::vector<sheet_event>& events)
    {
        float total { 0.0f };
        for (const sheet_event& event : events)
            total += event.drag.value_or(0.0f);
        return total;
    }
} // namespace

TEST_CASE("A bottom sheet stops at half and full and follows the drag between them", "[ui][sheet]")
{
    auto config { sheet_config() };
    REQUIRE(luil::sheet_detent_height(config, luil::sheet_detent::half, 800.0f) == 400.0f);
    REQUIRE(luil::sheet_detent_height(config, luil::sheet_detent::full, 800.0f) == 776.0f);
    REQUIRE(luil::sheet_visible_height(config, 800.0f) == 400.0f);
    // 아래로 끈 만큼 내려가고, 위로 끈 것은 전체 높이에서 멈춘다.
    config.drag_offset = 100.0f;
    REQUIRE(luil::sheet_visible_height(config, 800.0f) == 300.0f);
    config.drag_offset = -500.0f;
    REQUIRE(luil::sheet_visible_height(config, 800.0f) == 776.0f);
    config.drag_offset = 900.0f;
    REQUIRE(luil::sheet_visible_height(config, 800.0f) == 0.0f);
    // 아래 시트는 아래에 붙고 넓은 자리에서는 최대 폭으로 가운데에 선다.
    config.drag_offset = 0.0f;
    REQUIRE(same(luil::sheet_surface_bounds(config, { 0.0f, 0.0f, 400.0f, 800.0f }, 1.0f), luil::rect_f { 0.0f, 400.0f, 400.0f, 400.0f }));
    REQUIRE(same(luil::sheet_surface_bounds(config, { 0.0f, 0.0f, 1000.0f, 800.0f }, 1.0f), luil::rect_f { 180.0f, 400.0f, 640.0f, 400.0f }));
    // 배율 2의 1000×800은 논리 500×400이다. 최대 폭보다 좁아 꽉 차고, 반 높이(200)가 물리 400이다.
    REQUIRE(same(luil::sheet_surface_bounds(config, { 0.0f, 0.0f, 1000.0f, 800.0f }, 2.0f), luil::rect_f { 0.0f, 400.0f, 1000.0f, 400.0f }));
}

TEST_CASE("Letting go settles on the nearest stop or closes", "[ui][sheet]")
{
    auto config { sheet_config() };
    const auto settle_at = [&config](const luil::sheet_detent detent, const float offset) {
        config.detent = detent;
        config.drag_offset = offset;
        return luil::settle_sheet(config, 800.0f);
    };
    // 반에서 조금 끈 것은 반으로 돌아온다. 반 높이의 반보다 더 내리면 닫힌다.
    REQUIRE(settle_at(luil::sheet_detent::half, 100.0f) == luil::sheet_settle { luil::sheet_detent::half, false });
    REQUIRE(settle_at(luil::sheet_detent::half, 320.0f).dismiss);
    // 위로 끌어 전체에 가까우면 전체다.
    REQUIRE(settle_at(luil::sheet_detent::half, -300.0f) == luil::sheet_settle { luil::sheet_detent::full, false });
    // 전체에서 조금 내린 것은 전체, 많이 내리면 반이다.
    REQUIRE(settle_at(luil::sheet_detent::full, 100.0f) == luil::sheet_settle { luil::sheet_detent::full, false });
    REQUIRE(settle_at(luil::sheet_detent::full, 300.0f) == luil::sheet_settle { luil::sheet_detent::half, false });
    // 닫힘과 반 사이 한가운데는 닫지 않는다.
    REQUIRE(settle_at(luil::sheet_detent::half, 200.0f).dismiss == false);
}

TEST_CASE("A fling moves one stop in its direction", "[ui][sheet]")
{
    auto config { sheet_config() };
    const auto settle_at = [&config](const luil::sheet_detent detent, const float offset, const float velocity) {
        config.detent = detent;
        config.drag_offset = offset;
        return luil::settle_sheet(config, 800.0f, velocity);
    };
    // 기준(300)보다 빠르면 지금 높이에서 그 방향의 다음 자리다. 끈 양이 tree에 실리지 않아 멈춤점 높이 그대로여도 한 칸 간다.
    REQUIRE(settle_at(luil::sheet_detent::full, 0.0f, 500.0f) == luil::sheet_settle { luil::sheet_detent::half, false });
    REQUIRE(settle_at(luil::sheet_detent::half, 0.0f, 500.0f).dismiss);
    REQUIRE(settle_at(luil::sheet_detent::half, 0.0f, -500.0f) == luil::sheet_settle { luil::sheet_detent::full, false });
    REQUIRE(settle_at(luil::sheet_detent::full, 0.0f, -500.0f) == luil::sheet_settle { luil::sheet_detent::full, false });
    // 반보다 위로 끌어 놓고 아래로 튕기면 반이고, 반보다 아래에서 위로 튕기면 반이다.
    REQUIRE(settle_at(luil::sheet_detent::half, -60.0f, 500.0f) == luil::sheet_settle { luil::sheet_detent::half, false });
    REQUIRE(settle_at(luil::sheet_detent::half, 60.0f, -500.0f) == luil::sheet_settle { luil::sheet_detent::half, false });
    // 기준보다 느리면 가장 가까운 자리다. 반에서 조금 내린 시트는 반으로 돌아온다.
    REQUIRE(settle_at(luil::sheet_detent::half, 100.0f, 200.0f) == luil::sheet_settle { luil::sheet_detent::half, false });
    // 기준이 0이면 속도를 보지 않는다. 값이 아닌 속도도 보지 않는다.
    config.fling_velocity = 0.0f;
    REQUIRE(settle_at(luil::sheet_detent::half, 0.0f, 5000.0f) == luil::sheet_settle { luil::sheet_detent::half, false });
    config.fling_velocity = 300.0f;
    REQUIRE(settle_at(luil::sheet_detent::half, 0.0f, std::numeric_limits<float>::quiet_NaN()) == luil::sheet_settle { luil::sheet_detent::half, false });
}

TEST_CASE("Flinging the handle or the sheet body steps the sheet", "[ui][sheet]")
{
    const auto config { sheet_config() };
    // 손잡이를 아래로 빠르게 튕기면 반 높이 시트가 닫힌다 (앱이 아직 tree를 다시 짓지 않았어도).
    {
        luil::interaction_controller controller {};
        controller.set_tree(sheet_tree(config));
        static_cast<void>(controller.process(touch_press(200.0f, 410.0f, 0)));
        for (int step { 1 }; step <= 4; ++step)
            static_cast<void>(controller.process(touch_move(200.0f, 410.0f + 25.0f * static_cast<float>(step), 10 * step)));
        const auto events { events_in(controller.process(touch_release(200.0f, 510.0f, 50))) };
        REQUIRE(events.size() == 1u);
        REQUIRE(events[0].dismiss == luil::sheet_dismiss_reason::drag);
    }
    // 목록 없는 시트 몸을 위로 튕기면 전체 높이다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(sheet_tree(config));
        static_cast<void>(controller.process(touch_press(200.0f, 600.0f, 0)));
        for (int step { 1 }; step <= 5; ++step)
            static_cast<void>(controller.process(touch_move(200.0f, 600.0f - 20.0f * static_cast<float>(step), 10 * step)));
        const auto events { events_in(controller.process(touch_release(200.0f, 500.0f, 60))) };
        REQUIRE(events.back().settle == luil::sheet_detent::full);
    }
    // 마우스로 손잡이를 위로 튕겨도 같다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(sheet_tree(config));
        static_cast<void>(controller.process(luil::pointer_pressed_event { 200.0f, 410.0f, luil::pointer_button::left, at(0) }));
        for (int step { 1 }; step <= 4; ++step)
            static_cast<void>(controller.process(luil::pointer_moved_event { 200.0f, 410.0f - 20.0f * static_cast<float>(step), at(10 * step) }));
        const auto events { events_in(controller.process(luil::pointer_released_event { 200.0f, 330.0f, luil::pointer_button::left, at(50) })) };
        REQUIRE(events.size() == 1u);
        REQUIRE(events[0].settle == luil::sheet_detent::full);
    }
}

TEST_CASE("A sheet does not fling on a pause, a jitter or a far away release", "[ui][sheet]")
{
    const auto config { sheet_config() };
    // 빠르게 끌다 멈추고 200ms 뒤에 놓으면 튕김이 아니다. 가장 가까운 자리(반)다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(sheet_tree(config));
        static_cast<void>(controller.process(touch_press(200.0f, 410.0f, 0)));
        for (int step { 1 }; step <= 4; ++step)
            static_cast<void>(controller.process(touch_move(200.0f, 410.0f + 25.0f * static_cast<float>(step), 10 * step)));
        const auto events { events_in(controller.process(touch_release(200.0f, 510.0f, 240))) };
        REQUIRE(events.size() == 1u);
        REQUIRE(events[0].settle == luil::sheet_detent::half);
    }
    // 손잡이를 눌렀다 떼는 동안 손가락이 조금 흔들린 것은 튕김이 아니다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(sheet_tree(config));
        static_cast<void>(controller.process(touch_press(200.0f, 410.0f, 0)));
        static_cast<void>(controller.process(touch_move(200.0f, 413.0f, 16)));
        static_cast<void>(controller.process(touch_move(200.0f, 410.0f, 32)));
        const auto events { events_in(controller.process(touch_release(200.0f, 410.0f, 40))) };
        REQUIRE(events.size() == 1u);
        REQUIRE(events[0].settle == luil::sheet_detent::half);
    }
    // 마우스가 capture를 잃으면 화면 밖 자리의 합성 뗌이 온다. 그 자리는 속도에 넣지 않는다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(sheet_tree(config));
        static_cast<void>(controller.process(luil::pointer_pressed_event { 200.0f, 410.0f, luil::pointer_button::left, at(0) }));
        static_cast<void>(controller.process(luil::pointer_moved_event { 200.0f, 405.0f, at(40) }));
        static_cast<void>(controller.process(luil::pointer_moved_event { 200.0f, 400.0f, at(80) }));
        const auto events { events_in(controller.process(luil::pointer_released_event { -32000.0f, -32000.0f, luil::pointer_button::left, at(90) })) };
        REQUIRE(events.size() == 1u);
        REQUIRE(events[0].settle == luil::sheet_detent::half);
    }
    // 시스템이 접촉을 거두면 속도는 0이다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(sheet_tree(config));
        static_cast<void>(controller.process(touch_press(200.0f, 410.0f, 0)));
        for (int step { 1 }; step <= 4; ++step)
            static_cast<void>(controller.process(touch_move(200.0f, 410.0f + 25.0f * static_cast<float>(step), 10 * step)));
        const auto events { events_in(controller.process(luil::pointer_cancelled_event { luil::pointer_device::touch, 1, {}, at(50) })) };
        REQUIRE(events.size() == 1u);
        REQUIRE(events[0].settle == luil::sheet_detent::half);
    }
}

TEST_CASE("Dragging the handle down past half closes the sheet", "[ui][sheet]")
{
    luil::interaction_controller controller {};
    auto config { sheet_config() };
    controller.set_tree(sheet_tree(config));
    // 손잡이는 시트 위 24 논리 픽셀이다 (400~424).
    REQUIRE(controller.process(touch_press(200.0f, 410.0f, 0)).empty());
    auto events { events_in(controller.process(touch_move(200.0f, 510.0f, 50))) };
    REQUIRE(dragged(events) == 100.0f);
    // 앱이 끈 양을 상태에 쌓아 다시 지었다. 손잡이가 손가락을 따라 내려가 누른 자리(410)는 이제 scrim이지만 끌기는
    // 이어진다.
    config.drag_offset = 100.0f;
    controller.set_tree(sheet_tree(config));
    events = events_in(controller.process(touch_move(200.0f, 710.0f, 100)));
    REQUIRE(dragged(events) == 200.0f);
    config.drag_offset = 300.0f;
    controller.set_tree(sheet_tree(config));
    // 놓으면 남은 높이(100)가 닫힘에 가장 가깝다.
    events = events_in(controller.process(touch_release(200.0f, 710.0f, 150)));
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].dismiss == luil::sheet_dismiss_reason::drag);
}

TEST_CASE("A mouse can drag the handle and letting go settles", "[ui][sheet]")
{
    luil::interaction_controller controller {};
    auto config { sheet_config() };
    controller.set_tree(sheet_tree(config));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 200.0f, 410.0f, luil::pointer_button::left, at(0) }));
    REQUIRE(dragged(events_in(controller.process(luil::pointer_moved_event { 200.0f, 310.0f, at(20) }))) == -100.0f);
    config.drag_offset = -100.0f;
    controller.set_tree(sheet_tree(config));
    const auto events { events_in(controller.process(luil::pointer_released_event { 200.0f, 310.0f, luil::pointer_button::left, at(40) })) };
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].settle == luil::sheet_detent::half);
}

TEST_CASE("A sheet body includes the last release delta when choosing its stop", "[ui][sheet][touch]")
{
    for (const float scale : { 1.0f, 2.0f })
        for (const bool upward : { false, true })
        {
            CAPTURE(scale, upward);
            auto config { sheet_config() };
            luil::interaction_controller controller {};
            const auto publish = [&] { controller.set_tree(sheet_tree(config, {}, 400.0f * scale, 800.0f * scale, {}, scale)); };
            publish();
            const float start { upward ? 600.0f : 500.0f };
            const float direction { upward ? -1.0f : 1.0f };
            static_cast<void>(controller.process(touch_press(200.0f * scale, start * scale, 0)));
            REQUIRE(dragged(events_in(controller.process(touch_move(200.0f * scale, (start + direction * 20.0f) * scale, 40)))) == direction * 20.0f);
            config.drag_offset = direction * 20.0f;
            publish();
            const float offset { upward ? -178.0f : 190.0f };
            REQUIRE(dragged(events_in(controller.process(touch_move(200.0f * scale, (start + offset) * scale, 900)))) == offset - config.drag_offset);
            config.drag_offset = offset;
            publish();
            // 멈춤 경계를 마지막 뗌에서 넘는다. 느린 손짓이라 속도 대신 최종 높이가 결정을 내려야 한다.
            const auto events { events_in(controller.process(touch_release(200.0f * scale, (start + offset + direction * 20.0f) * scale, 1200))) };
            REQUIRE(events.size() == 2u);
            REQUIRE(events[0].drag == direction * 20.0f);
            if (upward)
                REQUIRE(events[1].settle == luil::sheet_detent::full);
            else
                REQUIRE(events[1].dismiss == luil::sheet_dismiss_reason::drag);
        }
}

TEST_CASE("An unavailable mouse or pen handle stops receiving drag and end callbacks", "[ui][sheet]")
{
    for (const auto device : { luil::pointer_device::mouse, luil::pointer_device::pen })
        for (int unavailable { 1 }; unavailable <= 3; ++unavailable)
            for (int finish { 0 }; finish < 4; ++finish)
            {
                CAPTURE(device, unavailable, finish);
                auto config { sheet_config() };
                luil::interaction_controller controller {};
                controller.set_tree(sheet_tree(config));
                static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 200.0f, .y = 410.0f, .time = at(0), .device = device, .pointer_id = 2 }));
                REQUIRE(dragged(events_in(controller.process(luil::pointer_moved_event { .x = 200.0f, .y = 440.0f, .time = at(40), .device = device, .pointer_id = 2, .in_contact = true }))) == 30.0f);
                config.drag_offset = 30.0f;
                controller.set_tree(sheet_tree(config, {}, 400.0f, 800.0f, {}, 1.0f, unavailable));
                if (finish == 0)
                    REQUIRE(events_in(controller.process(luil::pointer_released_event { .x = 200.0f, .y = 440.0f, .time = at(80), .device = device, .pointer_id = 2 })).empty());
                else if (finish == 1)
                    REQUIRE(events_in(controller.process(luil::pointer_cancelled_event { device, 2, {}, at(80) })).empty());
                else if (finish == 2)
                    REQUIRE(events_in(controller.cancel_dropped_gestures()).empty());
                else
                {
                    REQUIRE(events_in(controller.process(luil::pointer_moved_event { .x = 200.0f, .y = 460.0f, .time = at(80), .device = device, .pointer_id = 2, .in_contact = true })).empty());
                    REQUIRE(events_in(controller.process(luil::pointer_released_event { .x = 200.0f, .y = 460.0f, .time = at(100), .device = device, .pointer_id = 2 })).empty());
                }
            }
}

TEST_CASE("A sheet keeps its stop through a rotation and turns into a side sheet when wide", "[ui][sheet]")
{
    auto config { sheet_config() };
    config.detent = luil::sheet_detent::full;
    config.side_from_width = 600.0f;
    // 세로 400×800에서는 전체 높이의 아래 시트다.
    const auto portrait { sheet_tree(config, {}, 400.0f, 800.0f) };
    REQUIRE(host_of(*portrait).kind() == luil::sheet_kind::bottom);
    REQUIRE(same(host_of(*portrait).surface_bounds(), luil::rect_f { 0.0f, 24.0f, 400.0f, 776.0f }));
    // 돌려 800×400이 되면 넓은 자리라 오른쪽 옆 시트다. 멈춤점은 앱 상태라 그대로다.
    const auto landscape { sheet_tree(config, {}, 800.0f, 400.0f) };
    REQUIRE(host_of(*landscape).kind() == luil::sheet_kind::end);
    REQUIRE(same(host_of(*landscape).surface_bounds(), luil::rect_f { 440.0f, 0.0f, 360.0f, 400.0f }));
    // 옆 시트에는 끌기 손잡이가 없다.
    const luil::ui_element* const handle { landscape->find(luil::ui_element_id { luil::ui_element_kind::sheet_handle, u8"sheet" }) };
    REQUIRE(handle != nullptr);
    REQUIRE(handle->visible() == false);
    // 다시 세로가 되면 같은 전체 높이의 아래 시트다. 좁은 화면 분할(500)도 아래 시트다.
    REQUIRE(same(host_of(*sheet_tree(config, {}, 400.0f, 800.0f)).surface_bounds(), luil::rect_f { 0.0f, 24.0f, 400.0f, 776.0f }));
    REQUIRE(host_of(*sheet_tree(config, {}, 500.0f, 600.0f)).kind() == luil::sheet_kind::bottom);
}

TEST_CASE("Side sheets and drawers take the edge and leave room behind", "[ui][sheet]")
{
    REQUIRE(same(host_of(*sheet_tree(sheet_config(luil::sheet_kind::start), {}, 800.0f, 400.0f)).surface_bounds(), luil::rect_f { 0.0f, 0.0f, 360.0f, 400.0f }));
    // 좁은 자리에서는 뒤가 보이도록 56을 남긴다.
    REQUIRE(same(host_of(*sheet_tree(sheet_config(luil::sheet_kind::start), {}, 300.0f, 600.0f)).surface_bounds(), luil::rect_f { 0.0f, 0.0f, 244.0f, 600.0f }));
    REQUIRE(same(host_of(*sheet_tree(sheet_config(luil::sheet_kind::end), {}, 300.0f, 600.0f)).surface_bounds(), luil::rect_f { 56.0f, 0.0f, 244.0f, 600.0f }));
}

TEST_CASE("A modal sheet traps the focus and reports the scrim and Esc", "[ui][sheet]")
{
    const auto tree { sheet_tree(sheet_config()) };
    const luil::sheet_host_element& host { host_of(*tree) };
    REQUIRE(host.accessibility().role == luil::access_role::dialog);
    REQUIRE(host.accessibility().name == u8"Sheet");
    REQUIRE(tree->focus_trap() == &host);
    // 뒤는 scrim이 막고, 누르면 scrim 계기로 닫자고 한다.
    const luil::ui_element* const hit { tree->hit_test(10.0f, 10.0f) };
    REQUIRE(hit != nullptr);
    REQUIRE(hit->id().kind == luil::ui_element_kind::sheet_scrim);
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    static_cast<void>(controller.process(luil::pointer_pressed_event { 10.0f, 10.0f, luil::pointer_button::left, at(0) }));
    auto events { events_in(controller.process(luil::pointer_released_event { 10.0f, 10.0f, luil::pointer_button::left, at(30) })) };
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].dismiss == luil::sheet_dismiss_reason::scrim);
    // 시트의 빈 자리는 흡수한다. Esc는 시트를 닫자고 한다.
    REQUIRE(tree->hit_test(200.0f, 600.0f)->id().kind == luil::ui_element_kind::none);
    REQUIRE(host.dismiss_action() != nullptr);
    events = events_in((*host.dismiss_action())(luil::ui_action_context {}));
    REQUIRE(events[0].dismiss == luil::sheet_dismiss_reason::escape);
}

TEST_CASE("A non modal sheet leaves the screen behind usable", "[ui][sheet]")
{
    const auto tree { sheet_tree(sheet_config(luil::sheet_kind::bottom, false)) };
    const luil::sheet_host_element& host { host_of(*tree) };
    REQUIRE(host.accessibility().role == luil::access_role::pane);
    REQUIRE(tree->focus_trap() == nullptr);
    REQUIRE(host.dismiss_action() == nullptr);
    REQUIRE(tree->hit_test(10.0f, 10.0f)->id() == luil::ui_element_id { kind_test, u8"behind" });
}

TEST_CASE("A list at its top hands a downward swipe to the sheet", "[ui][sheet][touch]")
{
    luil::interaction_controller controller {};
    // 맨 위의 목록을 아래로 쓸면 시트가 내려간다.
    controller.set_tree(sheet_tree(sheet_config(), std::make_unique<test_list>(true)));
    static_cast<void>(controller.process(touch_press(200.0f, 600.0f, 0)));
    auto events { events_in(controller.process(touch_move(200.0f, 660.0f, 40))) };
    REQUIRE(dragged(events) == 60.0f);
    events = events_in(controller.process(touch_release(200.0f, 680.0f, 80)));
    REQUIRE(events.size() == 2u);
    REQUIRE(events[0].drag == 20.0f);
    REQUIRE(events[1].settle == luil::sheet_detent::half);

    // 위로 쓸면 목록이 흐른다.
    static_cast<void>(controller.process(touch_press(200.0f, 600.0f, 1000)));
    events = events_in(controller.process(touch_move(200.0f, 540.0f, 1040)));
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].scroll == 60.0f);
    static_cast<void>(controller.process(touch_release(200.0f, 540.0f, 1080)));

    // 맨 위가 아닌 목록은 아래로 쓸어도 목록이 흐른다. 접촉 도중 맨 위에 닿아도 넘기지 않는다.
    controller.set_tree(sheet_tree(sheet_config(), std::make_unique<test_list>(false)));
    static_cast<void>(controller.process(touch_press(200.0f, 600.0f, 2000)));
    events = events_in(controller.process(touch_move(200.0f, 660.0f, 2040)));
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].scroll == -60.0f);
    controller.set_tree(sheet_tree(sheet_config(), std::make_unique<test_list>(true)));
    events = events_in(controller.process(touch_move(200.0f, 700.0f, 2060)));
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].scroll == -40.0f);
}

TEST_CASE("A sheet without a list takes the swipe and a cancelled swipe settles", "[ui][sheet][touch]")
{
    luil::interaction_controller controller {};
    controller.set_tree(sheet_tree(sheet_config()));
    // 흘리는 창이 없는 시트 몸은 위로도 아래로도 끈다.
    static_cast<void>(controller.process(touch_press(200.0f, 600.0f, 0)));
    REQUIRE(dragged(events_in(controller.process(touch_move(200.0f, 560.0f, 40)))) == -40.0f);
    // 시스템이 접촉을 거두면 끄던 자리에 남지 않고 멈춤점을 정한다.
    const auto events { events_in(controller.process(luil::pointer_cancelled_event { luil::pointer_device::touch, 1, {}, at(60) })) };
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].settle.has_value());

    // 마우스로 시트 몸을 끄는 것은 몸짓이 아니다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 200.0f, 600.0f, luil::pointer_button::left, at(1000) }));
    REQUIRE(events_in(controller.process(luil::pointer_moved_event { 200.0f, 700.0f, at(1020) })).empty());
}

TEST_CASE("Losing input in the middle of a drag still settles the sheet", "[ui][sheet]")
{
    const auto config { sheet_config() };
    // 입력 큐가 넘쳐 몸짓을 거두면 터치 손잡이는 끝을 알린다. 늦게 온 뗌은 아무것도 내지 않는다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(sheet_tree(config));
        static_cast<void>(controller.process(touch_press(200.0f, 410.0f, 0)));
        REQUIRE(dragged(events_in(controller.process(touch_move(200.0f, 440.0f, 40)))) == 30.0f);
        const auto events { events_in(controller.cancel_dropped_gestures()) };
        REQUIRE(events.size() == 1u);
        REQUIRE(events[0].settle == luil::sheet_detent::half);
        REQUIRE(events_in(controller.process(touch_release(200.0f, 440.0f, 80))).empty());
    }
    // 넘겨받은 시트 몸도 같다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(sheet_tree(config));
        static_cast<void>(controller.process(touch_press(200.0f, 600.0f, 0)));
        REQUIRE(dragged(events_in(controller.process(touch_move(200.0f, 560.0f, 40)))) == -40.0f);
        const auto events { events_in(controller.cancel_dropped_gestures()) };
        REQUIRE(events.size() == 1u);
        REQUIRE(events[0].settle.has_value());
    }
    // 마우스 손잡이도 같다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(sheet_tree(config));
        static_cast<void>(controller.process(luil::pointer_pressed_event { 200.0f, 410.0f, luil::pointer_button::left, at(0) }));
        REQUIRE(dragged(events_in(controller.process(luil::pointer_moved_event { 200.0f, 310.0f, at(20) }))) == -100.0f);
        const auto events { events_in(controller.cancel_dropped_gestures()) };
        REQUIRE(events.size() == 1u);
        REQUIRE(events[0].settle == luil::sheet_detent::half);
        REQUIRE(events_in(controller.process(luil::pointer_released_event { 200.0f, 310.0f, luil::pointer_button::left, at(40) })).empty());
    }
    // 끄던 것이 없으면 아무것도 내지 않는다.
    {
        luil::interaction_controller controller {};
        controller.set_tree(sheet_tree(config));
        REQUIRE(controller.cancel_dropped_gestures().empty());
    }
}

TEST_CASE("A pen dragging the handle settles when the system cancels it", "[ui][sheet]")
{
    luil::interaction_controller controller {};
    controller.set_tree(sheet_tree(sheet_config()));
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 200.0f, .y = 410.0f, .time = at(0), .device = luil::pointer_device::pen, .pointer_id = 2 }));
    const luil::pointer_moved_event move { .x = 200.0f, .y = 600.0f, .time = at(20), .device = luil::pointer_device::pen, .pointer_id = 2, .in_contact = true };
    REQUIRE(dragged(events_in(controller.process(move))) == 190.0f);
    // 다른 펜의 취소는 이 끌기와 상관없다.
    REQUIRE(events_in(controller.process(luil::pointer_cancelled_event { luil::pointer_device::pen, 3, {}, at(30) })).empty());
    const auto events { events_in(controller.process(luil::pointer_cancelled_event { luil::pointer_device::pen, 2, {}, at(40) })) };
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].settle == luil::sheet_detent::half);
}

TEST_CASE("A handle on another surface hears its release", "[ui][sheet]")
{
    // 끝 알림은 누른 표면의 손잡이로 간다 (popup 같은 다른 표면).
    luil::interaction_controller controller {};
    controller.set_surface_trees({ { u8"popup", sheet_tree(sheet_config()) } });
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 200.0f, .y = 410.0f, .time = at(0), .surface = u8"popup" }));
    REQUIRE(dragged(events_in(controller.process(luil::pointer_moved_event { .x = 200.0f, .y = 310.0f, .time = at(20), .surface = u8"popup" }))) == -100.0f);
    const auto events { events_in(controller.process(luil::pointer_released_event { .x = 200.0f, .y = 310.0f, .time = at(40), .surface = u8"popup" })) };
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].settle == luil::sheet_detent::half);
}

TEST_CASE("A sheet background reaches the surface edges it touches", "[ui][sheet]")
{
    // 세로 Android: 자리는 400×800, 표면은 위 30·아래 20만큼 더 크다.
    const luil::rect_f slot { 0.0f, 0.0f, 400.0f, 800.0f };
    const luil::rect_f surface { 0.0f, -30.0f, 400.0f, 850.0f };
    // 아래 시트는 아래와 좌우에 닿는다. 위(안쪽)는 그대로다.
    REQUIRE(same(luil::sheet_background_bounds({ 0.0f, 400.0f, 400.0f, 400.0f }, slot, surface), luil::rect_f { 0.0f, 400.0f, 400.0f, 420.0f }));
    // 넓은 자리의 가운데 아래 시트는 아래만 늘린다.
    const luil::rect_f wide { 0.0f, 0.0f, 1000.0f, 800.0f };
    REQUIRE(same(luil::sheet_background_bounds({ 180.0f, 400.0f, 640.0f, 400.0f }, wide, { -60.0f, -30.0f, 1060.0f, 850.0f }), luil::rect_f { 180.0f, 400.0f, 640.0f, 420.0f }));
    // 옆 시트는 위·아래·오른쪽, 서랍은 위·아래·왼쪽이다 (가로의 왼쪽 컷아웃 60).
    REQUIRE(same(luil::sheet_background_bounds({ 640.0f, 0.0f, 360.0f, 800.0f }, wide, { -60.0f, -30.0f, 1060.0f, 850.0f }), luil::rect_f { 640.0f, -30.0f, 360.0f, 850.0f }));
    REQUIRE(same(luil::sheet_background_bounds({ 0.0f, 0.0f, 360.0f, 800.0f }, wide, { -60.0f, -30.0f, 1060.0f, 850.0f }), luil::rect_f { -60.0f, -30.0f, 420.0f, 850.0f }));
    // 표면을 모르면 그대로다.
    REQUIRE(same(luil::sheet_background_bounds({ 0.0f, 400.0f, 400.0f, 400.0f }, slot, {}), luil::rect_f { 0.0f, 400.0f, 400.0f, 400.0f }));
}

TEST_CASE("A sheet that knows the surface keeps its place and its taps in the safe area", "[ui][sheet]")
{
    const luil::rect_f surface { 0.0f, -30.0f, 400.0f, 850.0f };
    const auto plain { sheet_tree(sheet_config()) };
    const auto edge_to_edge { sheet_tree(sheet_config(), {}, 400.0f, 800.0f, surface) };
    // 시트 표면의 자리(손잡이·내용·누르기)는 표면을 알아도 같다.
    REQUIRE(same(host_of(*edge_to_edge).surface_bounds(), host_of(*plain).surface_bounds()));
    luil::interaction_controller controller {};
    controller.set_tree(edge_to_edge);
    // 상태 표시줄 자리(자리 밖)를 누른 것은 scrim이 받지 않는다.
    static_cast<void>(controller.process(touch_press(200.0f, -10.0f, 0)));
    REQUIRE(events_in(controller.process(touch_release(200.0f, -10.0f, 40))).empty());
    // 자리 안의 scrim은 지금처럼 닫는다.
    static_cast<void>(controller.process(touch_press(200.0f, 100.0f, 1000)));
    const auto events { events_in(controller.process(touch_release(200.0f, 100.0f, 1040))) };
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].dismiss == luil::sheet_dismiss_reason::scrim);
}
