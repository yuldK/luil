#include "win32/skia_renderer.h"

#include <utility>

namespace luil::win32 {
    IDCompositionVisual* renderer_underlay(renderer_host& host) noexcept
    {
        // 합성 자리를 내는 것은 Direct3D 렌더러뿐이다. 물러선 CPU 렌더러는 이 확장이 없다.
        auto* const composition { dynamic_cast<composition_renderer*>(&host.current()) };
        return composition != nullptr ? composition->underlay() : nullptr;
    }

    std::unique_ptr<renderer_host> create_renderer_host(const HWND window, const renderer_mode mode, const renderer_fault_injection fault, IDCompositionDevice* const composition, std::u8string& error)
    {
        renderer_factories factories {};
        factories.gpu_name = u8"Direct3D";
        factories.create_gpu = [window, fault, composition] {
            renderer_factory_result result {};
            if (fault.at_creation)
                result.error = u8"The smoke test injected a Direct3D initialization failure.";
            else if (composition == nullptr && direct3d_renderer_built())
                result.error = u8"Direct3D presentation needs a DirectComposition device.";
            else
                // 렌더러를 뺀 빌드에서는 stub이 그 사실을 오류로 돌려준다.
                result = create_direct3d_skia_renderer(window, composition);
            return result;
        };
        factories.create_cpu = [window] { return create_cpu_skia_renderer(window); };
        return renderer_host::create(mode, fault, std::move(factories), error);
    }
} // namespace luil::win32
