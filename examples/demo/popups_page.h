#pragma once

// 메뉴·팝업 페이지다: popup 창을 실제로 쓰는 세 가지 예다.
//  - 드롭다운(select): 닫힌 칸을 누르면 아래에 popup 목록이 뜬다.
//    목록 위의 검색 칸은 **popup 안의 텍스트 입력**이다 — popup은 keyboard
//    focus를 받지 못하지만 앵커(여기서는 주 창)의 IME session이 대신 봐서
//    조합까지 된다 (popup-ime-design.md).
//  - 컨텍스트 메뉴: 카드를 오른쪽 클릭하면 그 자리에 popup 메뉴가 뜬다.
//  - 분할 버튼("저장")과 드롭다운 단추("보내기"): 본체를 누르면 동작이 나가고 화살표를 누르면 필드 아래에
//    메뉴가 뜬다. 자리는 `menu_popup_config::below`가 정하고 아래가 모자라면 위로 뒤집힌다
//    (split-button-design.md).
//  - 날짜 칸과 인라인 달력: 칸을 누르면 아래에 달력 popup이 뜨고 초점이 커서 날로 간다. 화살표·Page·Home·End가 커서를 옮기고
//    Enter·누름이 고른다. Esc나 날 고르기로 닫으면 칸으로 돌아온다 (date-picker-design.md).
//    "OS 첫 요일"을 켜면 두 달력의 주 첫 요일이 OS 값이다. 값은 셸이 읽어 메시지로 넣는다 (i18n-adoption-design.md).
// 떠 있는 popup은 한 번에 하나다. 어느 필드의 것인지를 `open_popup_` 하나가 들고 있어 둘이 함께 열릴 수 없다.
// popup은 창 경계를 넘을 수 있고, 닫힘 계기는 **이유와 함께** 온다: 컨텍스트
// 메뉴는 어느 계기에도 닫히지만, 검색 중인 드롭다운은 창 이동·활성 전환에는
// 남는다 — 잠깐 다른 창을 봤다고 치던 글이 사라지면 안 되기 때문이다.

