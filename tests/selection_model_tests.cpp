#include "luil/ui/selection_model.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {
    // a b c d e f — d는 비활성이다.
    [[nodiscard]] std::vector<luil::virtual_list_item> make_items()
    {
        std::vector<luil::virtual_list_item> items {};
        for (const char8_t* const key : { u8"a", u8"b", u8"c", u8"d", u8"e", u8"f" })
        {
            luil::virtual_list_item item {};
            item.key = key;
            item.label = key;
            items.push_back(std::move(item));
        }
        items[3].enabled = false;
        return items;
    }

    [[nodiscard]] luil::ui_action_context click(const bool control, const bool shift)
    {
        luil::ui_action_context context {};
        context.control = control;
        context.shift = shift;
        return context;
    }

    [[nodiscard]] luil::virtual_list_move_context step(const bool extend, const bool keep)
    {
        return { luil::value_step::increase, extend, keep };
    }

    using keys = std::vector<std::u8string>;
}

TEST_CASE("A plain pick selects one item and moves the anchor and cursor onto it", "[ui][selection]")
{
    const auto items { make_items() };
    const luil::item_selection picked { luil::selection_pick({ u8"a", u8"a", { u8"a", u8"b" } }, items, u8"c", click(false, false)) };
    REQUIRE(picked == luil::item_selection { u8"c", u8"c", keys { u8"c" } });
}

TEST_CASE("Ctrl+click toggles one item and moves the anchor", "[ui][selection]")
{
    const auto items { make_items() };
    luil::item_selection selection { u8"a", u8"a", { u8"a" } };
    selection = luil::selection_pick(selection, items, u8"c", click(true, false));
    REQUIRE(selection == luil::item_selection { u8"c", u8"c", keys { u8"a", u8"c" } });
    selection = luil::selection_pick(selection, items, u8"a", click(true, false));
    REQUIRE(selection == luil::item_selection { u8"a", u8"a", keys { u8"c" } });
}

TEST_CASE("Shift+click selects from the anchor and skips disabled items", "[ui][selection]")
{
    const auto items { make_items() };
    luil::item_selection selection { u8"b", u8"b", { u8"b" } };
    selection = luil::selection_pick(selection, items, u8"f", click(false, true));
    // d는 비활성이라 구간 안이어도 고르지 않는다. 기준점은 그대로다.
    REQUIRE(selection == luil::item_selection { u8"b", u8"f", keys { u8"b", u8"c", u8"e", u8"f" } });
    // 기준점에서 다시 줄이면 그 구간만 남는다.
    selection = luil::selection_pick(selection, items, u8"c", click(false, true));
    REQUIRE(selection == luil::item_selection { u8"b", u8"c", keys { u8"b", u8"c" } });
}

TEST_CASE("Ctrl+Shift+click adds the range to what is already selected", "[ui][selection]")
{
    const auto items { make_items() };
    luil::item_selection selection { u8"e", u8"e", { u8"a", u8"e" } };
    selection = luil::selection_pick(selection, items, u8"f", click(true, true));
    REQUIRE(selection == luil::item_selection { u8"e", u8"f", keys { u8"a", u8"e", u8"f" } });
}

TEST_CASE("A disabled or unknown item cannot be picked", "[ui][selection]")
{
    const auto items { make_items() };
    const luil::item_selection before { u8"a", u8"a", { u8"a" } };
    REQUIRE(luil::selection_pick(before, items, u8"d", click(false, false)) == before);
    REQUIRE(luil::selection_pick(before, items, u8"zz", click(false, false)) == before);
}

