#include "luil/ui/tab_bar_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace {
    struct select_intent
    {
        std::u8string key {};
    };

    struct close_intent
    {
        std::u8string key {};
    };

    struct reorder_intent
    {
        std::u8string moved {};
        std::u8string target {};
    };

    struct move_intent
    {
        std::u8string moved {};
        std::u8string target {};
        luil::drop_side side { luil::drop_side::before };
    };

    struct receive_intent
    {
        std::u8string moved {};
        luil::ui_element_id from {};
        std::u8string target {};
        luil::drop_side side { luil::drop_side::before };
    };

    struct detach_intent
    {
        std::u8string key {};
        float grab_offset_x { 0.0f };
        luil::drag_release release {};
    };

    struct menu_intent
    {
        std::u8string key {};
        float x { 0.0f };
        float y { 0.0f };
    };

    [[nodiscard]] luil::tab_bar_config make_config(std::size_t count)
    {
        luil::tab_bar_config config {};
        for (std::size_t index = 0; index < count; ++index)
        {
            luil::tab_item item {};
            item.key = u8"tab-" + std::u8string { static_cast<char8_t>(u8'a' + index) };
            item.label = item.key;
            config.items.push_back(std::move(item));
        }
        return config;
    }

    // 탭은 막대의 직접 자식이 아니라 **띠 안의 레인**에 담긴다.
    // 조립이 바뀌어도 test가 짚는 자리는 이 두 도우미 안에서만 바뀐다.
    [[nodiscard]] const luil::ui_element& tab_lane(const luil::tab_bar_element& bar)
    {
        return *bar.children()[0]->children()[0];
    }

    [[nodiscard]] const luil::ui_element& tab_at(const luil::tab_bar_element& bar, const std::size_t index)
    {
        return *tab_lane(bar).children()[index];
    }

    // 첫 input_action에서 앱 메시지 T를 꺼낸다.
    template<typename T>
    [[nodiscard]] const T* single_message(std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1)
            return nullptr;
        auto* const message { std::get_if<luil::app_message>(&actions[0]) };
        return message != nullptr ? message->get<T>() : nullptr;
    }
} // namespace

TEST_CASE("A tab bar builds a tab per item and close buttons only where allowed", "[ui][tab]")
{
    luil::tab_bar_config config { make_config(2) };
    config.items[0].closable = true;
    config.close = [](const std::u8string& key) { return luil::input_action { luil::app_message { close_intent { key } } }; };
    const luil::tab_bar_element bar { std::move(config) };

    // 막대의 직접 자식은 띠 하나다 (넘침 액션이 없어 버튼도 없다).
    REQUIRE(bar.children().size() == 1);
    REQUIRE(bar.children()[0]->id().kind == luil::ui_element_kind::tab_strip);
    REQUIRE(tab_lane(bar).id().kind == luil::ui_element_kind::tab_lane);
    REQUIRE(tab_lane(bar).children().size() == 2);
    REQUIRE(tab_at(bar, 0).id() == luil::ui_element_id { luil::ui_element_kind::tab, u8"tab-a" });
    // 닫기 버튼은 closable인 탭에만 있다.
    REQUIRE(tab_at(bar, 0).children().size() == 1);
    REQUIRE(tab_at(bar, 0).children()[0]->id().kind == luil::ui_element_kind::tab_close);
    REQUIRE(tab_at(bar, 1).children().empty());

    // close factory가 없으면 closable이어도 버튼이 없다.
    luil::tab_bar_config silent { make_config(1) };
    silent.items[0].closable = true;
    const luil::tab_bar_element no_close { std::move(silent) };
    REQUIRE(tab_at(no_close, 0).children().empty());
}

