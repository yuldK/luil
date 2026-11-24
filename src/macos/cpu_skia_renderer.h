#pragma once

#include "host/skia_renderer.h"

namespace luil::macos {
    // Skia raster로 그린 픽셀을 view의 layer 내용(`CALayer.contents`, CGImage)으로 낸다.
    //  - `layer`는 `CALayer*`다. 렌더러보다 오래 살아야 하고, 렌더러는 UI thread(메인 thread)에서만 쓴다.
    //  - 픽셀 형식은 Skia의 N32(BGRA, premultiplied)이고 색 공간은 sRGB다. CGImage가 같은 배치를 그대로 읽어 변환이 없다.
    [[nodiscard]] renderer_factory_result create_cpu_skia_renderer(void* layer);
}
