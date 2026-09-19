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
} // namespace luil
