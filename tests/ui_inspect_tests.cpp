#include "luil/ui/ui_element.h"
#include "luil/ui/ui_inspect.h"
#include "luil/ui/ui_tree.h"

#include "ui/inspect_geometry.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_box { luil::application_element_kind(1) };

    // 자식을 담고 자기 자리와 접근 정보만 내는 최소 요소다. 배치는 만들 때 준 상자 그대로다.
    class probe_node final : public luil::ui_element
    {
    public:
        probe_node(luil::ui_element_id id, const luil::rect_f& box, luil::access_info info = {})
            : ui_element { std::move(id) }
            , info_ { std::move(info) }
        {
            set_bounds(box);
        }

        // 배치되지 않은 요소다 (`arranged()`가 거짓).
        explicit probe_node(luil::ui_element_id id)
            : ui_element { std::move(id) }
        {
        }

        probe_node* add(std::unique_ptr<probe_node> child)
        {
            probe_node* const raw { child.get() };
            add_child(std::move(child));
            return raw;
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }

        [[nodiscard]] luil::access_info accessibility() const override
        {
            return info_;
        }

    private:
        luil::access_info info_ {};
    };

    [[nodiscard]] luil::ui_element_id box_id(const std::u8string& owner)
    {
        return { kind_box, owner };
    }

    [[nodiscard]] std::unique_ptr<probe_node> make_box(const std::u8string& owner, const luil::rect_f& box, luil::access_info info = {})
    {
        return std::make_unique<probe_node>(box_id(owner), box, std::move(info));
    }

    [[nodiscard]] std::unique_ptr<probe_node> make_root(const luil::rect_f& box)
    {
        return std::make_unique<probe_node>(luil::ui_element_id { luil::ui_element_kind::root, {} }, box);
    }

    [[nodiscard]] bool same_rect(const luil::rect_f& actual, const luil::rect_f& expected)
    {
        return actual.x == expected.x && actual.y == expected.y && actual.width == expected.width && actual.height == expected.height;
    }

    // 모든 test가 쓰는 tree의 이름표다. 물리 픽셀이고 배율은 2다.
    //  root         (0,0 400x300)
    //    column     (0,0 400x300)
    //      title    (10,10 100x20)   접근: static_text "Title"
    //      scroller (0,40 400x100)   자식을 자른다
    //        row_a  (0,0 400x60)     위쪽 40..60만 보인다
    //        row_b  (0,200 400x60)   통째로 잘린다
    //    hidden     (0,0 50x50)      보이지 않는 가지
    //      hidden_child (0,0 10x10)
    struct sample_tree
    {
        luil::ui_tree tree;

        [[nodiscard]] const luil::ui_element& at(const std::u8string& owner) const
        {
            const luil::ui_element* const found { tree.find(box_id(owner)) };
            REQUIRE(found != nullptr);
            return *found;
        }
    };

    [[nodiscard]] sample_tree make_sample_tree()
    {
        auto root { make_root({ 0.0f, 0.0f, 400.0f, 300.0f }) };
        probe_node* const column { root->add(make_box(u8"column", { 0.0f, 0.0f, 400.0f, 300.0f })) };
        column->add(make_box(u8"title", { 10.0f, 10.0f, 100.0f, 20.0f }, { .role = luil::access_role::static_text, .name = u8"Title" }));
        probe_node* const scroller { column->add(make_box(u8"scroller", { 0.0f, 40.0f, 400.0f, 100.0f })) };
        scroller->set_clip_children(true);
        scroller->add(make_box(u8"row_a", { 0.0f, 0.0f, 400.0f, 60.0f }));
        scroller->add(make_box(u8"row_b", { 0.0f, 200.0f, 400.0f, 60.0f }));
        probe_node* const hidden { root->add(make_box(u8"hidden", { 0.0f, 0.0f, 50.0f, 50.0f })) };
        hidden->set_visible(false);
        hidden->add(make_box(u8"hidden_child", { 0.0f, 0.0f, 10.0f, 10.0f }));
        return { luil::ui_tree { std::move(root) } };
    }

    [[nodiscard]] std::vector<std::u8string> owners_of(const std::vector<luil::inspect_node>& nodes)
    {
        std::vector<std::u8string> owners {};
        for (const luil::inspect_node& node : nodes)
            owners.push_back(node.element->id().owner);
        return owners;
    }

    [[nodiscard]] luil::inspector_config names_config()
    {
        luil::inspector_config config {};
        config.kind_names.push_back({ kind_box, u8"box" });
        return config;
    }
}

