#pragma once

// 파일 페이지: 여러 항목을 함께 고르는 가상 목록과, 고른 것을 끌어 폴더에 놓기·끌어서 범위 고르기·파일 클립보드.
//
// 라이브러리가 맡는 것과 앱이 맡는 것이 갈리는 자리를 한 화면에 모았다.
//  - 고름은 앱 상태다. 목록은 누름의 수식 키(`pick`)와 보조 기술의 더하기·빼기를 메시지로 줄 뿐이고, Ctrl은 하나
//    더, Shift는 범위라는 규칙은 이 페이지의 `handle`이 정한다.
//  - 끌기는 고른 것 모두를 싣는다 (`drag_payload::dragged_owners`). 놓을 수 있는 것은 폴더 행뿐이고, 끌리는 폴더
//    자신에는 놓지 못한다. 놓는 순간의 Ctrl이 복사다.
//  - 행 왼쪽의 손잡이를 끌면 범위를 고른다. 목록 가장자리에 대면 목록이 흘러 범위가 이어서 늘어난다.
//  - 복사·잘라내기는 고른 항목의 경로를 OS 클립보드에 넣는다. 탐색기에서 복사한 파일도 붙여 넣어 읽는다.
//    데모는 파일을 실제로 옮기지 않는다 — 경로만 주고받는다.
//  - 아래 감시 구역만은 실제 임시 폴더에 파일을 만들고 고치고 지우고 이름을 바꾼다. 그 폴더를 `file_watcher`가 보고,
//    마지막으로 받은 묶음을 label에 그린다 (file-watch-design.md의 "확인").

#include "demo/common.h"

