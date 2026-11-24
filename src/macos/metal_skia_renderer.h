#pragma once

#include "host/skia_renderer.h"

namespace luil::macos {
    // Skia Ganesh를 Metal 위에 세워 그리는 렌더러다. view layer의 자식으로 `CAMetalLayer`를 하나 달고 그 drawable에 그린다.
    //  - `layer`는 Skia view의 `CALayer*`다. 렌더러보다 오래 살아야 하고, 렌더러는 UI thread(메인 thread)에서만 쓴다.
    //  - Metal layer는 불투명하지 않다. 아래층 웹뷰의 구멍(알파 0)이 그대로 비친다.
    //  - 렌더러가 사라지면 Metal layer도 떼어 낸다. CPU로 물러서면 부모 layer의 내용(CGImage)이 다시 보인다.
    //  - 장치를 만들지 못하면(Metal이 없는 가상 기계 등) 이유와 함께 렌더러가 없다. `automatic`은 CPU로 물러선다.
    [[nodiscard]] renderer_factory_result create_metal_skia_renderer(void* layer);
}