TEST_CASE("The flattened tree follows the drawing order with depth and parent", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    const std::vector<luil::inspect_node> nodes { luil::inspect_nodes(sample.tree) };
    const std::vector<std::u8string> expected_owners { u8"", u8"column", u8"title", u8"scroller", u8"row_a", u8"row_b", u8"hidden", u8"hidden_child" };
    REQUIRE(owners_of(nodes) == expected_owners);

    const std::array<std::size_t, 8> depths { 0, 1, 2, 2, 3, 3, 1, 2 };
    const std::array<std::size_t, 8> parents { luil::no_inspect_parent, 0, 1, 1, 3, 3, 0, 6 };
    for (std::size_t index { 0 }; index < nodes.size(); ++index)
    {
        REQUIRE(nodes[index].depth == depths[index]);
        REQUIRE(nodes[index].parent == parents[index]);
    }
    // 접근 역할이 없는 배치 전용 컨테이너(column)도 한 줄이다.
    REQUIRE(nodes[1].element->accessibility().role == luil::access_role::none);
}

TEST_CASE("A hidden branch stays in the list with a false visible path", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    const std::vector<luil::inspect_node> nodes { luil::inspect_nodes(sample.tree) };
    for (std::size_t index { 0 }; index < 6; ++index)
        REQUIRE(nodes[index].visible_path);
    REQUIRE(nodes[6].visible_path == false);
    REQUIRE(nodes[7].visible_path == false);
    // 보이지 않는 가지는 그려지지 않으니 보이는 영역도 없다.
    REQUIRE(nodes[6].visible_bounds.has_value() == false);
    REQUIRE(nodes[7].visible_bounds.has_value() == false);
}

TEST_CASE("A child half inside a clipping parent keeps the overlap and a fully clipped child keeps nothing", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    const std::vector<luil::inspect_node> nodes { luil::inspect_nodes(sample.tree) };
    // 잘림이 없는 요소는 자기 bounds 그대로다.
    REQUIRE(nodes[2].visible_bounds.has_value());
    REQUIRE(same_rect(*nodes[2].visible_bounds, { 10.0f, 10.0f, 100.0f, 20.0f }));
    // scroller는 자식을 자를 뿐 자기 자리는 자르지 않는다.
    REQUIRE(same_rect(*nodes[3].visible_bounds, { 0.0f, 40.0f, 400.0f, 100.0f }));
    // row_a는 y 0..60 가운데 scroller 안의 40..60만 남는다.
    REQUIRE(nodes[4].visible_bounds.has_value());
    REQUIRE(same_rect(*nodes[4].visible_bounds, { 0.0f, 40.0f, 400.0f, 20.0f }));
    REQUIRE(same_rect(nodes[4].bounds, { 0.0f, 0.0f, 400.0f, 60.0f }));
    // row_b는 통째로 잘린다.
    REQUIRE(nodes[5].visible_bounds.has_value() == false);
    REQUIRE(nodes[5].visible_path);
}

TEST_CASE("A fully clipped parent leaves nothing visible below it", "[ui][inspect]")
{
    auto root { make_root({ 0.0f, 0.0f, 100.0f, 100.0f }) };
    probe_node* const clipper { root->add(make_box(u8"clipper", { 0.0f, 0.0f, 0.0f, 0.0f })) };
    clipper->set_clip_children(true);
    clipper->add(make_box(u8"inside", { 0.0f, 0.0f, 50.0f, 50.0f }));
    const luil::ui_tree tree { std::move(root) };
    const std::vector<luil::inspect_node> nodes { luil::inspect_nodes(tree) };
    REQUIRE(nodes.size() == 3);
    REQUIRE(nodes[2].visible_bounds.has_value() == false);
}

TEST_CASE("An empty tree flattens to nothing and answers nothing", "[ui][inspect]")
{
    const luil::ui_tree tree { nullptr };
    REQUIRE(luil::inspect_nodes(tree).empty());
    REQUIRE(luil::inspect_at(tree, 1.0f, 1.0f) == nullptr);
    REQUIRE(luil::inspect_step(tree, {}, luil::inspect_move::next) == luil::ui_element_id {});
    const luil::interaction_snapshot interaction {};
    REQUIRE(luil::pick_inspected(tree, interaction, luil::inspector_config {}) == nullptr);
    REQUIRE(luil::dump_tree(tree, 1.0f, {}) == u8"elements 0  dup 0  unarranged 0  @1.0x");
}

