#pragma once

// 데모 전체가 공유하는 기반이다.
//  - 앱 정의 kind·커서·텍스트 입력 대상의 등록부 (한곳에 모아 충돌을 막는다)
//  - 셸 수준 메시지 (종료·창 크기·페이지 이동·사이드바·팝업 닫기·토스트 요청)
//  - 페이지들이 같이 쓰는 작은 element와 도우미

#include "luil/text/text_edit.h"
#include "luil/ui/app_message.h"
#include "luil/ui/label_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/root_element.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/toast_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"
#include "luil/win32/app_host.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demo {
    // --- kind 등록부 ---
    // 라이브러리는 값을 해석하지 않고 정체성으로만 쓴다.
    // 앱 kind는 application_element_kind()로 정의한다.
    // 최상위 컨테이너는 예약 kind인 root_element를 사용한다.
    constexpr luil::ui_element_kind kind_counter_label { luil::application_element_kind(1) };
    constexpr luil::ui_element_kind kind_increment { luil::application_element_kind(2) };
    constexpr luil::ui_element_kind kind_text { luil::application_element_kind(3) };
    constexpr luil::ui_element_kind kind_number_input { luil::application_element_kind(4) };
    constexpr luil::ui_element_kind kind_note_input { luil::application_element_kind(5) };
    // 7~9는 목록이 라이브러리로 간 자리다 (창·행·막대는 이제 `list_element`가 짓는다).
    // 남은 상수를 당겨 메우지 않는다 — 무관한 줄이 전부 바뀌고, 번호는 이름의 일부가 아니다.
    constexpr luil::ui_element_kind kind_list_panel { luil::application_element_kind(6) };
    constexpr luil::ui_element_kind kind_dialog_open { luil::application_element_kind(10) };
    // modal scrim은 예약 kind인 modal_host_element가 만든다.
    constexpr luil::ui_element_kind kind_dialog { luil::application_element_kind(12) };
    constexpr luil::ui_element_kind kind_dialog_cancel { luil::application_element_kind(13) };
    constexpr luil::ui_element_kind kind_dialog_confirm { luil::application_element_kind(14) };
    constexpr luil::ui_element_kind kind_theme { luil::application_element_kind(15) };
    constexpr luil::ui_element_kind kind_accent { luil::application_element_kind(16) };
    // 배치만 하는 컨테이너들이다. 상호작용이 없어 owner로만 구분한다.
    constexpr luil::ui_element_kind kind_layout { luil::application_element_kind(17) };
    constexpr luil::ui_element_kind kind_toast_trigger { luil::application_element_kind(18) };
    constexpr luil::ui_element_kind kind_card { luil::application_element_kind(19) };
    constexpr luil::ui_element_kind kind_grouped_list { luil::application_element_kind(20) };
    constexpr luil::ui_element_kind kind_page_panel { luil::application_element_kind(21) };
    // 창 페이지의 도구 창 토글과 도구 창 안의 element들이다.
    constexpr luil::ui_element_kind kind_tool_toggle { luil::application_element_kind(22) };
    constexpr luil::ui_element_kind kind_tool_note_input { luil::application_element_kind(23) };
    constexpr luil::ui_element_kind kind_tool_toast { luil::application_element_kind(24) };
    constexpr luil::ui_element_kind kind_tool_menu { luil::application_element_kind(25) };
    // popup 안의 검색 칸이다 (드롭다운 목록·도구 창 메뉴).
    // popup마다 kind가 달라야 policy가 서로 다른 대상으로 나른다.
    constexpr luil::ui_element_kind kind_dropdown_search_input { luil::application_element_kind(26) };
    constexpr luil::ui_element_kind kind_tool_menu_search_input { luil::application_element_kind(27) };
    // 목록 페이지의 두 판을 나눈 자리다.
    constexpr luil::ui_element_kind kind_list_split { luil::application_element_kind(28) };
    // 기본 페이지의 이미지 견본이다 (fit별로 owner가 갈린다).
    constexpr luil::ui_element_kind kind_picture { luil::application_element_kind(29) };
    // 이미지 페이지의 드롭 영역과 미리 보기 칸이다.
    constexpr luil::ui_element_kind kind_image_drop { luil::application_element_kind(30) };
    constexpr luil::ui_element_kind kind_image_preview { luil::application_element_kind(31) };

    // --- UI thread에서 실행해야 하는 앱 명령 ---
    // 라이브러리는 이 번호를 해석하지 않고 셸의 delegate에 그대로 넘긴다
    // (`app_ui_command` — ui_events.h). 파일 dialog가 그 자리다: 창을 가진
    // thread에서만 modal로 뜨므로 logic을 거치는 앱 메시지로는 낼 수 없다.
    inline constexpr std::uint32_t app_command_open_image { 1 };

    // 앱이 정의하는 포인터 모양이다 (세로 목록의 순서 바꾸기).
    constexpr luil::ui_cursor cursor_reorder { luil::application_cursor(0) };

    // 텍스트 입력 대상이다.
    constexpr luil::text_input_target target_number { static_cast<luil::text_input_target>(1) };
    constexpr luil::text_input_target target_note { static_cast<luil::text_input_target>(2) };
    constexpr luil::text_input_target target_tool_note { static_cast<luil::text_input_target>(3) };
    constexpr luil::text_input_target target_dropdown_search { static_cast<luil::text_input_target>(4) };
    constexpr luil::text_input_target target_tool_menu_search { static_cast<luil::text_input_target>(5) };

    // --- 페이지 키 ---
    // 사이드바 내비게이션과 셸의 페이지 전환이 쓴다.
    inline constexpr std::u8string_view page_basics { u8"basics" };
    inline constexpr std::u8string_view page_lists { u8"lists" };
    inline constexpr std::u8string_view page_tabs { u8"tabs" };
    inline constexpr std::u8string_view page_groups { u8"groups" };
    inline constexpr std::u8string_view page_theme { u8"theme" };

    // --- 셸 수준 메시지 ---
    struct close_intent
    {};

    struct window_metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 1.0f };
    };

    // 창 배치 보고다 (이동·크기 조절 끝, 최대화 전환, 종료 직전 — 라이브러리가 낸다).
    // 셸이 마지막 값을 들고 있다가 종료 저장에 쓴다.
    struct placement_intent
    {
        luil::win32::window_placement placement {};
    };

    struct navigate_intent
    {
        std::u8string page {};
    };

    struct sidebar_toggle_intent
    {};

    // 손잡이를 끈 만큼의 폭 변화다 (+가 넓어지는 쪽, 논리 픽셀).
    struct sidebar_resize_intent
    {
        float delta { 0.0f };
    };

    // 떠 있는 popup(드롭다운·컨텍스트 메뉴·탭 넘침)을 모두 닫자는 요청이다.
    // popup을 소유한 페이지들이 각자 자기 열림 상태를 지운다.
    struct popup_close_intent
    {};

    // 텍스트 편집·IME 조합 메시지다.
    // policy가 모든 텍스트 칸의 요청을 이 타입으로 나르고,
    // 페이지들이 target을 보고 자기 것만 갖는다 (기본 페이지·창 페이지).
    struct edit_intent
    {
        luil::text_edit_request request {};
    };

    struct composition_intent
    {
        luil::text_composition_event event {};
    };

    // 토스트를 하나 띄우자는 요청이다.
    // 어느 페이지에서든 보낼 수 있고 토스트 목록은 토스트 페이지가 소유한다.
    struct toast_request_intent
    {
        std::u8string text {};
        luil::toast_severity severity { luil::toast_severity::info };
        // 0이면 계속 남는 토스트다.
        std::chrono::milliseconds duration { 3000 };
    };

    // --- 공용 도우미 ---
    // 편집 적용(apply_text_edit)과 view 합성(make_text_input_view)은
    // 라이브러리(luil)의 것을 그대로 쓴다.
    [[nodiscard]] std::u8string to_u8(int value);

    [[nodiscard]] std::unique_ptr<luil::label_element> make_label(luil::ui_element_id id, std::u8string text, float font_size, luil::label_color_role color);

    // --- 검색 칸이 달린 메뉴 popup ---
    // popup은 keyboard focus를 받지 못해 자기 IME session이 없다.
    // 그래도 앱이 하는 일은 여느 텍스트 칸과 똑같다 — 앵커 표면의 session이
    // 이 popup의 tree와 창을 대신 본다 (popup-ime-design.md).

    // 검색 칸의 높이와 둘레·사이 여백이다 (논리 픽셀).
    inline constexpr float search_menu_field_height { 26.0f };
    inline constexpr float search_menu_padding { 4.0f };

    // 이 설정으로 popup이 차지할 높이다 (논리 픽셀).
    [[nodiscard]] float search_menu_height(const luil::menu_config& menu) noexcept;

    // 검색 칸의 글로 메뉴 항목을 좁힌다.
    // 어떤 글이 맞는지는 앱 정책이고(여기서는 부분 일치), 조합 중인 글자를
    // 어떻게 다룰지는 라이브러리의 `search_query`가 정한다 — "토ㅅ"처럼 아직
    // 어디에도 없는 중간 단계에서 목록이 사라졌다 나타나지 않는다.
    // 남는 것이 없으면 눌리지 않는 한 줄로 "없다"를 말한다 (빈 상자를 두지 않는다).
    [[nodiscard]] std::vector<luil::menu_item_config> narrow_menu_items(std::vector<luil::menu_item_config> items, const luil::text_input_view& search);

    // 검색 칸이 있는 popup이 이 계기에 남을지다.
    // 창을 옮기거나 다른 창을 잠깐 보는 것은 "이 검색을 그만두자"가 아니다 —
    // 치던 글이 그것으로 사라지면 안 된다. 남는 popup은 앵커 표면을 따라 함께
    // 옮겨지므로 자리도 어긋나지 않는다.
    // 밖 클릭·Esc·휠에는 그대로 닫는다 (휠은 popup이 붙어 있던 자리를 움직인다).
    [[nodiscard]] bool search_popup_stays(luil::win32::popup_dismiss_reason reason) noexcept;

    // 검색 칸 + 메뉴로 된 popup tree다.
    // 배경 panel이 popup 창 전체를 칠하고 클릭을 흡수한다 — popup 여백을 눌러도
    // 아래로 새지 않는다.
    // 같은 화면에 여럿이면 `menu.owner`가 element들을 구분한다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_search_menu_tree(
        luil::menu_config menu, luil::ui_element_kind search_kind, luil::text_input_view search, std::u8string placeholder, float width, float scale);

    // 최상위 컨테이너(root_element)와 배경 panel(panel_element)은
    // 라이브러리의 것을 그대로 쓴다.
} // namespace demo
