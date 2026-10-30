#pragma once

#include <cstdint>
#include <string>

namespace luil {
    // element의 종류다.
    // 종류 + 소유 키(owner)가 tree 안의 안정적인 정체성이 되어 snapshot이
    // 다시 빌드되어도 hover·pressed·tooltip이 같은 대상으로 이어진다.
    //
    // 라이브러리는 자기 element의 kind만 예약하고,
    // 나머지는 앱이 `application_element_kind`로 정의한다.
    // 열거자 밖의 값도 유효한 kind다.
    //  - 수는 두 대역이다: 라이브러리 [0, 1024), 앱 [1024, …).
    //    라이브러리 kind와 앱 kind가 같은 수가 되면 kind로 찾는 코드가 서로를 집는다.
    //  - 앱은 `constexpr ui_element_kind my_kind { application_element_kind(0) };` 식으로
    //    상수를 만들고 switch의 case로도 그대로 쓸 수 있다.
    enum class ui_element_kind : std::uint32_t
    {
        none = 0,
        root,
        caption,
        caption_title,
        caption_minimize,
        caption_maximize,
        caption_close,
        // caption 안에 앱이 둔 단추다 (`caption_config::app_buttons`, owner가 그 key다).
        // 비클라이언트 판정이 이 자리를 끌기가 아니라 client로 답한다.
        caption_app_button,
        // in-app dialog의 캡션 바와 그 닫기 버튼이다.
        // 창 caption과 달리 여럿이 동시에 있을 수 있어 owner로 구분한다.
        dialog_caption,
        dialog_caption_close,
        // 토스트 stack과 그 안의 토스트·액션 버튼이다.
        // 여럿이 동시에 있을 수 있어 owner로 구분한다.
        toast_stack,
        toast,
        toast_action,
        // 접이식 사이드바와 그 토글 버튼·폭 조절 손잡이다.
        sidebar,
        sidebar_toggle,
        sidebar_handle,
        // 탭 막대와 그 안의 탭·닫기 버튼·넘침 메뉴 버튼이다.
        // 탭은 항목 키를 owner로 쓴다.
        tab_bar,
        tab,
        tab_close,
        tab_overflow,
        // 탭을 가로로 흘리는 띠와 그 안에서 탭을 늘어놓는 레인이다.
        // 막대가 스스로 조립하는 부품이라 앱이 만들 일은 없다.
        tab_strip,
        tab_lane,
        // 제목 있는 접이식 섹션과 그 제목 줄이다.
        group,
        group_header,
        // 서로 배타적인 선택 그룹과 그 선택지다.
        // 선택지는 값을 owner로 쓴다.
        choice_group,
        choice,
        // 그룹 머리행이 위에 고정되는 목록과 그 머리행이다.
        // 머리행은 그룹 키를 owner로 쓴다.
        grouped_list,
        list_header,
        // 고르고 훑는 목록과 그 행이다. 행은 항목 키를 owner로 쓴다.
        list,
        list_row,
        // 행의 펼침 삼각형이다 (tree). 행의 owner를 그대로 물려받는다.
        list_expander,
        // 목록이 스스로 조립하는 부품이다 (흘리는 창·행을 늘어놓는 레인·막대).
        // 앱이 만들 일은 없다.
        list_scroll,
        list_lane,
        list_scrollbar,
        // 메뉴와 그 항목이다. 항목은 키를 owner로 쓴다.
        // 정책의 menu_kinds에 이 짝을 돌려주면 키보드 탐색이 붙는다.
        menu,
        menu_item,
        // 드롭다운(select)의 닫힌 칸이다.
        // 열린 목록은 popup의 menu가 맡는다.
        dropdown,
        // 텍스트 칸 안의 지우기 버튼이다.
        // 칸이 여럿이면 칸의 owner를 그대로 물려받아 구분한다.
        text_input_clear,
        // 낱개 켬/끔 컨트롤이다 (체크박스·라디오·스위치).
        // 여럿이 동시에 있을 수 있어 owner로 구분한다.
        check,
        // 화면을 덮는 modal dialog host와 그 뒤를 덮는 scrim이다.
        // 여럿이 겹칠 수 있어 owner로 구분한다.
        modal_host,
        modal_scrim,
        // 웹 콘텐츠가 앉을 자리다. 여럿이 동시에 있을 수 있어 owner로 구분한다.
        //  - owner가 곧 `ui_webview::id`다. 자리표와 수명이 그 값 하나로 만난다.
        webview,
        // 흘리는 창과 막대를 한 자리로 묶은 영역과 그 부품이다.
        // 부품은 영역이 스스로 조립하므로 앱이 만들 일은 없다.
        scroll_area,
        scroll_area_view,
        scroll_area_bar,
        // 창에 걸치는 행만 짓는 목록과 그 행이다. 행은 항목 키를 owner로 쓴다.
        //  - 행 kind가 `list_row`와 갈리는 이유는 접근성이다. 가상 목록의 행은
        //    스크롤할 때마다 tree에서 나고 사라지므로, 같은 kind로 두면 보조
        //    기술이 두 목록의 행을 한 이름 공간에서 본다 (list-view-design.md).
        virtual_list,
        virtual_list_row,
        // 목록이 스스로 조립하는 부품이다 (행을 늘어놓는 레인).
        virtual_list_lane,
        zoom_view,
        zoom_controls,
        zoom_decrease,
        zoom_label,
        zoom_increase,
        zoom_fit,
        // 모바일 화면 맨 위의 앱 바와 그 아이콘 버튼이다.
        // 버튼은 "navigation"과 "action:<순번>"을 owner로 쓴다.
        app_bar,
        app_bar_button,
        // 같은 표면 안에 겹쳐 그리는 시트·서랍과 그 scrim, 끌기 손잡이다 (sheet-design.md).
        // 여럿이 겹칠 수 있어 owner로 구분한다.
        sheet_host,
        sheet_scrim,
        sheet_handle,
        // 표다 (`table_element`). 본문은 가상 목록(`virtual_list`)이고, 표가 더하는 부품이 아래에 선다.
        table,
        table_header,
        table_header_cell,
        table_column_grip,
        table_row,
        // 표의 트리 열에 선 펼침 단추다. owner는 표와 행 키를 함께 담는다.
        table_expander,
        // 표에서 편집 중인 칸의 자리다. 편집기를 담고 초점을 가둔다. owner는 표와 행 키를 함께 담는다.
        table_editor,
        // 가상 목록 행 앞에 선 펼침 단추다. owner는 항목 키다.
        virtual_list_expander,
        // 탭 막대가 넘칠 때 양 끝에 서는 앞·뒤 단추다 (`tab_overflow_mode::step`). owner는 막대의 owner다.
        tab_step_back,
        tab_step_forward,
        // 견본 막대(`tab_bar_config::preview`)의 탭이다. 동작이 없는 그림이라 탭(`tab`)과 kind가 다르다 — 탭을 kind로
        // 찾는 앱 코드가 견본을 집지 않는다. owner는 막대 owner와 항목 키를 함께 담아 견본 막대가 여럿이어도 겹치지 않는다.
        tab_sample,
        // 경로 조각 막대와 그 조각·꺾쇠·접은 단추다 (`breadcrumb_element`). 조각·꺾쇠의 owner는 막대 owner와 조각 키를,
        // 접은 단추는 막대 owner를 담는다.
        breadcrumb,
        breadcrumb_item,
        breadcrumb_chevron,
        breadcrumb_overflow,
        // 숫자 칸의 스핀 단추다 (`text_input_number::spin_buttons`). owner는 칸 owner다.
        text_input_increase,
        text_input_decrease,
        // 명령 팔레트의 바탕·검색 칸·진입 단추다 (`make_command_palette`). owner는 팔레트 owner다.
        command_palette,
        command_palette_search,
        command_palette_entry,
        // 텍스트 칸 안의 링크다 (`text_area_config::links`·`text_input_config::links`). owner는 칸의 kind 수·owner와
        // 링크 순번을 함께 담는다. 칸과 kind가 다른 이유는 칸 kind가 텍스트 대상이라 같은 kind면 링크가 글 칸으로 읽혀서다.
        text_link,
        // 라이브러리 kind의 끝 표시다. kind가 아니다.
        //  - 새 라이브러리 kind는 이 바로 위에 더한다. 자동화 id(AutomationId)가 kind의 수라 가운데 끼우면 뒤의 수가 밀린다.
        //  - 이 값이 앱 대역에 닿으면 아래 static_assert가 막는다. 예약 대역이 64였을 때 라이브러리 kind가 넘쳐 앱 kind
        //    64부터와 같은 수가 된 적이 있다.
        end_of_library_kinds,
        // 앱이 정의하는 kind는 이 값부터다 (`application_element_kind`).
        // 라이브러리 kind가 늘어도 앱 상수가 밀리지 않도록 예약 대역을 넉넉히 둔다 (지금 쓰는 것의 열 배 넘게).
        //  - 64에서 1024로 넓혔다. 앱 상수의 수와 자동화 id의 앞 숫자는 그때 한 번 바뀌었다.
        first_application_kind = 1024,
    };