TEST_CASE("A tab click and its close button produce the app messages", "[ui][tab]")
{
    luil::tab_bar_config config { make_config(2) };
    config.items[1].closable = true;
    config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { select_intent { key } } }; };
    config.close = [](const std::u8string& key) { return luil::input_action { luil::app_message { close_intent { key } } }; };
    const luil::tab_bar_element bar { std::move(config) };

    const luil::ui_element* const second { &tab_at(bar, 1) };
    const luil::ui_action* const select { second->action(luil::ui_trigger::left_click) };
    REQUIRE(select != nullptr);
    auto selected { (*select)({ second->id(), 0.0f, 0.0f }) };
    const auto* const select_message { single_message<select_intent>(selected) };
    REQUIRE(select_message != nullptr);
    REQUIRE(select_message->key == u8"tab-b");

    const luil::ui_element* const close { second->children()[0].get() };
    const luil::ui_action* const close_action { close->action(luil::ui_trigger::left_click) };
    REQUIRE(close_action != nullptr);
    auto closed { (*close_action)({ close->id(), 0.0f, 0.0f }) };
    const auto* const close_message { single_message<close_intent>(closed) };
    REQUIRE(close_message != nullptr);
    REQUIRE(close_message->key == u8"tab-b");
}

TEST_CASE("A tab right click asks for its context menu at the pointer", "[ui][tab]")
{
    luil::tab_bar_config config { make_config(2) };
    config.items[1].closable = true;
    config.close = [](const std::u8string& key) { return luil::input_action { luil::app_message { close_intent { key } } }; };
    config.context_menu = [](const std::u8string& key, const luil::ui_action_context& context) { return luil::input_action { luil::app_message { menu_intent { key, context.x, context.y } } }; };
    const luil::tab_bar_element bar { std::move(config) };

    // 닫기 버튼이 없는 탭에도 선다.
    const luil::ui_element& first { tab_at(bar, 0) };
    const luil::ui_action* const first_menu { first.action(luil::ui_trigger::right_click) };
    REQUIRE(first_menu != nullptr);
    auto opened { (*first_menu)({ first.id(), 40.0f, 12.0f }) };
    const auto* const message { single_message<menu_intent>(opened) };
    REQUIRE(message != nullptr);
    REQUIRE(message->key == u8"tab-a");
    REQUIRE(message->x == 40.0f);
    REQUIRE(message->y == 12.0f);

    // 닫기 버튼 위의 우클릭도 그 탭의 메뉴다 — 우클릭은 부모로 올라가지 않는다.
    const luil::ui_element* const close { tab_at(bar, 1).children()[0].get() };
    const luil::ui_action* const close_menu { close->action(luil::ui_trigger::right_click) };
    REQUIRE(close_menu != nullptr);
    auto from_close { (*close_menu)({ close->id(), 300.0f, 10.0f }) };
    const auto* const close_message { single_message<menu_intent>(from_close) };
    REQUIRE(close_message != nullptr);
    REQUIRE(close_message->key == u8"tab-b");
}

TEST_CASE("A tab bar without a context menu factory registers no right click", "[ui][tab]")
{
    luil::tab_bar_config config { make_config(1) };
    config.items[0].closable = true;
    config.close = [](const std::u8string& key) { return luil::input_action { luil::app_message { close_intent { key } } }; };
    const luil::tab_bar_element bar { std::move(config) };

    const luil::ui_element& tab { tab_at(bar, 0) };
    REQUIRE(tab.action(luil::ui_trigger::right_click) == nullptr);
    REQUIRE(tab.children()[0]->action(luil::ui_trigger::right_click) == nullptr);
}

