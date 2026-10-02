#pragma once

#include <cstdint>
#include <string>

namespace luil {
    // element의 종류다.
    // 종류 + 소유 키(owner)가 tree 안의 안정적인 정체성이 되어 snapshot이
    // 다시 빌드되어도 hover·pressed·tooltip이 같은 대상으로 이어진다.
    //
    // 라이브러리는 자기 element(caption 계열)의 kind만 예약하고,
    // 나머지는 앱이 `application_element_kind`로 정의한다.
    // 열거자 밖의 값도 유효한 kind다.
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
        // 모바일 화면 맨 위의 앱 바와 그 아이콘 버튼이다.
        // 버튼은 "navigation"과 "action:<순번>"을 owner로 쓴다.
        app_bar,
        app_bar_button,
        // 앱이 정의하는 kind는 이 값부터다.
        // 라이브러리가 예약 대역을 넓혀도 기존 앱 상수가 밀리지 않도록 여유를 둔다.
        zoom_view,
        zoom_controls,
        zoom_decrease,
        zoom_label,
        zoom_increase,
        zoom_fit,
        first_application_kind = 64,
    };

    [[nodiscard]] constexpr ui_element_kind application_element_kind(const std::uint32_t index) noexcept
    {
        return static_cast<ui_element_kind>(static_cast<std::uint32_t>(ui_element_kind::first_application_kind) + index);
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
} // namespace luil