    // 라이브러리 kind가 앱 대역에 닿지 않는다.
    static_assert(static_cast<std::uint32_t>(ui_element_kind::end_of_library_kinds) <= static_cast<std::uint32_t>(ui_element_kind::first_application_kind),
        "Library element kinds overflow into application kinds. Widen the reserved band (first_application_kind).");

    [[nodiscard]] constexpr ui_element_kind application_element_kind(const std::uint32_t index) noexcept
    {
        return static_cast<ui_element_kind>(static_cast<std::uint32_t>(ui_element_kind::first_application_kind) + index);
    }

    // 그 kind가 앱이 정의한 것인가다.
    [[nodiscard]] constexpr bool is_application_element_kind(const ui_element_kind kind) noexcept
    {
        return static_cast<std::uint32_t>(kind) >= static_cast<std::uint32_t>(ui_element_kind::first_application_kind);
    }

    struct ui_element_id
    {
        ui_element_kind kind { ui_element_kind::none };
        // 같은 kind가 여럿일 때 대상을 구분하는 앱 정의
        // 키다 (항목의 식별자, 목록 행의 index 문자열 등).
        // 뜻은 앱이 정한다.
        std::u8string owner {};

        [[nodiscard]] bool operator==(const ui_element_id&) const = default;
    };
}
