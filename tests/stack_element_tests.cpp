#include "luil/ui/stack_element.h"

#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

namespace {
    // 배치만 검증하는 최소 element다.
    class slot_probe final : public luil::ui_element
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

    [[nodiscard]] std::unique_ptr<slot_probe> make_probe(const std::u8string& name)
    {
        return std::make_unique<slot_probe>(luil::ui_element_id { luil::application_element_kind(30), name });
    }
} // namespace

TEST_CASE("A column stack gives fixed lengths first and splits the rest by weight", "[ui][stack]")
{
    luil::stack_config config {};
    config.direction = luil::stack_direction::column;
    config.spacing = 10.0f;
    config.padding = luil::edge_insets::all(20.0f);
    luil::stack_element stack { luil::ui_element_id { luil::application_element_kind(31) }, config };

    auto header { make_probe(u8"header") };
    auto body { make_probe(u8"body") };
    auto footer { make_probe(u8"footer") };
    const luil::ui_element* const header_probe { header.get() };
    const luil::ui_element* const body_probe { body.get() };
    const luil::ui_element* const footer_probe { footer.get() };
    stack.add(std::move(header), 30.0f);
    stack.add_flexible(std::move(body));
    stack.add(std::move(footer), 40.0f);

    stack.arrange({ { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f });

    // 안쪽 높이 260에서 고정 70과 간격 20을 빼면 170이 남는다.
    REQUIRE(header_probe->bounds().y == 20.0f);
    REQUIRE(header_probe->bounds().height == 30.0f);
    REQUIRE(body_probe->bounds().y == 60.0f);
    REQUIRE(body_probe->bounds().height == 170.0f);
    REQUIRE(footer_probe->bounds().y == 240.0f);
    REQUIRE(footer_probe->bounds().height == 40.0f);

    // 교차축은 기본이 stretch라 안쪽 폭을 다 채운다.
    REQUIRE(header_probe->bounds().x == 20.0f);
    REQUIRE(header_probe->bounds().width == 160.0f);
}

TEST_CASE("A row stack shares the remainder in weight ratio and honours cross alignment", "[ui][stack]")
{
    luil::stack_config config {};
    config.direction = luil::stack_direction::row;
    config.cross_alignment = luil::stack_alignment::center;
    luil::stack_element stack { luil::ui_element_id { luil::application_element_kind(32) }, config };

    auto narrow { make_probe(u8"narrow") };
    auto wide { make_probe(u8"wide") };
    const luil::ui_element* const narrow_probe { narrow.get() };
    const luil::ui_element* const wide_probe { wide.get() };
    stack.add(std::move(narrow), { .weight = 1.0f, .cross_length = 20.0f });
    stack.add(std::move(wide), { .weight = 3.0f, .cross_length = 20.0f });

    stack.arrange({ { 0.0f, 0.0f, 400.0f, 100.0f }, 1.0f });

    REQUIRE(narrow_probe->bounds().width == 100.0f);
    REQUIRE(wide_probe->bounds().x == 100.0f);
    REQUIRE(wide_probe->bounds().width == 300.0f);
    // 교차축 길이를 정했으므로 가운데로 놓인다.
    REQUIRE(narrow_probe->bounds().y == 40.0f);
    REQUIRE(narrow_probe->bounds().height == 20.0f);
}

TEST_CASE("A stack gap takes room without adding an element", "[ui][stack]")
{
    luil::stack_config config {};
    config.direction = luil::stack_direction::row;
    luil::stack_element stack { luil::ui_element_id { luil::application_element_kind(33) }, config };

    auto first { make_probe(u8"first") };
    auto last { make_probe(u8"last") };
    const luil::ui_element* const first_probe { first.get() };
    const luil::ui_element* const last_probe { last.get() };
    stack.add(std::move(first), 50.0f);
    stack.add_flexible_gap();
    stack.add(std::move(last), 50.0f);

    stack.arrange({ { 0.0f, 0.0f, 300.0f, 40.0f }, 1.0f });

    // 자리만 비우므로 자식은 둘뿐이고 마지막은 오른쪽 끝에 붙는다.
    REQUIRE(stack.children().size() == 2u);
    REQUIRE(first_probe->bounds().x == 0.0f);
    REQUIRE(last_probe->bounds().x == 250.0f);
}

TEST_CASE("Stack lengths are logical pixels scaled at arrange time", "[ui][stack]")
{
    luil::stack_config config {};
    config.spacing = 8.0f;
    config.padding = luil::edge_insets::symmetric(10.0f, 0.0f);
    luil::stack_element stack { luil::ui_element_id { luil::application_element_kind(34) }, config };

    auto first { make_probe(u8"first") };
    auto second { make_probe(u8"second") };
    const luil::ui_element* const first_probe { first.get() };
    const luil::ui_element* const second_probe { second.get() };
    stack.add(std::move(first), 30.0f);
    stack.add(std::move(second), 30.0f);

    stack.arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 2.0f });

    // 배율 2에서 길이 30은 60이고 간격 8은 16이다.
    REQUIRE(first_probe->bounds().height == 60.0f);
    REQUIRE(second_probe->bounds().y == 76.0f);
    // 좌우 여백 10도 20씩이다.
    REQUIRE(first_probe->bounds().x == 20.0f);
    REQUIRE(first_probe->bounds().width == 160.0f);
}