TEST_CASE("A tab drag carries its key and dropping on another tab asks for a reorder", "[ui][tab]")
{
    luil::tab_bar_config config { make_config(2) };
    config.reorder = [](const std::u8string& moved, const std::u8string& target) { return luil::input_action { luil::app_message { reorder_intent { moved, target } } }; };
    luil::tab_bar_element bar { std::move(config) };
    bar.arrange({ { 0.0f, 0.0f, 800.0f, 36.0f }, 1.0f });

    const luil::ui_element* const first { &tab_at(bar, 0) };
    const luil::ui_element* const second { &tab_at(bar, 1) };
    REQUIRE(first->drag() != nullptr);
    const luil::drag_payload payload { first->drag()->make_payload({ first->id(), 10.0f, 10.0f }) };
    REQUIRE(payload.dragged_owner == u8"tab-a");
    // 잡은 지점과 탭 원점의 차이가 실려 ghost가 그 자리를 따라온다.
    REQUIRE(payload.grab_offset_x == 10.0f);

    // 자기 자리에 놓는 것은 이동이 아니다.
    REQUIRE(first->drop() != nullptr);
    REQUIRE(first->drop()->accepts(payload) == false);
    REQUIRE(second->drop()->accepts(payload));
    auto foreign { payload };
    foreign.container.owner = u8"another-tab-bar";
    REQUIRE(second->drop()->accepts(foreign) == false);
    foreign = payload;
    foreign.source.kind = luil::ui_element_kind::list_row;
    REQUIRE(second->drop()->accepts(foreign) == false);
    auto dropped { second->drop()->on_drop(payload, { second->id(), 0.0f, 0.0f }) };
    const auto* const reorder_message { single_message<reorder_intent>(dropped) };
    REQUIRE(reorder_message != nullptr);
    REQUIRE(reorder_message->moved == u8"tab-a");
    REQUIRE(reorder_message->target == u8"tab-b");
}

TEST_CASE("A tab bar without factories builds inert tabs", "[ui][tab]")
{
    const luil::tab_bar_element bar { make_config(1) };
    const luil::ui_element* const tab { &tab_at(bar, 0) };
    REQUIRE(tab->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(tab->drag() == nullptr);
    REQUIRE(tab->drop() == nullptr);
    REQUIRE(tab->children().empty());
}

TEST_CASE("A tab bar scrolls overflowing tabs and clips them from hit testing", "[ui][tab]")
{
    luil::tab_bar_config config { make_config(10) };
    config.tab_width = 160.0f;
    config.scroll_offset = 9999.0f;
    config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { select_intent { key } } }; };
    luil::tab_bar_element bar { std::move(config) };
    bar.arrange({ { 0.0f, 0.0f, 800.0f, 36.0f }, 1.0f });

    // 열 개 중 다섯 개 폭만 보이므로 나머지 다섯 개 폭이 최대치다.
    REQUIRE(bar.maximum_scroll() == 800.0f);
    REQUIRE(bar.scroll_offset() == 800.0f);
    // 끝까지 흘렀으니 첫 탭은 왼쪽 밖에 있다.
    REQUIRE(tab_at(bar, 0).bounds().x == -800.0f);
    REQUIRE(tab_at(bar, 9).bounds().x == 640.0f);

    // 막대 안은 탭이 받고 막대 밖은 아무도 받지 않는다.
    const luil::ui_element* const visible { bar.hit_test(700.0f, 18.0f) };
    REQUIRE(visible != nullptr);
    REQUIRE(visible->id() == luil::ui_element_id { luil::ui_element_kind::tab, u8"tab-j" });
    REQUIRE(bar.hit_test(900.0f, 18.0f) == nullptr);

    // 배율에서도 논리 픽셀 값은 같다.
    luil::tab_bar_config scaled { make_config(10) };
    scaled.scroll_offset = 9999.0f;
    luil::tab_bar_element scaled_bar { std::move(scaled) };
    scaled_bar.arrange({ { 0.0f, 0.0f, 1600.0f, 72.0f }, 2.0f });
    REQUIRE(scaled_bar.maximum_scroll() == 800.0f);
    REQUIRE(scaled_bar.scroll_offset() == 800.0f);
}