TEST_CASE("The point query picks the deepest visible element even without interaction", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    // 글이나 컨테이너처럼 누를 수 없는 요소도 집는다 (`hit_test`는 이 자리에서 nullptr이다).
    REQUIRE(sample.tree.hit_test(20.0f, 20.0f) == nullptr);
    REQUIRE(luil::inspect_at(sample.tree, 20.0f, 20.0f) == &sample.at(u8"title"));
    // 아무 자식에도 들지 않은 자리는 컨테이너다.
    REQUIRE(luil::inspect_at(sample.tree, 300.0f, 20.0f) == &sample.at(u8"column"));
    REQUIRE(luil::inspect_at(sample.tree, 300.0f, 50.0f) == &sample.at(u8"row_a"));
    REQUIRE(luil::inspect_at(sample.tree, 300.0f, 100.0f) == &sample.at(u8"scroller"));
}

TEST_CASE("The point query skips hidden elements and the clipped part of a child", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    // hidden은 (0,0)~(50,50)을 덮지만 보이지 않으니 column이 집힌다.
    REQUIRE(luil::inspect_at(sample.tree, 5.0f, 5.0f) == &sample.at(u8"column"));
    // row_b는 y 200..260에 놓였지만 scroller가 40..140 밖을 잘라 그 자리는 column이다.
    REQUIRE(luil::inspect_at(sample.tree, 300.0f, 250.0f) == &sample.at(u8"column"));
}

TEST_CASE("The point query prefers what is drawn later and finds nothing outside the root", "[ui][inspect]")
{
    auto root { make_root({ 0.0f, 0.0f, 200.0f, 200.0f }) };
    root->add(make_box(u8"under", { 0.0f, 0.0f, 100.0f, 100.0f }));
    root->add(make_box(u8"over", { 50.0f, 50.0f, 100.0f, 100.0f }));
    const luil::ui_tree tree { std::move(root) };
    REQUIRE(luil::inspect_at(tree, 75.0f, 75.0f) == tree.find(box_id(u8"over")));
    REQUIRE(luil::inspect_at(tree, 25.0f, 25.0f) == tree.find(box_id(u8"under")));
    REQUIRE(luil::inspect_at(tree, 175.0f, 25.0f) == tree.root());
    REQUIRE(luil::inspect_at(tree, -1.0f, 5.0f) == nullptr);
    REQUIRE(luil::inspect_at(tree, 200.0f, 5.0f) == nullptr);
}

TEST_CASE("The inspected element follows hover, focus, press, the chosen id or the point", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    luil::interaction_snapshot interaction {};
    interaction.hovered = box_id(u8"title");
    interaction.focused = box_id(u8"row_a");
    interaction.pressed = box_id(u8"scroller");
    luil::inspector_config config {};
    config.selected = box_id(u8"column");
    config.point = luil::point_f { 300.0f, 50.0f };

    config.pick = luil::inspector_pick::hovered;
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == &sample.at(u8"title"));
    config.pick = luil::inspector_pick::focused;
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == &sample.at(u8"row_a"));
    config.pick = luil::inspector_pick::pressed;
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == &sample.at(u8"scroller"));
    config.pick = luil::inspector_pick::selected;
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == &sample.at(u8"column"));
    config.pick = luil::inspector_pick::point;
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == &sample.at(u8"row_a"));
}

TEST_CASE("Ancestor levels climb to the parent and stop at the root", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    luil::interaction_snapshot interaction {};
    interaction.hovered = box_id(u8"row_a");
    luil::inspector_config config {};
    config.ancestor_levels = 1;
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == &sample.at(u8"scroller"));
    config.ancestor_levels = 2;
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == &sample.at(u8"column"));
    config.ancestor_levels = 3;
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == sample.tree.root());
    config.ancestor_levels = 40;
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == sample.tree.root());
}

TEST_CASE("Nothing is inspected for an id that is not in the tree or sits in a hidden branch", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    luil::interaction_snapshot interaction {};
    luil::inspector_config config {};
    // 아무것도 가리키지 않는다.
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == nullptr);
    interaction.hovered = box_id(u8"nobody");
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == nullptr);
    // 숨은 가지의 요소는 tree 색인에 있어도 집지 않는다.
    interaction.hovered = box_id(u8"hidden_child");
    REQUIRE(sample.tree.find(box_id(u8"hidden_child")) != nullptr);
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == nullptr);
    config.pick = luil::inspector_pick::point;
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == nullptr);
    config.point = luil::point_f { -10.0f, -10.0f };
    REQUIRE(luil::pick_inspected(sample.tree, interaction, config) == nullptr);
}