TEST_CASE("Keyboard steps follow the extended selection rules", "[ui][selection]")
{
    const auto items { make_items() };
    luil::item_selection selection { u8"a", u8"a", { u8"a" } };

    SECTION("Shift extends from a fixed anchor")
    {
        selection = luil::selection_move(selection, items, u8"b", step(true, false));
        selection = luil::selection_move(selection, items, u8"c", step(true, false));
        REQUIRE(selection == luil::item_selection { u8"a", u8"c", keys { u8"a", u8"b", u8"c" } });
        // 기준점 위로 넘어가도 기준점은 그대로다.
        selection = luil::selection_move(selection, items, u8"a", step(true, false));
        REQUIRE(selection == luil::item_selection { u8"a", u8"a", keys { u8"a" } });
    }

    SECTION("Ctrl moves only the cursor and Ctrl+Space toggles there")
    {
        selection = luil::selection_move(selection, items, u8"b", step(false, true));
        selection = luil::selection_move(selection, items, u8"c", step(false, true));
        REQUIRE(selection == luil::item_selection { u8"a", u8"c", keys { u8"a" } });
        selection = luil::selection_toggle(selection, items, u8"c");
        REQUIRE(selection == luil::item_selection { u8"c", u8"c", keys { u8"a", u8"c" } });
        // 새 기준점에서 Ctrl+Shift는 구간을 더한다.
        selection = luil::selection_move(selection, items, u8"e", step(true, true));
        REQUIRE(selection == luil::item_selection { u8"c", u8"e", keys { u8"a", u8"c", u8"e" } });
    }

    SECTION("A plain step collapses to one item")
    {
        selection = luil::selection_select_all(selection, items);
        selection = luil::selection_move(selection, items, u8"b", step(false, false));
        REQUIRE(selection == luil::item_selection { u8"b", u8"b", keys { u8"b" } });
    }
}

TEST_CASE("A missing anchor falls back to the cursor, then to the target", "[ui][selection]")
{
    const auto items { make_items() };
    luil::item_selection selection { u8"gone", u8"b", { u8"b" } };
    selection = luil::selection_move(selection, items, u8"c", step(true, false));
    REQUIRE(selection == luil::item_selection { u8"b", u8"c", keys { u8"b", u8"c" } });

    luil::item_selection empty {};
    empty = luil::selection_move(empty, items, u8"e", step(true, false));
    REQUIRE(empty == luil::item_selection { u8"e", u8"e", keys { u8"e" } });
}

TEST_CASE("Select all picks every enabled item and keeps anchor and cursor", "[ui][selection]")
{
    const auto items { make_items() };
    const luil::item_selection all { luil::selection_select_all({ u8"b", u8"c", { u8"b" } }, items) };
    REQUIRE(all == luil::item_selection { u8"b", u8"c", keys { u8"a", u8"b", u8"c", u8"e", u8"f" } });
    REQUIRE(luil::selection_contains(all, u8"e"));
    REQUIRE(luil::selection_contains(all, u8"d") == false);
}

TEST_CASE("Reconcile keeps surviving keys and moves a lost cursor to the nearest survivor", "[ui][selection]")
{
    const auto before { make_items() };
    // c와 e가 걸러져 사라졌다.
    std::vector<luil::virtual_list_item> after {};
    for (const luil::virtual_list_item& item : before)
        if (item.key != u8"c" && item.key != u8"e")
            after.push_back(item);

    const luil::item_selection selection { u8"b", u8"c", { u8"b", u8"c", u8"e" } };
    const luil::item_selection reconciled { luil::selection_reconcile(selection, before, after) };
    // 커서 c의 뒤쪽 가장 가까운 남은 항목은 d다 (비활성이어도 커서는 설 수 있다).
    REQUIRE(reconciled == luil::item_selection { u8"b", u8"d", keys { u8"b" } });

    SECTION("Without the previous model the cursor goes to the first enabled item")
    {
        REQUIRE(luil::selection_reconcile(selection, {}, after).cursor == u8"a");
    }

    SECTION("A lost anchor moves to the nearest survivor too")
    {
        const luil::item_selection lone { u8"c", u8"b", {} };
        const std::vector<luil::virtual_list_item> only_b { before[1] };
        REQUIRE(luil::selection_reconcile(lone, before, only_b) == luil::item_selection { u8"b", u8"b", {} });
    }

    SECTION("An empty model clears everything")
    {
        REQUIRE(luil::selection_reconcile(selection, before, {}) == luil::item_selection {});
    }
}
