#pragma once

// 창 페이지다: 보조 top-level 창(도구 창)을 여닫고 주 창을 전체 화면으로 넣는 예다.
//  - 열림·닫힘은 앱 상태다: frame의 windows 목록에 실으면 창이 생기고 빼면 사라진다.
//  - **전체 화면은 앱 상태가 아니다.** 그 단추는 메시지가 아니라 `ui_command` 하나를
//    돌려주고 logic thread를 지나지 않는다. 그래서 이 페이지는 지금 전체 화면인지
//    모르고, 아는 것은 배치 보고를 받아 캡션 줄을 접는 셸이다 (demo_main.cpp).
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
//  - 작업 표시줄 구역은 주 창 단추와 독립 창(`independent`) 단추에 진행률과 오버레이를 거는 예다
//    (taskbar-design.md). 요청은 앱 상태라 `make_taskbar_request`가 frame마다 그대로 싣고, 셸에 닿은 정도는
//    라이브러리가 알리는 보고(`taskbar_report_intent`)로 되읽는다. 세 줄이 UIA로 읽힌다: 능력·요청·반영.
//  - 둘째 인스턴스 구역은 같은 이름의 둘째 실행이 넘긴 인자와 작업 폴더를 한 줄로 보인다 (single-instance-args-design.md).
//    라이브러리가 `window_delegate::on_instance_arguments`로 건넨 것을 셸이 `instance_arguments_intent`로 게시한다.
//  - 점프 목록 줄은 작업 표시줄 단추의 오른쪽 클릭 목록에 작업 둘을 싣는 예다 (jump-list-design.md). 작업은 데모를
//    `--send-to-first`로 띄워 떠 있는 데모의 둘째 인스턴스 줄에 닿는다. 두 줄이 UIA로 읽힌다: 요청·반영.
//  - 알림 영역 구역은 트레이 아이콘을 세우고 그림을 바꾸고 그 사건을 받는 예다 (tray-icon-design.md). 아이콘의 메뉴는
//    시스템 메뉴로 뜨고 `창 보이기`·`주 창 숨기기`·`끝내기`가 있다. 주 창을 숨기는 것은 앱 상태라 `main_hidden`을 셸이
//    frame에 싣는다. 네 줄이 UIA로 읽힌다: 능력·요청·반영·사건.
//  - 전역 단축키 구역은 앱이 뒤에 있어도 받는 키 조합(`Ctrl+Shift+Alt+F11`)을 걸고 그 눌림을 세는 예다 (global-hotkey-design.md).
//    둘째 id를 같은 조합으로 더하는 충돌 시험은 같은 목록의 앞 항목이 이겨 둘째가 충돌로 보고되는 것을 보인다. 눌렸을 때
//    창을 앞으로 세우는 것은 앱 몫이라 `main_hidden`과 활성화 초점 요청을 앱 상태로 싣는다. 네 줄이 UIA로 읽힌다: 능력·요청·반영·눌림.
//  - 본문 전체가 흘리는 영역에 든다 (scroll-area-design.md). 구역을 모두 쌓으면 기본 창(1280 x 720)보다 길어
//    맨 아래 구역이 잘렸다. 영역의 내용 높이는 담긴 줄에서 읽으므로 구역이 줄을 더해도 그대로 흐른다.
//    위쪽에 고정한 줄 `페이지 흘림: <흘린 양> / <흘릴 수 있는 최대>`(논리 픽셀)가 UIA로 읽힌다.

#include "demo/common.h"
#include "luil/app/platform_services.h"
#include "luil/win32/app_host.h"
#include "luil/win32/win32_window.h"