TEST_CASE("Another surface's hover is not found once the interaction is filtered for this surface", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    luil::interaction_snapshot published {};
    published.hovered = box_id(u8"title");
    published.hovered_surface = u8"tool";
    luil::inspector_config config {};
    // 주 창의 눈으로 걸러 보면 도구 창의 hover는 없다.
    const luil::interaction_snapshot main_view { luil::interaction_for_surface(published, {}) };
    REQUIRE(luil::pick_inspected(sample.tree, main_view, config) == nullptr);
    // 도구 창의 눈으로 걸러 보면 같은 id가 이 tree에서 집힌다.
    const luil::interaction_snapshot tool_view { luil::interaction_for_surface(published, u8"tool") };
    REQUIRE(luil::pick_inspected(sample.tree, tool_view, config) == &sample.at(u8"title"));
}

TEST_CASE("A duplicated id inspects the first element in drawing order and climbs its own parents", "[ui][inspect]")
{
    auto root { make_root({ 0.0f, 0.0f, 100.0f, 100.0f }) };
    probe_node* const first { root->add(make_box(u8"first", { 0.0f, 0.0f, 50.0f, 50.0f })) };
    first->add(make_box(u8"twin", { 0.0f, 0.0f, 10.0f, 10.0f }));
    probe_node* const second { root->add(make_box(u8"second", { 50.0f, 50.0f, 50.0f, 50.0f })) };
    second->add(make_box(u8"twin", { 50.0f, 50.0f, 10.0f, 10.0f }));
    const luil::ui_tree tree { std::move(root) };
    luil::interaction_snapshot interaction {};
    interaction.hovered = box_id(u8"twin");
    luil::inspector_config config {};
    config.ancestor_levels = 1;
    REQUIRE(luil::pick_inspected(tree, interaction, config) == tree.find(box_id(u8"first")));
    // 좌표로 집은 둘째 쌍둥이는 자기 부모를 따라 올라간다.
    config.pick = luil::inspector_pick::point;
    config.point = luil::point_f { 55.0f, 55.0f };
    REQUIRE(luil::pick_inspected(tree, interaction, config) == tree.find(box_id(u8"second")));
}

TEST_CASE("Stepping the selection walks the six moves and ends with an empty id", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    const luil::ui_element_id none {};
    const auto step = [&sample](const std::u8string& from, const luil::inspect_move move) {
        return luil::inspect_step(sample.tree, from.empty() ? luil::ui_element_id {} : box_id(from), move);
    };
    REQUIRE(step(u8"row_a", luil::inspect_move::parent) == box_id(u8"scroller"));
    REQUIRE(step(u8"column", luil::inspect_move::parent) == luil::ui_element_id { luil::ui_element_kind::root, {} });
    REQUIRE(luil::inspect_step(sample.tree, { luil::ui_element_kind::root, {} }, luil::inspect_move::parent) == none);

    REQUIRE(step(u8"column", luil::inspect_move::first_child) == box_id(u8"title"));
    REQUIRE(step(u8"scroller", luil::inspect_move::first_child) == box_id(u8"row_a"));
    REQUIRE(step(u8"title", luil::inspect_move::first_child) == none);

    REQUIRE(step(u8"title", luil::inspect_move::next_sibling) == box_id(u8"scroller"));
    REQUIRE(step(u8"scroller", luil::inspect_move::next_sibling) == none);
    REQUIRE(step(u8"row_a", luil::inspect_move::next_sibling) == box_id(u8"row_b"));
    REQUIRE(step(u8"scroller", luil::inspect_move::previous_sibling) == box_id(u8"title"));
    REQUIRE(step(u8"title", luil::inspect_move::previous_sibling) == none);
    REQUIRE(step(u8"row_b", luil::inspect_move::previous_sibling) == box_id(u8"row_a"));

    REQUIRE(step(u8"title", luil::inspect_move::next) == box_id(u8"scroller"));
    REQUIRE(step(u8"scroller", luil::inspect_move::next) == box_id(u8"row_a"));
    REQUIRE(step(u8"row_b", luil::inspect_move::next) == none);
    REQUIRE(step(u8"scroller", luil::inspect_move::previous) == box_id(u8"title"));
    REQUIRE(step(u8"column", luil::inspect_move::previous) == luil::ui_element_id { luil::ui_element_kind::root, {} });
    REQUIRE(luil::inspect_step(sample.tree, { luil::ui_element_kind::root, {} }, luil::inspect_move::previous) == none);
}

