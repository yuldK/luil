#pragma once

// 변경 작업대 페이지다: 저장소 계층(펼치면 불러오는 지연 로딩), 변경 표(체크·정렬·여럿 고르기·하위 메뉴가 있는 행 메뉴),
// 변경 내용 보기(읽기 전용 문서 칸의 줄 장식·구간 색·검색 강조), 커밋 양식(편집형 콤보의 작성자 칸)을 한 화면에 모은다.
// 패치 파일은 공용 파일 대화상자로 불러와 같은 보기에 띄운다.
//
// **이 페이지에는 `ui_element`를 상속한 것이 없다.** 그리기·hit test·접근성을 재정의한 커스텀 element 없이
// 라이브러리 부품만으로 버전 관리 도구의 한 화면을 조립하는 것이 이 페이지의 목적이다 (ui-gap-handoff.md C8).

#include "demo/common.h"
#include "luil/app/file_dialog.h"
#include "luil/text/text_edit.h"
#include "luil/ui/combo_box_element.h"
#include "luil/ui/item_model.h"
#include "luil/ui/selection_model.h"
#include "luil/ui/table_element.h"
#include "luil/ui/text_area_element.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/ui_interaction.h"
#include "luil/win32/app_host.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace demo {
    constexpr luil::ui_element_kind kind_workbench { luil::application_element_kind(39) };
    constexpr luil::ui_element_kind kind_workbench_summary_input { luil::application_element_kind(40) };
    // 작성자 콤보는 root와 안쪽 입력 칸이 이 kind를 같이 쓴다 (owner만 다르다, combo_box_element.h).
    constexpr luil::ui_element_kind kind_workbench_author_input { luil::application_element_kind(41) };
    constexpr luil::ui_element_kind kind_workbench_diff { luil::application_element_kind(42) };
    constexpr luil::ui_element_kind kind_workbench_find_input { luil::application_element_kind(43) };

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

    // 표의 가로 막대에서 보조 기술이 자리를 정했다 (논리 픽셀, 절대 값).
    struct workbench_change_scroll_x_to_intent
    {
        float offset { 0.0f };
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

    // 행 메뉴의 하위 메뉴를 연다. 빈 키면 닫는다 (열림 경로는 앱 상태다).
    struct workbench_submenu_intent
    {
        std::u8string key {};
    };

    // --- 변경 내용 보기의 메시지 ---
    // 패치 파일을 고르자는 요청이다. 셸이 공용 파일 대화상자 요청으로 바꾼다.
    struct workbench_import_intent
    {};

    // 고른 패치 파일이다. 페이지가 읽어 보기에 띄운다.
    struct workbench_patch_intent
    {
        std::u8string path {};
    };

    // 불러온 패치를 닫고 표의 커서 행으로 돌아간다.
    struct workbench_patch_close_intent
    {};

    struct workbench_diff_scroll_intent
    {
        bool horizontal { false };
        float delta { 0.0f };
    };

    struct workbench_diff_scroll_to_intent
    {
        bool horizontal { false };
        float offset { 0.0f };
    };

    // 찾은 곳 사이를 옮긴다 (1이면 다음, -1이면 이전).
    struct workbench_find_step_intent
    {
        int direction { 1 };
    };

    // --- 양식의 메시지 ---
    // 전체 체크다. 일부만 스테이지했으면 모두 스테이지한다.
    struct workbench_stage_all_intent
    {};

    struct workbench_commit_intent
    {};

    // 작성자 콤보의 메시지다. 후보·열림·강조·스크롤은 전부 앱 상태다 (combo-box-design.md).
    struct workbench_author_open_intent
    {
        bool open { false };
    };

    struct workbench_author_move_intent
    {
        std::u8string key {};
    };

    struct workbench_author_select_intent
    {
        std::u8string key {};
    };

    // 후보 밖의 글을 Enter로 확정했다.
    struct workbench_author_commit_intent
    {
        std::u8string text {};
    };

    // 닫힌 콤보의 Esc다. 마지막으로 확정한 작성자로 되돌린다.
    struct workbench_author_restore_intent
    {};

    struct workbench_author_scroll_intent
    {
        float delta { 0.0f };
    };

    struct workbench_author_scroll_to_intent
    {
        float offset { 0.0f };
    };

    class workbench_page
    {
    public:
        workbench_page();

        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);
        [[nodiscard]] std::vector<luil::win32::ui_popup> make_popups(float scale) const;
        void close_popups() noexcept;

        // 흉내 낸 불러오기(폴더 목록·작성자 후보)가 끝나는 가장 이른 시각이다. 없으면 기다릴 것이 없다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_due() const;
        // 그 시각까지 끝난 불러오기를 들인다.
        void tick(std::chrono::steady_clock::time_point now);
        // 패치 파일 대화상자에 실을 거름틀이다.
        [[nodiscard]] static luil::file_dialog_request patch_dialog_request(std::uint64_t id);

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
        // 보기에 띄울 문서(불러온 패치, 없으면 표의 커서 행)가 바뀌었으면 글·장식을 다시 짓는다.
        void refresh_diff();
        // 찾는 글이 바뀌었으면 찾은 곳을 다시 모으고 참을 돌려준다.
        [[nodiscard]] bool refresh_matches();
        // 지금 찾은 곳으로 caret을 옮기고 보이게 한다.
        void reveal_match();
        void apply_author_edit(const luil::text_edit_request& request);
        // 작성자 후보를 칸의 보이는 글로 새로 묻는다. 앞선 물음의 답은 token이 달라 버려진다.
        void query_authors(std::chrono::steady_clock::time_point now);
        void set_author(std::u8string text);

        [[nodiscard]] std::unique_ptr<luil::ui_element> make_header(float scale);
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_tree_panel();
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_changes_table();
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_diff_panel();
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
        // 행 메뉴에서 연 하위 메뉴의 항목 키다. 비면 닫혀 있다.
        std::u8string submenu_ {};

        // 변경 내용 보기의 앱 상태다. 글은 읽기 전용이고 caret·선택만 움직인다.
        std::u8string diff_source_ {};
        luil::text::text_edit_state diff_ {};
        std::shared_ptr<const std::vector<luil::text_span>> diff_spans_ {};
        std::shared_ptr<const std::vector<luil::line_decoration>> diff_lines_ {};
        std::shared_ptr<luil::text_area_wrap_cache> diff_wrap_ { std::make_shared<luil::text_area_wrap_cache>() };
        float diff_scroll_ { 0.0f };
        float diff_scroll_x_ { 0.0f };
        // 불러온 패치다. 경로가 비면 표의 커서 행을 보인다.
        std::u8string patch_path_ {};
        std::u8string patch_text_ {};
        std::u8string patch_error_ {};
        // 찾는 칸과 찾은 곳이다.
        luil::text::text_edit_state find_ {};
        std::optional<luil::text_composition_event> find_composition_ {};
        std::u8string matched_query_ {};
        // 문서가 바뀌어 같은 글이어도 다시 찾아야 한다.
        bool matches_stale_ { false };
        std::vector<luil::text_highlight> matches_ {};
        std::size_t current_match_ { 0 };

        // 양식의 앱 상태다.
        luil::text::text_edit_state summary_ {};
        std::optional<luil::text_composition_event> composition_ {};
        std::u8string last_commit_ {};

        // 작성자 콤보의 앱 상태다. 후보는 흉내 낸 서버가 늦게 답한다.
        struct pending_authors
        {
            std::uint64_t token { 0 };
            std::u8string query {};
            std::chrono::steady_clock::time_point due {};
        };
        luil::text::text_edit_state author_ {};
        std::optional<luil::text_composition_event> author_composition_ {};
        std::u8string author_committed_ {};
        luil::combo_suggestion_state author_suggestions_ {};
        std::optional<pending_authors> author_pending_ {};
        std::u8string author_highlight_ {};
        std::u8string author_error_ {};
        float author_scroll_ { 0.0f };
        bool author_open_ { false };
        // 이번 frame에 배치한 콤보다. 후보 popup은 그 자리를 따라 선다.
        const luil::combo_box_element* author_combo_ { nullptr };
    };
} // namespace demo