#include <array>
#include <optional>
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

    // 주 창 캡션을 창 위에 둘지 아래에 둘지 고른다.
    // 캡션의 자리는 셸의 배치가 정하고, 끌기 띠·창 버튼·시스템 메뉴의 판정이 그 자리를 따른다
    // (caption-button-design.md의 "caption의 자리").
    struct caption_position_intent
    {
        bool bottom { false };
    };

    // 주 창의 위·왼쪽 끝을 옮겨 크기를 바꾼다 (논리 px, 양수면 커진다). 아래·오른쪽 끝은 그대로다.
    // 셸이 마지막 배치 보고에 더해 배치 요청(`window_placement_request`)으로 낸다. 레이어드 표시
    // (`--presentation=layered`)는 그 크기의 tree와 창 배치를 한 합성에 내므로 아래 캡션이 한 frame도 튀지 않는다
    // (layered-presentation-design.md).
    struct edge_resize_intent
    {
        float left { 0.0f };
        float top { 0.0f };
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

    // 도구 창을 열고(열려 있으면 앞으로 세우고) 그 안의 메모 칸에 초점을 청한다.
    // 칸은 요청하는 frame에 아직 없다 — 창은 UI thread가 다음 대조에서 만든다. 그래서 luil이 요청을
    // 들고 있다가 칸이 서는 순간 이행한다 (deferred-focus-design.md).
    struct tool_focus_intent
    {
    };

    // 플랫폼 도구다 (platform-services-design.md): 데모 실행 파일을 탐색기에서 고른 채로 보이고, 메모장을 띄운다.
    struct reveal_executable_intent
    {
    };

    struct launch_notepad_intent
    {
    };

    // 초점 요청의 결과다 (`ui_frame::focus_report`).
    struct tool_focus_report_intent
    {
        std::uint64_t id { 0 };
        luil::focus_request_outcome outcome { luil::focus_request_outcome::fulfilled };
    };

    // OS에서 도구 창의 메모 칸에 끌어다 놓은 파일이다.
    // 보조 창도 표면이라 주 창과 같은 길로 받는다 (os-dragdrop-design.md).
    struct tool_note_file_intent
    {
        std::u8string path {};
    };

    // 페이지 본문을 흘린다 (논리 픽셀, 양수면 내용이 위로 간다). 휠·막대·초점 되살리기가 같은 메시지를 낸다.
    struct windows_scroll_intent
    {
        float delta { 0.0f };
    };

    // 페이지 본문을 이 자리로 옮긴다 (논리 픽셀). 보조 기술이 자리를 정할 때 온다.
    struct windows_scroll_to_intent
    {
        float offset { 0.0f };
    };

    // --- 작업 표시줄 구역의 메시지 ---
    // 주 창 단추의 진행 상태를 고른다. 값은 따로 정한다 (`taskbar_value_intent`).
    struct taskbar_state_intent
    {
        luil::taskbar_progress_state state { luil::taskbar_progress_state::none };
    };

    // 진행 값(퍼센트, 0..100)이다. 막대를 끌면 변화량이고 `relative`가 거짓이면 그 값이다.
    struct taskbar_value_intent
    {
        float value { 0.0f };
        bool relative { false };
    };

    // 주 창 단추의 오버레이를 켜고 끈다.
    struct taskbar_overlay_intent
    {
        bool on { false };
    };

    // 독립 창을 열고 닫는다. 독립 창은 작업 표시줄에 따로 서므로 단추도 따로 있다.
    struct taskbar_window_toggle_intent
    {
        bool open { false };
    };

    // 독립 창 단추의 진행 상태를 다음 것으로 돌린다 (정상 → 오류 → 일시정지 → 불확정 → 없음).
    struct taskbar_window_step_intent
    {
    };

    // 독립 창의 실제 client 크기·배율이다 (생성 직후·크기 조절·DPI 변경).
    struct taskbar_window_metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 1.0f };
    };

    // 라이브러리가 알린 작업 표시줄 단추의 상태다 (`window_delegate::make_taskbar_message`).
    // 주 창과 독립 창이 같은 메시지로 온다. 어느 단추인지는 `report.surface`다.
    struct taskbar_report_intent
    {
        luil::win32::taskbar_report report {};
    };

    // 점프 목록을 채우거나 비운다. 처음에는 손대지 않는다 (`taskbar_request::jump_list`가 비어 있다).
    struct jump_list_fill_intent
    {
        bool fill { false };
    };

    // 라이브러리가 알린 점프 목록의 보고다 (`window_delegate::make_jump_list_message`).
    struct jump_list_report_intent
    {
        luil::win32::jump_list_report report {};
    };

    // 같은 이름의 둘째 인스턴스가 보낸 인자와 작업 폴더다 (`window_delegate::on_instance_arguments`).
    struct instance_arguments_intent
    {
        luil::instance_arguments arguments {};
    };

    // --- 알림 영역 구역의 메시지 ---
    // 트레이 아이콘을 켜고 끈다. 끄면 숨긴 주 창도 다시 보인다 (아이콘 없이 숨은 창을 되살릴 길이 없다).
    struct tray_toggle_intent
    {
        bool on { false };
    };

    // 아이콘 그림을 다른 색 원으로 바꾼다.
    struct tray_picture_intent
    {
    };

    // 아이콘의 사건이다 (`tray_icon::activate`). 누름·두 번 누름이 온다. 메뉴가 있어 오른쪽 클릭은 오지 않는다.
    struct tray_event_intent
    {
        luil::tray_icon_event event {};
    };

    // 아이콘 메뉴에서 고른 항목이다 (`menu_config::select`). 끝내기는 메시지가 아니라 `ui_command`라 여기 오지 않는다.
    struct tray_menu_intent
    {
        std::u8string key {};
    };

    // 라이브러리가 알린 알림 영역 아이콘의 상태다 (`window_delegate::make_tray_message`).
    struct tray_report_intent
    {
        luil::win32::tray_report report {};
    };

    // --- 전역 단축키 구역의 메시지 ---
    // 전역 단축키를 켜고 끈다. 끄면 충돌 시험도 함께 끈다.
    struct hotkey_toggle_intent
    {
        bool on { false };
    };

    // 같은 조합의 둘째 id를 목록에 더한다. 앞 항목이 이겨 둘째는 충돌로 보고된다. 켜져 있을 때만 받는다.
    struct hotkey_conflict_intent
    {
    };

    // 눌렸을 때 주 창을 앞으로 세울지 정한다.
    struct hotkey_front_intent
    {
        bool on { false };
    };

    // 전역 단축키가 눌렸다 (`global_hotkey::pressed`).
    struct hotkey_pressed_intent
    {
        std::u8string id {};
    };

    // 라이브러리가 알린 전역 단축키 하나의 걸어 본 결과다 (`window_delegate::make_hotkey_message`).
    struct hotkey_report_intent
    {
        luil::win32::hotkey_report report {};
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

        // 주 창 단추에 바라는 진행률과 오버레이, 점프 목록이다. 셸이 매 frame `ui_frame::taskbar`에 그대로 싣는다.
        [[nodiscard]] luil::taskbar_request make_taskbar_request() const;

        // 알림 영역 아이콘이다. 끈 동안은 없다. 셸이 매 frame `ui_frame::tray`에 그대로 싣는다.
        //  - 그림은 페이지가 쥔 것을 frame마다 같이 실어 셸에 다시 내지 않는다. 메뉴와 사건 함수는 매번 새로 짓는다.
        [[nodiscard]] std::optional<luil::tray_icon> make_tray_request() const;

        // 전역 단축키 목록이다. 끈 동안은 비어 있다. 셸이 매 frame `ui_frame::hotkeys`에 그대로 싣는다.
        //  - 눌림 함수는 매번 새로 짓는다. 라이브러리는 id와 조합만 견주고 눌림 함수는 눌릴 때 그 순간의 frame에서 읽는다.
        [[nodiscard]] std::vector<luil::global_hotkey> make_hotkeys_request() const;

        // 트레이 메뉴로 주 창을 숨겼는가다. 셸이 매 frame `ui_frame::main_hidden`에 그대로 싣는다.
        [[nodiscard]] bool main_hidden() const noexcept
        {
            return tray_hidden_;
        }

        // 마지막으로 청한 초점 요청이다: 도구 창 메모 칸, 또는 트레이 메뉴의 `창 보이기`가 청한 주 창 활성화다.
        // 한 번도 청하지 않았으면 없다.
        //  - 같은 번호를 frame마다 실어도 luil은 한 번만 이행한다.
        [[nodiscard]] std::optional<luil::focus_request> make_focus_request() const;

        // 떠 있는 메뉴를 닫는다 (Esc·바깥 클릭).
        void close_popups() noexcept
        {
            close_tool_menu();
        }

        // 주 창 캡션을 창 아래에 둘 것인가다. 셸이 캡션과 내용을 배치할 때 읽는다.
        [[nodiscard]] bool caption_at_bottom() const noexcept
        {
            return caption_bottom_;
        }

        // 캡션을 아래에 두고 시작한다 (`--caption=bottom`).
        void set_caption_at_bottom(const bool bottom) noexcept
        {
            caption_bottom_ = bottom;
        }

        // 주 창이 레이어드 표시인가다. 끝 옮기기 칸의 안내 글이 쓴다 (`--presentation=`).
        void set_layered_presentation(const bool layered) noexcept
        {
            layered_ = layered;
        }

    private:
        // 본문의 줄들을 쌓는다. 구역을 더하는 자리는 여기다.
        [[nodiscard]] std::unique_ptr<luil::stack_element> build_content();
        // 쌓은 본문을 흘리는 영역에 담고 위쪽에 흘림 상태 줄을 붙인다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_scroll_page(std::unique_ptr<luil::stack_element> content, float width, float height, float scale);
        [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_tool_tree();
        [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_taskbar_window_tree();
        // 독립 창 단추에 바라는 진행률이다. 값은 60%로 고정하고 상태만 돌린다.
        [[nodiscard]] luil::taskbar_request make_taskbar_window_request() const;
        // 작업 표시줄 구역을 `column`에 더한다.
        void add_taskbar_section(luil::stack_element& column);
        // 점프 목록 줄을 `column`에 더한다 (작업 표시줄 구역 끝).
        void add_jump_list_section(luil::stack_element& column);
        // 둘째 인스턴스 구역을 `column`에 더한다.
        void add_instance_section(luil::stack_element& column);
        // 알림 영역 구역을 `column`에 더한다 (점프 목록 줄 뒤).
        void add_tray_section(luil::stack_element& column);
        // 전역 단축키 구역을 `column`에 더한다 (알림 영역 구역 뒤, 본문 끝).
        void add_hotkey_section(luil::stack_element& column);
        // 메뉴를 닫고 검색 상태도 함께 비운다.
        void close_tool_menu() noexcept;

        bool tool_open_ { false };
        bool tool_menu_open_ { false };
        bool caption_bottom_ { false };
        bool layered_ { false };
        // 본문이 흘러간 양이다 (논리 픽셀). 창이 커져 흘릴 것이 줄면 `make_scroll_page`가 범위 안으로 되돌린다.
        float scroll_ { 0.0f };
        // 메모 칸 초점 요청의 번호다. 청할 때마다 하나씩 올린다.
        std::uint64_t focus_revision_ { 0 };
        // 마지막 초점 요청이 주 창 활성화(트레이 메뉴의 `창 보이기`)인가다. 아니면 도구 창 메모 칸이다.
        bool focus_main_ { false };
        // 플랫폼 도구를 마지막으로 부른 결과 한 줄이다.
        std::u8string platform_report_ {};
        // 이번 frame이 만든 도구 창의 메뉴 버튼이다.
        // popup의 자리 계산에만 쓰고 frame이 게시되기 전까지만 유효하다.
        const luil::ui_element* tool_menu_button_ { nullptr };
        // 도구 창의 실제 client 크기(물리 픽셀)와 배율이다.
        // 창이 만들어지면 metrics 메시지가 곧바로 채우고, 그 전에는 요청 크기로 배치한다.
        tool_window_metrics_intent tool_metrics_ {};
        // 작업 표시줄 구역의 상태다 (모두 앱 상태다).
        luil::taskbar_progress_state taskbar_state_ { luil::taskbar_progress_state::none };
        float taskbar_percent_ { 42.0f };
        bool taskbar_overlay_on_ { false };
        luil::ui_image taskbar_icon_ {};
        bool taskbar_window_open_ { false };
        // 독립 창 단추의 진행 상태가 돈 순서의 자리다 (0은 없음).
        int taskbar_window_step_ { 0 };
        taskbar_window_metrics_intent taskbar_window_metrics_ {};
        // 마지막으로 받은 보고다. 요청이 기본값이던 단추는 보고가 없다.
        std::optional<luil::win32::taskbar_report> taskbar_report_ {};
        std::optional<luil::win32::taskbar_report> taskbar_window_report_ {};
        // 점프 목록을 채웠는가다. 비어 있으면 손대지 않는다 (앞 실행이 지은 목록도 그대로다).
        std::optional<bool> jump_list_filled_ {};
        // 마지막으로 받은 점프 목록 보고다. 셸에 낸 일이 없으면 없다.
        std::optional<luil::win32::jump_list_report> jump_list_report_ {};
        // 마지막으로 받은 둘째 인스턴스의 인자다. 받은 적이 없으면 없다.
        std::optional<luil::instance_arguments> instance_arguments_ {};
        // 알림 영역 구역의 상태다 (모두 앱 상태다).
        bool tray_on_ { false };
        // 고른 그림의 자리다 (0이나 1).
        int tray_picture_ { 0 };
        // 두 색 원이다. 처음 켤 때 한 번 짓고 frame마다 같은 것을 싣는다.
        std::array<luil::ui_image, 2> tray_pictures_ {};
        bool tray_hidden_ { false };
        // 마지막으로 받은 보고다. 아이콘을 켠 적이 없으면 없다.
        std::optional<luil::win32::tray_report> tray_report_ {};
        // 마지막 사건을 화면 글로 옮긴 것이다. 받은 적이 없으면 비었다.
        std::u8string tray_event_ {};
        // 전역 단축키 구역의 상태다 (모두 앱 상태다).
        bool hotkey_on_ { false };
        // 같은 조합의 둘째 id를 목록에 더했는가다.
        bool hotkey_conflict_ { false };
        // 눌렸을 때 주 창을 앞으로 세우는가다.
        bool hotkey_front_ { false };
        int hotkey_presses_ { 0 };
        // id마다 마지막으로 받은 보고다. 끄고 켜도 지우지 않는다 (까닭은 `hotkey_toggle_intent`를 받는 자리에 있다).
        std::vector<luil::win32::hotkey_report> hotkey_reports_ {};
        luil::text::text_edit_state note_ {};
        // 도구 창 메뉴 안 검색 칸의 상태다.
        // 조합 상태는 창 안 메모 칸과 하나를 같이 쓴다 — target이 구분한다.
        luil::text::text_edit_state menu_search_ {};
        std::optional<luil::text_composition_event> composition_ {};
    };
}
