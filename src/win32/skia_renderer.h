#pragma once

#include "host/skia_renderer.h"

#include <windows.h>

#include <memory>
#include <string>

struct IDCompositionDevice;
struct IDCompositionVisual;

namespace luil::win32 {
    // DirectComposition에 제시하는 렌더러다 (Direct3D).
    // 렌더러 interface(host/skia_renderer.h)는 플랫폼을 모르므로 합성 자리는 이 확장이 낸다.
    class composition_renderer : public skia_renderer
    {
    public:
        // 우리가 그린 것 **아래**에 놓이는 visual이다. 웹뷰가 여기 들어간다.
        [[nodiscard]] virtual IDCompositionVisual* underlay() noexcept = 0;
    };

    // 지금 렌더러가 내주는 웹뷰 자리다.
    //
    // 없으면 nullptr다 — CPU 렌더러가 그렇다. 그 백엔드에는 합성이 없어
    // 웹뷰가 설 자리도 없고, 자리표의 placeholder가 그 사실을 화면에 남긴다
    // (webview-composition-design.md).
    //  - CPU로 물러선 창은 여기서 nullptr가 되고, 그 순간 웹뷰는 설 자리를 잃는다.
    [[nodiscard]] IDCompositionVisual* renderer_underlay(renderer_host& host) noexcept;

    // 창 하나의 렌더러를 세운다.
    // `composition`은 Direct3D 제시가 붙을 DirectComposition device다
    // (프로세스에 하나 — composition_device.h). nullptr이면 Direct3D를
    // 만들지 않는다: 합성 스왑체인은 device 없이 화면에 닿지 못하므로
    // 그것이 곧 "Direct3D를 쓸 수 없다"이고, automatic 모드는 CPU로 물러선다.
    [[nodiscard]] std::unique_ptr<renderer_host> create_renderer_host(HWND window, renderer_mode mode, renderer_fault_injection fault, IDCompositionDevice* composition, std::u8string& error);

    [[nodiscard]] renderer_factory_result create_cpu_skia_renderer(HWND window);
    // 이 빌드에 Direct3D 렌더러가 들어 있는가 (LUIL_ENABLE_DIRECT3D).
    // 없으면 표면은 DirectComposition device도 묻지 않는다 — 쓸 곳이 없는데
    // 물으면 그 때문에 프로세스에 device가 생긴다.
    [[nodiscard]] bool direct3d_renderer_built() noexcept;
    // 합성 스왑체인을 만들어 `composition`이 만든 visual에 얹는다.
    // 창의 `IDCompositionTarget`도 이 렌더러가 소유한다 — HWND당 하나뿐인 자원이라
    // 렌더러가 살아 있는 동안만 창이 합성 대상이고, 놓으면 GDI가 다시 드러난다
    // (webview-composition-design.md).
    [[nodiscard]] renderer_factory_result create_direct3d_skia_renderer(HWND window, IDCompositionDevice* composition);
} // namespace luil::win32
