#include "luil/ui/list_element.h"

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

    struct toggle_intent
    {
        std::u8string key {};
    };

    struct reorder_intent
    {
        std::u8string moved {};
        std::u8string target {};
    };

    struct scroll_intent
    {
        float delta { 0.0f };
    };

    [[nodiscard]] luil::list_config make_config(const std::size_t count)
    {
        luil::list_config config {};
        config.row_height = 20.0f;
        for (std::size_t index = 0; index < count; ++index)
        {
            luil::list_item item {};
            item.key = u8"item-" + std::u8string { static_cast<char8_t>(u8'a' + index) };
            item.label = item.key;
            config.items.push_back(std::move(item));
        }
        return config;
    }

    [[nodiscard]] luil::ui_element_id row_id(const std::u8string_view key)
    {
        return luil::ui_element_id { luil::ui_element_kind::list_row, std::u8string { key } };
    }

    // 행은 목록의 직접 자식이 아니라 **창 안의 레인**에 담긴다.
    // 조립이 바뀌어도 test가 짚는 자리는 이 두 도우미 안에서만 바뀐다.
    [[nodiscard]] const luil::ui_element& list_lane(const luil::list_element& list)
    {
        return *list.children()[0]->children()[0];
    }

    [[nodiscard]] const luil::ui_element& row_at(const luil::list_element& list, const std::size_t index)
    {
        return *list_lane(list).children()[index];
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

TEST_CASE("A list builds a row per item and a scrollbar only where it can scroll", "[ui][list]")
{
    const luil::list_element quiet { make_config(3) };
    // 스크롤 메시지를 만들 수 없으면 막대를 만들지 않는다.
    REQUIRE(quiet.children().size() == 1);
    REQUIRE(quiet.children()[0]->id().kind == luil::ui_element_kind::list_scroll);
    REQUIRE(list_lane(quiet).id().kind == luil::ui_element_kind::list_lane);
    REQUIRE(list_lane(quiet).children().size() == 3);
    REQUIRE(row_at(quiet, 0).id() == row_id(u8"item-a"));
    REQUIRE(quiet.content_height() == 60.0f);

    luil::list_config config { make_config(3) };
    config.scroll = [](const float delta) { return luil::input_action { luil::app_message { scroll_intent { delta } } }; };
    const luil::list_element list { std::move(config) };
    REQUIRE(list.children().size() == 2);
    REQUIRE(list.children()[1]->id().kind == luil::ui_element_kind::list_scrollbar);
    // 목록 안의 막대는 **Tab의 자리가 아니다.** 목록 자신이 ↑/↓·Home/End를 가진
    // 묶음이라, 막대까지 자리가 되면 같은 목록에 자리가 둘 선다.
    //  - 홀로 서는 막대는 자기 자리를 자처한다. 거절은 담는 쪽의 몫이다.
    REQUIRE(list.children()[1]->tab_stop() == false);
}

TEST_CASE("A list row click asks for the selection and a disabled row does not", "[ui][list]")
{
    luil::list_config config { make_config(3) };
    config.items[2].enabled = false;
    config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { select_intent { key } } }; };
    const luil::list_element list { std::move(config) };

    const luil::ui_element* const second { &row_at(list, 1) };
    const luil::ui_action* const select { second->action(luil::ui_trigger::left_click) };
    REQUIRE(select != nullptr);
    auto selected { (*select)({ second->id(), 0.0f, 0.0f }) };
    const auto* const message { single_message<select_intent>(selected) };
    REQUIRE(message != nullptr);
    REQUIRE(message->key == u8"item-b");

    // 비활성 행도 액션은 갖지만 실행되지 않는다 (자리 판정이 `enabled`를 본다).
    REQUIRE(row_at(list, 2).enabled() == false);
}

