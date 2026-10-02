#pragma once

// mobile demo 전체가 공유하는 기반이다.
//  - 앱 정의 kind·텍스트 입력 대상의 등록부 (한곳에 모아 충돌을 막는다)
//  - 셸 수준 메시지 (종료·화면 크기·페이지 이동·뒤로 가기·팝업 닫기·토스트 요청)
//  - 페이지들이 같이 쓰는 작은 element와 도우미
//
// 데스크톱 demo(examples/demo)에서 휴대폰·태블릿에 맞는 페이지만 옮겼다. 별도 창, 웹뷰, 되돌이
// HTTP, 파일 끌어 놓기·열기 dialog처럼 모바일 앱의 UI와 맞지 않는 것은 뺐다.

#include "luil/app/app_host.h"
#include "luil/text/text_edit.h"
#include "luil/ui/app_message.h"
#include "luil/ui/label_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/root_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/toast_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mobile_demo {
    // --- kind 등록부 ---
    // 라이브러리는 값을 해석하지 않고 정체성으로만 쓴다.
    constexpr luil::ui_element_kind kind_counter_label { luil::application_element_kind(1) };
    constexpr luil::ui_element_kind kind_increment { luil::application_element_kind(2) };
    constexpr luil::ui_element_kind kind_text { luil::application_element_kind(3) };
    constexpr luil::ui_element_kind kind_number_input { luil::application_element_kind(4) };
    constexpr luil::ui_element_kind kind_note_input { luil::application_element_kind(5) };
    constexpr luil::ui_element_kind kind_list_panel { luil::application_element_kind(6) };
    constexpr luil::ui_element_kind kind_dialog_open { luil::application_element_kind(10) };
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
    // popup 안의 검색 칸이다 (드롭다운 목록).
    constexpr luil::ui_element_kind kind_dropdown_search_input { luil::application_element_kind(26) };
    // 목록 페이지의 두 판을 나눈 자리다.
    constexpr luil::ui_element_kind kind_list_split { luil::application_element_kind(28) };
    // 기본 페이지의 이미지 견본이다 (fit별로 owner가 갈린다).
    constexpr luil::ui_element_kind kind_picture { luil::application_element_kind(29) };
    // 셸의 페이지 목록 항목이다 (owner가 페이지 키다).
    constexpr luil::ui_element_kind kind_page_entry { luil::application_element_kind(40) };
    // 흘리는 페이지 창이다 (owner가 페이지 키다).
    constexpr luil::ui_element_kind kind_page_scroll { luil::application_element_kind(41) };

    // 앱이 정의하는 포인터 모양이다 (세로 목록의 순서 바꾸기). 마우스를 꽂은 태블릿에서만 보인다.
    constexpr luil::ui_cursor cursor_reorder { luil::application_cursor(0) };

    // 텍스트 입력 대상이다.
    constexpr luil::text_input_target target_number { static_cast<luil::text_input_target>(1) };
    constexpr luil::text_input_target target_note { static_cast<luil::text_input_target>(2) };
    constexpr luil::text_input_target target_dropdown_search { static_cast<luil::text_input_target>(4) };

    // --- 페이지 키 ---
    inline constexpr std::u8string_view page_basics { u8"basics" };
    inline constexpr std::u8string_view page_lists { u8"lists" };
    inline constexpr std::u8string_view page_tabs { u8"tabs" };
    inline constexpr std::u8string_view page_groups { u8"groups" };
    inline constexpr std::u8string_view page_toasts { u8"toasts" };
    inline constexpr std::u8string_view page_popups { u8"popups" };
    inline constexpr std::u8string_view page_zoom { u8"zoom" };
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

    // 페이지를 연다. 좁은 화면에서는 목록 위로 페이지가 들어서고, 넓은 화면에서는 오른쪽 판이 바뀐다.
    struct navigate_intent
    {
        std::u8string page {};
    };

    // 뒤로 간다 (앱 바의 뒤로 단추, Android의 뒤로 가기). 좁은 화면에서 페이지를 닫고 목록으로 간다.
    struct back_intent
    {};

    // 떠 있는 popup(드롭다운·컨텍스트 메뉴·탭 넘침)을 모두 닫자는 요청이다.
    struct popup_close_intent
    {};

    // 텍스트 편집·IME 조합 메시지다.
    // policy가 모든 텍스트 칸의 요청을 이 타입으로 나르고, 페이지들이 target을 보고 자기 것만 갖는다.
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
    [[nodiscard]] std::u8string to_u8(int value);

    [[nodiscard]] std::unique_ptr<luil::label_element> make_label(luil::ui_element_id id, std::u8string text, float font_size, luil::label_color_role color);

    // 페이지 글의 크기와 그 줄의 높이다 (논리 픽셀). 손가락이 가리는 화면이라 데스크톱 demo보다 한
    // 단계 크게 쓴다. 라이브러리의 label은 한 줄이라 좁은 화면의 설명은 짧게 끊어 여러 줄로 쓴다.
    inline constexpr float body_text_size { 15.0f };
    inline constexpr float note_text_size { 13.0f };
    inline constexpr float heading_text_size { 17.0f };
    inline constexpr float body_line_height { 26.0f };
    inline constexpr float note_line_height { 22.0f };
    inline constexpr float heading_line_height { 30.0f };
    // 페이지 가장자리 여백이다. 휴대폰은 화면이 좁아 데스크톱의 24보다 작다.
    inline constexpr float page_padding { 16.0f };
    // 손가락으로 누르는 단추의 높이다 (Material의 최소 터치 영역).
    inline constexpr float touch_height { 44.0f };

    // 흘려 보는 페이지의 내용과 그 높이다 (논리 픽셀). 셸이 scroll view에 담는다.
    struct page_content
    {
        std::unique_ptr<luil::ui_element> element {};
        float height { 0.0f };
    };

    // 위에서 아래로 쌓으며 높이를 함께 세는 페이지 기둥이다. stack은 측정 단계가 없어 흘리는 창에
    // 내용 높이를 앱이 알려 줘야 한다.
    class page_column
    {
    public:
        explicit page_column(std::u8string owner, float padding = page_padding);

        void add(std::unique_ptr<luil::ui_element> child, float height);
        // 가로 폭이 정해진 항목이다 (나머지는 비운다).
        void add(std::unique_ptr<luil::ui_element> child, float height, float width);
        void gap(float height);
        // 회색 설명 한 줄, 본문 한 줄, 머리글 한 줄이다.
        void note(std::u8string owner, std::u8string text);
        void body(std::u8string owner, std::u8string text);
        void heading(std::u8string owner, std::u8string text);

        [[nodiscard]] page_content finish();

    private:
        std::unique_ptr<luil::stack_element> column_ {};
        float height_ { 0.0f };
    };

    // --- 검색 칸이 달린 메뉴 popup ---
    // popup은 keyboard focus를 받지 못해 자기 IME session이 없다. 앵커 표면의 session이
    // 이 popup의 tree를 대신 본다 (popup-ime-design.md).

    // 검색 칸의 높이와 둘레·사이 여백이다 (논리 픽셀).
    inline constexpr float search_menu_field_height { 36.0f };
    inline constexpr float search_menu_padding { 6.0f };

    // 이 설정으로 popup이 차지할 높이다 (논리 픽셀).
    [[nodiscard]] float search_menu_height(const luil::menu_config& menu) noexcept;

    // 검색 칸의 글로 메뉴 항목을 좁힌다 (부분 일치). 남는 것이 없으면 눌리지 않는 한 줄로 "없다"를 말한다.
    [[nodiscard]] std::vector<luil::menu_item_config> narrow_menu_items(std::vector<luil::menu_item_config> items, const luil::text_input_view& search);

    // 검색 칸이 있는 popup이 이 계기에 남을지다. 다른 창을 잠깐 보는 것(알림 창)은 "이 검색을
    // 그만두자"가 아니다. 바깥 누름·뒤로 가기·회전에는 닫는다.
    [[nodiscard]] bool search_popup_stays(luil::popup_dismiss_reason reason) noexcept;

    // 검색 칸 + 메뉴로 된 popup tree다. 배경 panel이 popup 전체를 칠하고 누름을 흡수한다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_search_menu_tree(
        luil::menu_config menu, luil::ui_element_kind search_kind, luil::text_input_view search, std::u8string placeholder, float width, float scale);
} // namespace mobile_demo
