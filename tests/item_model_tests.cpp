#include "luil/ui/item_model.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
    [[nodiscard]] luil::tree_node node(std::u8string key, std::vector<luil::tree_node> children = {}, const luil::node_load load = luil::node_load::loaded)
    {
        luil::tree_node made {};
        made.key = std::move(key);
        made.label = made.key;
        made.children = std::move(children);
        made.load = load;
        return made;
    }

    // branches/ ⊃ release-0.1, release-0.2 — tags/ (아직 묻지 않았다) — archive/ (실패) — README
    [[nodiscard]] std::vector<luil::tree_node> repository()
    {
        return {
            node(u8"branches", { node(u8"release-0.2"), node(u8"release-0.1") }),
            node(u8"tags", {}, luil::node_load::unloaded),
            node(u8"archive", {}, luil::node_load::failed),
            node(u8"README"),
        };
    }

    [[nodiscard]] std::vector<std::u8string> keys_of(const std::vector<luil::tree_row>& rows)
    {
        std::vector<std::u8string> keys {};
        for (const luil::tree_row& row : rows)
            keys.push_back(row.item.key);
        return keys;
    }

    [[nodiscard]] std::u8string status_key(const std::u8string_view node, const std::u8string_view suffix)
    {
        std::u8string key { node };
        key.append(suffix);
        return key;
    }

    const luil::tree_flatten_options labelled { .loading_label = u8"불러오는 중…", .failed_label = u8"목록을 가져오지 못했습니다" };
}

TEST_CASE("Flattening a tree shows expanded children and status rows", "[ui][model]")
{
    const std::vector<luil::tree_node> roots { repository() };

    SECTION("Everything collapsed shows the roots, with branches marked")
    {
        const std::vector<luil::tree_row> rows { luil::flatten_tree(roots, {}, labelled) };
        REQUIRE(keys_of(rows) == std::vector<std::u8string> { u8"branches", u8"tags", u8"archive", u8"README" });
        REQUIRE(rows[0].item.expansion == luil::list_expansion::collapsed);
        // 아직 묻지 않은 노드는 자식이 없어도 가지다.
        REQUIRE(rows[1].item.expansion == luil::list_expansion::collapsed);
        REQUIRE(rows[3].item.expansion == luil::list_expansion::none);
    }

    SECTION("Expanded branches show children one level deeper and status rows for unloaded or failed ones")
    {
        const std::unordered_set<std::u8string> expanded { u8"branches", u8"tags", u8"archive" };
        const std::vector<luil::tree_row> rows { luil::flatten_tree(roots, expanded, labelled) };
        REQUIRE(keys_of(rows)
            == std::vector<std::u8string> {
                u8"branches",
                u8"release-0.2",
                u8"release-0.1",
                u8"tags",
                status_key(u8"tags", luil::tree_loading_suffix),
                u8"archive",
                status_key(u8"archive", luil::tree_failed_suffix),
                u8"README",
            });
        REQUIRE(rows[1].item.depth == 1);
        REQUIRE(rows[4].kind == luil::tree_row_kind::loading);
        REQUIRE(rows[4].node == u8"tags");
        REQUIRE(rows[4].item.label == u8"불러오는 중…");
        // 불러오는 중 줄은 설 자리가 아니고, 실패 줄은 다시 시도의 자리라 고를 수 있다.
        REQUIRE(rows[4].item.enabled == false);
        REQUIRE(rows[6].kind == luil::tree_row_kind::failed);
        REQUIRE(rows[6].item.enabled);
        REQUIRE(luil::tree_row_items(rows).size() == rows.size());
    }

    SECTION("Siblings sort within their parent")
    {
        luil::tree_flatten_options sorted { labelled };
        sorted.less = [](const luil::tree_node& left, const luil::tree_node& right) { return left.label < right.label; };
        const std::vector<luil::tree_row> rows { luil::flatten_tree(roots, { u8"branches" }, sorted) };
        REQUIRE(keys_of(rows) == std::vector<std::u8string> { u8"README", u8"archive", u8"branches", u8"release-0.1", u8"release-0.2", u8"tags" });
    }

    SECTION("A filter keeps matches and the path to them")
    {
        luil::tree_flatten_options filtered { labelled };
        filtered.keep = [](const luil::tree_node& candidate) { return candidate.key == u8"release-0.1"; };
        // 펼치지 않았으면 맞는 것의 조상만 보인다.
        REQUIRE(keys_of(luil::flatten_tree(roots, {}, filtered)) == std::vector<std::u8string> { u8"branches" });
        // 맞는 것을 펼쳐 보이게 하면 길이 열린다. 펼침 상태는 그대로다.
        filtered.expand_matches = true;
        REQUIRE(keys_of(luil::flatten_tree(roots, {}, filtered)) == std::vector<std::u8string> { u8"branches", u8"release-0.1" });
    }
}