#include "luil/app/file_watcher.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/virtual_list_element.h"
#include "luil/win32/app_host.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace demo {
    constexpr luil::ui_element_kind kind_files_grip { luil::application_element_kind(60) };
    constexpr luil::ui_element_kind kind_files_button { luil::application_element_kind(61) };
    inline constexpr std::u8string_view page_files { u8"files" };

    // 행을 눌렀다 (수식 키와 함께).
    struct files_pick_intent
    {
        std::u8string key {};
        bool control { false };
        bool shift { false };
    };
    // 보조 기술이 고른 것에 더하라·빼라고 했다.
    struct files_add_intent
    {
        std::u8string key {};
    };
    struct files_remove_intent
    {
        std::u8string key {};
    };
    struct files_cursor_intent
    {
        std::u8string key {};
    };
    struct files_scroll_intent
    {
        float delta { 0.0f };
    };
    struct files_scroll_to_intent
    {
        float offset { 0.0f };
    };
    // 끌린 항목들을 폴더에 놓았다. `copy`면 Ctrl을 누른 채 놓았다.
    struct files_drop_intent
    {
        std::u8string list {};
        std::u8string folder {};
        std::vector<std::u8string> items {};
        bool copy { false };
    };
    // 손잡이로 범위를 고른다. y는 표면 좌표(물리 픽셀)다.
    struct files_range_start_intent
    {
        std::u8string key {};
        float y { 0.0f };
    };
    struct files_range_move_intent
    {
        float y { 0.0f };
    };
    struct files_range_end_intent
    {
    };
    // 클립보드에서 읽은 파일 목록과 클립보드의 변화다 (delegate가 보낸다).
    struct files_clipboard_read_intent
    {
        luil::file_clipboard_content content {};
    };
    struct files_clipboard_changed_intent
    {
        luil::clipboard_change change {};
    };
    // 잘라 넣었다. 다음 클립보드 알림의 번호가 잘라 둔 표시의 번호다.
    struct files_cut_intent
    {
    };
    // 옮긴 뒤 비우기를 청했다. 비울지는 UI thread가 클립보드 번호로 정한다.
    struct files_clear_intent
    {
    };

    // 감시 구역의 단추다. 감시 폴더의 파일 하나를 바꾼다.
    enum class files_watch_operation
    {
        create,
        modify,
        remove,
        rename,
    };
    struct files_watch_intent
    {
        files_watch_operation operation { files_watch_operation::create };
    };
    // 감시자가 낸 묶음이다 (감시 thread의 `deliver`가 보낸다).
    struct files_watch_batch_intent
    {
        luil::file_change_batch batch {};
    };

    // 묶음 하나를 한 줄로 적는다 ("변경: created a.txt, renamed a.txt→b.txt, rescan"). UIA Name으로 되읽는 글이다.
    [[nodiscard]] std::u8string describe_file_change_batch(const luil::file_change_batch& batch);

    class files_page
    {
    public:
        files_page();
        files_page(const files_page&) = delete;
        files_page& operator=(const files_page&) = delete;
        // 감시자를 멈추고 임시 폴더를 지운다.
        ~files_page();

        bool handle(const luil::app_message& message);
        // 창과 host가 준비된 직후 UI thread가 넘긴다. 감시자의 `deliver`가 묶음을 logic thread로 나를 길이다.
        void bind_host(luil::win32::app_host& host) noexcept;
        // 종료 3단계다 (UI thread). 이 뒤로 `deliver`가 불리지 않는다.
        void stop_watcher() noexcept;
        // width·height는 내용 영역의 논리 픽셀 크기다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

    private:
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_files_list();
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_archive_list();
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_buttons() const;
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_watch_buttons() const;
        // 처음 누른 단추에서 감시자를 세우고 폴더를 건다. 파일을 바꾸기 전에 걸어야 그 변경을 놓치지 않는다.
        void ensure_watcher();
        void run_watch_operation(files_watch_operation operation);
        [[nodiscard]] std::optional<std::size_t> index_of(const std::u8string& key) const;
        [[nodiscard]] std::vector<std::u8string> selected_paths() const;
        void select_range(std::size_t from, std::size_t to);
        [[nodiscard]] float clamp(float offset) const;

        std::vector<luil::virtual_list_item> items_ {};
        // 고른 항목의 키다. 고른 차례가 아니라 모델 차례로 둔다.
        std::vector<std::u8string> selection_ {};
        std::u8string cursor_ {};
        // Shift+클릭 범위의 시작이다 (마지막으로 그냥 누르거나 Ctrl로 누른 행).
        std::u8string anchor_ {};
        float scroll_ { 0.0f };
        float list_height_ { 300.0f };
        float scale_ { 1.0f };

        // 손잡이로 범위를 고르는 중이다.
        struct range_drag
        {
            std::size_t anchor { 0 };
            float press_y { 0.0f };
            float press_scroll { 0.0f };
        };
        std::optional<range_drag> range_ {};

        // 잘라 둔 항목이다. 클립보드가 바뀌면 거둔다.
        std::vector<std::u8string> cut_keys_ {};
        bool cut_pending_ { false };
        std::optional<std::uint32_t> cut_sequence_ {};
        // 마지막으로 붙여 넣은 것을 읽은 순간의 클립보드 번호다. "옮긴 뒤 비우기"가 이 번호일 때만 비운다.
        std::optional<std::uint32_t> pasted_sequence_ {};

        std::u8string last_action_ { u8"아직 없음" };
        std::u8string clipboard_text_ { u8"아직 읽지 않음" };

        // 감시 구역이다. 감시자는 logic thread가 쥐고, UI thread의 종료 단계는 원자적 손잡이로만 본다
        // (demo_main.cpp의 `live_client_`와 같은 이유다).
        std::atomic<luil::win32::app_host*> host_ { nullptr };
        std::unique_ptr<luil::file_watcher> watcher_ {};
        std::atomic<luil::file_watcher*> live_watcher_ { nullptr };
        std::filesystem::path watch_folder_ {};
        luil::file_watch_ticket watch_ticket_ {};
        std::u8string watch_state_ { u8"감시: 꺼짐 (단추를 누르면 임시 폴더를 건다)" };
        std::u8string watch_text_ { u8"변경: 아직 없음" };
    };
}
