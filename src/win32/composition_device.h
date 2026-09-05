#pragma once

#include <dcomp.h>

#include <string>

namespace luil::win32 {
    // 프로세스에 하나뿐인 DirectComposition device다.
    //
    // 창마다 만들 이유가 없다 — device 하나가 여러 HWND의 target을 만들고 한 번의
    // `Commit`으로 전부 반영한다 (webview-composition-design.md).
    // 반대로 `IDCompositionTarget`은 **HWND당 정확히 하나**이며 같은 창에 두 번째를
    // 만들면 device를 바꿔도 `DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED`다. 그래서
    // 창별로 갖는 것은 target·visual·스왑체인이고 device는 여기 하나다.
    //
    // D3D device는 넘기지 않는다. `DCompositionCreateDevice2`에 `ID3D12Device*`를
    // 주면 `E_NOINTERFACE`이고, 넘길 이유도 없다 — 합성은 스왑체인을 통해 붙는다.
    class composition_host
    {
    public:
        composition_host() = default;
        composition_host(const composition_host&) = delete;
        composition_host(composition_host&&) = delete;
        composition_host& operator=(const composition_host&) = delete;
        composition_host& operator=(composition_host&&) = delete;
        ~composition_host();

        // 아직 없으면 만든다. 이미 있으면 그것을 돌려준다.
        // 실패하면 nullptr이고 `error`가 채워진다 — 부르는 쪽은 Direct3D 제시를
        // 포기하고 CPU로 물러선다 (합성 스왑체인은 device 없이 화면에 닿지 못한다).
        [[nodiscard]] IDCompositionDevice* acquire(std::u8string& error);

    private:
        IDCompositionDevice* device_ { nullptr };
        // 한 번 실패하면 다시 시도하지 않는다.
        // 창을 만들 때마다 같은 실패를 되풀이할 이유가 없다.
        bool attempted_ { false };
    };
} // namespace luil::win32
