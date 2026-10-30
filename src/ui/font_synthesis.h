#pragma once

#include "luil/text/font_style.h"

#include "include/core/SkRefCnt.h"

class SkFont;
class SkTypeface;

// face가 요청한 굵기·기울임을 다 내지 못할 때 덧칠·눕힘으로 메우는 규칙이다 (docs/font-style-design.md).
// 글꼴 등록부와 대체 글꼴을 고르는 그리기·배치가 같은 규칙을 써야 한 줄의 굵기가 고르다.
//  - Skia만 본다. 글꼴 등록부를 부르지 않아 등록부 없이 링크하는 그리기 조각도 쓴다.
namespace luil {
    // 기울임을 흉내 낼 때 글자를 눕히는 정도다 (`SkFont::setSkewX`).
    inline constexpr float synthetic_skew { -0.2f };

    // face가 실제로 그리는 굵기다 (1~1000).
    // 가변 글꼴은 `wght` 축의 지금 자리를 먼저 본다 — DirectWrite는 축으로 clone한 face에도 원래 face의 굵기를 밝힌다.
    [[nodiscard]] int achieved_weight(const SkTypeface& face);

    // face의 `wght` 축 범위다. 축이 없으면 거짓이다.
    [[nodiscard]] bool weight_axis_range(const SkTypeface& face, float& minimum, float& maximum);

    // face를 `requested` 모양으로 그리는 데 필요한 메움이다.
    //  - 굵기: `synthetic_bold_threshold` 이상을 요청했는데 face가 그보다 가늘면 덧칠한다.
    //  - 기울임: 기울임을 요청했는데 face가 똑바로 서 있으면 눕힌다.
    struct font_synthesis
    {
        bool embolden { false };
        bool skew { false };
    };
    [[nodiscard]] font_synthesis synthesis_for(const SkTypeface& face, font_style requested);

    // `font`의 face를 바꾸고, 그 face로 `requested`를 그리도록 덧칠·눕힘을 다시 건다.
    // 대체 글꼴로 넘어가는 글자가 쓴다 — 바탕 글꼴의 메움을 그대로 이으면, 바탕이 진짜 Bold일 때 굵은 대체 face가
    // 덧칠을 못 받거나 바탕이 덧칠일 때 굵은 대체 face가 두 번 굵어진다.
    void use_face(SkFont& font, sk_sp<SkTypeface> face, font_style requested);
}
