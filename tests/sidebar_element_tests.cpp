#include "luil/ui/sidebar_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace {
    // 폭 변화 메시지의 앱 쪽 정의다.
    struct width_intent
    {
        float delta { 0.0f };
    };

    [[nodiscard]] luil::split_resize_message_factory make_resize_factory()
    {
        return [](const float delta) { return luil::input_action { luil::app_message { width_intent { delta } } }; };
    }

    [[nodiscard]] luil::ui_action make_noop_action()
    {
        return [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
    }

    class content_probe final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    [[nodiscard]] std::unique_ptr<content_probe> make_content()
    {
        return std::make_unique<content_probe>(luil::ui_element_id { luil::application_element_kind(50) });
    }
} // namespace

TEST_CASE("A sidebar takes its width from the collapse state and clings to its side", "[ui][sidebar]")
{
    const luil::rect_f slot { 0.0f, 0.0f, 1000.0f, 600.0f };

    luil::sidebar_config left {};
    left.expanded_width = 240.0f;
    luil::sidebar_element expanded { left };
    expanded.arrange({ slot, 2.0f });
    REQUIRE(luil::sidebar_element::width_for(left) == 240.0f);
    REQUIRE(expanded.bounds().x == 0.0f);
    REQUIRE(expanded.bounds().width == 480.0f);
    REQUIRE(expanded.bounds().height == 600.0f);

    luil::sidebar_config right {};
    right.side = luil::sidebar_side::right;
    right.collapsed = true;
    right.collapsed_width = 48.0f;
    luil::sidebar_element collapsed { right };
    collapsed.arrange({ slot, 2.0f });
    REQUIRE(luil::sidebar_element::width_for(right) == 48.0f);
    REQUIRE(collapsed.bounds().x == 1000.0f - 96.0f);
    REQUIRE(collapsed.bounds().width == 96.0f);
}

TEST_CASE("A sidebar takes the width the app gives it while a transition runs", "[ui][sidebar]")
{
    const luil::rect_f slot { 0.0f, 0.0f, 1000.0f, 600.0f };

    // 목표는 접힘인데 지금은 절반쯤 와 있다 — 둘은 다른 것을 말한다.
    luil::sidebar_config config {};
    config.expanded_width = 240.0f;
    config.collapsed_width = 48.0f;
    config.collapsed = true;
    config.width = 144.0f;
    REQUIRE(luil::sidebar_element::width_for(config) == 144.0f);

    luil::sidebar_element sidebar { config };
    sidebar.arrange({ slot, 1.0f });
    REQUIRE(sidebar.bounds().width == 144.0f);

    // 전환이 끝나 앱이 값을 놓으면 목표가 곧 지금이다.
    config.width.reset();
    REQUIRE(luil::sidebar_element::width_for(config) == 48.0f);

    // 음수는 0으로 잘린다 (지금까지의 규칙 그대로다).
    config.width = -10.0f;
    REQUIRE(luil::sidebar_element::width_for(config) == 0.0f);
}

TEST_CASE("A sidebar collapsed to zero width builds nothing", "[ui][sidebar]")
{
    luil::sidebar_config config {};
    config.collapsed = true;
    config.collapsed_width = 0.0f;
    config.toggle = make_noop_action();
    config.resize = make_resize_factory();
    const luil::sidebar_element hidden { config, make_content() };

    // 보일 것이 없으면 내용도 버튼도 손잡이도 만들지 않는다.
    REQUIRE(hidden.children().empty());
    REQUIRE(luil::sidebar_element::width_for(config) == 0.0f);
}

TEST_CASE("A sidebar builds its toggle and handle only from what the config gives", "[ui][sidebar]")
{
    // 아무 설정도 없으면 판만 있다.
    const luil::sidebar_element bare { luil::sidebar_config {} };
    REQUIRE(bare.children().empty());

    // 토글 액션이 있으면 버튼이 생긴다.
    luil::sidebar_config with_toggle {};
    with_toggle.owner = u8"files";
    with_toggle.toggle = make_noop_action();
    const luil::sidebar_element toggleable { with_toggle };
    REQUIRE(toggleable.children().size() == 1);
    REQUIRE(toggleable.children()[0]->id() == luil::ui_element_id { luil::ui_element_kind::sidebar_toggle, u8"files" });

    // 손잡이는 펼쳐진 동안만 있다.
    luil::sidebar_config with_resize {};
    with_resize.resize = make_resize_factory();
    const luil::sidebar_element resizable { with_resize };
    REQUIRE(resizable.children().size() == 1);
    REQUIRE(resizable.children()[0]->id().kind == luil::ui_element_kind::sidebar_handle);

    with_resize.collapsed = true;
    const luil::sidebar_element collapsed { with_resize };
    REQUIRE(collapsed.children().empty());
}