TEST_CASE("A hidden fixed child keeps its room and its slot", "[ui][stack]")
{
    luil::stack_config config {};
    config.spacing = 10.0f;
    luil::stack_element stack { luil::ui_element_id { luil::application_element_kind(35) }, config };

    auto first { make_probe(u8"first") };
    auto hidden { make_probe(u8"hidden") };
    auto last { make_probe(u8"last") };
    const luil::ui_element* const first_probe { first.get() };
    const luil::ui_element* const hidden_probe { hidden.get() };
    const luil::ui_element* const last_probe { last.get() };
    hidden->set_visible(false);
    stack.add(std::move(first), 30.0f);
    stack.add(std::move(hidden), 40.0f);
    stack.add(std::move(last), 30.0f);

    stack.arrange({ { 0.0f, 0.0f, 100.0f, 300.0f }, 1.0f });

    // 보임은 표시의 일이고 자리는 배치의 일이라, 숨긴 자식도 길이와 간격을 그대로 먹는다.
    // 뒤 항목이 그만큼 밀려 있는 것이 그 증거다.
    REQUIRE(first_probe->bounds().y == 0.0f);
    REQUIRE(hidden_probe->bounds().y == 40.0f);
    REQUIRE(hidden_probe->bounds().height == 40.0f);
    REQUIRE(last_probe->bounds().y == 90.0f);
    // 자리를 받았으므로 배치된 것이다.
    //  - `ui_tree::unarranged()`로는 이것을 볼 수 없다. 보이지 않는 element를 세지
    //    않으므로 배치를 건너뛰어도 그 목록은 비어 있다.
    REQUIRE(hidden_probe->arranged());
}

TEST_CASE("A hidden flexible child still takes its share of the remainder", "[ui][stack]")
{
    luil::stack_config config {};
    luil::stack_element stack { luil::ui_element_id { luil::application_element_kind(36) }, config };

    auto shown { make_probe(u8"shown") };
    auto hidden { make_probe(u8"hidden") };
    const luil::ui_element* const shown_probe { shown.get() };
    const luil::ui_element* const hidden_probe { hidden.get() };
    hidden->set_visible(false);
    stack.add_flexible(std::move(shown));
    stack.add_flexible(std::move(hidden));

    stack.arrange({ { 0.0f, 0.0f, 100.0f, 200.0f }, 1.0f });

    // 숨긴 자식이 weight 몫을 그대로 가져가므로 아래 절반이 빈 채로 남는다.
    REQUIRE(shown_probe->bounds().height == 100.0f);
    REQUIRE(hidden_probe->bounds().y == 100.0f);
    REQUIRE(hidden_probe->bounds().height == 100.0f);
}

