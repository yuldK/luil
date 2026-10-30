#include "demo/workbench_page.h"

#include "luil/ui/grid_element.h"
#include "luil/ui/table_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/virtual_list_element.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // 글자 byte 하나가 글꼴 크기의 절반 폭인 측정기다. 배치가 글꼴 등록부를 부르지 않는다.
    const luil::text_measurer half_width { [](const std::u8string_view text, const float pixel_size) { return static_cast<float>(text.size()) * pixel_size * 0.5f; } };

    [[nodiscard]] luil::ui_tree published(demo::workbench_page& page, const float width = 1000.0f, const float height = 700.0f)
    {
        std::unique_ptr<luil::ui_element> root { page.build(width, height, 1.0f) };
        root->arrange({ .slot = { 0.0f, 0.0f, width, height }, .scale = 1.0f, .measurer = &half_width });
        return luil::ui_tree { std::move(root) };
    }

    [[nodiscard]] luil::ui_element_id workbench(std::u8string owner)
    {
        return { demo::kind_workbench, std::move(owner) };
    }

    [[nodiscard]] const luil::virtual_list_element& repository(const luil::ui_tree& tree)
    {
        const auto* const list { dynamic_cast<const luil::virtual_list_element*>(tree.find({ luil::ui_element_kind::virtual_list, u8"workbench-tree" })) };
        REQUIRE(list != nullptr);
        return *list;
    }

    [[nodiscard]] bool shows(const luil::ui_tree& tree, const std::u8string_view key)
    {
        for (const luil::virtual_list_item& item : repository(tree).model())
            if (item.key == key)
                return true;
        return false;
    }

    [[nodiscard]] std::u8string loading(const std::u8string_view node)
    {
        std::u8string key { node };
        key.append(luil::tree_loading_suffix);
        return key;
    }

    [[nodiscard]] const luil::table_element& changes(const luil::ui_tree& tree)
    {
        const auto* const table { dynamic_cast<const luil::table_element*>(tree.find({ luil::ui_element_kind::table, u8"workbench-changes" })) };
        REQUIRE(table != nullptr);
        return *table;
    }

    [[nodiscard]] luil::app_message summary_text(std::u8string text)
    {
        luil::text_edit_request request {};
        request.target = demo::target_workbench_summary;
        request.text = std::move(text);
        return luil::app_message { demo::edit_intent { request } };
    }

    [[nodiscard]] std::chrono::steady_clock::time_point later()
    {
        return std::chrono::steady_clock::now() + std::chrono::hours { 1 };
    }
}

TEST_CASE("The workbench assembles without duplicate or unarranged elements", "[demo][workbench]")
{
    demo::workbench_page page {};
    const luil::ui_tree tree { published(page) };
    REQUIRE(tree.duplicate_ids().empty());
    REQUIRE(tree.unarranged().empty());
    REQUIRE(shows(tree, u8"trunk/src"));
    // 같은 경로가 저장소 계층과 변경 표에 함께 선다. 행 자리표는 목록마다 다르다.
    REQUIRE(tree.find(luil::virtual_list_row_id(u8"workbench-tree", u8"trunk/CMakeLists.txt")) != nullptr);
    REQUIRE(tree.find(luil::virtual_list_row_id(u8"workbench-changes", u8"trunk/CMakeLists.txt")) != nullptr);
    REQUIRE(changes(tree).body().model().size() == 45u);
}

TEST_CASE("Expanding a folder loads it later and drops a stale result", "[demo][workbench]")
{
    demo::workbench_page page {};
    REQUIRE(page.next_due().has_value() == false);
    REQUIRE(page.handle(luil::app_message { demo::workbench_tree_expand_intent { u8"branches", true } }));
    REQUIRE(shows(published(page), loading(u8"branches")));
    REQUIRE(page.next_due().has_value());

    // 묻는 중에 접었다 다시 펼치면 요청이 새로 나가고, 옛 요청의 결과는 버려진다.
    REQUIRE(page.handle(luil::app_message { demo::workbench_tree_expand_intent { u8"tags", true } }));
    REQUIRE(page.handle(luil::app_message { demo::workbench_tree_expand_intent { u8"tags", false } }));
    REQUIRE(page.handle(luil::app_message { demo::workbench_tree_expand_intent { u8"tags", true } }));

    page.tick(later());
    const luil::ui_tree tree { published(page) };
    REQUIRE(shows(tree, u8"branches/release-0.1"));
    REQUIRE(shows(tree, loading(u8"branches")) == false);
    REQUIRE(shows(tree, u8"tags/v0.2"));
    REQUIRE(tree.duplicate_ids().empty());
    REQUIRE(page.next_due().has_value() == false);
}