TEST_CASE("A sidebar keeps its content below the toggle header", "[ui][sidebar]")
{
    const luil::rect_f slot { 0.0f, 0.0f, 1000.0f, 600.0f };

    luil::sidebar_config with_toggle {};
    with_toggle.toggle = make_noop_action();
    auto content { make_content() };
    const content_probe* const probe { content.get() };
    luil::sidebar_element sidebar { with_toggle, std::move(content) };
    sidebar.arrange({ slot, 1.0f });
    REQUIRE(probe->bounds().y == luil::sidebar_header_height);
    REQUIRE(probe->bounds().height == 600.0f - luil::sidebar_header_height);

    // 토글이 없으면 내용이 전체를 쓴다.
    auto full_content { make_content() };
    const content_probe* const full_probe { full_content.get() };
    luil::sidebar_element plain { luil::sidebar_config {}, std::move(full_content) };
    plain.arrange({ slot, 1.0f });
    REQUIRE(full_probe->bounds().y == 0.0f);
    REQUIRE(full_probe->bounds().height == 600.0f);
}

TEST_CASE("A sidebar handle straddles the inner edge and wins the hit test there", "[ui][sidebar]")
{
    luil::sidebar_config config {};
    config.expanded_width = 240.0f;
    config.resize = make_resize_factory();
    luil::sidebar_element sidebar { config, make_content() };
    sidebar.arrange({ { 0.0f, 0.0f, 1000.0f, 600.0f }, 1.0f });

    // 안쪽 가장자리(x=240)의 양쪽 절반이 손잡이다.
    const luil::ui_element* const inside { sidebar.hit_test(238.0f, 300.0f) };
    REQUIRE(inside != nullptr);
    REQUIRE(inside->id().kind == luil::ui_element_kind::sidebar_handle);
    const luil::ui_element* const outside { sidebar.hit_test(242.0f, 300.0f) };
    REQUIRE(outside != nullptr);
    REQUIRE(outside->id().kind == luil::ui_element_kind::sidebar_handle);
    REQUIRE(inside->cursor() == luil::ui_cursor::resize_horizontal);
}

TEST_CASE("A sidebar handle turns a drag into width deltas toward wider", "[ui][sidebar]")
{
    const luil::rect_f slot { 0.0f, 0.0f, 1000.0f, 600.0f };

    luil::sidebar_config left {};
    left.resize = make_resize_factory();
    luil::sidebar_element left_sidebar { left };
    left_sidebar.arrange({ slot, 2.0f });
    const luil::ui_element* const left_handle { left_sidebar.children()[0].get() };
    REQUIRE(left_handle->pointer_drag() != nullptr);

    // 왼쪽 판은 오른쪽으로 끌면 넓어진다.
    // 물리 10px 이동은 배율 2에서 논리 5px다.
    const luil::ui_action_context from { left_handle->id(), 480.0f, 300.0f };
    const luil::ui_action_context to { left_handle->id(), 490.0f, 300.0f };
    auto actions { left_handle->pointer_drag()->on_move(from, to) };
    REQUIRE(actions.size() == 1);
    auto* message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<width_intent>()->delta == 5.0f);

    // 오른쪽 판은 왼쪽으로 끌면 넓어진다.
    luil::sidebar_config right {};
    right.side = luil::sidebar_side::right;
    right.resize = make_resize_factory();
    luil::sidebar_element right_sidebar { right };
    right_sidebar.arrange({ slot, 2.0f });
    const luil::ui_element* const right_handle { right_sidebar.children()[0].get() };
    auto mirrored { right_handle->pointer_drag()->on_move(to, from) };
    REQUIRE(mirrored.size() == 1);
    auto* mirrored_message { std::get_if<luil::app_message>(&mirrored[0]) };
    REQUIRE(mirrored_message != nullptr);
    REQUIRE(mirrored_message->get<width_intent>()->delta == 5.0f);
}
