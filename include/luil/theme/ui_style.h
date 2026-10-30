#pragma once

#include "luil/theme/ui_theme.h"

namespace luil {
    // 라이브러리가 그리기 안에 박아 두었던 논리 픽셀 치수다.
    // element config에 이미 열린 값(`label_config::font_size`·`button_config::corner_radius`
    // 같은 것)은 앱이 element마다 정하므로 여기 담지 않는다 — 여기 담는 것은 config에
    // 자리가 없어 앱이 손댈 수 없던 값들이다.
    //  - 배치 상수(행 높이·간격, `check_row_height` 같은 `inline constexpr`)는 담지 않는다.
    //    앱이 tree를 짓는 산수에 쓰는 값이라 실행 시점 값이 되면 그 산수가 전부 함수 호출이 된다.
    //  - 텍스트 입력의 글꼴 크기도 담지 않는다. 포인터 자리를 caret으로 옮기는 계산이
    //    그리기 밖에서 같은 글꼴을 재므로 draw context가 닿지 않는다.
    struct ui_metrics
    {
        // 목록 행·메뉴·탭·버튼·드롭다운·체크 라벨의 본문 글자다.
        float body_font_size { 12.0f };
        // 머리행 제목·tooltip·끌기 ghost의 작은 글자다.
        float small_font_size { 11.0f };
        // 버튼·입력칸·메뉴 항목·hover 채움의 모서리다.
        float control_corner_radius { 3.0f };
        // 고른 행의 채움·그룹 틀의 모서리다.
        float row_corner_radius { 4.0f };

        [[nodiscard]] bool operator==(const ui_metrics&) const noexcept = default;
    };

    // 팔레트의 역할 하나를 이름으로 가리킨다.
    // 컴포넌트 스타일이 "어느 역할로 칠할지"를 값으로 고르는 자리다 — 색 자체가 아니라 역할이라 테마 전환과
    // 고대비가 그대로 따라온다. 선택자(`std::function`)와 달리 비교되고 JSON에 적힌다.
    //  - 바탕으로 칠할 만한 역할만 둔다. 글자·선 역할은 그 자리의 뜻이 정해져 있어 고를 일이 없다.
    enum class palette_role
    {
        window_background,
        surface_background,
        input_background,
        tooltip_background,
        notice_background,
        caption_background,
        button_hover_background,
        accent_soft,
    };

    // 역할이 그 팔레트에서 갖는 색이다.
    [[nodiscard]] ui_color palette_color(const ui_color_palette& palette, palette_role role) noexcept;

    // 탭 막대를 어떤 모양으로 그리는가다.
    enum class tab_bar_variant
    {
        // 선택된 탭 바탕 + 아래 강조선이다 (처음부터의 모양).
        underline,
        // 선택된 탭을 아래 내용 영역과 한 판으로 잇는다. 위 모서리는 둥글고 아래 끝은 바깥으로 휜다.
        connected,
        // 탭을 세로 칸막이로 맞붙인다. 선택된 칸은 위 강조선을 긋고 내용 바탕으로 아래가 열린다.
        flat,
    };

    // 탭의 닫기 단추를 언제 보이는가다. 숨어도 자리는 지켜 이름이 들썩이지 않는다.
    enum class tab_close_visibility
    {
        always,
        // 선택된 탭과 포인터가 올라간 탭에서만 보인다.
        selected_or_hovered,
    };

    // 탭 상태 표시(`tab_indicator`)를 어디에 두는가다.
    enum class tab_indicator_placement
    {
        // 아이콘의 오른쪽 아래 모서리다. 진행 중이면 아이콘을 감싸는 호다.
        icon_corner,
        // 닫기 단추 자리다. 포인터를 단추에 올리거나 초점이 가면 닫기 단추로 바뀐다.
        close_slot,
    };

    // 자기 창을 갖는 popup(메뉴·목록·떠 있는 판)의 모서리다 (`ui_popup::corner`).
    //  - 둥근 모서리는 OS가 창을 잘라 내는 것이다 (Windows 11의 DWM). 그것을 못 하는 OS·환경(Windows 10 이하,
    //    원격 데스크톱)에서는 모두 직각으로 물러서고 처음부터의 모양(창 그림자 + 1px 선)이다.
    //  - 고른 모서리의 실제 반지름은 `popup_corner_radius`가 답한다 (app_host.h).
    enum class popup_corner
    {
        // 이 OS가 popup에 쓰는 모양이다. Windows 11은 메뉴·tooltip처럼 작게 둥글다.
        system,
        round,
        round_small,
        // 직각이다. 꼬리 달린 말풍선처럼 모양을 직접 그리는 popup이 고른다.
        square,
    };

    // popup의 앱 스타일이다 (`ui_style::popup`). popup마다 적지 않으면 이 값이다.
    struct popup_style
    {
        popup_corner corner { popup_corner::system };

        [[nodiscard]] bool operator==(const popup_style&) const noexcept = default;
    };

    // 탭 막대의 앱 스타일이다 (`ui_style::tab_bar`).
    //  - 막대마다 config에 적지 않고 앱이 한 번 정하면 그 앱의 모든 탭 막대가 같은 모양이다. 한 막대만 달라야
    //    하면 `tab_bar_config::style`이 통째로 덮는다.
    //  - 값은 enum과 역할뿐이다 — 비교되고 JSON(`assets/style.json`의 `tabBar`)에 적힌다.
    struct tab_bar_style
    {
        tab_bar_variant variant { tab_bar_variant::underline };
        tab_close_visibility close_visibility { tab_close_visibility::always };
        tab_indicator_placement indicator_placement { tab_indicator_placement::icon_corner };
        // 이어지는 두 모양(`connected`·`flat`)의 막대 바탕과 선택된 탭 바탕이다.
        //  - 선택된 탭 바탕은 막대 아래 내용 영역과 같은 역할이어야 한 판으로 이어 보인다.
        //  - 두 역할이 같은 색이 되면(고대비는 창 바탕과 표면이 같다) 선택된 탭에 테를 둘러 선택을 남긴다.
        //  - `underline`은 막대 바탕을 칠하지 않는다 (처음부터의 모양).
        palette_role bar_background { palette_role::window_background };
        palette_role selected_background { palette_role::surface_background };

