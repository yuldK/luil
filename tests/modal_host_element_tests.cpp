#include "luil/ui/modal_host_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_dialog { luil::application_element_kind(0) };

    struct close_intent
    {
        std::u8string reason {};
    };

    [[nodiscard]] luil::ui_action make_close(std::u8string reason)
    {
        return [reason = std::move(reason)](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::make_app_action(close_intent { reason }) }; };
    }

    // 자식을 자기 자리 그대로 겹쳐 두는 test 컨테이너다 (뒤 화면 + modal host).
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

    // dialog 본체다.
    // 버튼을 아래쪽 절반에만 두어 위쪽에 **빈 자리**가 남는다 — 그 자리를 눌러
    // 뒤의 scrim으로 새는지 볼 수 있어야 한다.
    class test_dialog final : public luil::ui_element
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
            const luil::rect_f lower { context.slot.x, context.slot.y + context.slot.height / 2.0f, context.slot.width, context.slot.height / 2.0f };
            for (const std::unique_ptr<ui_element>& child : children())
                child->arrange(context.for_child(lower));
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    [[nodiscard]] std::unique_ptr<test_dialog> make_content()
    {
        auto body { std::make_unique<test_dialog>(luil::ui_element_id { kind_dialog, u8"body" }) };
        auto confirm { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_dialog, u8"confirm" }, luil::panel_config {}) };
        confirm->set_action(luil::ui_trigger::left_click, make_close(u8"confirm"));
        body->add(std::move(confirm));
        return body;
    }
} // namespace

TEST_CASE("A modal host centers its content and follows the app offset", "[ui][modal]")
{
    luil::modal_host_config config {};
    config.content_width = 100.0f;
    config.content_height = 40.0f;

    const auto place = [&config] {
        auto host { std::make_unique<luil::modal_host_element>(config) };
        host->set_content(make_content());
        return luil::make_arranged_tree(std::move(host), { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f);
    };

    // 크기는 논리 픽셀이고 arrange가 배율을 곱한다.
    const luil::ui_tree centered { place() };
    const luil::ui_element* const body { centered.find(luil::ui_element_id { kind_dialog, u8"body" }) };
    REQUIRE(body != nullptr);
    REQUIRE(body->bounds().width == 200.0f);
    REQUIRE(body->bounds().height == 80.0f);
    REQUIRE(body->bounds().x == 100.0f);
    REQUIRE(body->bounds().y == 60.0f);

    // scrim은 준 자리를 그대로 덮는다.
    const luil::ui_element* const scrim { centered.find(luil::ui_element_id { luil::ui_element_kind::modal_scrim }) };
    REQUIRE(scrim != nullptr);
    REQUIRE(scrim->bounds().width == 400.0f);
    REQUIRE(scrim->bounds().height == 200.0f);

    // 잡아 끈 만큼 가운데에서 밀려난다. 그 값은 앱 상태라 host가 기억하지 않는다.
    config.offset_x = 30.0f;
    config.offset_y = -10.0f;
    const luil::ui_tree moved { place() };
    const luil::ui_element* const shifted { moved.find(luil::ui_element_id { kind_dialog, u8"body" }) };
    REQUIRE(shifted != nullptr);
    REQUIRE(shifted->bounds().x == 160.0f);
    REQUIRE(shifted->bounds().y == 40.0f);
}

TEST_CASE("A modal host blocks the pointer and traps the focus", "[ui][modal]")
{
    luil::modal_host_config config {};
    config.content_width = 100.0f;
    config.content_height = 40.0f;
    config.outside = make_close(u8"outside");

    auto shell { std::make_unique<test_shell>(luil::ui_element_id { luil::ui_element_kind::root }) };
    auto behind { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_dialog, u8"behind" }, luil::panel_config {}) };
    behind->set_action(luil::ui_trigger::left_click, make_close(u8"behind"));
    shell->add(std::move(behind));

    auto host { std::make_unique<luil::modal_host_element>(config) };
    host->set_content(make_content());
    shell->add(std::move(host));

    const luil::ui_tree tree { luil::make_arranged_tree(std::move(shell), { 0.0f, 0.0f, 400.0f, 200.0f }, 1.0f) };

    // 뒤의 element는 scrim이 흡수해 눌리지 않는다.
    const luil::ui_element* const behind_hit { tree.hit_test(10.0f, 10.0f) };
    REQUIRE(behind_hit != nullptr);
    REQUIRE(behind_hit->id().kind == luil::ui_element_kind::modal_scrim);

    // dialog의 빈 자리를 눌러도 뒤의 scrim으로 새지 않는다.
    // 앱이 흡수 액션을 손으로 달지 않아도 host가 세운다.
    const luil::ui_element* const body_hit { tree.hit_test(155.0f, 85.0f) };
    REQUIRE(body_hit != nullptr);
    REQUIRE(body_hit->id() == luil::ui_element_id { kind_dialog, u8"body" });

    // Tab은 dialog 안만 돈다. scrim은 눌리지만 자리가 아니다.
    REQUIRE(tree.focus_trap() != nullptr);
    REQUIRE(tree.focus_trap()->id().kind == luil::ui_element_kind::modal_host);
    REQUIRE(tree.focus_order() == std::vector<luil::ui_element_id> { luil::ui_element_id { kind_dialog, u8"confirm" } });
}

TEST_CASE("A modal host takes the scrim and the Esc route from its config", "[ui][modal]")
{
    // 없는 것은 두지 않는다: dismiss가 비어 있으면 Esc를 받지 않는다.
    const luil::modal_host_element silent { luil::modal_host_config {} };
    REQUIRE(silent.dismiss_action() == nullptr);
    REQUIRE(silent.focus_trap());

    luil::modal_host_config config {};
    config.owner = u8"confirm";
    config.dismiss = make_close(u8"escape");
    const luil::modal_host_element host { config };
    REQUIRE(host.id() == luil::ui_element_id { luil::ui_element_kind::modal_host, u8"confirm" });
    REQUIRE(host.dismiss_action() != nullptr);

    const std::vector<luil::input_action> actions { (*host.dismiss_action())(luil::ui_action_context {}) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<close_intent>() != nullptr);
    REQUIRE(message->get<close_intent>()->reason == u8"escape");

    // scrim은 진하기와 무관하게 언제나 있다 — 포인터를 막는 것이 modal의 몫이다.
    luil::modal_host_config clear {};
    clear.scrim_opacity = 0.0f;
    const luil::modal_host_element invisible { clear };
    REQUIRE(invisible.children().size() == 1u);
    REQUIRE(invisible.children()[0]->id().kind == luil::ui_element_kind::modal_scrim);
    REQUIRE(invisible.children()[0]->hit_opaque());
    REQUIRE(invisible.children()[0]->tab_stop() == false);
}
