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

#include "demo/common.h"

#include "luil/ui/ui_events.h"
#include "luil/ui/virtual_list_element.h"

#include <cstddef>
#include <cstdint>
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
    {};
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
    {};
    // 옮긴 뒤 비우기를 청했다. 비울지는 UI thread가 클립보드 번호로 정한다.
    struct files_clear_intent
    {};

    class files_page
    {
    public:
        files_page();

        bool handle(const luil::app_message& message);
        // width·height는 내용 영역의 논리 픽셀 크기다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

    private:
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_files_list();
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_archive_list();
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_buttons() const;
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
    };
} // namespace demo
