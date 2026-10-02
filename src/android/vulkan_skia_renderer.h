#pragma once

#include "host/skia_renderer.h"

struct ANativeWindow;

namespace luil::android {
    class vulkan_device;

    // 네이티브 창 하나에 Vulkan 스왑체인을 세워 Skia(Ganesh)로 그린다.
    //  - 장치는 빌려 쓴다. 앱 host가 쥐고, 이 렌더러보다 오래 산다 (vulkan_device.h).
    //  - 창의 수명은 CPU 렌더러와 같다. 렌더러가 참조를 하나 더 잡고, 창이 사라지는 알림
    //    (APP_CMD_TERM_WINDOW) 안에서 렌더러가 먼저 사라져야 한다.
    //  - 소멸자가 스왑체인과 `VkSurfaceKHR`을 부숴 창과의 연결을 끊는다. 창은 생산자를 하나만
    //    받으므로, CPU로 물러설 때 CPU 렌더러가 버퍼를 잠그려면 이것이 먼저다.
    [[nodiscard]] renderer_factory_result create_vulkan_skia_renderer(vulkan_device& device, ANativeWindow* window);
} // namespace luil::android