TEST_CASE("Stepping skips hidden branches and a missing start only moves to the root", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    // row_b 다음은 숨은 가지(hidden)를 건너뛰어 끝이다.
    REQUIRE(luil::inspect_step(sample.tree, box_id(u8"row_b"), luil::inspect_move::next) == luil::ui_element_id {});
    REQUIRE(luil::inspect_step(sample.tree, box_id(u8"column"), luil::inspect_move::next_sibling) == luil::ui_element_id {});
    // 숨은 요소에서는 출발하지 않는다.
    REQUIRE(luil::inspect_step(sample.tree, box_id(u8"hidden_child"), luil::inspect_move::parent) == luil::ui_element_id {});
    REQUIRE(luil::inspect_step(sample.tree, box_id(u8"hidden"), luil::inspect_move::next) == luil::ui_element_id { luil::ui_element_kind::root, {} });
    // 빈 from의 next는 뿌리다. 나머지 이동은 비어 있다.
    const luil::ui_element_id root_id { luil::ui_element_kind::root, {} };
    REQUIRE(luil::inspect_step(sample.tree, {}, luil::inspect_move::next) == root_id);
    REQUIRE(luil::inspect_step(sample.tree, {}, luil::inspect_move::first_child) == luil::ui_element_id {});
    REQUIRE(luil::inspect_step(sample.tree, {}, luil::inspect_move::previous) == luil::ui_element_id {});
    REQUIRE(luil::inspect_step(sample.tree, box_id(u8"nobody"), luil::inspect_move::next) == root_id);
    REQUIRE(luil::inspect_step(sample.tree, box_id(u8"nobody"), luil::inspect_move::parent) == luil::ui_element_id {});
}

TEST_CASE("Every library kind has a name and the names differ", "[ui][inspect]")
{
    std::set<std::u8string> seen {};
    const auto end { static_cast<std::uint32_t>(luil::ui_element_kind::end_of_library_kinds) };
    for (std::uint32_t value { 0 }; value < end; ++value)
    {
        const std::u8string_view name { luil::library_kind_name(static_cast<luil::ui_element_kind>(value)) };
        INFO("kind " << value);
        REQUIRE(name.empty() == false);
        REQUIRE(seen.insert(std::u8string { name }).second);
    }
    REQUIRE(seen.size() == end);
}

TEST_CASE("Library kind names are the enumerator names", "[ui][inspect]")
{
    REQUIRE(luil::library_kind_name(luil::ui_element_kind::none) == u8"none");
    REQUIRE(luil::library_kind_name(luil::ui_element_kind::root) == u8"root");
    REQUIRE(luil::library_kind_name(luil::ui_element_kind::info_bar_close) == u8"info_bar_close");
    REQUIRE(luil::library_kind_name(luil::ui_element_kind::split_button_arrow) == u8"split_button_arrow");
    REQUIRE(luil::library_kind_name(luil::ui_element_kind::ink_canvas) == u8"ink_canvas");
    REQUIRE(luil::library_kind_name(luil::ui_element_kind::markup_node) == u8"markup_node");
    // 끝 표시와 앱 대역은 라이브러리 kind가 아니다.
    REQUIRE(luil::library_kind_name(luil::ui_element_kind::end_of_library_kinds).empty());
    REQUIRE(luil::library_kind_name(luil::ui_element_kind::first_application_kind).empty());
    REQUIRE(luil::library_kind_name(luil::application_element_kind(7)).empty());
}

TEST_CASE("An app kind is named by the frame and falls back to its number", "[ui][inspect]")
{
    const luil::inspector_config config { names_config() };
    REQUIRE(luil::kind_display_name(kind_box, config) == u8"box");
    REQUIRE(luil::kind_display_name(luil::application_element_kind(0), config) == u8"kind#1024");
    REQUIRE(luil::kind_display_name(luil::application_element_kind(0), luil::inspector_config {}) == u8"kind#1024");
    // 라이브러리 이름이 먼저다. 앱이 같은 수에 다른 이름을 붙여도 덮지 못한다.
    luil::inspector_config shadow {};
    shadow.kind_names.push_back({ luil::ui_element_kind::root, u8"mine" });
    REQUIRE(luil::kind_display_name(luil::ui_element_kind::root, shadow) == u8"root");
}