TEST_CASE("A list is one Tab stop that enters at the selected row", "[ui][list]")
{
    luil::list_config config { make_config(4) };
    config.selected = u8"item-c";
    config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { select_intent { key } } }; };
    auto list { std::make_unique<luil::list_element>(std::move(config)) };
    const luil::list_element* const view { list.get() };
    REQUIRE(view->focus_group() == luil::focus_axis::vertical);
    REQUIRE(view->focus_entry() == row_id(u8"item-c"));

    list->arrange({ { 0.0f, 0.0f, 200.0f, 40.0f }, 1.0f });
    const luil::ui_tree tree { std::move(list) };

    // 목록은 Tab에서 한 자리이고 그 자리가 고른 행이다.
    const std::vector<luil::ui_element_id> order { tree.focus_order() };
    REQUIRE(order.size() == 1u);
    REQUIRE(order[0] == row_id(u8"item-c"));

    // 그 안은 화살표가 돈다 — 축과 항목이 묶음에서 나온다.
    const luil::focus_group_scope scope { tree.focus_group_of(row_id(u8"item-c")) };
    REQUIRE(scope.axis == luil::focus_axis::vertical);
    REQUIRE(scope.members.size() == 4u);
    REQUIRE(scope.members.front() == row_id(u8"item-a"));
    REQUIRE(scope.members.back() == row_id(u8"item-d"));
}

TEST_CASE("Every list item stays in the tree even where the viewport cannot show it", "[ui][list]")
{
    luil::list_config config { make_config(12) };
    config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { select_intent { key } } }; };
    auto list { std::make_unique<luil::list_element>(std::move(config)) };
    // 창은 두 줄만 보여 준다 (12줄 × 20 = 240 중 40).
    list->arrange({ { 0.0f, 0.0f, 200.0f, 40.0f }, 1.0f });
    const luil::ui_tree tree { std::move(list) };

    // 가상화하지 않는다 — 창 밖의 행도 묶음의 항목이다.
    // 이것이 아니면 ↓가 목록의 처음으로 돌고 End가 마지막으로 **보이는** 행으로
    // 간다 (list-view-design.md).
    const luil::focus_group_scope scope { tree.focus_group_of(row_id(u8"item-a")) };
    REQUIRE(scope.members.size() == 12u);
    REQUIRE(scope.members.back() == row_id(u8"item-l"));
    REQUIRE(tree.find(row_id(u8"item-l")) != nullptr);
}

TEST_CASE("A list without a select factory is not a keyboard destination", "[ui][list]")
{
    auto list { std::make_unique<luil::list_element>(make_config(3)) };
    list->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    const luil::ui_tree tree { std::move(list) };

    // 누르는 액션이 없으면 자리가 아니다 (`tab_stop`의 기본값).
    // 파생마다 키보드를 켜고 끄는 규칙을 따로 두지 않는다는 확인이다.
    REQUIRE(tree.focus_order().empty());
    REQUIRE(tree.focus_group_of(row_id(u8"item-a")).members.empty());
}

TEST_CASE("A list clamps the scroll offset it was given and reports the maximum", "[ui][list]")
{
    luil::list_config config { make_config(10) };
    config.scroll_offset = 500.0f;
    config.scroll = [](const float delta) { return luil::input_action { luil::app_message { scroll_intent { delta } } }; };
    luil::list_element list { std::move(config) };
    // 내용 200, 창 80이라 최대 120이다.
    list.arrange({ { 0.0f, 0.0f, 200.0f, 80.0f }, 1.0f });
    REQUIRE(list.content_height() == 200.0f);
    REQUIRE(list.maximum_scroll() == 120.0f);
    REQUIRE(list.scroll_offset() == 120.0f);

    // 내용이 창보다 짧으면 흘릴 것이 없다.
    luil::list_config short_config { make_config(2) };
    short_config.scroll_offset = 30.0f;
    luil::list_element short_list { std::move(short_config) };
    short_list.arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    REQUIRE(short_list.maximum_scroll() == 0.0f);
    REQUIRE(short_list.scroll_offset() == 0.0f);
}

TEST_CASE("A tree row carries an expander only where the branch and the factory meet", "[ui][list]")
{
    luil::list_config config { make_config(3) };
    config.items[0].expansion = luil::list_expansion::expanded;
    config.items[1].depth = 1;
    config.items[1].expansion = luil::list_expansion::collapsed;
    // items[2]는 잎이다.
    config.toggle = [](const std::u8string& key) { return luil::input_action { luil::app_message { toggle_intent { key } } }; };
    const luil::list_element tree { std::move(config) };

    REQUIRE(row_at(tree, 0).children().size() == 1);
    REQUIRE(row_at(tree, 0).children()[0]->id() == luil::ui_element_id { luil::ui_element_kind::list_expander, u8"item-a" });
    REQUIRE(row_at(tree, 1).children().size() == 1);
    // 잎에는 삼각형이 없다 (자리만 비운다).
    REQUIRE(row_at(tree, 2).children().empty());

    // factory가 없으면 가지여도 삼각형을 만들지 않는다.
    luil::list_config silent { make_config(1) };
    silent.items[0].expansion = luil::list_expansion::collapsed;
    const luil::list_element quiet { std::move(silent) };
    REQUIRE(row_at(quiet, 0).children().empty());
}