TEST_CASE("An overflowing stack gives the flexible child nothing and lets the fixed ones out", "[ui][stack]")
{
    luil::stack_config config {};
    luil::stack_element stack { luil::ui_element_id { luil::application_element_kind(37) }, config };

    auto first { make_probe(u8"first") };
    auto body { make_probe(u8"body") };
    auto second { make_probe(u8"second") };
    const luil::ui_element* const first_probe { first.get() };
    const luil::ui_element* const body_probe { body.get() };
    const luil::ui_element* const second_probe { second.get() };
    stack.add(std::move(first), 200.0f);
    stack.add_flexible(std::move(body));
    stack.add(std::move(second), 200.0f);

    stack.arrange({ { 0.0f, 0.0f, 100.0f, 300.0f }, 1.0f });

    // 남는 자리가 음수면 0으로 자른다 — 유연 항목이 먼저 사라진다.
    REQUIRE(first_probe->bounds().height == 200.0f);
    REQUIRE(body_probe->bounds().height == 0.0f);
    // 고정 항목은 제 길이를 지키므로 slot 밖으로 나간다. stack은 자르지 않는다.
    REQUIRE(second_probe->bounds().y == 200.0f);
    REQUIRE(second_probe->bounds().height == 200.0f);
}

TEST_CASE("Cross alignment places a child that was given a cross length", "[ui][stack]")
{
    const auto place = [](const luil::stack_alignment alignment) {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.cross_alignment = alignment;
        auto stack { std::make_unique<luil::stack_element>(luil::ui_element_id { luil::application_element_kind(38) }, config) };
        auto child { make_probe(u8"child") };
        const luil::ui_element* const probe { child.get() };
        stack->add(std::move(child), { .length = 50.0f, .cross_length = 20.0f });
        stack->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
        return probe->bounds();
    };

    REQUIRE(place(luil::stack_alignment::start).y == 0.0f);
    REQUIRE(place(luil::stack_alignment::center).y == 40.0f);
    REQUIRE(place(luil::stack_alignment::end).y == 80.0f);
    REQUIRE(place(luil::stack_alignment::end).height == 20.0f);
}

TEST_CASE("Stretch is start once the child was given a cross length", "[ui][stack]")
{
    const auto place = [](const luil::stack_alignment alignment, const float cross_length) {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.cross_alignment = alignment;
        auto stack { std::make_unique<luil::stack_element>(luil::ui_element_id { luil::application_element_kind(39) }, config) };
        auto child { make_probe(u8"child") };
        const luil::ui_element* const probe { child.get() };
        stack->add(std::move(child), { .length = 50.0f, .cross_length = cross_length });
        stack->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
        return probe->bounds();
    };

    // 실제로 늘어나는지는 열거값이 아니라 교차축 길이를 주었는가가 정한다.
    // 길이를 주지 않으면 값과 무관하게 다 채우고,
    REQUIRE(place(luil::stack_alignment::stretch, 0.0f).height == 100.0f);
    REQUIRE(place(luil::stack_alignment::end, 0.0f).height == 100.0f);
    // 길이를 주면 `stretch`는 `start`와 같은 자리다 — 늘릴 길이가 없어서다.
    REQUIRE(place(luil::stack_alignment::stretch, 20.0f).y == 0.0f);
    REQUIRE(place(luil::stack_alignment::stretch, 20.0f).height == 20.0f);
}

TEST_CASE("A fixed gap takes room without adding an element", "[ui][stack]")
{
    luil::stack_config config {};
    config.direction = luil::stack_direction::row;
    luil::stack_element stack { luil::ui_element_id { luil::application_element_kind(40) }, config };

    auto first { make_probe(u8"first") };
    auto last { make_probe(u8"last") };
    const luil::ui_element* const first_probe { first.get() };
    const luil::ui_element* const last_probe { last.get() };
    stack.add(std::move(first), 50.0f);
    stack.add_gap(30.0f);
    stack.add(std::move(last), 50.0f);

    stack.arrange({ { 0.0f, 0.0f, 300.0f, 40.0f }, 1.0f });

    REQUIRE(stack.children().size() == 2u);
    REQUIRE(first_probe->bounds().x == 0.0f);
    REQUIRE(last_probe->bounds().x == 80.0f);
}

