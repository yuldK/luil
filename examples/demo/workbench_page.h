#pragma once

// 변경 작업대 페이지다: 저장소 계층(펼치면 불러오는 지연 로딩), 변경 표(체크·정렬·여럿 고르기·행 메뉴),
// 커밋 양식(라벨 열이 가장 긴 라벨 폭인 grid)을 한 화면에 모은다.
//
// **이 페이지에는 `ui_element`를 상속한 것이 없다.** 그리기·hit test·접근성을 재정의한 커스텀 element 없이
// 라이브러리 부품만으로 버전 관리 도구의 한 화면을 조립하는 것이 이 페이지의 목적이다 (ui-gap-handoff.md C8).
//  - 아직 없는 부품의 자리는 비워 두었다: 변경 내용 보기는 문서 보기(Codex X2), 작성자 칸은 편집형 콤보(X6),
//    행 메뉴의 하위 메뉴는 X4, 패치 불러오기는 파일 대화상자(X7)가 들어오면 그 자리에 선다.

#include "demo/common.h"
#include "luil/text/text_edit.h"
#include "luil/ui/item_model.h"
#include "luil/ui/selection_model.h"
#include "luil/ui/table_element.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/ui_interaction.h"
#include "luil/win32/app_host.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace demo {
    constexpr luil::ui_element_kind kind_workbench { luil::application_element_kind(39) };
    constexpr luil::ui_element_kind kind_workbench_summary_input { luil::application_element_kind(40) };

    // --- 저장소 계층의 메시지 ---
    struct workbench_tree_expand_intent
    {
        std::u8string key {};
        bool expanded { false };
    };

    struct workbench_tree_cursor_intent
    {
        std::u8string key {};
    };

    // Enter·Space다. 실패 줄이면 다시 묻는다.
    struct workbench_tree_activate_intent
    {
        std::u8string key {};
    };

    // 실패 줄의 "다시 시도"다 (노드 키).
    struct workbench_tree_retry_intent
    {
        std::u8string key {};
    };

    struct workbench_tree_scroll_intent
    {
        float delta { 0.0f };
    };

    // --- 변경 표의 메시지 ---
    struct workbench_change_pick_intent
    {
        std::u8string key {};
        luil::ui_action_context context {};
    };

    struct workbench_change_move_intent
    {
        std::u8string key {};
        luil::virtual_list_move_context context {};
    };

    struct workbench_change_toggle_intent
    {
        std::u8string key {};
    };

    struct workbench_change_select_all_intent
    {};

    // 체크 칸을 눌렀다 (그 행의 스테이지를 뒤집는다).
    struct workbench_change_check_intent
    {
        std::u8string key {};
    };

    struct workbench_sort_intent
    {
        std::u8string column {};
    };

    struct workbench_resize_intent
    {
        std::u8string column {};
        float delta { 0.0f };
    };

    struct workbench_change_scroll_intent
    {
        float delta { 0.0f };
    };

    struct workbench_change_scroll_x_intent
    {
        float delta { 0.0f };
    };

    // 행을 우클릭했다. 메뉴는 그 자리에 선다 (물리 픽셀).
    struct workbench_menu_intent
    {
        std::u8string key {};
        float x { 0.0f };
        float y { 0.0f };
    };

    struct workbench_menu_close_intent
    {};

    struct workbench_menu_select_intent
    {
        std::u8string command {};
    };

    // --- 양식의 메시지 ---
    // 전체 체크다. 일부만 스테이지했으면 모두 스테이지한다.
    struct workbench_stage_all_intent
    {};

    struct workbench_commit_intent
    {};

    class workbench_page
    {
    public:
        workbench_page();

        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);
        [[nodiscard]] std::vector<luil::win32::ui_popup> make_popups(float scale) const;
        void close_popups() noexcept;

        // 흉내 낸 불러오기가 끝나는 가장 이른 시각이다. 없으면 기다릴 것이 없다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_due() const;
        // 그 시각까지 끝난 불러오기를 들인다.
        void tick(std::chrono::steady_clock::time_point now);

    private:
        // 변경 하나다 (앱의 모델).
        struct change
        {
            std::u8string path {};
            std::u8string status {};
            int size { 0 };
            bool staged { false };
        };

        // 묻는 중인 폴더 하나다. 끝나는 시각에 결과가 온 것처럼 들인다.
        struct pending_load
        {
            std::u8string key {};
            luil::request_token token {};
            std::chrono::steady_clock::time_point due {};
        };

        // 펼쳤지만 묻지 않은 폴더에 요청을 낸다.
        void start_loads(std::chrono::steady_clock::time_point now);
        [[nodiscard]] luil::tree_node* find_node(const std::u8string& key);
        // 정렬한 보기의 변경 항목이다 (표의 행 모델).
        [[nodiscard]] std::vector<luil::virtual_list_item> change_items() const;
        [[nodiscard]] std::vector<change>::iterator find_change(const std::u8string& key);
        // 모델이 바뀐 뒤 선택을 새 보기에 맞춘다.
        void reconcile(const std::vector<luil::virtual_list_item>& previous);
        // 선택한 행들(없으면 커서 행)의 스테이지를 정한다.
        void stage_selection(bool staged);

        [[nodiscard]] std::unique_ptr<luil::ui_element> make_header(float scale);
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_tree_panel();
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_changes_table();
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_form();

        // 저장소 계층의 앱 상태다.
        std::vector<luil::tree_node> repository_ {};
        std::unordered_set<std::u8string> expanded_ {};
        std::u8string tree_cursor_ {};
        float tree_scroll_ { 0.0f };
        luil::request_ledger loads_ {};
        std::vector<pending_load> pending_ {};
        // 보관함은 처음 한 번 실패한다 (실패 줄과 다시 시도의 시연).
        int archive_attempts_ { 0 };

        // 변경 표의 앱 상태다.
        std::vector<change> changes_ {};
        std::vector<luil::table_column> columns_ {};
        std::u8string sort_column_ { u8"path" };
        luil::sort_direction sort_direction_ { luil::sort_direction::ascending };
        luil::item_selection selection_ {};
        float changes_scroll_ { 0.0f };
        float changes_scroll_x_ { 0.0f };
        bool menu_open_ { false };
        float menu_x_ { 0.0f };
        float menu_y_ { 0.0f };

        // 양식의 앱 상태다.
        luil::text::text_edit_state summary_ {};
        std::optional<luil::text_composition_event> composition_ {};
        std::u8string last_commit_ {};
    };
} // namespace demo