TEST_CASE("An expander asks for the toggle and is not a Tab stop", "[ui][list]")
{
    luil::list_config config { make_config(2) };
    config.items[0].expansion = luil::list_expansion::collapsed;
    config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { select_intent { key } } }; };
    config.toggle = [](const std::u8string& key) { return luil::input_action { luil::app_message { toggle_intent { key } } }; };
    auto list { std::make_unique<luil::list_element>(std::move(config)) };
    list->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });

    const luil::ui_element* const expander { row_at(*list, 0).children()[0].get() };
    const luil::ui_action* const action { expander->action(luil::ui_trigger::left_click) };
    REQUIRE(action != nullptr);
    auto toggled { (*action)({ expander->id(), 0.0f, 0.0f }) };
    const auto* const message { single_message<toggle_intent>(toggled) };
    REQUIRE(message != nullptr);
    REQUIRE(message->key == u8"item-a");

    // 항목에 딸린 보조 버튼이라 ↑↓가 행과 삼각형을 번갈아 지나지 않는다.
    REQUIRE(expander->tab_stop() == false);
    const luil::ui_tree tree { std::move(list) };
    const luil::focus_group_scope scope { tree.focus_group_of(row_id(u8"item-a")) };
    REQUIRE(scope.members.size() == 2u);
}

TEST_CASE("Depth moves the expander and the label by the same step", "[ui][list]")
{
    luil::list_config config { make_config(3) };
    config.items[0].expansion = luil::list_expansion::expanded;
    config.items[1].depth = 1;
    config.items[1].expansion = luil::list_expansion::expanded;
    config.items[2].depth = 2;
    config.items[2].expansion = luil::list_expansion::collapsed;
    config.toggle = [](const std::u8string& key) { return luil::input_action { luil::app_message { toggle_intent { key } } }; };
    luil::list_element tree { std::move(config) };
    tree.arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });

    // 깊이가 미는 것은 삼각형 자리 하나이고 글은 그 뒤를 따른다.
    const float base { row_at(tree, 0).children()[0]->bounds().x };
    REQUIRE(row_at(tree, 1).children()[0]->bounds().x == base + luil::list_indent_step);
    REQUIRE(row_at(tree, 2).children()[0]->bounds().x == base + 2.0f * luil::list_indent_step);
    // 배율은 arrange가 곱한다 (설정 값은 논리 픽셀이다).
    luil::list_config scaled_config { make_config(2) };
    scaled_config.items[1].depth = 1;
    scaled_config.items[1].expansion = luil::list_expansion::collapsed;
    scaled_config.items[0].expansion = luil::list_expansion::collapsed;
    scaled_config.toggle = [](const std::u8string& key) { return luil::input_action { luil::app_message { toggle_intent { key } } }; };
    luil::list_element scaled { std::move(scaled_config) };
    scaled.arrange({ { 0.0f, 0.0f, 400.0f, 200.0f }, 2.0f });
    const float step { row_at(scaled, 1).children()[0]->bounds().x - row_at(scaled, 0).children()[0]->bounds().x };
    REQUIRE(step == 2.0f * luil::list_indent_step);
}