TEST_CASE("A tab bar shows its overflow button only while tabs overflow", "[ui][tab]")
{
    struct overflow_intent
    {};

    luil::tab_bar_config config { make_config(10) };
    config.tab_width = 160.0f;
    config.overflow = [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_message { overflow_intent {} } } }; };
    luil::tab_bar_element bar { std::move(config) };

    // 막대가 좁으면 버튼이 오른쪽 끝 자리를 가져가고 흘릴 수 있는 최대치도 그만큼 는다.
    bar.arrange({ { 0.0f, 0.0f, 800.0f, 36.0f }, 1.0f });
    // 막대의 직접 자식은 띠와 넘침 버튼 둘뿐이다.
    REQUIRE(bar.children().size() == 2);
    const luil::ui_element* const button { bar.children()[1].get() };
    REQUIRE(button->id().kind == luil::ui_element_kind::tab_overflow);
    REQUIRE(button->visible());
    REQUIRE(button->bounds().x == 800.0f - luil::tab_overflow_width);
    REQUIRE(bar.maximum_scroll() == 800.0f + luil::tab_overflow_width);

    // 버튼 자리는 탭이 아니라 버튼이 hit를 가져간다.
    const luil::ui_element* const hit { bar.hit_test(790.0f, 18.0f) };
    REQUIRE(hit == button);
    auto actions { (*button->action(luil::ui_trigger::left_click))({ button->id(), 790.0f, 18.0f }) };
    REQUIRE(actions.size() == 1);
    auto* const message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<overflow_intent>() != nullptr);

    // 다 들어가면 버튼이 사라지고 눌리지도 않는다.
    bar.arrange({ { 0.0f, 0.0f, 2000.0f, 36.0f }, 1.0f });
    REQUIRE(button->visible() == false);
    REQUIRE(bar.maximum_scroll() == 0.0f);
    REQUIRE(bar.hit_test(1990.0f, 18.0f) == nullptr);

    // 액션이 없으면 버튼 자체가 없다 — 남는 자식은 띠 하나다.
    const luil::tab_bar_element plain { make_config(10) };
    REQUIRE(plain.children().size() == 1);
}

TEST_CASE("A tab bar is one tab stop and its close buttons are not stops", "[ui][tab][focus]")
{
    luil::tab_bar_config config { make_config(3) };
    config.selected = u8"tab-b";
    config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { select_intent { key } } }; };
    config.close = [](const std::u8string& key) { return luil::input_action { luil::app_message { close_intent { key } } }; };

    auto bar { std::make_unique<luil::tab_bar_element>(std::move(config)) };
    REQUIRE(bar->focus_group() == luil::focus_axis::horizontal);
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(bar), { 0.0f, 0.0f, 800.0f, 36.0f }, 1.0f) };

    // 막대는 Tab에서 한 자리이고, 들어가는 자리는 선택된 탭이다.
    // 탭마다 달린 닫기 버튼은 자리가 아니다 — 있으면 ←/→가 탭과 닫기를 번갈아 지난다.
    REQUIRE(tree.focus_order() == std::vector<luil::ui_element_id> { luil::ui_element_id { luil::ui_element_kind::tab, u8"tab-b" } });
}

TEST_CASE("An overflow button narrows the tab lane instead of covering it", "[ui][tab]")
{
    struct overflow_intent
    {};

    luil::tab_bar_config config { make_config(10) };
    config.tab_width = 160.0f;
    config.overflow = [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_message { overflow_intent {} } } }; };
    auto bar { std::make_unique<luil::tab_bar_element>(std::move(config)) };
    const luil::tab_bar_element* const probe { bar.get() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(bar), { 0.0f, 0.0f, 800.0f, 36.0f }, 1.0f) };

    // 버튼이 오른쪽 28을 가져가므로 레인은 772까지다.
    // 다섯째 탭은 640~800에 놓이지만 **보이는 것은 772까지**다 — 버튼 밑을 지나지 않는다.
    REQUIRE(tab_at(*probe, 4).bounds().x == 640.0f);
    REQUIRE(tab_at(*probe, 4).bounds().width == 160.0f);
    const auto visible { tree.visible_bounds(tab_at(*probe, 4)) };
    REQUIRE(visible.has_value());
    REQUIRE(visible->x == 640.0f);
    REQUIRE(visible->width == 800.0f - luil::tab_overflow_width - 640.0f);

    // 그다음 탭은 레인 밖이라 아예 보이지 않는다.
    REQUIRE(tree.visible_bounds(tab_at(*probe, 5)).has_value() == false);
}

