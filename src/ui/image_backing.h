#pragma once

#include "luil/ui/image_element.h"

#include "include/core/SkImage.h"
#include "include/core/SkPicture.h"
#include "include/core/SkRefCnt.h"

#include <chrono>
#include <memory>

class SkCanvas;
struct SkRect;

namespace luil {
    // `ui_image`의 실체다.
    //
    // 둘 중 하나가 선다. 래스터 그림(코덱·`make_rgba_image`)은 `image`이고, 벡터 그림(SVG)은
    // `picture`다 — 그리기는 `picture`가 있으면 화면 배율대로 다시 그리고, 없으면 픽셀을
    // 뽑는다. 벡터를 픽셀로 바꿔 들지 않는 것이 요점이다: 확대(`zoom_view`)와 높은 배율의
    // 창에서 선이 그대로 날카롭다.
    //  - `SkPicture`는 만들어진 뒤 바뀌지 않아 thread 사이로 나눠 그려도 된다. 그래서 SVG
    //    DOM이 아니라 그것을 한 번 기록한 그림을 든다 (DOM은 그릴 때마다 상태를 바꾼다).
    struct image_backing
    {
        sk_sp<SkImage> image {};
        sk_sp<SkPicture> picture {};
    };

    // `ui_lottie`의 실체다.
    //
    // **그리기만 내보이는 인터페이스다.** 구현(`lottie_decode.cpp`)이 Skottie를 든다 —
    // `image_element`가 Skottie를 직접 부르면 그림을 그리는 모든 앱이 Skottie를 링크하게
    // 된다. 가상 함수 하나를 사이에 두면 Lottie를 읽는 앱만 그 파일과 아카이브를 끌어온다.
    //  - `draw`는 `const`이지만 구현은 장면을 그 시각으로 옮긴다(Skottie의 `seek`).
    //    그래서 구현이 자물쇠로 감싼다 — 같은 값을 두 창이 함께 그릴 수 있다.
    struct lottie_backing
    {
        lottie_backing() = default;
        lottie_backing(const lottie_backing&) = delete;
        lottie_backing(lottie_backing&&) = delete;
        lottie_backing& operator=(const lottie_backing&) = delete;
        lottie_backing& operator=(lottie_backing&&) = delete;
        virtual ~lottie_backing() = default;

        // `seconds` 시각의 장면을 `bounds`(그림 자신의 좌표, `{0, 0, width, height}`)에 그린다.
        virtual void draw(SkCanvas& canvas, double seconds, const SkRect& bounds) const = 0;
    };

    // 라이브러리 안에서 값을 짓는 문이다 (공개 헤더에 Skia 타입을 늘리지 않으려고 둔다).
    struct image_access
    {
        // 기록한 그림 하나로 벡터 `ui_image`를 짓는다. 그림의 좌표는 `{0, 0, width, height}`다.
        // 그림이 없거나 크기가 양수가 아니면 빈 값이다.
        [[nodiscard]] static ui_image picture_image(sk_sp<SkPicture> picture, int width, int height);
        // premul 래스터 그림 하나로 `ui_image`를 짓는다 (고품질 축소의 결과). 그림이 없으면 빈 값이다.
        [[nodiscard]] static ui_image raster_image(sk_sp<SkImage> image);
        // Lottie 하나를 짓는다. 길이와 장 빠르기가 양수가 아니면 빈 값이다.
        [[nodiscard]] static ui_lottie lottie(std::shared_ptr<const lottie_backing> backing, int width, int height, std::chrono::nanoseconds duration, double frame_rate);
    };
}