TEST_CASE("An empty stack takes its slot and a non-positive weight becomes one", "[ui][stack]")
{
    luil::stack_config config {};
    config.spacing = 10.0f;
    luil::stack_element empty { luil::ui_element_id { luil::application_element_kind(41) }, config };
    empty.arrange({ { 10.0f, 20.0f, 100.0f, 50.0f }, 1.0f });

    // 항목이 없으면 자기 자리만 잡고 돌아온다 (간격 계산이 항목 수에서 하나를 빼므로).
    REQUIRE(empty.bounds().x == 10.0f);
    REQUIRE(empty.bounds().width == 100.0f);
    REQUIRE(empty.children().empty());

    luil::stack_config plain {};
    luil::stack_element stack { luil::ui_element_id { luil::application_element_kind(41), u8"weights" }, plain };
    auto first { make_probe(u8"first") };
    auto second { make_probe(u8"second") };
    const luil::ui_element* const first_probe { first.get() };
    const luil::ui_element* const second_probe { second.get() };
    stack.add_flexible(std::move(first), -1.0f);
    stack.add_flexible(std::move(second), 1.0f);

    stack.arrange({ { 0.0f, 0.0f, 100.0f, 200.0f }, 1.0f });

    // 0 이하의 weight는 1.0으로 본다 — 둘이 반씩 나눈다.
    REQUIRE(first_probe->bounds().height == 100.0f);
    REQUIRE(second_probe->bounds().height == 100.0f);
}

TEST_CASE("Main alignment moves the free room to the middle or the end", "[ui][stack]")
{
    const auto place = [](const luil::stack_main_alignment alignment) {
        luil::stack_config config {};
        config.spacing = 10.0f;
        config.padding = luil::edge_insets::all(20.0f);
        config.main_alignment = alignment;
        auto stack { std::make_unique<luil::stack_element>(luil::ui_element_id { luil::application_element_kind(42) }, config) };
        auto first { make_probe(u8"first") };
        auto second { make_probe(u8"second") };
        const luil::ui_element* const first_probe { first.get() };
        const luil::ui_element* const second_probe { second.get() };
        stack->add(std::move(first), 30.0f);
        stack->add(std::move(second), 30.0f);
        stack->arrange({ { 0.0f, 0.0f, 100.0f, 300.0f }, 1.0f });
        return std::pair { first_probe->bounds().y, second_probe->bounds().y };
    };

    // 안쪽 높이 260에서 고정 60과 간격 10을 빼면 190이 남는다.
    REQUIRE(place(luil::stack_main_alignment::start).first == 20.0f);
    REQUIRE(place(luil::stack_main_alignment::center).first == 115.0f);
    REQUIRE(place(luil::stack_main_alignment::end).first == 210.0f);
    // 마지막 항목의 아래끝이 안쪽 끝(280)에 붙는다.
    REQUIRE(place(luil::stack_main_alignment::end).second == 250.0f);
}

TEST_CASE("A flexible item leaves no free room to align", "[ui][stack]")
{
    const auto place = [](const luil::stack_main_alignment alignment) {
        luil::stack_config config {};
        config.main_alignment = alignment;
        auto stack { std::make_unique<luil::stack_element>(luil::ui_element_id { luil::application_element_kind(43) }, config) };
        auto head { make_probe(u8"head") };
        const luil::ui_element* const probe { head.get() };
        stack->add(std::move(head), 30.0f);
        stack->add_flexible(make_probe(u8"body"));
        stack->arrange({ { 0.0f, 0.0f, 100.0f, 300.0f }, 1.0f });
        return probe->bounds().y;
    };

    // 남는 자리를 유연 항목이 다 가져가므로 정렬은 아무 일도 하지 않는다.
    REQUIRE(place(luil::stack_main_alignment::start) == 0.0f);
    REQUIRE(place(luil::stack_main_alignment::center) == 0.0f);
    REQUIRE(place(luil::stack_main_alignment::end) == 0.0f);
}

