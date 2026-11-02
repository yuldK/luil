#pragma once

#include "luil/ui/ink.h"

#include "include/core/SkPath.h"
#include "include/core/SkPicture.h"
#include "include/core/SkRefCnt.h"

#include <mutex>
#include <vector>

class SkCanvas;

namespace luil {
    // `ink_stroke`의 실체다. 만든 뒤 바뀌지 않는다.
    struct ink_stroke_backing
    {
        std::uint64_t id { 0 };
        std::vector<ink_point> points {};
        ink_style style {};
        // 내용 논리 픽셀의 채울 모양이다 (nonzero).
        SkPath path {};
        rect_f bounds {};
    };

    // `ink_layer`의 실체다. 획 목록은 바뀌지 않고, 그림 memo만 자물쇠 안에서 처음 그릴 때 선다.
    //  - memo는 기본색 하나에 매인다. 테마가 바뀌어 기본색이 달라지면 다시 기록한다.
    struct ink_layer_backing
    {
        std::vector<ink_stroke> strokes {};
        mutable std::mutex mutex {};
        mutable sk_sp<SkPicture> picture {};
        mutable ui_color picture_color { 0 };
    };

    // 라이브러리 안에서 획과 묶음을 그리는 문이다 (공개 헤더에 Skia 타입을 늘리지 않으려고 둔다).
    struct ink_access
    {
        // 획 하나를 지금 변환 그대로 그린다 (내용 논리 픽셀). 기본색 획은 `default_color`다.
        static void draw(SkCanvas& canvas, const ink_stroke& stroke, ui_color default_color);
        // 묶음을 memo한 그림 한 장으로 그린다. 기본색이 memo와 다르면 다시 기록한다.
        static void draw(SkCanvas& canvas, const ink_layer& layer, ui_color default_color);
        // 묶음의 그림이 지금 memo에 있는가다 (test가 다시 쓰기를 확인한다).
        [[nodiscard]] static const SkPicture* memo(const ink_layer& layer);
    };
}
