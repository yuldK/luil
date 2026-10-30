#pragma once

#include "include/core/SkRefCnt.h"

namespace SkShapers {
    class Factory;
}

namespace luil {
    // UI text_layout·SVG의 `<text>`·Lottie 글자 레이어가 함께 쓰는 모양 잡기 공장이다.
    //
    // 두 모듈은 공장을 받지 않으면 primitive shaper(글자마다 cmap 글리프 하나)로 그린다 —
    // svg·skottie 아카이브가 HarfBuzz를 참조하지 않게 skia-prep이 그렇게 세웠다
    // (skia-prep docs/skia-build.md 6.4). 그래서 모양 잡기는 텍스트 기능을 켠 구성이
    // 공장을 넘길 때만 선다.
    //  - 켠 구성(`LUIL_ENABLE_TEXT_SHAPING`)은 `text_shaping_harfbuzz.cpp`다. HarfBuzz와
    //    **실행 시점에 연 플랫폼 ICU**(Windows 10 1903의 `icu.dll`, Android 12의
    //    `libicu.so`)로 모양 잡는다. ICU가 없을 때 `LUIL_ENABLE_TEXT_SHAPING_FALLBACK`을
    //    켠 구성은 libgrapheme과 ICU BiDi 조각을 쓴다. 끈 구성은 primitive 공장이다.
    //  - 끈 구성은 `text_shaping_default.cpp`이고 nullptr이다. 모듈의 기본을 쓰라는 뜻이며,
    //    부르는 쪽은 공장을 넘기지 않는다.
    //  - 공장은 프로세스에 하나다. ICU를 여는 일(라이브러리 적재와 함수 서른 개 찾기)을
    //    그림마다 하지 않는다. 공장은 shaper를 부를 때마다 새로 짓고 Unicode 래퍼만
    //    나눠 쓰므로 여러 디코딩 thread가 함께 써도 된다.
    [[nodiscard]] sk_sp<SkShapers::Factory> text_shaping_factory();
}
