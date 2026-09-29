#pragma once

#include "luil/win32/renderer_policy.h"
#include "win32/frame_state.h"

#include <windows.h>

#include <memory>
#include <string>

struct IDCompositionDevice;
struct IDCompositionVisual;

namespace luil::win32 {
    class skia_renderer
    {
    public:
        virtual ~skia_renderer() = default;

        [[nodiscard]] virtual renderer_backend backend() const noexcept = 0;
        [[nodiscard]] virtual bool resize(int width, int height, std::u8string& error) = 0;
        [[nodiscard]] virtual bool render(const frame_state& state, std::u8string& error) = 0;

        // 우리가 그린 것 **아래**에 놓이는 visual이다. 웹뷰가 여기 들어간다.
        //
        // 없으면 nullptr다 — CPU 렌더러가 그렇다. 그 백엔드에는 합성이 없어
        // 웹뷰가 설 자리도 없고, 자리표의 placeholder가 그 사실을 화면에 남긴다
        // (webview-composition-design.md).
        [[nodiscard]] virtual IDCompositionVisual* underlay() noexcept
        {
            return nullptr;
        }
    };

    struct renderer_factory_result
    {
        std::unique_ptr<skia_renderer> renderer {};
        std::u8string error {};
    };

    // 렌더러 경로를 재현하기 위한 주입점이다 (smoke test 전용).
    // 두 실패는 화면에서 하는 일이 달라 갈라 둔다 — win32_window.h의
    // `simulate_direct3d_loss_after_frames`에 그 이유가 있다.
    struct renderer_fault_injection
    {
        // 만들 때 Direct3D를 실패시킨다. 스왑체인이 아예 서지 않는다.
        bool at_creation { false };
        // 이만큼 그린 뒤 다음 `render`를 실패시킨다 (0이면 하지 않는다).
        int after_frames { 0 };
    };

    class renderer_host
    {
    public:
        // `composition`은 Direct3D 제시가 붙을 DirectComposition device다
        // (프로세스에 하나 — composition_device.h). nullptr이면 Direct3D를
        // 만들지 않는다: 합성 스왑체인은 device 없이 화면에 닿지 못하므로
        // 그것이 곧 "Direct3D를 쓸 수 없다"이고, automatic 모드는 CPU로 물러선다.
        static std::unique_ptr<renderer_host> create(HWND window, renderer_mode mode, renderer_fault_injection fault, IDCompositionDevice* composition, std::u8string& error);

        [[nodiscard]] renderer_backend backend() const noexcept;
        [[nodiscard]] bool used_fallback() const noexcept;
        [[nodiscard]] bool resize(int width, int height, std::u8string& error);
        [[nodiscard]] bool render(frame_state state, std::u8string& error);
        // 지금 렌더러가 내주는 웹뷰 자리다 (없으면 nullptr).
        //  - CPU로 물러선 창은 여기서 nullptr가 되고, 그 순간 웹뷰는 설 자리를 잃는다.
        [[nodiscard]] IDCompositionVisual* underlay() noexcept;

    private:
        renderer_host(HWND window, renderer_mode mode, std::unique_ptr<skia_renderer> renderer, bool used_fallback, int loss_after_frames) noexcept;

        [[nodiscard]] bool switch_to_cpu(std::u8string& error);
        // 주입된 device loss가 이번 frame에 오는가.
        // Direct3D로 그리는 동안에만 세고, 세는 값에 닿으면 실제 렌더를 부르지
        // 않고 실패로 친다 — device lost가 그리는 도중에 나는 것과 같은 자리다.
        [[nodiscard]] bool injected_loss_due() noexcept;

        HWND window_ { nullptr };
        renderer_mode mode_ { renderer_mode::automatic };
        std::unique_ptr<skia_renderer> renderer_ {};
        bool used_fallback_ { false };
        int width_ { 1 };
        int height_ { 1 };
        int loss_after_frames_ { 0 };
        int frames_drawn_ { 0 };
    };

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