TEST_CASE("A scaled tab bar keeps its lane and maximum in logical pixels", "[ui][tab]")
{
    struct overflow_intent
    {};

    luil::tab_bar_config config { make_config(10) };
    config.tab_width = 160.0f;
    config.scroll_offset = 9999.0f;
    config.overflow = [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { luil::input_action { luil::app_message { overflow_intent {} } } }; };
    luil::tab_bar_element bar { std::move(config) };

    // 논리 폭 800과 같은 화면이다 (배율 2).
    bar.arrange({ { 0.0f, 0.0f, 1600.0f, 72.0f }, 2.0f });

    // 버튼 폭도 논리 픽셀이라 배율 1일 때와 같은 값이 나온다.
    REQUIRE(bar.children()[1]->bounds().x == 1600.0f - luil::tab_overflow_width * 2.0f);
    REQUIRE(bar.maximum_scroll() == 800.0f + luil::tab_overflow_width);
    REQUIRE(bar.scroll_offset() == 800.0f + luil::tab_overflow_width);
}

TEST_CASE("A tab badge speaks after the tab name", "[ui][tab][accessibility]")
{
    // 개수가 바뀌면 이름이 바뀌어 보조 기술이 그 변화를 듣는다.
    luil::tab_bar_config config { make_config(4) };
    config.items[0].badge = luil::tab_badge { .text = u8"3", .description = u8"읽지 않음 3개" };
    config.items[1].badge = luil::tab_badge { .text = u8"12" };
    config.items[2].badge = luil::tab_badge {};
    const luil::tab_bar_element bar { std::move(config) };

    REQUIRE(tab_at(bar, 0).accessibility().name == u8"tab-a, 읽지 않음 3개");
    REQUIRE(tab_at(bar, 1).accessibility().name == u8"tab-b, 12");
    // 글도 설명도 없는 상태 점은 읽을 말이 없다 — 이름만 남는다.
    REQUIRE(tab_at(bar, 2).accessibility().name == u8"tab-c");
    REQUIRE(tab_at(bar, 3).accessibility().name == u8"tab-d");
    // 글자 탐색은 탭에 적힌 이름만 본다.
    REQUIRE(tab_at(bar, 0).search_label() == u8"tab-a");
}

namespace {
    // 막대 하나를 tree에 담는다 — drop 판정은 tree가 자식부터 훑는 순서로 한다.
    [[nodiscard]] luil::ui_tree bar_tree(luil::tab_bar_config config)
    {
        auto bar { std::make_unique<luil::tab_bar_element>(std::move(config)) };
        bar->arrange({ { 0.0f, 0.0f, 800.0f, 36.0f }, 1.0f });
        return luil::ui_tree { std::move(bar) };
    }

    [[nodiscard]] const luil::tab_bar_element& bar_of(const luil::ui_tree& tree)
    {
        return static_cast<const luil::tab_bar_element&>(*tree.root());
    }

    [[nodiscard]] luil::input_action move_message(const std::u8string& moved, const std::u8string& target, const luil::drop_side side)
    {
        return luil::input_action { luil::app_message { move_intent { moved, target, side } } };
    }

    // 그 자리에 놓았을 때의 메시지다. 받는 대상이 없으면 빈 목록이다.
    [[nodiscard]] std::vector<luil::input_action> drop_at(const luil::ui_tree& tree, const luil::drag_payload& payload, const float x)
    {
        const luil::ui_element* const target { tree.find_drop_target(x, 18.0f, payload) };
        if (target == nullptr)
            return {};
        return target->drop()->on_drop(payload, { target->id(), x, 18.0f });
    }
} // namespace

