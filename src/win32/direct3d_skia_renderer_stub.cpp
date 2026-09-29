#include "win32/skia_renderer.h"

// Direct3D 렌더러를 끈 구성(LUIL_ENABLE_DIRECT3D=OFF)의 자리다.
// Ganesh·D3D12 헤더 없이 컴파일되며 아무것도 세우지 않는다 — `automatic`은
// 생성 실패와 같은 길로 CPU에 물러서고, `direct3d`를 명시한 창은 이 오류로 멈춘다.
// luil에서 Ganesh를 부르는 파일은 실제 렌더러 하나뿐이라, 이것으로 바뀌면
// 링커가 Skia의 GPU 코드를 실행 파일에 싣지 않는다.
namespace luil::win32 {
    bool direct3d_renderer_built() noexcept
    {
        return false;
    }

    renderer_factory_result create_direct3d_skia_renderer(const HWND window, IDCompositionDevice* const composition)
    {
        static_cast<void>(window);
        static_cast<void>(composition);
        return { nullptr, u8"This build has no Direct3D renderer (LUIL_ENABLE_DIRECT3D=OFF)." };
    }
} // namespace luil::win32
