#include "host/cursor_idle.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/root_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

namespace {
    using namespace std::chrono_literals;
    using luil::cursor_idle_tracker;
    using clock_type = std::chrono::steady_clock;

    // 그림만 있는 보기다 — hit를 받지 않고 그리지도 않는다.
    class viewer final : public luil::ui_element
    {
    public:
        viewer()
            : ui_element { luil::ui_element_id { luil::application_element_kind(1), u8"viewer" } }
        {}

        void add(std::unique_ptr<luil::ui_element> child)
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

    [[nodiscard]] std::unique_ptr<luil::text_button_element> make_button(const char8_t* key, const luil::rect_f& slot)
    {
        auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { luil::application_element_kind(2), key }, luil::text_button_config { .text = u8"단추" }) };
        button->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        button->arrange({ slot, 1.0f });
        return button;
    }
} // namespace

TEST_CASE("A still pointer hides the cursor after the delay and a move brings it back", "[cursor][idle]")
{
    cursor_idle_tracker tracker {};
    const clock_type::time_point start {};
    // 움직임이 없었으면 잴 것이 없다.
    REQUIRE(tracker.deadline(1500ms).has_value() == false);
    REQUIRE(tracker.hide_if_due(start + 10s, 1500ms) == false);

    REQUIRE(tracker.activity(start) == false);
    REQUIRE(tracker.deadline(1500ms) == start + 1500ms);
    REQUIRE(tracker.hide_if_due(start + 1499ms, 1500ms) == false);
    REQUIRE(tracker.hide_if_due(start + 1500ms, 1500ms));
    REQUIRE(tracker.hidden());
    // 한 번만 감춘다. 감춘 동안에는 기한도 없다.
    REQUIRE(tracker.hide_if_due(start + 2s, 1500ms) == false);
    REQUIRE(tracker.deadline(1500ms).has_value() == false);

    // 움직이거나 누르면 곧바로 되돌리라고 답한다. 시계는 그때부터 다시 잰다.
    REQUIRE(tracker.activity(start + 3s));
    REQUIRE(tracker.hidden() == false);
    REQUIRE(tracker.deadline(1500ms) == start + 4500ms);
}

TEST_CASE("Leaving, losing focus or losing the policy reveals the cursor", "[cursor][idle]")
{
    cursor_idle_tracker tracker {};
    const clock_type::time_point start {};
    static_cast<void>(tracker.activity(start));
    REQUIRE(tracker.hide_if_due(start + 2s, 1s));
    // 창을 떠남·초점 상실·정책이 빠짐은 모두 되돌림이다. 감춘 것이 있었을 때만 참이다.
    REQUIRE(tracker.reveal());
    REQUIRE(tracker.reveal() == false);
    // 정책이 없으면(0) 감추지 않는다.
    REQUIRE(tracker.hide_if_due(start + 1h, 0ms) == false);
}

TEST_CASE("A policy that appears under a still pointer starts its clock then", "[cursor][idle]")
{
    // 오래 멈춰 있던 포인터 아래 전체 화면 보기가 서는 순간 커서가 사라지지 않는다.
    cursor_idle_tracker tracker {};
    const clock_type::time_point start {};
    static_cast<void>(tracker.activity(start));
    tracker.restart(start + 1min);
    REQUIRE(tracker.hide_if_due(start + 1min + 500ms, 1s) == false);
    REQUIRE(tracker.hide_if_due(start + 1min + 1s, 1s));
}

TEST_CASE("The tree answers the idle delay only where the policy owns the hit", "[cursor][idle]")
{
    auto root { std::make_unique<luil::root_element>() };
    root->arrange({ { 0.0f, 0.0f, 400.0f, 300.0f }, 1.0f });
    auto view { std::make_unique<viewer>() };
    view->set_cursor_idle_hide(1500ms);
    view->arrange({ { 0.0f, 0.0f, 400.0f, 300.0f }, 1.0f });
    // 보기 안의 단추는 보기의 자손이다 — 그 위에서도 감춘다.
    view->add(make_button(u8"inner", { 20.0f, 20.0f, 60.0f, 28.0f }));
    root->add(std::move(view));
    // 보기 위를 덮은 도구 막대는 보기의 자손이 아니다 — 그 위에서는 감추지 않는다.
    root->add(make_button(u8"toolbar", { 0.0f, 260.0f, 400.0f, 40.0f }));
    const luil::ui_tree tree { std::move(root) };

    REQUIRE(tree.cursor_idle_hide_at(200.0f, 150.0f) == 1500ms);
    REQUIRE(tree.cursor_idle_hide_at(40.0f, 30.0f) == 1500ms);
    REQUIRE(tree.cursor_idle_hide_at(200.0f, 280.0f) == 0ms);
    // 표면 밖은 정책의 자리가 아니다.
    REQUIRE(tree.cursor_idle_hide_at(500.0f, 150.0f) == 0ms);

    // 정책이 없는 tree는 언제나 0이다.
    auto plain { std::make_unique<luil::root_element>() };
    plain->arrange({ { 0.0f, 0.0f, 400.0f, 300.0f }, 1.0f });
    REQUIRE(luil::ui_tree { std::move(plain) }.cursor_idle_hide_at(200.0f, 150.0f) == 0ms);
}

TEST_CASE("A hidden policy element does not hide the cursor", "[cursor][idle]")
{
    auto root { std::make_unique<luil::root_element>() };
    root->arrange({ { 0.0f, 0.0f, 400.0f, 300.0f }, 1.0f });
    auto view { std::make_unique<viewer>() };
    view->set_cursor_idle_hide(1500ms);
    view->set_visible(false);
    view->arrange({ { 0.0f, 0.0f, 400.0f, 300.0f }, 1.0f });
    root->add(std::move(view));
    REQUIRE(luil::ui_tree { std::move(root) }.cursor_idle_hide_at(200.0f, 150.0f) == 0ms);
    // 음수·0은 끔이다.
    viewer off {};
    off.set_cursor_idle_hide(-5ms);
    REQUIRE(off.cursor_idle_hide() == 0ms);
}
