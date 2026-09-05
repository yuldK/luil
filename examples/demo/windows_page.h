#pragma once

// 창 페이지다: 보조 top-level 창(도구 창)을 여닫는 예다.
//  - 열림·닫힘은 앱 상태다: frame의 windows 목록에 실으면 창이 생기고 빼면 사라진다.
//  - 창의 실제 크기·배율은 metrics 메시지로 돌아오고, 페이지는 그 값으로 도구 창 tree를 배치한다.
//  - 도구 창의 캡션에는 **닫기만** 둔다. 버튼이 빠지면 그 창 스타일도 함께
//    빠져 캡션 더블클릭·Win+↑로도 최대화되지 않는다 (caption-button-design.md).
//  - 도구 창 안의 텍스트 박스는 주 창과 같은 경로(초점·편집·IME)로 동작하고,
//    토스트 버튼은 페이지를 넘는 메시지가 창도 넘는 것을 보여 준다.
//  - 도구 창 안의 메뉴는 **앵커가 그 창인 popup**이다: 자리도 그 창 기준이고
//    창 위에 뜨며 창을 옮기면 따라온다 (popup-anchor-design.md).
//  - 그 메뉴의 검색 칸은 **보조 창에 붙은 popup 안의 텍스트 입력**이다.
//    조합도 후보 창도 도구 창의 IME session이 대신 본다 (popup-ime-design.md).
//    그래서 이 popup은 도구 창을 끌거나 주 창을 잠깐 보는 계기에는 **남는다**
//    (`popup_dismiss_reason`을 보고 앱이 가른다).

#include "demo/common.h"
#include "luil/win32/app_host.h"

#include <vector>

namespace demo {
    // --- 이 페이지의 메시지 ---
    struct tool_window_toggle_intent
    {
        bool open { false };
    };

    // 도구 창의 실제 client 크기·배율이다 (생성 직후·크기 조절·DPI 변경).
    struct tool_window_metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 1.0f };
    };

    // 도구 창 안의 메뉴를 여닫는다.
    struct tool_menu_toggle_intent
    {
        bool open { false };
    };

    // 도구 창 메뉴에서 항목을 골랐다.
    // 페이지는 메뉴를 닫고(그리고 닫기 항목이면 창도 닫고), 셸이 토스트로 잇는다.
    struct tool_menu_select_intent
    {
        std::u8string key {};
    };

    // OS에서 도구 창의 메모 칸에 끌어다 놓은 파일이다.
    // 보조 창도 표면이라 주 창과 같은 길로 받는다 (os-dragdrop-design.md).
    struct tool_note_file_intent
    {
        std::u8string path {};
    };

    class windows_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 열려 있는 도구 창을 frame의 windows 목록으로 만든다.
        // 도구 창은 페이지를 옮겨도 열려 있다 — 독립된 창이라 페이지의 일부가 아니다.
        [[nodiscard]] std::vector<luil::win32::ui_window> make_windows();

        // 도구 창 안에 떠 있는 메뉴를 popup으로 만든다.
        // 앵커가 도구 창이라 자리·소유자·배율이 전부 그 창을 따른다.
        // 같은 frame에서 make_windows가 만든 메뉴 버튼의 자리를 쓰므로 그 뒤에 불러야 한다.
        [[nodiscard]] std::vector<luil::win32::ui_popup> make_popups() const;

        // 떠 있는 메뉴를 닫는다 (Esc·바깥 클릭).
        void close_popups() noexcept
        {
            close_tool_menu();
        }

    private:
        [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_tool_tree();
        // 메뉴를 닫고 검색 상태도 함께 비운다.
        void close_tool_menu() noexcept;

        bool tool_open_ { false };
        bool tool_menu_open_ { false };
        // 이번 frame이 만든 도구 창의 메뉴 버튼이다.
        // popup의 자리 계산에만 쓰고 frame이 게시되기 전까지만 유효하다.
        const luil::ui_element* tool_menu_button_ { nullptr };
        // 도구 창의 실제 client 크기(물리 픽셀)와 배율이다.
        // 창이 만들어지면 metrics 메시지가 곧바로 채우고, 그 전에는 요청 크기로 배치한다.
        tool_window_metrics_intent tool_metrics_ {};
        luil::text::text_edit_state note_ {};
        // 도구 창 메뉴 안 검색 칸의 상태다.
        // 조합 상태는 창 안 메모 칸과 하나를 같이 쓴다 — target이 구분한다.
        luil::text::text_edit_state menu_search_ {};
        std::optional<luil::text_composition_event> composition_ {};
    };
} // namespace demo