TEST_CASE("Access roles have their names and an unknown role has none", "[ui][inspect]")
{
    REQUIRE(luil::access_role_name(luil::access_role::none) == u8"none");
    REQUIRE(luil::access_role_name(luil::access_role::button) == u8"button");
    REQUIRE(luil::access_role_name(luil::access_role::navigation) == u8"navigation");
    REQUIRE(luil::access_role_name(static_cast<luil::access_role>(900)).empty());
    // 글에서는 모르는 값이 `role#<수>`로 읽힌다.
    auto root { make_root({ 0.0f, 0.0f, 100.0f, 100.0f }) };
    root->add(make_box(u8"odd", { 0.0f, 0.0f, 10.0f, 10.0f }, { .role = static_cast<luil::access_role>(900) }));
    const luil::ui_tree tree { std::move(root) };
    const std::u8string text { luil::describe_element(tree, *tree.find(box_id(u8"odd")), 1.0f, names_config()) };
    REQUIRE(text.find(u8"access role#900") != std::u8string::npos);
}

TEST_CASE("The class name is the dynamic type without the compiler's keyword", "[ui][inspect]")
{
    const probe_node node { box_id(u8"x") };
    const std::u8string name { luil::element_class_name(node) };
    REQUIRE(name.find(u8"probe_node") != std::u8string::npos);
    REQUIRE(name.starts_with(u8"class ") == false);
    REQUIRE(name.starts_with(u8"struct ") == false);
    // 부모 참조로 물어도 실제 타입이다.
    const luil::ui_element& base { node };
    REQUIRE(luil::element_class_name(base) == name);
}

TEST_CASE("A described element reads in logical pixels with its place, flags and access", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    const luil::inspector_config config { names_config() };
    const luil::ui_element& title { sample.at(u8"title") };
    const std::u8string expected {
        u8"box:title  " + luil::element_class_name(title) + u8"\n"
        u8"bounds 5,5 50x10 @2.0x\n"
        u8"children 0  depth 2  flags enabled\n"
        u8"handles -  access static_text \"Title\"\n"
        u8"elements 8  dup 0  unarranged 0"
    };
    REQUIRE(luil::describe_element(sample.tree, title, 2.0f, config) == expected);
}

TEST_CASE("A clipped element says what is left visible and a hidden one says none", "[ui][inspect]")
{
    const sample_tree sample { make_sample_tree() };
    const luil::inspector_config config { names_config() };
    const std::u8string clipped { luil::describe_element(sample.tree, sample.at(u8"row_a"), 2.0f, config) };
    REQUIRE(clipped.find(u8"\nbounds 0,0 200x30 @2.0x  visible 0,20 200x10\n") != std::u8string::npos);
    const std::u8string gone { luil::describe_element(sample.tree, sample.at(u8"row_b"), 2.0f, config) };
    REQUIRE(gone.find(u8"\nbounds 0,100 200x30 @2.0x  visible none\n") != std::u8string::npos);
    const std::u8string hidden { luil::describe_element(sample.tree, sample.at(u8"hidden_child"), 2.0f, config) };
    REQUIRE(hidden.find(u8"  visible none\n") != std::u8string::npos);
    REQUIRE(hidden.find(u8"depth 2  flags enabled hidden\n") != std::u8string::npos);
}

TEST_CASE("The inner padding is measured from the children's union in logical pixels", "[ui][inspect]")
{
    auto root { make_root({ 0.0f, 0.0f, 400.0f, 300.0f }) };
    probe_node* const panel { root->add(make_box(u8"panel", { 20.0f, 20.0f, 200.0f, 120.0f })) };
    panel->add(make_box(u8"content", { 30.0f, 40.0f, 100.0f, 60.0f }));
    // 넓이 없는 자식은 합집합을 끌어당기지 않는다.
    panel->add(make_box(u8"nothing", { 0.0f, 0.0f, 0.0f, 0.0f }));
    const luil::ui_tree tree { std::move(root) };
    const std::u8string text { luil::describe_element(tree, *tree.find(box_id(u8"panel")), 2.0f, names_config()) };
    // 위 20, 오른쪽 220-130=90, 아래 140-100=40, 왼쪽 10 (물리) -> 논리 10 45 20 5.
    REQUIRE(text.find(u8"\nbounds 10,10 100x60 @2.0x  inset 10 45 20 5\n") != std::u8string::npos);
    REQUIRE(luil::inspect_content_bounds(*tree.find(box_id(u8"panel"))).has_value());
    REQUIRE(luil::inspect_content_bounds(*tree.find(box_id(u8"content"))).has_value() == false);
}