TEST_CASE("Only expanded, unasked nodes need loading", "[ui][model]")
{
    std::vector<luil::tree_node> roots { repository() };
    roots[0].children.push_back(node(u8"feature", {}, luil::node_load::unloaded));
    REQUIRE(luil::tree_nodes_to_load(roots, {}).empty());
    // 접힌 가지 아래는 보지 않는다.
    REQUIRE(luil::tree_nodes_to_load(roots, { u8"feature" }).empty());
    REQUIRE(luil::tree_nodes_to_load(roots, { u8"branches", u8"feature", u8"tags", u8"archive" }) == std::vector<std::u8string> { u8"feature", u8"tags" });
}

TEST_CASE("A view order sorts stably and filters without touching the source", "[ui][model]")
{
    struct change
    {
        std::u8string path {};
        int status { 0 };
    };
    const std::vector<change> changes { { u8"b.cpp", 2 }, { u8"a.cpp", 1 }, { u8"c.cpp", 2 }, { u8"d.txt", 1 } };
    const luil::view_order by_status { luil::sort_filter(changes, [](const change& left, const change& right) { return left.status < right.status; }) };
    // 같은 값끼리는 원본 순서다.
    REQUIRE(by_status.source_index == std::vector<std::size_t> { 1, 3, 0, 2 });
    REQUIRE(by_status.view_index(0) == 2u);

    const auto by_path = [](const change& left, const change& right) { return left.path < right.path; };
    const auto sources = [](const change& item) { return item.path.ends_with(u8".cpp"); };
    const luil::view_order sources_only { luil::sort_filter(changes, by_path, sources) };
    REQUIRE(sources_only.source_index == std::vector<std::size_t> { 1, 0, 2 });
    REQUIRE(sources_only.view_index(3).has_value() == false);

    REQUIRE(luil::sort_filter(changes, {}).source_index == std::vector<std::size_t> { 0, 1, 2, 3 });
}

TEST_CASE("A request ledger drops late and duplicate results", "[ui][model]")
{
    luil::request_ledger ledger {};
    const luil::request_token first { ledger.issue(u8"tags") };
    // 같은 폴더를 다시 펼쳤다. 먼저 낸 요청은 낡았다.
    const luil::request_token second { ledger.issue(u8"tags") };
    const luil::request_token other { ledger.issue(u8"archive") };
    REQUIRE(ledger.pending() == 2u);
    REQUIRE(ledger.current(u8"tags", first) == false);
    REQUIRE(ledger.settle(u8"tags", first) == false);
    REQUIRE(ledger.settle(u8"tags", second));
    // 같은 결과가 두 번 오면 한 번만 받는다.
    REQUIRE(ledger.settle(u8"tags", second) == false);
    // 다른 키는 서로 모른다. 거둔 키의 결과는 낡았다.
    REQUIRE(ledger.current(u8"archive", other));
    ledger.cancel(u8"archive");
    REQUIRE(ledger.settle(u8"archive", other) == false);
    REQUIRE(ledger.pending() == 0u);
    REQUIRE(ledger.current(u8"never", luil::request_token {}) == false);
}