TEST_CASE("Moving a tab names the side and refuses the spots that keep the order", "[ui][tab][drag]")
{
    // 탭 폭 160: a는 0–160, b는 160–320, c는 320–480이고 그 뒤는 빈 틈이다.
    luil::tab_bar_config config { make_config(3) };
    config.move = move_message;
    const luil::ui_tree tree { bar_tree(std::move(config)) };
    const luil::tab_bar_element& bar { bar_of(tree) };

    const luil::ui_element& first { tab_at(bar, 0) };
    REQUIRE(first.drag() != nullptr);
    const luil::drag_payload payload { first.drag()->make_payload({ first.id(), 20.0f, 10.0f }) };

    SECTION("그대로인 자리는 받지 않는다")
    {
        // 자기 자신의 두 반쪽과 바로 뒤 탭의 앞 반쪽이다. 강조도 서지 않는다.
        REQUIRE(tree.find_drop_target(40.0f, 18.0f, payload) == nullptr);
        REQUIRE(tree.find_drop_target(140.0f, 18.0f, payload) == nullptr);
        REQUIRE(tree.find_drop_target(170.0f, 18.0f, payload) == nullptr);
        REQUIRE(drop_at(tree, payload, 170.0f).empty());
    }

    SECTION("바뀌는 자리는 앞·뒤를 싣는다")
    {
        auto after_b { drop_at(tree, payload, 300.0f) };
        const auto* const behind { single_message<move_intent>(after_b) };
        REQUIRE(behind != nullptr);
        REQUIRE(behind->moved == u8"tab-a");
        REQUIRE(behind->target == u8"tab-b");
        REQUIRE(behind->side == luil::drop_side::after);

        auto before_c { drop_at(tree, payload, 330.0f) };
        const auto* const ahead { single_message<move_intent>(before_c) };
        REQUIRE(ahead != nullptr);
        REQUIRE(ahead->target == u8"tab-c");
        REQUIRE(ahead->side == luil::drop_side::before);
    }

    SECTION("마지막 탭 뒤의 빈 곳은 마지막 탭의 뒤다")
    {
        const luil::ui_element* const end { tree.find_drop_target(600.0f, 18.0f, payload) };
        REQUIRE(end == &bar);
        auto dropped { drop_at(tree, payload, 600.0f) };
        const auto* const message { single_message<move_intent>(dropped) };
        REQUIRE(message != nullptr);
        REQUIRE(message->target == u8"tab-c");
        REQUIRE(message->side == luil::drop_side::after);

        // 이미 마지막인 탭에는 그 틈도 그대로인 자리다.
        const luil::ui_element& last { tab_at(bar, 2) };
        const luil::drag_payload trailing { last.drag()->make_payload({ last.id(), 20.0f, 10.0f }) };
        REQUIRE(tree.find_drop_target(600.0f, 18.0f, trailing) == nullptr);
        REQUIRE(tree.find_drop_target(460.0f, 18.0f, trailing) == nullptr);
        REQUIRE(tree.find_drop_target(300.0f, 18.0f, trailing) == nullptr);
        REQUIRE(tree.find_drop_target(20.0f, 18.0f, trailing) == &tab_at(bar, 0));
    }

    SECTION("받는 자리는 테두리 대신 틈으로 보인다")
    {
        REQUIRE(tab_at(bar, 1).drop()->custom_highlight);
        REQUIRE(bar.drop()->custom_highlight);
    }

    SECTION("다른 막대의 탭은 move로 받지 않는다")
    {
        luil::drag_payload foreign { payload };
        foreign.dragged_owner = u8"tab-x";
        REQUIRE(tree.find_drop_target(300.0f, 18.0f, foreign) == nullptr);
        REQUIRE(tree.find_drop_target(600.0f, 18.0f, foreign) == nullptr);
    }
}