TEST_CASE("A described element lists its flags and the roles it handles", "[ui][inspect]")
{
    auto root { make_root({ 0.0f, 0.0f, 400.0f, 300.0f }) };
    probe_node* const bar { root->add(make_box(u8"bar", { 0.0f, 0.0f, 100.0f, 50.0f }, { .role = luil::access_role::alert, .name = u8"Sync failed" })) };
    bar->set_hit_opaque(true);
    bar->set_clip_children(true);
    bar->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; });
    bar->set_tooltip(u8"help");
    bar->set_enabled(false);
    const luil::ui_tree tree { std::move(root) };
    const std::u8string text { luil::describe_element(tree, *tree.find(box_id(u8"bar")), 1.0f, names_config()) };
    REQUIRE(text.find(u8"\nchildren 0  depth 1  flags disabled opaque tab clip\n") != std::u8string::npos);
    REQUIRE(text.find(u8"\nhandles click tooltip  access alert \"Sync failed\"\n") != std::u8string::npos);
}

TEST_CASE("A described element hides the access value and flattens control characters", "[ui][inspect]")
{
    auto root { make_root({ 0.0f, 0.0f, 100.0f, 100.0f }) };
    luil::access_info info {};
    info.role = luil::access_role::edit;
    info.name = u8"Pass\nword";
    info.value = u8"hunter2";
    root->add(make_box(u8"secret", { 0.0f, 0.0f, 50.0f, 20.0f }, std::move(info)));
    const luil::ui_tree tree { std::move(root) };
    const std::u8string text { luil::describe_element(tree, *tree.find(box_id(u8"secret")), 1.0f, names_config()) };
    REQUIRE(text.find(u8"hunter2") == std::u8string::npos);
    REQUIRE(text.find(u8"access edit \"Pass word\"") != std::u8string::npos);
    // 줄은 다섯 개다.
    REQUIRE(std::count(text.begin(), text.end(), u8'\n') == 4);
}

TEST_CASE("Fractional logical sizes keep one decimal and the scale shows its decimals", "[ui][inspect]")
{
    auto root { make_root({ 0.0f, 0.0f, 300.0f, 300.0f }) };
    root->add(make_box(u8"odd", { 100.0f, 50.0f, 100.0f, 25.0f }));
    const luil::ui_tree tree { std::move(root) };
    const luil::ui_element& odd { *tree.find(box_id(u8"odd")) };
    // 1.5배: 100/1.5 = 66.666 -> 66.7, 50/1.5 = 33.3, 25/1.5 = 16.7.
    REQUIRE(luil::describe_element(tree, odd, 1.5f, names_config()).find(u8"\nbounds 66.7,33.3 66.7x16.7 @1.5x\n") != std::u8string::npos);
    REQUIRE(luil::describe_element(tree, odd, 1.25f, names_config()).find(u8" @1.25x\n") != std::u8string::npos);
    // 쓸 수 없는 배율은 1로 본다.
    REQUIRE(luil::describe_element(tree, odd, 0.0f, names_config()).find(u8"\nbounds 100,50 100x25 @1.0x\n") != std::u8string::npos);
}

TEST_CASE("The tree dump locks one line per element under the totals header", "[ui][inspect]")
{
    auto root { make_root({ 0.0f, 0.0f, 400.0f, 300.0f }) };
    probe_node* const column { root->add(make_box(u8"column", { 0.0f, 0.0f, 400.0f, 300.0f })) };
    column->add(make_box(u8"title", { 10.0f, 10.0f, 100.0f, 20.0f }, { .role = luil::access_role::static_text, .name = u8"Title" }));
    probe_node* const hidden { root->add(make_box(u8"gone", { 0.0f, 0.0f, 20.0f, 20.0f })) };
    hidden->set_visible(false);
    const luil::ui_tree tree { std::move(root) };
    const std::u8string root_class { luil::element_class_name(*tree.root()) };
    const std::u8string expected {
        u8"elements 4  dup 0  unarranged 0  @2.0x\n"
        u8"root:  " + root_class + u8"  0,0 200x150\n"
        u8"  box:column  " + root_class + u8"  0,0 200x150\n"
        u8"    box:title  " + root_class + u8"  5,5 50x10  access static_text \"Title\"\n"
        u8"  box:gone  " + root_class + u8"  0,0 10x10  hidden"
    };
    REQUIRE(luil::dump_tree(tree, 2.0f, names_config()) == expected);
}

