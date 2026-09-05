#pragma once

namespace luil {
    // caption 그리기(caption_element)와 Win32 비클라이언트
    // hit test(caption_layout)가 공유하는 논리 픽셀 치수다.
    // 의존성이 없는 독립 헤더라 어느 계층에서도 포함할 수 있다.
    struct caption_ui_metrics
    {
        int height { 36 };
        int button_width { 42 };
        int application_icon_slot_width { 40 };
        int title_left_padding { 14 };
        int title_icon_gap { 6 };
        int title_font_size { 14 };
        int application_icon_size { 16 };
        int button_icon_size { 14 };
    };

    inline constexpr caption_ui_metrics default_caption_ui_metrics {};

    // caption에 둘 창 버튼이다.
    // 그리기(caption_element)·비클라이언트 hit test(caption_layout)·창 스타일이
    // **같은 값**을 본다 — 셋이 어긋나면 "버튼은 없는데 그 자리가 최대화"이거나
    // "버튼은 없는데 더블클릭으로 최대화되는" 창이 된다
    // (caption-button-design.md).
    //
    // 존재를 키로 삼을 데이터가 없어(명령은 라이브러리가 고정하고 툴팁은 있어도
    // 없어도 된다) 집합을 그대로 말한다. `tab_item::closable`과 같은 선례다.
    // 기본은 셋 다 있음이라 값을 주지 않던 앱은 그대로다.
    struct caption_buttons
    {
        bool minimize { true };
        bool maximize { true };
        bool close { true };

        [[nodiscard]] bool operator==(const caption_buttons&) const = default;
    };

    inline constexpr caption_buttons default_caption_buttons {};

    // Win32 비클라이언트 hit(WM_NCMOUSEMOVE)의 UI thread 추적 상태다.
    // UI thread가 element id로 변환해 interaction snapshot에 합친다.
    enum class caption_button_hover
    {
        none,
        minimize,
        maximize,
        close,
    };
} // namespace luil