TEST_CASE("A tab bar receives a tab dragged from another bar", "[ui][tab][drag]")
{
    // 두 창이 같은 코드로 막대를 지어 owner가 같다 — 키로 갈라야 남의 탭을 알아본다.
    luil::tab_bar_config other_config { make_config(2) };
    for (luil::tab_item& item : other_config.items)
        item.key = item.key == u8"tab-a" ? u8"tab-x" : u8"tab-y";
    other_config.move = move_message;
    const luil::ui_tree other { bar_tree(std::move(other_config)) };
    const luil::ui_element& dragged { tab_at(bar_of(other), 0) };
    const luil::drag_payload payload { dragged.drag()->make_payload({ dragged.id(), 20.0f, 10.0f }) };

    const auto receive = [](const luil::drag_payload& source, const std::u8string& target, const luil::drop_side side) {
        return luil::input_action { luil::app_message { receive_intent { source.dragged_owner, source.container, target, side } } };
    };

    SECTION("어느 틈이든 받고 앞·뒤를 싣는다")
    {
        luil::tab_bar_config config { make_config(3) };
        config.move = move_message;
        config.receive = receive;
        const luil::ui_tree tree { bar_tree(std::move(config)) };

        // 같은 막대라면 그대로인 자리였을 곳도 남의 탭에는 새 자리다.
        auto front { drop_at(tree, payload, 20.0f) };
        const auto* const first { single_message<receive_intent>(front) };
        REQUIRE(first != nullptr);
        REQUIRE(first->moved == u8"tab-x");
        REQUIRE(first->from == payload.container);
        REQUIRE(first->target == u8"tab-a");
        REQUIRE(first->side == luil::drop_side::before);

        auto end { drop_at(tree, payload, 700.0f) };
        const auto* const last { single_message<receive_intent>(end) };
        REQUIRE(last != nullptr);
        REQUIRE(last->target == u8"tab-c");
        REQUIRE(last->side == luil::drop_side::after);
    }

    SECTION("탭이 없는 막대는 막대 전체가 끝의 틈이다")
    {
        luil::tab_bar_config config {};
        config.receive = receive;
        const luil::ui_tree tree { bar_tree(std::move(config)) };
        auto dropped { drop_at(tree, payload, 50.0f) };
        const auto* const message { single_message<receive_intent>(dropped) };
        REQUIRE(message != nullptr);
        REQUIRE(message->target.empty());
        REQUIRE(message->side == luil::drop_side::after);
    }

    SECTION("receive가 없으면 남의 탭을 받지 않는다")
    {
        luil::tab_bar_config config { make_config(3) };
        config.move = move_message;
        const luil::ui_tree tree { bar_tree(std::move(config)) };
        REQUIRE(tree.find_drop_target(300.0f, 18.0f, payload) == nullptr);
        REQUIRE(tree.find_drop_target(700.0f, 18.0f, payload) == nullptr);
    }

    SECTION("탭이 아닌 끌기와 파일은 받지 않는다")
    {
        luil::tab_bar_config config { make_config(3) };
        config.receive = receive;
        const luil::ui_tree tree { bar_tree(std::move(config)) };
        luil::drag_payload row { payload };
        row.source.kind = luil::ui_element_kind::list_row;
        REQUIRE(tree.find_drop_target(300.0f, 18.0f, row) == nullptr);
        luil::drag_payload files {};
        files.files = { u8"C:\\a.txt" };
        REQUIRE(tree.find_drop_target(300.0f, 18.0f, files) == nullptr);
    }
}

TEST_CASE("A tab dropped where nothing takes it reports its key", "[ui][tab][drag]")
{
    // 떼어 내기만 하는 막대도 탭을 끌 수 있다 (`reorder`·`move` 없이).
    luil::tab_bar_config config { make_config(2) };
    config.outside_cursor = luil::application_cursor(2);
    config.unclaimed_drop = [](const luil::drag_payload& payload, const luil::drag_release& release) {
        return luil::input_action { luil::app_message { detach_intent { payload.dragged_owner, payload.grab_offset_x, release } } };
    };
    luil::tab_bar_element bar { std::move(config) };
    bar.arrange({ { 0.0f, 0.0f, 800.0f, 36.0f }, 1.0f });

    const luil::ui_element& second { tab_at(bar, 1) };
    REQUIRE(second.drag() != nullptr);
    REQUIRE(second.drop() == nullptr);
    const luil::drag_payload payload { second.drag()->make_payload({ second.id(), 170.0f, 10.0f }) };
    REQUIRE(payload.outside_cursor == luil::application_cursor(2));
    REQUIRE(second.drag()->on_unclaimed_drop != nullptr);

    luil::drag_release release {};
    release.outside = true;
    release.screen = luil::screen_point { 1200, 300 };
    auto actions { second.drag()->on_unclaimed_drop(payload, release) };
    const auto* const message { single_message<detach_intent>(actions) };
    REQUIRE(message != nullptr);
    REQUIRE(message->key == u8"tab-b");
    // 잡은 지점이 함께 와서 새 창에서 그 탭을 놓은 점에 둘 수 있다.
    REQUIRE(message->grab_offset_x == 10.0f);
    REQUIRE(message->release == release);
}