TEST_CASE("Duplicate ids and unarranged elements are counted with an exclamation mark", "[ui][inspect]")
{
    auto root { make_root({ 0.0f, 0.0f, 100.0f, 100.0f }) };
    root->add(make_box(u8"twin", { 0.0f, 0.0f, 10.0f, 10.0f }));
    root->add(make_box(u8"twin", { 20.0f, 0.0f, 10.0f, 10.0f }));
    root->add(std::make_unique<probe_node>(box_id(u8"lost")));
    const luil::ui_tree tree { std::move(root) };
    const std::u8string text { luil::describe_element(tree, *tree.find(box_id(u8"twin")), 1.0f, names_config()) };
    REQUIRE(text.ends_with(u8"\nelements 4  dup 1!  unarranged 1!"));
    const std::u8string lost { luil::describe_element(tree, *tree.find(box_id(u8"lost")), 1.0f, names_config()) };
    REQUIRE(lost.find(u8"flags enabled unarranged\n") != std::u8string::npos);
}

TEST_CASE("The palette swatches name each of the 24 core roles with its own color", "[ui][inspect]")
{
    using member = luil::ui_color luil::ui_color_palette::*;
    const std::pair<std::u8string_view, member> roles[] {
        { u8"window_background", &luil::ui_color_palette::window_background },
        { u8"surface_background", &luil::ui_color_palette::surface_background },
        { u8"primary_foreground", &luil::ui_color_palette::primary_foreground },
        { u8"secondary_foreground", &luil::ui_color_palette::secondary_foreground },
        { u8"disabled_foreground", &luil::ui_color_palette::disabled_foreground },
        { u8"divider", &luil::ui_color_palette::divider },
        { u8"input_background", &luil::ui_color_palette::input_background },
        { u8"input_border", &luil::ui_color_palette::input_border },
        { u8"control_border", &luil::ui_color_palette::control_border },
        { u8"accent", &luil::ui_color_palette::accent },
        { u8"accent_hover", &luil::ui_color_palette::accent_hover },
        { u8"accent_soft", &luil::ui_color_palette::accent_soft },
        { u8"accent_foreground", &luil::ui_color_palette::accent_foreground },
        { u8"selection_background", &luil::ui_color_palette::selection_background },
        { u8"selection_foreground", &luil::ui_color_palette::selection_foreground },
        { u8"raised_background", &luil::ui_color_palette::raised_background },
        { u8"notice_background", &luil::ui_color_palette::notice_background },
        { u8"warning_accent", &luil::ui_color_palette::warning_accent },
        { u8"error_accent", &luil::ui_color_palette::error_accent },
        { u8"tooltip_background", &luil::ui_color_palette::tooltip_background },
        { u8"tooltip_border", &luil::ui_color_palette::tooltip_border },
        { u8"button_hover_background", &luil::ui_color_palette::button_hover_background },
        { u8"button_pressed_background", &luil::ui_color_palette::button_pressed_background },
        { u8"backdrop_background", &luil::ui_color_palette::backdrop_background },
    };
    // 멤버마다 서로 다른 색을 넣고, 이름마다 제 멤버의 색이 나오는지 본다.
    luil::ui_color_palette palette {};
    for (std::size_t index { 0 }; index < std::size(roles); ++index)
        palette.*roles[index].second = static_cast<luil::ui_color>(0xFF000000u + (index + 1) * 0x010203u);
    const std::vector<luil::inspector_swatch> swatches { luil::inspector_palette_swatches(palette) };
    REQUIRE(swatches.size() == std::size(roles));
    for (std::size_t index { 0 }; index < std::size(roles); ++index)
    {
        const std::string role_name { reinterpret_cast<const char*>(roles[index].first.data()), roles[index].first.size() };
        INFO(role_name);
        REQUIRE(swatches[index].name == roles[index].first);
        REQUIRE(swatches[index].color == palette.*roles[index].second);
    }
}
