#pragma once

#include "host/skia_renderer.h"

struct ANativeWindow;

namespace luil::android {
    // 네이티브 창의 버퍼를 잠가 그 메모리에 Skia raster로 바로 그린다.
    // 복사가 없고, 버퍼를 풀어 올리는 것이 곧 제시다 (`ANativeWindow_unlockAndPost`).
    //  - 창의 수명은 앱 host가 쥔다. 렌더러는 그동안 참조를 하나 더 잡고, 창이 사라지는
    //    알림(APP_CMD_TERM_WINDOW) 안에서 렌더러가 먼저 사라져야 한다.
    [[nodiscard]] renderer_factory_result create_cpu_skia_renderer(ANativeWindow* window);
} // namespace luil::android
