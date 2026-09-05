#include "luil/ui/ui_element.h"

#include "luil/ui/button_element.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

namespace {
    // 기반 클래스 계약만 검증하는 최소 element다.
    // 그리기는 하지 않는다.
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

    // 회전하는 진행 표시를 흉내 내는 element다.
    class spinning_panel final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}

        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const luil::update_context& context, const luil::interaction_snapshot&) const override
        {
            // "지금 이하"는 연속 애니메이션이라는 뜻이다.
            return context.now;
        }
    };

    luil::ui_action noop_action()
    {
        return [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
    }

    // test 정의 kind다.
    // 앱이 자기 kind를 정의하는 것과 같은 경로다.
    constexpr luil::ui_element_kind kind_card_list { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_card_body { luil::application_element_kind(1) };
    constexpr luil::ui_element_kind kind_card_refresh { luil::application_element_kind(2) };
    constexpr luil::ui_element_kind kind_card_update { luil::application_element_kind(3) };

    luil::ui_element_id card_id(const luil::ui_element_kind kind, const std::u8string_view value)
    {
        return { kind, std::u8string { value } };
    }
} // namespace

TEST_CASE("An element is interactive once it has an action, tooltip, or drag role", "[ui][element]")
{
    test_panel panel { luil::ui_element_id { kind_card_list } };
    REQUIRE(panel.interactive() == false);

    SECTION("액션")
    {
        panel.set_action(luil::ui_trigger::left_click, noop_action());
        REQUIRE(panel.interactive());
        REQUIRE(panel.action(luil::ui_trigger::left_click) != nullptr);
        REQUIRE(panel.action(luil::ui_trigger::right_click) == nullptr);

        panel.clear_action(luil::ui_trigger::left_click);
        REQUIRE(panel.interactive() == false);
    }

    SECTION("tooltip")
    {
        panel.set_tooltip(u8"설명");
        REQUIRE(panel.interactive());
        REQUIRE(panel.tooltip() == u8"설명");
    }

    SECTION("drag와 drop은 재설정할 수 있다")
    {
        panel.set_drag_source(luil::drag_source { [](const luil::ui_action_context&) { return luil::drag_payload {}; } });
        REQUIRE(panel.interactive());
        REQUIRE(panel.drag() != nullptr);
        panel.set_drag_source(std::nullopt);
        REQUIRE(panel.drag() == nullptr);
        REQUIRE(panel.interactive() == false);

        panel.set_drop_target(luil::drop_target {});
        REQUIRE(panel.drop() != nullptr);
        panel.set_drop_target(std::nullopt);
        REQUIRE(panel.drop() == nullptr);
    }
}

TEST_CASE("Hit testing prefers the topmost interactive child", "[ui][element]")
{
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    root->set_action(luil::ui_trigger::left_click, noop_action());

    auto below { std::make_unique<test_panel>(card_id(kind_card_body, u8"below")) };
    below->arrange({ { 10.0f, 10.0f, 40.0f, 40.0f }, 1.0f });
    below->set_action(luil::ui_trigger::left_click, noop_action());

    auto above { std::make_unique<test_panel>(card_id(kind_card_refresh, u8"above")) };
    above->arrange({ { 20.0f, 20.0f, 40.0f, 40.0f }, 1.0f });
    above->set_action(luil::ui_trigger::left_click, noop_action());

    auto hidden { std::make_unique<test_panel>(card_id(kind_card_update, u8"hidden")) };
    hidden->arrange({ { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    hidden->set_action(luil::ui_trigger::left_click, noop_action());
    hidden->set_visible(false);

    const luil::ui_element* const below_raw { below.get() };
    const luil::ui_element* const above_raw { above.get() };
    root->add(std::move(below));
    root->add(std::move(above));
    root->add(std::move(hidden));
    const luil::ui_tree tree { std::move(root) };

    // 겹치는 곳은 나중에 추가된(위에 그려진) 자식이 이긴다.
    REQUIRE(tree.hit_test(30.0f, 30.0f) == above_raw);
    REQUIRE(tree.hit_test(12.0f, 12.0f) == below_raw);
    // 보이지 않는 element는 전체를 덮어도 잡히지 않는다.
    REQUIRE(tree.hit_test(90.0f, 90.0f) == tree.root());
    // tree 밖은 아무것도 아니다.
    REQUIRE(tree.hit_test(150.0f, 150.0f) == nullptr);
}

TEST_CASE("The tree finds elements by identity and enumerates kinds in draw order", "[ui][element]")
{
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    auto first { std::make_unique<test_panel>(card_id(kind_card_body, u8"one")) };
    auto second { std::make_unique<test_panel>(card_id(kind_card_body, u8"two")) };
    root->add(std::move(first));
    root->add(std::move(second));
    const luil::ui_tree tree { std::move(root) };

    REQUIRE(tree.find(card_id(kind_card_body, u8"two")) != nullptr);
    REQUIRE(tree.find(card_id(kind_card_body, u8"three")) == nullptr);

    const auto cards { tree.ids_of_kind(kind_card_body) };
    REQUIRE(cards.size() == 2u);
    REQUIRE(cards[0].owner == u8"one");
    REQUIRE(cards[1].owner == u8"two");
}

TEST_CASE("The tree exposes duplicate ids and the first registration wins", "[ui][element]")
{
    // 정체성 축이 id 하나라 같은 id가 둘이면 find·hover·초점이 조용히
    // 앞의 것만 본다. tree가 중복을 드러내 앱 test가 잡아낼 수 있게 한다.
    constexpr luil::ui_element_kind kind_button { luil::application_element_kind(0) };
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto first { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"same" }) };
    first->arrange({ { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    const luil::ui_element* const first_pointer { first.get() };
    root->add(std::move(first));
    auto second { std::make_unique<test_panel>(luil::ui_element_id { kind_button, u8"same" }) };
    second->arrange({ { 100.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    root->add(std::move(second));
    const luil::ui_tree tree { std::move(root) };

    REQUIRE(tree.duplicate_ids().size() == 1u);
    REQUIRE(tree.duplicate_ids()[0] == luil::ui_element_id { kind_button, u8"same" });
    REQUIRE(tree.find(luil::ui_element_id { kind_button, u8"same" }) == first_pointer);

    // 중복 없는 tree는 빈 목록이다 — 앱 test가 이 값을 확인하면 된다.
    auto clean { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    const luil::ui_tree clean_tree { std::move(clean) };
    REQUIRE(clean_tree.duplicate_ids().empty());
}

TEST_CASE("The tree gathers the earliest next update over its elements", "[ui][element][update]")
{
    const luil::update_context context { std::chrono::steady_clock::time_point { std::chrono::seconds { 100 } } };
    const luil::interaction_snapshot idle {};

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->add(std::make_unique<test_panel>(card_id(kind_card_body, u8"still")));
    const luil::ui_tree quiet { std::move(root) };
    // 평소에는 예고가 없다.
    //  - platform이 timer를 걸지 않는다.
    REQUIRE(quiet.next_update(context, idle).has_value() == false);

    auto busy_root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    auto branch { std::make_unique<test_panel>(card_id(kind_card_body, u8"branch")) };
    branch->add(std::make_unique<spinning_panel>(card_id(kind_card_refresh, u8"busy")));
    busy_root->add(std::move(branch));
    const luil::ui_tree busy { std::move(busy_root) };
    // 깊이 묻힌 element 하나의 예고도 tree의 답이 된다.
    REQUIRE(busy.next_update(context, idle) == context.now);
}

TEST_CASE("A hidden element or a hidden ancestor never asks for the next update", "[ui][element][update]")
{
    const luil::update_context context { std::chrono::steady_clock::time_point { std::chrono::seconds { 100 } } };
    const luil::interaction_snapshot idle {};

    // 숨긴 element 자신이다. 색인(`find`)에는 남지만 그려지지 않으므로 창을
    // 깨울 이유가 없다.
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        auto hidden { std::make_unique<spinning_panel>(card_id(kind_card_refresh, u8"busy")) };
        hidden->set_visible(false);
        root->add(std::move(hidden));
        const luil::ui_tree tree { std::move(root) };
        REQUIRE(tree.find(card_id(kind_card_refresh, u8"busy")) != nullptr);
        REQUIRE(tree.next_update(context, idle).has_value() == false);
    }

    // 조상만 숨은 element다 — 다른 탭 안의 움직이는 그림이 이 모양이다.
    // 자기는 보이는 채라 `visible()`만 보면 걸러지지 않는다.
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        auto branch { std::make_unique<test_panel>(card_id(kind_card_body, u8"other-tab")) };
        branch->set_visible(false);
        branch->add(std::make_unique<spinning_panel>(card_id(kind_card_refresh, u8"busy")));
        root->add(std::move(branch));
        const luil::ui_tree tree { std::move(root) };
        REQUIRE(tree.find(card_id(kind_card_refresh, u8"busy"))->visible());
        REQUIRE(tree.next_update(context, idle).has_value() == false);
    }

    // 형제 가지가 숨어도 보이는 가지의 예고는 그대로다 — 거르는 것은 숨은 경로뿐이다.
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        auto hidden { std::make_unique<test_panel>(card_id(kind_card_body, u8"other-tab")) };
        hidden->set_visible(false);
        hidden->add(std::make_unique<spinning_panel>(card_id(kind_card_refresh, u8"hidden-busy")));
        root->add(std::move(hidden));
        root->add(std::make_unique<spinning_panel>(card_id(kind_card_refresh, u8"shown-busy")));
        const luil::ui_tree tree { std::move(root) };
        REQUIRE(tree.next_update(context, idle) == context.now);
    }
}

TEST_CASE("The tree announces the tooltip delay as a next update", "[ui][element][update]")
{
    const std::chrono::steady_clock::time_point hover_started { std::chrono::seconds { 100 } };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    auto target { std::make_unique<test_panel>(card_id(kind_card_body, u8"hinted")) };
    target->set_tooltip(u8"설명");
    root->add(std::move(target));
    const luil::ui_tree tree { std::move(root) };

    luil::interaction_snapshot interaction {};
    interaction.hovered = card_id(kind_card_body, u8"hinted");
    interaction.hover_started_at = hover_started;

    // 지연이 끝나는 시각을 예고한다.
    const luil::update_context waiting { hover_started + luil::tooltip_delay / 2 };
    REQUIRE(tree.next_update(waiting, interaction) == hover_started + luil::tooltip_delay);

    // 이미 떠 있으면 더 그릴 것이 없다.
    const luil::update_context shown { hover_started + luil::tooltip_delay * 2 };
    REQUIRE(tree.next_update(shown, interaction).has_value() == false);

    // tooltip이 없는 element 위에서는 예고도 없다.
    luil::interaction_snapshot plain {};
    plain.hovered = luil::ui_element_id { luil::ui_element_kind::root };
    plain.hover_started_at = hover_started;
    REQUIRE(tree.next_update(waiting, plain).has_value() == false);
}

TEST_CASE("The spinner angle is a function of the clock alone", "[ui][element][spinner]")
{
    const std::chrono::steady_clock::time_point base {};
    REQUIRE(luil::spinner_angle(base) == 0.0f);
    // 반주기면 반 바퀴다.
    REQUIRE(luil::spinner_angle(base + luil::spinner_period / 2) == 180.0f);
    // 한 바퀴를 돌면 처음으로 돌아온다.
    //  - 시작 시각을 들고 다니지 않아도 위상이 맞는다.
    REQUIRE(luil::spinner_angle(base + luil::spinner_period) == 0.0f);
    REQUIRE(luil::spinner_angle(base + luil::spinner_period * 3 + luil::spinner_period / 4) == 90.0f);
}

TEST_CASE("A disabled button keeps its tooltip target but blocks nothing else at the element level", "[ui][element]")
{
    luil::button_element button { card_id(kind_card_update, u8"card"), luil::button_config {} };
    button.set_tooltip(u8"단계 7에서 활성화");
    button.set_enabled(false);
    button.arrange({ { 0.0f, 0.0f, 32.0f, 32.0f }, 1.0f });

    // 비활성이어도 hit는 되어야 tooltip을 보여 줄 수 있다.
    // 액션 차단은 interaction controller의 몫이다.
    REQUIRE(button.hit_test(10.0f, 10.0f) == &button);
    REQUIRE(button.enabled() == false);
}

TEST_CASE("An element picks a cursor from its role unless the app names one", "[ui][element][cursor]")
{
    const luil::interaction_snapshot idle {};
    constexpr luil::ui_element_kind kind_plain { luil::application_element_kind(20) };
    constexpr luil::ui_element_kind kind_draggable { luil::application_element_kind(21) };

    // 아무 역할도 없으면 정하지 않는다.
    //  - 창의 기본 모양이 그대로 쓰인다.
    test_panel plain { luil::ui_element_id { kind_plain } };
    plain.arrange({ { 0.0f, 0.0f, 100.0f, 40.0f }, 1.0f });
    REQUIRE(plain.cursor_at(10.0f, 10.0f, idle) == luil::ui_cursor::inherit);

    // 끌 수 있으면 잡는 모양이고, 끄는 중이면 잡은 모양이다.
    test_panel draggable { luil::ui_element_id { kind_draggable, u8"item" } };
    draggable.arrange({ { 0.0f, 0.0f, 100.0f, 40.0f }, 1.0f });
    luil::drag_source source {};
    source.make_payload = [](const luil::ui_action_context& context) {
        luil::drag_payload payload {};
        payload.source = context.element;
        return payload;
    };
    draggable.set_drag_source(std::move(source));
    REQUIRE(draggable.cursor_at(10.0f, 10.0f, idle) == luil::ui_cursor::grab);

    luil::interaction_snapshot dragging {};
    dragging.drag = luil::drag_visual {};
    dragging.drag->payload.source = draggable.id();
    REQUIRE(draggable.cursor_at(10.0f, 10.0f, dragging) == luil::ui_cursor::grabbing);

    // 앱이 지정한 값이 역할보다 세다.
    draggable.set_cursor(luil::ui_cursor::not_allowed);
    REQUIRE(draggable.cursor_at(10.0f, 10.0f, idle) == luil::ui_cursor::not_allowed);
}

TEST_CASE("The tree keeps the held cursor while a drag is in flight", "[ui][element][cursor]")
{
    constexpr luil::ui_element_kind kind_source { luil::application_element_kind(22) };
    constexpr luil::ui_element_kind kind_other { luil::application_element_kind(23) };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });

    auto dragged { std::make_unique<test_panel>(luil::ui_element_id { kind_source, u8"item" }) };
    dragged->arrange({ { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    luil::drag_source source {};
    source.make_payload = [](const luil::ui_action_context& context) {
        luil::drag_payload payload {};
        payload.source = context.element;
        return payload;
    };
    dragged->set_drag_source(std::move(source));
    const luil::ui_element_id dragged_id { dragged->id() };
    root->add(std::move(dragged));

    auto other { std::make_unique<test_panel>(luil::ui_element_id { kind_other }) };
    other->arrange({ { 100.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    other->set_cursor(luil::ui_cursor::text);
    root->add(std::move(other));

    const luil::ui_tree tree { std::move(root) };
    const luil::interaction_snapshot idle {};
    REQUIRE(tree.cursor_at(150.0f, 50.0f, idle) == luil::ui_cursor::text);

    // 끌고 있는 동안에는 포인터가 다른 element 위에 있어도 잡은 모양이 이어진다.
    luil::interaction_snapshot dragging {};
    dragging.drag = luil::drag_visual {};
    dragging.drag->payload.source = dragged_id;
    REQUIRE(tree.cursor_at(150.0f, 50.0f, dragging) == luil::ui_cursor::grabbing);
}

TEST_CASE("A child arrange context carries the scale and scroll offset", "[ui][element][arrange]")
{
    // 자식 문맥을 손으로 지으면 뒤의 값을 빠뜨리기 쉽다.
    // 자리만 새로 주고 나머지는 부모의 것을 그대로 잇는 것이 계약이다
    // (tree-arrange-design.md).
    const luil::arrange_context parent { { 10.0f, 20.0f, 300.0f, 400.0f }, 1.5f, 64.0f };
    const luil::arrange_context child { parent.for_child({ 0.0f, 100.0f, 120.0f, 24.0f }) };

    REQUIRE(child.slot.x == 0.0f);
    REQUIRE(child.slot.y == 100.0f);
    REQUIRE(child.slot.width == 120.0f);
    REQUIRE(child.slot.height == 24.0f);
    REQUIRE(child.scale == parent.scale);
    REQUIRE(child.scroll_offset == parent.scroll_offset);
}

TEST_CASE("The tree exposes elements that were never arranged", "[ui][element][arrange]")
{
    // 배치 사슬이 끊긴 element는 bounds가 0이라 그려지지도 맞지도 않는다.
    // 중복 id와 같은 종류의 조용한 오동작이라 같은 방법으로 드러낸다.
    constexpr luil::ui_element_kind kind_item { luil::application_element_kind(0) };
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });

    auto placed { std::make_unique<test_panel>(luil::ui_element_id { kind_item, u8"placed" }) };
    placed->arrange({ { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    root->add(std::move(placed));

    // 담기만 하고 배치를 빠뜨렸다.
    root->add(std::make_unique<test_panel>(luil::ui_element_id { kind_item, u8"forgotten" }));

    // 보이지 않는 element는 배치할 이유가 없어 세지 않는다
    // (넘치지 않는 탭 막대의 넘침 버튼이 그렇다).
    auto hidden { std::make_unique<test_panel>(luil::ui_element_id { kind_item, u8"hidden" }) };
    hidden->set_visible(false);
    root->add(std::move(hidden));

    const luil::ui_tree tree { std::move(root) };
    REQUIRE(tree.unarranged() == std::vector<luil::ui_element_id> { luil::ui_element_id { kind_item, u8"forgotten" } });
}

TEST_CASE("make_arranged_tree places the root and leaves nothing unarranged", "[ui][element][arrange]")
{
    // root가 자식까지 배치하는 tree는 "배치하고 감싼다"가 언제나 같은 두 줄이었다.
    constexpr luil::ui_element_kind kind_row { luil::application_element_kind(1) };
    luil::stack_config config {};
    config.spacing = 4.0f;
    auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_row }, config) };
    column->add(std::make_unique<test_panel>(luil::ui_element_id { kind_row, u8"first" }), 20.0f);
    column->add(std::make_unique<test_panel>(luil::ui_element_id { kind_row, u8"second" }), 20.0f);

    const luil::ui_tree tree { luil::make_arranged_tree(std::move(column), { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f) };
    REQUIRE(tree.unarranged().empty());
    REQUIRE(tree.root() != nullptr);
    REQUIRE(tree.root()->bounds().width == 200.0f);
    REQUIRE(tree.find(luil::ui_element_id { kind_row, u8"second" })->bounds().y == 24.0f);
}

TEST_CASE("An empty tree answers every query without a root", "[ui][element]")
{
    // 생성자가 null root를 허용하므로 빈 tree는 지원되는 상태다 — 질의는 전부
    // "없다"로 답하고 어디서도 역참조하지 않는다 (find·hit_test와 같은 규칙).
    const luil::ui_tree empty { nullptr };
    REQUIRE(empty.root() == nullptr);
    REQUIRE(empty.hit_test(10.0f, 10.0f) == nullptr);
    REQUIRE(empty.find(luil::ui_element_id { luil::ui_element_kind::root }) == nullptr);
    REQUIRE(empty.focus_order().empty());
    REQUIRE(empty.duplicate_ids().empty());
    REQUIRE(empty.unarranged().empty());
    REQUIRE(empty.focus_trap() == nullptr);
    REQUIRE(empty.visibly_contains(luil::ui_element_id { luil::ui_element_kind::root }) == false);

    // make_arranged_tree도 null root를 같은 규칙으로 지나보낸다.
    const luil::ui_tree arranged { luil::make_arranged_tree(nullptr, { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f) };
    REQUIRE(arranged.root() == nullptr);
}

TEST_CASE("Anything that can be pressed is a tab stop unless the maker refuses", "[ui][element][focus]")
{
    // 마우스로 누를 수 있는데 키보드로는 갈 수 없는 자리를 만들지 않는 것이
    // 기본값의 뜻이다 (keyboard-focus-design.md).
    test_panel plain { luil::ui_element_id { kind_card_list } };
    REQUIRE(plain.tab_stop() == false);
    plain.set_action(luil::ui_trigger::left_click, noop_action());
    REQUIRE(plain.tab_stop());

    // 자리라고 해서 지금 초점을 받을 수 있는 것은 아니다.
    REQUIRE(plain.focusable() == false);
    plain.arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    REQUIRE(plain.focusable());
    plain.set_enabled(false);
    REQUIRE(plain.focusable() == false);
    plain.set_enabled(true);
    plain.set_visible(false);
    REQUIRE(plain.focusable() == false);
    plain.set_visible(true);

    // 만드는 쪽이 거절하면 누를 수 있어도 자리가 아니다 (창 버튼·메뉴 항목).
    plain.set_tab_stop(false);
    REQUIRE(plain.tab_stop() == false);
    REQUIRE(plain.focusable() == false);

    // 액션 없이 초점만 받는 것은 자처한다.
    test_panel box { luil::ui_element_id { kind_card_body } };
    box.arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    box.set_tab_stop(true);
    REQUIRE(box.focusable());
}

TEST_CASE("The focus order follows the drawing order and skips hidden branches", "[ui][element][focus]")
{
    constexpr luil::ui_element_kind kind_stop { luil::application_element_kind(4) };
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });

    const auto add_stop = [&root](const std::u8string_view name, const bool enabled, const bool refused) {
        auto stop { std::make_unique<test_panel>(card_id(kind_stop, name)) };
        stop->set_action(luil::ui_trigger::left_click, noop_action());
        stop->set_enabled(enabled);
        if (refused)
            stop->set_tab_stop(false);
        stop->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
        root->add(std::move(stop));
    };

    add_stop(u8"first", true, false);
    add_stop(u8"disabled", false, false);
    add_stop(u8"refused", true, true);

    // 보이지 않는 가지는 그 안의 자리까지 통째로 건너뛴다.
    // 자식만 보면 멀쩡한 자리라 tree를 직접 걸어야 드러난다.
    auto hidden { std::make_unique<test_panel>(card_id(kind_stop, u8"hidden-branch")) };
    hidden->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    hidden->set_visible(false);
    auto buried { std::make_unique<test_panel>(card_id(kind_stop, u8"buried")) };
    buried->set_action(luil::ui_trigger::left_click, noop_action());
    buried->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
    hidden->add(std::move(buried));
    root->add(std::move(hidden));

    add_stop(u8"last", true, false);

    const luil::ui_tree tree { std::move(root) };
    REQUIRE(tree.focus_order() == std::vector<luil::ui_element_id> { card_id(kind_stop, u8"first"), card_id(kind_stop, u8"last") });
}

TEST_CASE("A focus group folds into one stop and enters at its named item", "[ui][element][focus]")
{
    constexpr luil::ui_element_kind kind_stop { luil::application_element_kind(5) };
    constexpr luil::ui_element_kind kind_group { luil::application_element_kind(6) };

    // 같은 화면을 들어가는 자리만 바꿔 두 번 짓는다.
    // tree는 frame마다 다시 지어지므로 "선택된 것"이 바뀌면 이렇게 따라온다.
    const auto build = [](const std::u8string_view entry) {
        const auto make_stop = [](const std::u8string_view name) {
            auto stop { std::make_unique<test_panel>(card_id(kind_stop, name)) };
            stop->set_action(luil::ui_trigger::left_click, noop_action());
            stop->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
            return stop;
        };

        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        root->add(make_stop(u8"before"));

        auto group { std::make_unique<test_panel>(luil::ui_element_id { kind_group, u8"tabs" }) };
        group->arrange({ { 0.0f, 0.0f, 200.0f, 20.0f }, 1.0f });
        // 탭 막대·라디오 묶음처럼 항목이 줄선 것은 Tab에서 한 자리다.
        group->set_focus_group(luil::focus_axis::horizontal);
        if (entry.empty() == false)
            group->set_focus_entry(card_id(kind_stop, entry));
        group->add(make_stop(u8"tab-1"));
        group->add(make_stop(u8"tab-2"));
        group->add(make_stop(u8"tab-3"));
        root->add(std::move(group));
        root->add(make_stop(u8"after"));
        return luil::ui_tree { std::move(root) };
    };

    // 이름 붙이지 않으면 첫 항목이 들어가는 자리다.
    REQUIRE(build(u8"").focus_order() == std::vector<luil::ui_element_id> { card_id(kind_stop, u8"before"), card_id(kind_stop, u8"tab-1"), card_id(kind_stop, u8"after") });

    // 앱 상태의 "선택된 것"을 이름 붙이면 거기로 들어간다. 자리 수는 그대로 셋이다.
    REQUIRE(build(u8"tab-3").focus_order() == std::vector<luil::ui_element_id> { card_id(kind_stop, u8"before"), card_id(kind_stop, u8"tab-3"), card_id(kind_stop, u8"after") });
}

TEST_CASE("A named entry that cannot take focus falls back to the first item", "[ui][element][focus]")
{
    constexpr luil::ui_element_kind kind_stop { luil::application_element_kind(7) };
    constexpr luil::ui_element_kind kind_group { luil::application_element_kind(8) };

    auto group { std::make_unique<test_panel>(luil::ui_element_id { kind_group, u8"choices" }) };
    group->arrange({ { 0.0f, 0.0f, 200.0f, 60.0f }, 1.0f });
    group->set_focus_group(luil::focus_axis::vertical);
    group->set_focus_entry(card_id(kind_stop, u8"gone"));
    const auto add_item = [&group](const std::u8string_view name, const bool enabled) {
        auto item { std::make_unique<test_panel>(card_id(kind_stop, name)) };
        item->set_action(luil::ui_trigger::left_click, noop_action());
        item->set_enabled(enabled);
        item->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
        group->add(std::move(item));
    };
    // 비활성인 첫 항목은 자리가 아니라 항목에도 들지 않는다.
    add_item(u8"disabled", false);
    add_item(u8"first", true);
    add_item(u8"second", true);

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    root->add(std::move(group));

    const luil::ui_tree tree { std::move(root) };
    REQUIRE(tree.focus_order() == std::vector<luil::ui_element_id> { card_id(kind_stop, u8"first") });
}

TEST_CASE("An empty focus group makes no stop at all", "[ui][element][focus]")
{
    constexpr luil::ui_element_kind kind_group { luil::application_element_kind(9) };
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    auto group { std::make_unique<test_panel>(luil::ui_element_id { kind_group, u8"empty" }) };
    group->arrange({ { 0.0f, 0.0f, 200.0f, 20.0f }, 1.0f });
    group->set_focus_group(luil::focus_axis::both);
    // 묶음 자신이 눌린다고 자리가 되지 않는다. 자리는 항목이다.
    group->set_action(luil::ui_trigger::left_click, noop_action());
    root->add(std::move(group));

    const luil::ui_tree tree { std::move(root) };
    REQUIRE(tree.focus_order().empty());
}

TEST_CASE("A focus trap folds the order to what it holds", "[ui][element][focus]")
{
    constexpr luil::ui_element_kind kind_stop { luil::application_element_kind(10) };
    constexpr luil::ui_element_kind kind_trap { luil::application_element_kind(11) };

    const auto make_stop = [](const luil::ui_element_kind kind, const std::u8string_view name) {
        auto stop { std::make_unique<test_panel>(card_id(kind, name)) };
        stop->set_action(luil::ui_trigger::left_click, noop_action());
        stop->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
        return stop;
    };

    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    root->add(make_stop(kind_stop, u8"behind"));

    // scrim처럼 클릭을 흡수하느라 눌리는 element지만 Tab은 여기 서지 않는다.
    // 자리는 그 안의 컨트롤들이다 (묶음과 같은 규칙).
    auto trap { make_stop(kind_trap, u8"modal") };
    trap->set_focus_trap(true);
    trap->add(make_stop(kind_stop, u8"cancel"));
    trap->add(make_stop(kind_stop, u8"confirm"));
    root->add(std::move(trap));
    root->add(make_stop(kind_stop, u8"after"));

    const luil::ui_tree tree { std::move(root) };
    REQUIRE(tree.focus_order() == std::vector<luil::ui_element_id> { card_id(kind_stop, u8"cancel"), card_id(kind_stop, u8"confirm") });
    REQUIRE(tree.focus_trap() != nullptr);
    REQUIRE(tree.focus_trap()->id() == card_id(kind_trap, u8"modal"));
    REQUIRE(tree.within_focus_trap(card_id(kind_stop, u8"confirm")));
    // 가둠 밖의 자리는 tree에 있어도 안이 아니다 — 초점이 거기 남을 수 없다.
    REQUIRE(tree.within_focus_trap(card_id(kind_stop, u8"behind")) == false);
    REQUIRE(tree.within_focus_trap(card_id(kind_stop, u8"after")) == false);
}

TEST_CASE("The last visible focus trap in drawing order wins", "[ui][element][focus]")
{
    constexpr luil::ui_element_kind kind_stop { luil::application_element_kind(12) };
    constexpr luil::ui_element_kind kind_trap { luil::application_element_kind(13) };

    // 중첩된 가둠이 있는 화면과 없는 화면을 같은 조립으로 짓는다.
    const auto build = [](const bool nested) {
        const auto make_stop = [](const luil::ui_element_kind kind, const std::u8string_view name) {
            auto stop { std::make_unique<test_panel>(card_id(kind, name)) };
            stop->set_action(luil::ui_trigger::left_click, noop_action());
            stop->arrange({ { 0.0f, 0.0f, 100.0f, 20.0f }, 1.0f });
            return stop;
        };
        const auto make_trap = [&make_stop](const std::u8string_view name, const std::u8string_view stop) {
            auto trap { make_stop(kind_trap, name) };
            trap->set_focus_trap(true);
            trap->add(make_stop(kind_stop, stop));
            return trap;
        };

        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });

        // 보이지 않는 가둠은 없는 것이다 (hit test·focus_order와 같은 규칙).
        auto hidden { make_trap(u8"hidden", u8"hidden-stop") };
        hidden->set_visible(false);
        root->add(std::move(hidden));

        root->add(make_trap(u8"first", u8"first-stop"));
        auto second { make_trap(u8"second", u8"second-stop") };
        if (nested)
            second->add(make_trap(u8"inner", u8"inner-stop"));
        root->add(std::move(second));
        return luil::ui_tree { std::move(root) };
    };

    // 겹친 dialog 중 나중에 그린 것이 키보드를 갖는다.
    const luil::ui_tree stacked { build(false) };
    REQUIRE(stacked.focus_trap() != nullptr);
    REQUIRE(stacked.focus_trap()->id() == card_id(kind_trap, u8"second"));
    REQUIRE(stacked.focus_order() == std::vector<luil::ui_element_id> { card_id(kind_stop, u8"second-stop") });

    // 중첩도 같은 규칙으로 풀린다 — 자손이 그리기 순서에서 뒤에 온다.
    const luil::ui_tree inner { build(true) };
    REQUIRE(inner.focus_trap() != nullptr);
    REQUIRE(inner.focus_trap()->id() == card_id(kind_trap, u8"inner"));
    REQUIRE(inner.focus_order() == std::vector<luil::ui_element_id> { card_id(kind_stop, u8"inner-stop") });
}

TEST_CASE("A surface filter keeps only the interaction that happened on that surface", "[ui][element][window]")
{
    // `ui_element_id`는 tree 안에서만 안정적이라 두 표면에 같은 id가 사는 것이
    // 유효하다. 그래서 표면 경계에서 남의 것을 지운다 —
    // element는 지금처럼 자기 id와 비교하기만 한다 (multi-window-design.md).
    constexpr luil::ui_element_kind kind_row { luil::application_element_kind(30) };
    constexpr luil::ui_element_kind kind_item { luil::application_element_kind(31) };
    const luil::ui_element_id row { kind_row, u8"3" };
    const luil::ui_element_id item { kind_item, u8"copy" };
    const std::chrono::steady_clock::time_point moment { std::chrono::steady_clock::now() };

    // 보조 창 하나가 전부를 쥔 상태다.
    luil::interaction_snapshot published {};
    published.hovered = row;
    published.hovered_surface = u8"tool";
    published.hover_started_at = moment;
    published.pressed = row;
    published.pressed_surface = u8"tool";
    published.focused = row;
    published.focused_input = row;
    published.focused_surface = u8"tool";
    published.focus_started_at = moment;
    published.focus_visible = true;
    published.menu_highlight = item;
    published.menu_surface = u8"tool";
    published.drag = luil::drag_visual {};
    published.drag->surface = u8"tool";

    SECTION("자기 표면 것은 그대로 남는다")
    {
        REQUIRE(luil::interaction_for_surface(published, u8"tool") == published);
    }

    SECTION("남의 표면 것은 전부 빈다")
    {
        // 주 창이 그리는 자리다 (표면 id가 빈 문자열이다).
        const luil::interaction_snapshot view { luil::interaction_for_surface(published, {}) };
        REQUIRE(view.hovered == luil::ui_element_id {});
        REQUIRE(view.hovered_surface.empty());
        REQUIRE(view.hover_started_at.has_value() == false);
        REQUIRE(view.pressed == luil::ui_element_id {});
        REQUIRE(view.pressed_surface.empty());
        REQUIRE(view.focused == luil::ui_element_id {});
        REQUIRE(view.focused_input == luil::ui_element_id {});
        REQUIRE(view.focused_surface.empty());
        REQUIRE(view.focus_started_at.has_value() == false);
        REQUIRE(view.focus_visible == false);
        REQUIRE(view.menu_highlight == luil::ui_element_id {});
        REQUIRE(view.menu_surface.empty());
        REQUIRE(view.drag.has_value() == false);
        // 값과 표식을 함께 지웠으면 갓 만든 것과 구별되지 않는다.
        REQUIRE(view == luil::interaction_snapshot {});
    }

    SECTION("값마다 표식이 따로라 하나씩 갈린다")
    {
        // 포인터는 주 창에, 초점·메뉴 강조·끌기는 보조 창에 있는 흔한 상태다.
        luil::interaction_snapshot split { published };
        split.hovered_surface.clear();
        split.pressed_surface.clear();

        const luil::interaction_snapshot view { luil::interaction_for_surface(split, {}) };
        REQUIRE(view.hovered == row);
        REQUIRE(view.hover_started_at.has_value());
        REQUIRE(view.pressed == row);
        REQUIRE(view.focused == luil::ui_element_id {});
        REQUIRE(view.focused_input == luil::ui_element_id {});
        REQUIRE(view.menu_highlight == luil::ui_element_id {});
        REQUIRE(view.drag.has_value() == false);
    }

    SECTION("한 번 거른 것을 다시 걸러도 달라지지 않는다")
    {
        const luil::interaction_snapshot view { luil::interaction_for_surface(published, u8"tool") };
        REQUIRE(luil::interaction_for_surface(view, u8"tool") == view);
    }
}