TEST_CASE("A failed folder offers a retry that loads it", "[demo][workbench]")
{
    demo::workbench_page page {};
    REQUIRE(page.handle(luil::app_message { demo::workbench_tree_expand_intent { u8"archive", true } }));
    page.tick(later());
    const luil::ui_tree failed { published(page) };
    REQUIRE(failed.find(workbench(u8"retry:archive")) != nullptr);
    REQUIRE(shows(failed, u8"archive/old-trunk") == false);

    REQUIRE(page.handle(luil::app_message { demo::workbench_tree_retry_intent { u8"archive" } }));
    REQUIRE(shows(published(page), loading(u8"archive")));
    page.tick(later());
    REQUIRE(shows(published(page), u8"archive/old-trunk"));
}

TEST_CASE("The form's label column is as wide as its longest label", "[demo][workbench]")
{
    demo::workbench_page page {};
    const luil::ui_tree tree { published(page) };
    const auto* const form { dynamic_cast<const luil::grid_element*>(tree.find(workbench(u8"form"))) };
    REQUIRE(form != nullptr);
    // "마지막 커밋"은 16 byte다 — 12px 글에서 96px이다.
    REQUIRE(form->column_widths()[0] == 96.0f);
    REQUIRE(form->row_heights()[0] == 28.0f);
}

TEST_CASE("The repository panel grows with the window up to its maximum", "[demo][workbench]")
{
    demo::workbench_page page {};
    REQUIRE(published(page, 1600.0f).find(workbench(u8"tree-panel"))->bounds().width == 300.0f);
    REQUIRE(published(page, 700.0f).find(workbench(u8"tree-panel"))->bounds().width == 200.0f);
}

TEST_CASE("Committing needs a summary and staged files", "[demo][workbench]")
{
    demo::workbench_page page {};
    REQUIRE(published(page).find(workbench(u8"commit"))->enabled() == false);
    REQUIRE(published(page).find(workbench(u8"commit-count"))->accessibility().name == u8"2");
    REQUIRE(page.handle(summary_text(u8"표 정렬")));
    REQUIRE(published(page).find(workbench(u8"commit"))->enabled());

    REQUIRE(page.handle(luil::app_message { demo::workbench_commit_intent {} }));
    const luil::ui_tree tree { published(page) };
    REQUIRE(changes(tree).body().model().size() == 43u);
    REQUIRE(tree.find(workbench(u8"commit-count")) == nullptr);
    REQUIRE(tree.find(workbench(u8"last"))->accessibility().name.starts_with(u8"표 정렬"));
}

TEST_CASE("A header press sorts and a second press reverses", "[demo][workbench]")
{
    demo::workbench_page page {};
    REQUIRE(page.handle(luil::app_message { demo::workbench_sort_intent { u8"size" } }));
    REQUIRE(changes(published(page)).body().model().front().key == u8"trunk/src/main.cpp");
    REQUIRE(page.handle(luil::app_message { demo::workbench_sort_intent { u8"size" } }));
    REQUIRE(changes(published(page)).body().model().front().key == u8"trunk/src/ui/table.cpp");
}

TEST_CASE("A row menu acts on the row it opened on", "[demo][workbench]")
{
    demo::workbench_page page {};
    REQUIRE(page.handle(luil::app_message { demo::workbench_menu_intent { u8"trunk/CMakeLists.txt", 10.0f, 20.0f } }));
    const std::vector<luil::win32::ui_popup> popups { page.make_popups(1.0f) };
    REQUIRE(popups.size() == 1u);
    REQUIRE(popups.front().tree->duplicate_ids().empty());
    REQUIRE(page.handle(luil::app_message { demo::workbench_menu_select_intent { u8"stage" } }));
    REQUIRE(page.make_popups(1.0f).empty());
    REQUIRE(published(page).find(workbench(u8"commit-count"))->accessibility().name == u8"3");
}
