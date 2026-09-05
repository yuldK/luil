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
