#pragma once

#include "luil/theme/ui_theme.h"
#include "luil/ui/severity.h"

namespace luil {
    // 심각도를 그리는 규칙 한 벌이다 (토스트와 정보 막대가 같은 그림이다).
    //
    // 자리마다 따로 적으면 같은 경고가 자리마다 달라지고, 고대비 규칙(색이 접혀도 글리프가 뜻을 남긴다)을 한쪽만 지키게 된다.
    // 그래서 그림은 여기 하나다. 공개하지 않는다. 앱은 팔레트 역할(`warning_accent`·`error_accent`)을 쓴다.

    // 심각도의 역할색이다 (테와 글리프의 색).
    //  - info는 중립 알림이라 강조하지 않고 보조 글자색이다. success는 정상의 키 컬러이고 warning·error는 각자의 강조색이다.
    //  - 고대비 팔레트는 info·warning·error를 같은 창 전경색으로 접는다. success만 hotlight라 따로 선다.
    [[nodiscard]] ui_color severity_color(const ui_color_palette& palette, ui_severity severity) noexcept;

    // 심각도가 함께 나르는 글리프다 (info·pass·warning·error codicon). 네 심각도가 모두 다르다.
    [[nodiscard]] char32_t severity_glyph(ui_severity severity) noexcept;
}