TEST_CASE("An overflowing stack has no free room to align", "[ui][stack]")
{
    const auto place = [](const luil::stack_main_alignment alignment) {
        luil::stack_config config {};
        config.main_alignment = alignment;
        auto stack { std::make_unique<luil::stack_element>(luil::ui_element_id { luil::application_element_kind(44) }, config) };
        auto first { make_probe(u8"first") };
        const luil::ui_element* const probe { first.get() };
        stack->add(std::move(first), 200.0f);
        stack->add(make_probe(u8"second"), 200.0f);
        stack->arrange({ { 0.0f, 0.0f, 100.0f, 300.0f }, 1.0f });
        return probe->bounds().y;
    };

    // 남는 자리가 이미 0으로 잘려 있어 셋이 모두 같다 — 음수 여유가 계약에 없다.
    REQUIRE(place(luil::stack_main_alignment::start) == 0.0f);
    REQUIRE(place(luil::stack_main_alignment::center) == 0.0f);
    REQUIRE(place(luil::stack_main_alignment::end) == 0.0f);
}

TEST_CASE("A leading flexible gap and an end alignment agree exactly", "[ui][stack]")
{
    const auto place = [](const bool with_gap) {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 8.0f;
        config.main_alignment = with_gap ? luil::stack_main_alignment::start : luil::stack_main_alignment::end;
        auto stack { std::make_unique<luil::stack_element>(luil::ui_element_id { luil::application_element_kind(45) }, config) };
        if (with_gap)
            stack->add_flexible_gap();
        auto cancel { make_probe(u8"cancel") };
        auto confirm { make_probe(u8"confirm") };
        const luil::ui_element* const cancel_probe { cancel.get() };
        const luil::ui_element* const confirm_probe { confirm.get() };
        stack->add(std::move(cancel), 88.0f);
        stack->add(std::move(confirm), 88.0f);
        stack->arrange({ { 0.0f, 0.0f, 300.0f, 40.0f }, 1.0f });
        return std::pair { cancel_probe->bounds().x, confirm_probe->bounds().x };
    };

    // gap이 항목 수를 하나 늘려 간격 한 칸을 더하지만, 그 칸이 gap 자신의 전진에서
    // **정확히 상쇄된다**. 그래서 두 방식은 언제나 같은 자리를 준다.
    //  - 그래도 둘을 가르는 이유는 뜻이다. gap은 항목 **사이**의 도구이고 정렬은
    //    **바깥**의 도구다. 가운데 정렬을 gap으로 하려면 둘이 필요하다.
    REQUIRE(place(true) == place(false));
    REQUIRE(place(false).first == 116.0f);
    REQUIRE(place(false).second == 212.0f);
}

TEST_CASE("A flexible item never shrinks below its minimum", "[ui][stack]")
{
    luil::stack_config config {};
    config.direction = luil::stack_direction::row;
    luil::stack_element stack { luil::ui_element_id { luil::application_element_kind(46) }, config };

    auto fixed { make_probe(u8"fixed") };
    auto flexible { make_probe(u8"flexible") };
    const luil::ui_element* const fixed_probe { fixed.get() };
    const luil::ui_element* const flexible_probe { flexible.get() };
    stack.add(std::move(fixed), 200.0f);
    stack.add(std::move(flexible), { .weight = 1.0f, .minimum = 160.0f });

    stack.arrange({ { 0.0f, 0.0f, 300.0f, 40.0f }, 1.0f });

    // 남는 자리는 100뿐이지만 하한이 160이라 그만큼 받는다.
    REQUIRE(fixed_probe->bounds().width == 200.0f);
    REQUIRE(flexible_probe->bounds().x == 200.0f);
    REQUIRE(flexible_probe->bounds().width == 160.0f);
    // **하한은 넘침을 만든다.** 자르는 것은 이 element의 일이 아니다.
    REQUIRE(flexible_probe->bounds().x + flexible_probe->bounds().width == 360.0f);
}

