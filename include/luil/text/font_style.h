#pragma once

namespace luil {
    // 글꼴의 굵기다. CSS·OpenType의 `wght`와 같은 수(1~1000)다.
    //  - 이름 사이의 값(350 같은 것)은 `static_cast<font_weight>(350)`으로 넣는다. 가변 글꼴은 그 값 그대로 그리고,
    //    굵기별 face만 있는 글꼴은 가장 가까운 face를 고른다. 범위 밖 값은 1~1000으로 자른다.
    enum class font_weight : int
    {
        thin = 100,
        extra_light = 200,
        light = 300,
        regular = 400,
        medium = 500,
        semibold = 600,
        bold = 700,
        extra_bold = 800,
        black = 900,
    };

    // 글꼴의 기울임이다.
    enum class font_slant
    {
        upright,
        italic,
    };

    // 글꼴에 거는 모양이다 (docs/font-style-design.md).
    // 굵기와 기울임을 한 값으로 들고 다닌다 — 글꼴 등록부·측정·그리기가 이 값 하나로 같은 face를 찾고, 다음 글꼴
    // 처리(폭·자간 같은 것)도 이 값에 칸을 늘려 같은 길을 탄다.
    //  - 기본값(regular·upright)은 받은 face를 그대로 쓴다는 뜻이다. 굵은 파일을 읽은 앱 글꼴은 기본값이어도 굵다.
    struct font_style
    {
        font_weight weight { font_weight::regular };
        font_slant slant { font_slant::upright };

        [[nodiscard]] bool operator==(const font_style&) const noexcept = default;
    };

    // 이 굵기 이상을 요청했는데 찾은 face가 이보다 가늘면 획을 덧칠해 메운다.
    // 브라우저의 `font-synthesis`와 같은 기준이다 — 굵기가 하나뿐인 글꼴도 굵게 요청한 글은 굵어 보인다.
    inline constexpr font_weight synthetic_bold_threshold { font_weight::semibold };

    // 굵기를 1~1000으로 자른 수다.
    [[nodiscard]] constexpr int font_weight_value(const font_weight weight) noexcept
    {
        const int value { static_cast<int>(weight) };
        return value < 1 ? 1 : value > 1000 ? 1000 : value;
    }
}
