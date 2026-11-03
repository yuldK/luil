#include "demo/workbench_page.h"

#include "luil/ui/grid_element.h"
#include "luil/ui/table_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/virtual_list_element.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
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

    // 실행 취소 기록의 상태 글이다. UIA가 되읽는 이름과 같다.
    [[nodiscard]] std::u8string history_of(demo::workbench_page& page)
    {
        return published(page).find(workbench(u8"workbench-history"))->accessibility().name;
    }

    [[nodiscard]] bool enabled(demo::workbench_page& page, const std::u8string& owner)
    {
        // 찾은 요소는 tree의 것이라 tree를 살려 둔 채 읽는다.
        const luil::ui_tree tree { published(page) };
        const luil::ui_element* const button { tree.find(workbench(owner)) };
        REQUIRE(button != nullptr);
        return button->enabled();
    }

    [[nodiscard]] float column_width(demo::workbench_page& page, const std::u8string_view key)
    {
        const luil::ui_tree tree { published(page) };
        for (const luil::table_column& column : changes(tree).config().columns)
            if (column.key == key)
                return column.width;
        FAIL("no such column");
        return 0.0f;
    }

    // 표 행이 보이는 차례의 키다.
    [[nodiscard]] std::vector<std::u8string> order_of(demo::workbench_page& page)
    {
        const luil::ui_tree tree { published(page) };
        std::vector<std::u8string> keys {};
        for (const luil::virtual_list_item& item : changes(tree).body().model())
            keys.push_back(item.key);
        return keys;
    }

    // 표의 체크 칸이 그리는 스테이지 상태를 행 순서대로 읽는다.
    [[nodiscard]] std::vector<bool> staged_of(demo::workbench_page& page)
    {
        const luil::ui_tree tree { published(page) };
        const luil::table_element& table { changes(tree) };
        std::vector<bool> staged {};
        std::size_t index { 0 };
        for (const luil::virtual_list_item& item : table.body().model())
            staged.push_back(table.config().check_state_of(item, index++) == luil::check_state::checked);
        return staged;
    }

    // 요소를 눌러 나온 메시지를 페이지에 돌려준다. 실제 입력 경로를 그대로 탄다. 돌려준 수를 돌려준다.
    int press_element(demo::workbench_page& page, const luil::ui_element_id& id)
    {
        const auto tree { std::make_shared<const luil::ui_tree>(published(page)) };
        const luil::ui_element* const button { tree->find(id) };
        REQUIRE(button != nullptr);
        const luil::rect_f box { button->bounds() };
        const float x { box.x + box.width / 2.0f };
        const float y { box.y + box.height / 2.0f };
        using time_point = std::chrono::steady_clock::time_point;
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        static_cast<void>(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, time_point {} }));
        int delivered { 0 };
        for (const luil::input_action& action : controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, time_point {} + std::chrono::milliseconds { 40 } }))
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
            {
                REQUIRE(page.handle(*message));
                ++delivered;
            }
        return delivered;
    }

    // 단추를 눌러 나온 메시지를 페이지에 돌려준다.
    int press(demo::workbench_page& page, const std::u8string& owner)
    {
        return press_element(page, workbench(owner));
    }

    // 경로 열 고정의 상태 글이다. UIA가 되읽는 이름과 같다.
    [[nodiscard]] std::u8string freeze_state_of(demo::workbench_page& page)
    {
        return published(page).find(workbench(u8"workbench-freeze-state"))->accessibility().name;
    }

    [[nodiscard]] luil::ui_element_id freeze_toggle()
    {
        return { luil::ui_element_kind::check, u8"workbench-freeze" };
    }

    void discard_row(demo::workbench_page& page, const std::u8string& key)
    {
        REQUIRE(page.handle(luil::app_message { demo::workbench_menu_intent { key, 10.0f, 20.0f } }));
        REQUIRE(page.handle(luil::app_message { demo::workbench_menu_select_intent { u8"discard" } }));
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

TEST_CASE("Undoing and redoing check, stage, stage all, discard and column width brings back each state", "[demo][workbench][undo]")
{
    demo::workbench_page page {};
    const std::vector<std::u8string> first_order { order_of(page) };
    const std::vector<bool> first_staged { staged_of(page) };
    const float first_width { column_width(page, u8"path") };
    REQUIRE(history_of(page) == u8"되돌리기 0 · 다시 실행 0 · 다음: 없음 · 저장 지점 일치");
    REQUIRE(enabled(page, u8"workbench-undo") == false);
    REQUIRE(enabled(page, u8"workbench-redo") == false);

    // 체크 칸 하나, 전체 체크, 한 행 버리기, 열 폭을 차례로 한다.
    REQUIRE(page.handle(luil::app_message { demo::workbench_change_check_intent { u8"trunk/src/ui/list.cpp" } }));
    const std::vector<bool> after_check { staged_of(page) };
    REQUIRE(after_check != first_staged);
    REQUIRE(history_of(page) == u8"되돌리기 1 · 다시 실행 0 · 다음: 스테이지 바꿈 · 저장 지점 다름");
    REQUIRE(page.handle(luil::app_message { demo::workbench_stage_all_intent {} }));
    const std::vector<bool> after_all { staged_of(page) };
    REQUIRE(std::count(after_all.begin(), after_all.end(), true) == 45);
    REQUIRE(published(page).find(workbench(u8"commit-count"))->accessibility().name == u8"45");
    discard_row(page, u8"trunk/CMakeLists.txt");
    const std::vector<std::u8string> after_discard { order_of(page) };
    REQUIRE(after_discard.size() == 44u);
    REQUIRE(page.handle(luil::app_message { demo::workbench_resize_intent { u8"path", 50.0f } }));
    REQUIRE(column_width(page, u8"path") == first_width + 50.0f);
    REQUIRE(history_of(page) == u8"되돌리기 4 · 다시 실행 0 · 다음: 열 폭 조절 · 저장 지점 다름");

    // 거꾸로 되돌린다. 단계마다 그 단계 앞의 상태다.
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(column_width(page, u8"path") == first_width);
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    // 버린 행이 같은 자리에 돌아온다.
    REQUIRE(order_of(page) == first_order);
    REQUIRE(staged_of(page) == after_all);
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(staged_of(page) == after_check);
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(staged_of(page) == first_staged);
    REQUIRE(order_of(page) == first_order);
    REQUIRE(history_of(page) == u8"되돌리기 0 · 다시 실행 4 · 다음: 없음 · 저장 지점 일치");
    REQUIRE(enabled(page, u8"workbench-undo") == false);
    REQUIRE(enabled(page, u8"workbench-redo"));
    // 끝에서 더 되돌려도 아무것도 바뀌지 않는다.
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(history_of(page) == u8"되돌리기 0 · 다시 실행 4 · 다음: 없음 · 저장 지점 일치");

    // 다시 실행하면 끝 상태로 간다.
    for (int step { 0 }; step < 4; ++step)
        REQUIRE(page.handle(luil::app_message { demo::workbench_redo_intent {} }));
    // 버린 행 하나가 빠진 채 모두 스테이지한 상태다.
    const std::vector<bool> staged_at_end { staged_of(page) };
    REQUIRE(staged_at_end.size() == 44u);
    REQUIRE(std::count(staged_at_end.begin(), staged_at_end.end(), true) == 44);
    REQUIRE(order_of(page) == after_discard);
    REQUIRE(column_width(page, u8"path") == first_width + 50.0f);
    REQUIRE(history_of(page) == u8"되돌리기 4 · 다시 실행 0 · 다음: 열 폭 조절 · 저장 지점 다름");
}

TEST_CASE("The row menu stage and unstage are steps that undo to the earlier flags", "[demo][workbench][undo]")
{
    demo::workbench_page page {};
    const std::vector<bool> first_staged { staged_of(page) };
    // 이미 스테이지한 행에 "스테이지"를 해도 바뀌는 것이 없어 걸음이 없다.
    REQUIRE(page.handle(luil::app_message { demo::workbench_menu_intent { u8"trunk/src/ui/table.cpp", 10.0f, 20.0f } }));
    REQUIRE(page.handle(luil::app_message { demo::workbench_menu_select_intent { u8"stage" } }));
    REQUIRE(history_of(page).starts_with(u8"되돌리기 0 "));

    REQUIRE(page.handle(luil::app_message { demo::workbench_menu_intent { u8"trunk/src/ui/table.cpp", 10.0f, 20.0f } }));
    REQUIRE(page.handle(luil::app_message { demo::workbench_menu_select_intent { u8"unstage" } }));
    REQUIRE(history_of(page) == u8"되돌리기 1 · 다시 실행 0 · 다음: 스테이지 취소 · 저장 지점 다름");
    REQUIRE(staged_of(page) != first_staged);
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(staged_of(page) == first_staged);
}

TEST_CASE("A new action after an undo clears the redo side", "[demo][workbench][undo]")
{
    demo::workbench_page page {};
    REQUIRE(page.handle(luil::app_message { demo::workbench_change_check_intent { u8"trunk/src/ui/list.cpp" } }));
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(enabled(page, u8"workbench-redo"));
    REQUIRE(page.handle(luil::app_message { demo::workbench_stage_all_intent {} }));
    REQUIRE(enabled(page, u8"workbench-redo") == false);
    REQUIRE(history_of(page) == u8"되돌리기 1 · 다시 실행 0 · 다음: 모두 스테이지 · 저장 지점 다름");
}

TEST_CASE("Consecutive width changes of one column are one step until the drag ends", "[demo][workbench][undo]")
{
    demo::workbench_page page {};
    const float first_width { column_width(page, u8"path") };
    REQUIRE(page.handle(luil::app_message { demo::workbench_resize_intent { u8"path", 10.0f } }));
    REQUIRE(page.handle(luil::app_message { demo::workbench_resize_intent { u8"path", 10.0f } }));
    REQUIRE(page.handle(luil::app_message { demo::workbench_resize_intent { u8"path", 10.0f } }));
    REQUIRE(column_width(page, u8"path") == first_width + 30.0f);
    REQUIRE(history_of(page).starts_with(u8"되돌리기 1 "));

    // 다른 열의 조절은 새 걸음이다.
    REQUIRE(page.handle(luil::app_message { demo::workbench_resize_intent { u8"status", 5.0f } }));
    REQUIRE(history_of(page).starts_with(u8"되돌리기 2 "));
    // 열 경계를 놓으면 같은 열의 다음 조절도 새 걸음이다.
    REQUIRE(page.handle(luil::app_message { demo::workbench_resize_intent { u8"path", 10.0f } }));
    REQUIRE(page.handle(luil::app_message { demo::workbench_resize_end_intent { u8"path" } }));
    REQUIRE(page.handle(luil::app_message { demo::workbench_resize_intent { u8"path", 10.0f } }));
    REQUIRE(history_of(page).starts_with(u8"되돌리기 4 "));

    // 걸음 하나가 끌기 하나를 되돌린다.
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(column_width(page, u8"path") == first_width + 40.0f);
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(column_width(page, u8"path") == first_width + 30.0f);
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(column_width(page, u8"path") == first_width);
    REQUIRE(column_width(page, u8"status") == 90.0f);
}

TEST_CASE("A width change that does not move the edge makes no step", "[demo][workbench][undo]")
{
    demo::workbench_page page {};
    REQUIRE(page.handle(luil::app_message { demo::workbench_resize_intent { u8"path", -1000.0f } }));
    REQUIRE(column_width(page, u8"path") == 120.0f);
    REQUIRE(page.handle(luil::app_message { demo::workbench_resize_end_intent { u8"path" } }));
    // 이미 최소 폭이라 더 줄일 수 없다.
    REQUIRE(page.handle(luil::app_message { demo::workbench_resize_intent { u8"path", -50.0f } }));
    REQUIRE(history_of(page).starts_with(u8"되돌리기 1 "));
}

TEST_CASE("Committing clears the undo stack because the committed rows are gone", "[demo][workbench][undo]")
{
    demo::workbench_page page {};
    REQUIRE(page.handle(luil::app_message { demo::workbench_change_check_intent { u8"trunk/src/ui/list.cpp" } }));
    REQUIRE(page.handle(summary_text(u8"표 정렬")));
    REQUIRE(page.handle(luil::app_message { demo::workbench_commit_intent {} }));
    REQUIRE(history_of(page) == u8"되돌리기 0 · 다시 실행 0 · 다음: 없음 · 저장 지점 일치");
    REQUIRE(enabled(page, u8"workbench-undo") == false);
    REQUIRE(enabled(page, u8"workbench-redo") == false);
}

TEST_CASE("The save point button marks the place and undo and redo move away from it and back", "[demo][workbench][undo]")
{
    demo::workbench_page page {};
    REQUIRE(page.handle(luil::app_message { demo::workbench_change_check_intent { u8"trunk/src/ui/list.cpp" } }));
    REQUIRE(history_of(page).ends_with(u8"저장 지점 다름"));
    REQUIRE(press(page, u8"workbench-mark-saved") == 1);
    REQUIRE(history_of(page).ends_with(u8"저장 지점 일치"));
    REQUIRE(page.handle(luil::app_message { demo::workbench_stage_all_intent {} }));
    REQUIRE(history_of(page).ends_with(u8"저장 지점 다름"));
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(history_of(page).ends_with(u8"저장 지점 일치"));
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(history_of(page).ends_with(u8"저장 지점 다름"));
}

TEST_CASE("The undo and redo buttons send the same requests as the keys and are off when there is nothing to do", "[demo][workbench][undo]")
{
    demo::workbench_page page {};
    // 꺼진 단추는 눌러도 메시지가 없다. 켜 둔 채 빈 동작을 내면 보조 기술의 Invoke가 오류로 끝난다.
    REQUIRE(press(page, u8"workbench-undo") == 0);
    REQUIRE(press(page, u8"workbench-redo") == 0);

    REQUIRE(page.handle(luil::app_message { demo::workbench_change_check_intent { u8"trunk/src/ui/list.cpp" } }));
    const std::vector<bool> staged { staged_of(page) };
    REQUIRE(press(page, u8"workbench-redo") == 0);
    REQUIRE(press(page, u8"workbench-undo") == 1);
    REQUIRE(staged_of(page) != staged);
    REQUIRE(enabled(page, u8"workbench-undo") == false);
    REQUIRE(press(page, u8"workbench-redo") == 1);
    REQUIRE(staged_of(page) == staged);
}

TEST_CASE("The undo state names the next step for the palette and the menu", "[demo][workbench][undo]")
{
    demo::workbench_page page {};
    luil::undo_command_state state { page.undo_state() };
    REQUIRE(state.undo_label == u8"실행 취소");
    REQUIRE(state.redo_label == u8"다시 실행");
    REQUIRE(state.can_undo == false);
    REQUIRE(state.can_redo == false);

    REQUIRE(page.handle(luil::app_message { demo::workbench_change_check_intent { u8"trunk/src/ui/list.cpp" } }));
    state = page.undo_state();
    REQUIRE(state.undo_label == u8"실행 취소: 스테이지 바꿈");
    REQUIRE(state.can_undo);
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    state = page.undo_state();
    REQUIRE(state.redo_label == u8"다시 실행: 스테이지 바꿈");
    REQUIRE(state.can_redo);
    REQUIRE(state.can_undo == false);

    // 단추의 툴팁은 같은 명령표의 글과 단축키다.
    REQUIRE(published(page).find(workbench(u8"workbench-redo"))->tooltip() == u8"다시 실행: 스테이지 바꿈 (Ctrl+Y)");
}

TEST_CASE("The freeze toggle pins the path column as a view state and makes no undo step", "[demo][workbench][frozen]")
{
    demo::workbench_page page {};
    const std::u8string before { history_of(page) };
    {
        const luil::ui_tree tree { published(page) };
        REQUIRE(tree.duplicate_ids().empty());
        REQUIRE(tree.unarranged().empty());
        REQUIRE(changes(tree).config().frozen_columns == 0u);
        REQUIRE(changes(tree).config().columns.front().key == u8"path");
        REQUIRE(tree.find(freeze_toggle())->accessibility().checked == false);
    }
    REQUIRE(freeze_state_of(page) == u8"고정 열 없음");

    // 실제 입력 경로로 켠다. 상태 글이 바뀌고 표가 앞 한 열을 고정한다.
    REQUIRE(press_element(page, freeze_toggle()) == 1);
    {
        const luil::ui_tree tree { published(page) };
        REQUIRE(tree.duplicate_ids().empty());
        REQUIRE(changes(tree).config().frozen_columns == 1u);
        REQUIRE(tree.find(freeze_toggle())->accessibility().checked == true);
    }
    REQUIRE(freeze_state_of(page) == u8"고정 열 1: 경로");

    // 보기 상태라 실행 취소 걸음이 늘지 않는다.
    REQUIRE(history_of(page) == before);
    REQUIRE(enabled(page, u8"workbench-undo") == false);

    REQUIRE(press_element(page, freeze_toggle()) == 1);
    REQUIRE(freeze_state_of(page) == u8"고정 열 없음");
    REQUIRE(history_of(page) == before);
}

TEST_CASE("A frozen path column stays while the others flow and its width drag is still one undo step", "[demo][workbench][frozen][undo]")
{
    demo::workbench_page page {};
    REQUIRE(page.handle(luil::app_message { demo::workbench_freeze_intent {} }));
    REQUIRE(page.handle(luil::app_message { demo::workbench_change_scroll_x_intent { 60.0f } }));
    const std::u8string header_owner { u8"workbench-changes" };
    const auto header_cell = [&header_owner](const std::u8string& key) { return luil::ui_element_id { luil::ui_element_kind::table_header_cell, header_owner + key }; };
    const auto grip = [&header_owner](const std::u8string& key) { return luil::ui_element_id { luil::ui_element_kind::table_column_grip, header_owner + key }; };

    const auto tree { std::make_shared<const luil::ui_tree>(published(page)) };
    const luil::table_element& table { changes(*tree) };
    // 열 합(830)이 표보다 넓어 흐르고, 창이 넓어 고정이 선다.
    REQUIRE(table.content_width() > table.viewport_width());
    REQUIRE(table.frozen_columns() == 1u);
    REQUIRE(table.scroll_x() == 60.0f);
    const float edge { tree->find(header_cell(u8"path"))->bounds().x };
    const float status_edge { tree->find(header_cell(u8"status"))->bounds().x };
    // 경로 열은 제자리이고(스크롤 0일 때의 자리) 나머지는 60만큼 밀렸다.
    REQUIRE(status_edge - edge == 340.0f - 60.0f);

    // 고정된 경로 열의 경계를 끌어 넓히면 폭이 바뀌고 끌기 하나가 걸음 하나다.
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    const luil::rect_f box { tree->find(grip(u8"path"))->bounds() };
    const float x { box.x + box.width / 2.0f };
    const float y { box.y + box.height / 2.0f };
    using time_point = std::chrono::steady_clock::time_point;
    const auto deliver = [&page](const std::vector<luil::input_action>& actions) {
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                REQUIRE(page.handle(*message));
    };
    const float first_width { column_width(page, u8"path") };
    deliver(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, time_point {} }));
    deliver(controller.process(luil::pointer_moved_event { x + 6.0f, y, time_point {} + std::chrono::milliseconds { 10 } }));
    deliver(controller.process(luil::pointer_moved_event { x + 12.0f, y, time_point {} + std::chrono::milliseconds { 20 } }));
    deliver(controller.process(luil::pointer_released_event { x + 12.0f, y, luil::pointer_button::left, time_point {} + std::chrono::milliseconds { 30 } }));
    REQUIRE(column_width(page, u8"path") == first_width + 12.0f);
    REQUIRE(history_of(page).starts_with(u8"되돌리기 1 "));
    REQUIRE(page.handle(luil::app_message { demo::workbench_undo_intent {} }));
    REQUIRE(column_width(page, u8"path") == first_width);
}