TEST_CASE("A minimum does not take room from the other flexible items", "[ui][stack]")
{
    const auto place = [](const float width) {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        auto stack { std::make_unique<luil::stack_element>(luil::ui_element_id { luil::application_element_kind(47) }, config) };
        auto guarded { make_probe(u8"guarded") };
        auto plain { make_probe(u8"plain") };
        const luil::ui_element* const guarded_probe { guarded.get() };
        const luil::ui_element* const plain_probe { plain.get() };
        stack->add(std::move(guarded), { .weight = 1.0f, .minimum = 160.0f });
        stack->add(std::move(plain), { .weight = 1.0f });
        stack->arrange({ { 0.0f, 0.0f, width, 40.0f }, 1.0f });
        return std::pair { guarded_probe->bounds().width, plain_probe->bounds().width };
    };

    // 하한이 넉넉하면 비율 분배 그대로다.
    REQUIRE(place(400.0f) == std::pair { 200.0f, 200.0f });
    // 하나가 하한에 걸려도 **다른 항목의 몫은 줄지 않는다** — 재분배하지 않는다.
    REQUIRE(place(300.0f) == std::pair { 160.0f, 150.0f });
}

TEST_CASE("A minimum is a logical pixel and only a flexible item has one", "[ui][stack]")
{
    luil::stack_config config {};
    config.direction = luil::stack_direction::row;
    luil::stack_element stack { luil::ui_element_id { luil::application_element_kind(48) }, config };

    auto scaled { make_probe(u8"scaled") };
    auto negative { make_probe(u8"negative") };
    auto pinned { make_probe(u8"pinned") };
    const luil::ui_element* const scaled_probe { scaled.get() };
    const luil::ui_element* const negative_probe { negative.get() };
    const luil::ui_element* const pinned_probe { pinned.get() };
    stack.add(std::move(scaled), { .weight = 1.0f, .minimum = 160.0f });
    stack.add(std::move(negative), { .weight = 1.0f, .minimum = -50.0f });
    // 고정 항목의 하한은 아무 일도 하지 않는다. `length`가 이미 하한이자 상한이다.
    stack.add(std::move(pinned), { .length = 50.0f, .minimum = 400.0f });

    stack.arrange({ { 0.0f, 0.0f, 600.0f, 40.0f }, 2.0f });

    // 고정 100(=50×2)을 뺀 500을 둘이 250씩 나누는데, 하한 160은 배율 2에서 320이다.
    REQUIRE(scaled_probe->bounds().width == 320.0f);
    // 음수 하한은 조용히 무시된다.
    REQUIRE(negative_probe->bounds().width == 250.0f);
    REQUIRE(pinned_probe->bounds().width == 100.0f);
}

TEST_CASE("A labelled input row keeps its flexible field when the window narrows", "[ui][stack]")
{
    // 데모 기본 화면의 입력 줄과 같은 모양이다 (고정 160 + 간격 24 + 유연 칸).
    const auto place = [](const float width) {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 24.0f;
        auto stack { std::make_unique<luil::stack_element>(luil::ui_element_id { luil::application_element_kind(49) }, config) };
        stack->add(make_probe(u8"number"), 160.0f);
        auto note { make_probe(u8"note") };
        const luil::ui_element* const probe { note.get() };
        stack->add(std::move(note), { .weight = 1.0f, .minimum = 160.0f });
        stack->arrange({ { 0.0f, 0.0f, width, 50.0f }, 1.0f });
        return probe->bounds().width;
    };

    // 넓을 때는 남는 자리를 다 갖는다.
    REQUIRE(place(400.0f) == 216.0f);
    // 좁아져도 하한 밑으로는 줄지 않는다 — 하한이 없으면 116이 되고, 더 좁으면 0이 된다.
    REQUIRE(place(300.0f) == 160.0f);
    REQUIRE(place(100.0f) == 160.0f);
}