#include "demo/common.h"
#include "luil/ui/calendar_date.h"
#include "luil/ui/color_space.h"
#include "luil/win32/app_host.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace demo {
    // --- 이 페이지의 메시지 ---
    struct dropdown_toggle_intent
    {
        bool open { false };
    };

    struct dropdown_select_intent
    {
        std::u8string value {};
    };

    // 카드의 오른쪽 클릭 자리다 (주 창 client 물리 픽셀).
    struct card_menu_intent
    {
        float x { 0.0f };
        float y { 0.0f };
    };

    struct card_menu_close_intent
    {
    };

    // 컨텍스트 메뉴에서 항목을 골랐다.
    // 페이지는 메뉴를 닫고, 셸이 토스트 알림으로 잇는다.
    struct card_menu_select_intent
    {
        std::u8string key {};
    };

    struct popups_scroll_intent
    {
        float delta { 0.0f };
    };

    // 분할 버튼("저장")이나 드롭다운 단추("보내기")의 메뉴를 이 상태로 하라는 절대 메시지다. 누름과 보조 기술의
    // 펼치기·접기, 키(Alt+↓·F4), 바깥 누름 닫기가 모두 이 하나로 간다.
    struct field_menu_intent
    {
        std::u8string owner {};
        bool open { false };
    };

    // 필드 메뉴에서 항목을 골랐다. 메뉴를 닫는 것은 `menu_config::after_select`가 따로 보낸다.
    struct field_menu_select_intent
    {
        std::u8string owner {};
        std::u8string key {};
    };

    // 분할 버튼의 본체("저장")를 눌렀다.
    struct split_invoke_intent
    {
    };

    // 색 칸의 popup을 이 상태로 하라는 절대 메시지다 (`color_field_config::set_open`). 닫기는 16진 초안을 먼저 확정한다.
    struct color_field_intent
    {
        bool open { false };
    };

    // 색 칸 popup의 닫힘 계기다. Esc로 닫으면 초점을 칸으로 돌린다 (color-picker-design.md 결정 3).
    struct color_field_dismiss_intent
    {
        luil::popup_dismiss_reason reason { luil::popup_dismiss_reason::pointer_press_outside };
    };

    // popup 안 색 고르기의 값이다.
    struct color_field_value_intent
    {
        luil::hsv_color value {};
    };

    // popup 안 16진 칸을 확정한다 (칸을 떠날 때와 Enter).
    struct color_field_commit_intent
    {
    };

    // 날짜 칸의 popup을 이 상태로 하라는 절대 메시지다 (`date_field_config::set_open`). 열면 커서를 시작 커서로 두고 초점을 옮긴다.
    struct date_field_intent
    {
        bool open { false };
    };

    // 날짜 칸 popup의 닫힘 계기다. 글 칸이 없어 모든 계기에 닫고, Esc로 닫으면 초점을 칸으로 돌린다.
    struct date_field_dismiss_intent
    {
        luil::popup_dismiss_reason reason { luil::popup_dismiss_reason::pointer_press_outside };
    };

    // 달력의 커서·단추·고르기다. `owner`는 어느 달력인가다 (popup 안의 것이 "popups/date", 인라인이 "popups/calendar").
    //  - 커서(`move_to`)는 커서를 옮기고 그 날로 초점 요청을 싣는다. 단추(`turn_month`)는 커서만 옮겨 초점이 단추에 남는다.
    struct date_move_intent
    {
        std::u8string owner {};
        std::chrono::year_month_day day {};
    };

    struct date_turn_intent
    {
        std::u8string owner {};
        std::chrono::year_month_day day {};
    };

    struct date_select_intent
    {
        std::u8string owner {};
        std::chrono::year_month_day day {};
    };

    // 날짜 칸의 값을 비운다. 처음 칸에는 오늘이 들어 있어 이것 없이는 placeholder가 서지 않는다.
    struct date_clear_intent
    {
    };

    // 셸이 읽어 넣는 OS의 주 첫 요일이다 (`luil::user_first_weekday`). 이 페이지는 core test에 함께 컴파일되어 플랫폼 함수를 부르면
    // 링크가 깨진다. 읽지 못했으면 빈 값이다.
    struct date_os_first_day_intent
    {
        std::optional<std::chrono::weekday> value {};
    };

    // "OS 첫 요일" 체크다. 기본은 꺼져 일요일이다.
    struct date_use_os_first_day_intent
    {
        bool use { false };
    };

    // popup 모양을 골랐다 (`setting`은 "corner"·"border", `value`는 고른 값).
    struct popup_shape_intent
    {
        std::u8string setting {};
        std::u8string value {};
    };

    class popups_page
    {
    public:
        // 오늘은 고정 날(2026-11-03)이다. test가 이것을 쓴다. 데모는 시작할 때 `local_today()`를 한 번 읽어 준다.
        popups_page() noexcept;
        explicit popups_page(std::chrono::year_month_day today) noexcept;

        // 이 PC의 지역 시각으로 본 오늘이다 (Win32 `localtime_s`, 기기 test로 Android에 실릴 때는 `localtime_r`). 시간대 계산은 하지 않는다.
        [[nodiscard]] static std::chrono::year_month_day local_today() noexcept;

        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 떠 있는 드롭다운·컨텍스트 메뉴·필드 메뉴를 popup으로 만든다.
        // 같은 frame에서 build가 만든 element의 자리(bounds)를 쓰므로 build 뒤에 불러야 한다.
        // `surface`는 앵커 창의 client 사각형이다 (물리 픽셀). 필드 메뉴가 이 안에서 위아래를 고른다.
        [[nodiscard]] std::vector<luil::win32::ui_popup> make_popups(float scale, const luil::rect_f& surface) const;

        // 떠 있는 popup을 닫는다 (페이지 이동·Esc·바깥 클릭). 색 칸 popup이면 16진 초안을 먼저 확정한다.
        void close_popups();

        // 색 칸 popup을 열 때 판으로, Esc로 닫을 때 칸으로 초점을 옮기는 요청이다. 번호가 바뀔 때만 새 요청이다.
        //  - 날짜 칸 popup은 열 때와 커서를 옮길 때 커서 날로, Esc·날 고르기로 닫을 때 칸으로 옮긴다. 인라인 달력은 커서 날로 옮긴다.
        [[nodiscard]] std::optional<luil::focus_request> make_focus_request() const;

    private:
        // 떠 있는 popup의 임자를 바꾼다. 빈 값이면 모두 닫는다.
        // 한 번에 하나만 띄운다. 임자가 바뀌면 검색 상태도 함께 비워, 다음에 드롭다운을 열 때 목록이 온전히 보이게 한다.
        void show_popup(std::u8string owner);
        // 그 임자의 popup이 떠 있을 때만 닫는다. 늦게 온 닫기 메시지가 다른 필드의 popup을 닫지 않는다.
        void close_popup(std::u8string_view owner);
        // 색 칸 popup의 16진 초안을 읽어 색으로 확정한다. 읽히지 않으면 칸을 붉게 둔다.
        void commit_color_hex();
        // 16진 초안을 지금 색의 글로 다시 쓴다.
        void reset_color_hex();
        // 초점 요청을 새로 싣는다.
        void request_focus(std::u8string surface, luil::ui_element_id target);
        // 두 달력에 넣는 주 첫 요일이다. 앱이 넣는 값이고 라이브러리의 기본은 일요일 그대로다.
        [[nodiscard]] std::chrono::weekday first_day() const noexcept;

        // 이 페이지의 popup에 거는 모서리와 경계다. 기본은 OS 기본(Windows 11은 작게 둥글다)과 경계 있음이다.
        luil::popup_corner corner_ { luil::popup_corner::system };
        bool popup_border_ { true };
        std::u8string presentation_ { u8"자세히" };
        bool pinned_ { false };
        bool locked_ { true };
        bool deleted_ { false };
        bool preview_ { false };
        float scroll_ { 0.0f };
        // 드롭다운 popup 안 검색 칸의 상태다.
        // 여느 텍스트 칸과 똑같이 앱이 들고 있다 — popup이라고 다르지 않다.
        luil::text::text_edit_state search_ {};
        std::optional<luil::text_composition_event> composition_ {};
        // 떠 있는 popup을 연 필드의 owner다 ("presentation"·"card"·"save"·"send"). 비어 있으면 없다.
        std::u8string open_popup_ {};
        // 분할 버튼 페이지가 마지막으로 낸 동작이다. 화면의 label이 보이고 UIA가 되읽는다.
        std::u8string last_action_ {};
        // 컨텍스트 메뉴를 연 자리다 (물리 픽셀; popup을 만들 때 논리로 되돌린다).
        float menu_x_ { 0.0f };
        float menu_y_ { 0.0f };
        // 이번 build가 만든 드롭다운 칸이다.
        // popup의 자리 계산에만 쓰고 frame이 게시되기 전까지만 유효하다.
        const luil::ui_element* dropdown_ { nullptr };
        // 이번 build가 만든 분할 버튼과 드롭다운 단추다. 같은 규칙으로 frame이 게시되기 전까지만 유효하다.
        const luil::ui_element* split_ { nullptr };
        const luil::ui_element* send_ { nullptr };
        const luil::ui_element* color_field_ { nullptr };
        // 색 칸의 값과 popup 안 16진 칸의 초안이다. 값은 HSV로 든다 (color-space-design.md).
        luil::hsv_color field_color_ { 225.0f, 0.8f, 1.0f, 1.0f };
        luil::text::text_edit_state field_hex_ {};
        std::optional<luil::text_composition_event> field_composition_ {};
        bool field_hex_invalid_ { false };
        // 날짜 칸과 인라인 달력이다. 오늘은 앱이 준다 (라이브러리는 시계를 읽지 않는다).
        //  - 인라인 달력은 오늘 앞 30일부터 뒤 60일까지이고 주말을 고를 수 없다.
        std::chrono::year_month_day today_ {};
        std::optional<std::chrono::year_month_day> date_value_ {};
        std::chrono::year_month_day date_cursor_ {};
        std::optional<std::chrono::year_month_day> calendar_value_ {};
        std::chrono::year_month_day calendar_cursor_ {};
        luil::calendar_bounds calendar_bounds_ {};
        const luil::ui_element* date_field_ { nullptr };
        // 셸이 넣어 준 OS의 주 첫 요일과 그 값을 달력에 쓰는가다.
        std::optional<std::chrono::weekday> os_first_day_ {};
        bool use_os_first_day_ { false };
        // 마지막 초점 요청이다. 열 때와 Esc로 닫을 때 번호를 올린다.
        std::uint64_t focus_revision_ { 0 };
        std::u8string focus_surface_ {};
        luil::ui_element_id focus_target_ {};
    };
}