TEST_CASE("A list row drag carries its key and dropping on another row asks for a reorder", "[ui][list]")
{
    luil::list_config config { make_config(3) };
    config.reorder = [](const std::u8string& moved, const std::u8string& target) { return luil::input_action { luil::app_message { reorder_intent { moved, target } } }; };
    luil::list_element list { std::move(config) };
    list.arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });

    const luil::ui_element* const first { &row_at(list, 0) };
    const luil::ui_element* const second { &row_at(list, 1) };
    REQUIRE(first->drag() != nullptr);
    const luil::drag_payload payload { first->drag()->make_payload({ first->id(), 12.0f, 4.0f }) };
    REQUIRE(payload.dragged_owner == u8"item-a");
    // 잡은 지점과 행 원점의 차이가 실려 ghost가 그 자리를 따라온다.
    REQUIRE(payload.grab_offset_x == 12.0f);

    REQUIRE(second->drop() != nullptr);
    REQUIRE(second->drop()->accepts(payload));
    auto foreign { payload };
    foreign.container.owner = u8"another-list";
    REQUIRE(second->drop()->accepts(foreign) == false);
    foreign = payload;
    foreign.source.kind = luil::ui_element_kind::tab;
    REQUIRE(second->drop()->accepts(foreign) == false);
    auto dropped { second->drop()->on_drop(payload, { second->id(), 0.0f, 0.0f }) };
    const auto* const message { single_message<reorder_intent>(dropped) };
    REQUIRE(message != nullptr);
    REQUIRE(message->moved == u8"item-a");
    REQUIRE(message->target == u8"item-b");

    // 자기 자리에 놓는 것은 이동이 아니다.
    REQUIRE(first->drop()->accepts(payload) == false);
}

TEST_CASE("A list row rejects file drags", "[ui][list]")
{
    // 재정렬의 수락은 dragged_owner를 요구한다 — 밖에서 온 파일 끌기(경로만
    // 실리고 owner가 비어 있다)는 저절로 거절된다 (os-dragdrop-design.md).
    luil::list_config config { make_config(2) };
    config.reorder = [](const std::u8string& moved, const std::u8string& target) { return luil::input_action { luil::app_message { reorder_intent { moved, target } } }; };
    luil::list_element list { std::move(config) };
    list.arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });

    luil::drag_payload files {};
    files.custom_visual = true;
    files.files = { u8"C:\\a.txt" };
    REQUIRE(row_at(list, 0).drop()->accepts(files) == false);
}

TEST_CASE("Without a reorder factory a list row has no handle and no drag", "[ui][list]")
{
    luil::list_config config { make_config(2) };
    config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { select_intent { key } } }; };
    luil::list_element plain { std::move(config) };
    plain.arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    REQUIRE(row_at(plain, 0).drag() == nullptr);
    REQUIRE(row_at(plain, 0).drop() == nullptr);

    // 손잡이는 깊이 밖의 칸이라, factory가 있으면 삼각형 자리가 그만큼 밀린다.
    luil::list_config dragging { make_config(2) };
    dragging.items[0].expansion = luil::list_expansion::collapsed;
    dragging.toggle = [](const std::u8string& key) { return luil::input_action { luil::app_message { toggle_intent { key } } }; };
    luil::list_config both { dragging };
    both.reorder = [](const std::u8string& moved, const std::u8string& target) { return luil::input_action { luil::app_message { reorder_intent { moved, target } } }; };

    luil::list_element without { std::move(dragging) };
    without.arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    luil::list_element with { std::move(both) };
    with.arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    const float shift { row_at(with, 0).children()[0]->bounds().x - row_at(without, 0).children()[0]->bounds().x };
    REQUIRE(shift == luil::list_handle_width);
}

TEST_CASE("A list scrollbar takes the right edge and the rows keep the rest", "[ui][list]")
{
    luil::list_config config { make_config(10) };
    config.scroll = [](const float delta) { return luil::input_action { luil::app_message { scroll_intent { delta } } }; };
    luil::list_element list { std::move(config) };
    list.arrange({ { 10.0f, 20.0f, 200.0f, 80.0f }, 1.0f });

    const luil::ui_element& scroll { *list.children()[0] };
    const luil::ui_element& bar { *list.children()[1] };
    REQUIRE(bar.bounds().x == 10.0f + 200.0f - luil::list_scrollbar_width);
    REQUIRE(bar.bounds().width == luil::list_scrollbar_width);
    // 창이 실제로 좁아지므로 행의 글이 막대 밑을 지나지 않는다.
    REQUIRE(scroll.bounds().width == 200.0f - luil::list_scrollbar_width);
    REQUIRE(row_at(list, 0).bounds().width == 200.0f - luil::list_scrollbar_width);
}