        [[nodiscard]] bool operator==(const tab_bar_style&) const noexcept = default;
    };

    // 한 줄 도우미(`draw_text`·`measure_text`)가 글 안의 줄바꿈을 어떻게 다루는가다.
    // 라벨·버튼·표 칸처럼 baseline 하나를 받고 폭 하나를 돌려주는 자리에 여러 줄 글(프로세스 출력·커밋 제목·
    // 붙여 넣은 값)이 오면, 전에는 shaping 구성이 줄을 아래로 내려 칸을 벗어나고 끈 구성은 .notdef 네모를
    // 그려 두 구성이 서로 다른 그림을 냈다. 어느 값이든 **두 구성이 같은 그림과 폭**을 낸다.
    //  - 줄바꿈 하나는 CR, LF, 또는 CRLF 짝(한 번으로 센다)이다.
    //  - 여러 줄을 offset과 함께 다루는 자리(`make_text_layout`·텍스트 입력·문서 보기)는 이 값을 보지 않는다.
    enum class single_line_break
    {
        // 줄바꿈마다 공백 하나로 그리고 잰다 (기본). 한 줄 칸이 무너지지 않는다.
        space,
        // 줄바꿈마다 보이는 되돌림 표시(U+21B5 "↵")로 그린다. 줄바꿈이 있다는 사실 자체가 내용인 로그·값 보기의 자리다.
        symbol,
        // 문단마다 첫 줄 아래로 한 줄씩 내려 그리고(글꼴의 줄 간격만큼), 가장 넓은 줄의 폭을 잰다.
        //  - 도우미는 높이를 알려 주지 않는다. 줄 수만큼 높은 칸은 부르는 쪽이 마련해야 하고, 그렇지 않으면
        //    아래 줄이 이웃을 덮는다.
        paragraphs,
    };

    // 한 줄 글의 앱 스타일이다 (`ui_style::text`).
    //  - popup처럼 host가 frame의 스타일(`ui_frame::style`)에서 읽어 한 줄 도우미에 건다(`set_single_line_break`).
    //    도우미에는 문맥 인자가 없어 배치 문맥으로는 닿지 않는다.
    //  - JSON(`assets/style.json`의 `text`)에 적힌다.
    struct text_style
    {
        single_line_break line_break { single_line_break::space };

        [[nodiscard]] bool operator==(const text_style&) const noexcept = default;
    };

    // 앱이 정하는 스타일이다 — 두 테마의 중립 색, accent tone, 치수.
    // 키 컬러(accent)와 테마 선호는 여기 없다. 그 둘은 사용자 설정(`appearance_settings`)이고,
    // 이것은 **앱의 것**이라 계층이 다르다: 설정이 바뀌어도 스타일은 그대로다.
    //
    // 내장값은 assets/style.json에서 빌드 시점에 만들어진다 (`default_ui_style`).
    // 앱은 (1) 자기 JSON으로 내장값을 바꿔치거나(LUIL_STYLE), (2) 실행 시점에 값을 세워
    // `ui_frame::style`로 싣는다. 둘 다 같은 자리로 온다.
    struct ui_style
    {
        neutral_color_palette dark {};
        neutral_color_palette light {};
        accent_tones tones {};
        ui_metrics metrics {};
        // 컴포넌트 스타일이다. 컴포넌트마다 한 칸이고, 그 컴포넌트가 그리기에서 읽는다
        // (`arrange_context::app_style`). 막대 config의 `style`이 비어 있으면 이 값이다.
        tab_bar_style tab_bar {};
        // popup 창의 모양이다. popup은 tree가 아니라 host가 만드는 창이라 배치 문맥이 아니라 frame의 스타일
        // (`ui_frame::style`)에서 읽는다.
        popup_style popup {};
        // 한 줄 도우미의 줄바꿈 처리다. popup과 같이 host가 frame의 스타일에서 읽는다.
        text_style text {};

        // 테마의 중립 색이다.
        // 고대비는 중립 색을 쓰지 않으므로 dark를 돌려준다 — 부르는 쪽이 고대비를
        // 먼저 걸러야 하고(`color_palette_for`가 그렇게 한다), 여기서는 답이 비지 않는다.
        [[nodiscard]] const neutral_color_palette& neutral_for(color_theme theme) const noexcept;

        [[nodiscard]] bool operator==(const ui_style&) const noexcept = default;
    };

    // 빌드 시점에 내장된 스타일이다 (assets/style.json 또는 LUIL_STYLE이 가리킨 파일).
    [[nodiscard]] const ui_style& default_ui_style() noexcept;

    // 스타일의 중립 색 위에 키 컬러를 얹은 팔레트다.
    // 고대비는 스타일을 무시하고 기본 시스템 색(검정 바탕 fallback)으로 합성한다 —
    // 접근성 설정은 앱의 스타일보다 세다. 실제 시스템 색을 아는 platform은
    // `high_contrast_palette_for`를 쓴다.
    [[nodiscard]] ui_color_palette color_palette_for(const ui_style& style, color_theme theme, const accent_definition& accent) noexcept;
}
