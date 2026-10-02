#pragma once

#include "host/frame_state.h"
#include "luil/app/renderer_policy.h"

#include <functional>
#include <memory>
#include <string>

namespace luil {
    // 표면 하나에 frame을 그려 화면에 내는 Skia 백엔드다.
    // 제시까지 `render` 안에서 끝낸다. 표면이 무엇인지(창 핸들, 네이티브 창)는 플랫폼이
    // 렌더러를 만들 때만 알고, 이 interface는 모른다.
    class skia_renderer
    {
    public:
        skia_renderer() = default;
        skia_renderer(const skia_renderer&) = delete;
        skia_renderer(skia_renderer&&) = delete;
        skia_renderer& operator=(const skia_renderer&) = delete;
        skia_renderer& operator=(skia_renderer&&) = delete;
        virtual ~skia_renderer() = default;

        [[nodiscard]] virtual renderer_backend backend() const noexcept = 0;
        [[nodiscard]] virtual bool resize(int width, int height, std::u8string& error) = 0;
        [[nodiscard]] virtual bool render(const frame_state& state, std::u8string& error) = 0;
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
        // 만들 때 GPU 렌더러를 실패시킨다. 스왑체인이 아예 서지 않는다.
        // 이 실패는 플랫폼의 GPU 생성 함수가 낸다 — 생성 전 조건(합성 device 등)과 같은
        // 자리에서 실패해야 재현이 된다.
        bool at_creation { false };
        // 이만큼 그린 뒤 다음 `render`를 실패시킨다 (0이면 하지 않는다).
        int after_frames { 0 };
    };

    // 플랫폼이 넘기는 렌더러 생성 함수들이다.
    // 실패 물러섬 정책(`renderer_host`)은 플랫폼을 가리지 않고, 무엇을 어떻게 만드는지만
    // 플랫폼마다 다르다.
    struct renderer_factories
    {
        // 오류 글에 쓰는 GPU 백엔드 이름이다 (Win32는 "Direct3D").
        std::u8string gpu_name { u8"GPU" };
        // 이 플랫폼의 GPU 렌더러를 만든다. 비어 있으면 GPU가 없는 것이다.
        std::function<renderer_factory_result()> create_gpu {};
        std::function<renderer_factory_result()> create_cpu {};
    };

    // 모드가 GPU를 반드시 요구하는가다 (`direct3d`, `gpu`).
    // 요구하는 모드는 GPU를 세우지 못하면 CPU로 물러서지 않고 실패를 돌려준다.
    [[nodiscard]] constexpr bool renderer_mode_requires_gpu(const renderer_mode mode) noexcept
    {
        return mode == renderer_mode::direct3d || mode == renderer_mode::gpu;
    }

    // 표면 하나의 렌더러와 그 실패 물러섬이다.
    // `automatic`은 GPU를 먼저 세우고, 만들거나 크기를 바꾸거나 그리다 실패하면 CPU로
    // 바꾼다. 한 번 물러선 표면은 GPU를 다시 시도하지 않는다 (rendering.md).
    class renderer_host
    {
    public:
        static std::unique_ptr<renderer_host> create(renderer_mode mode, renderer_fault_injection fault, renderer_factories factories, std::u8string& error);

        [[nodiscard]] renderer_backend backend() const noexcept;
        [[nodiscard]] bool used_fallback() const noexcept;
        [[nodiscard]] bool resize(int width, int height, std::u8string& error);
        [[nodiscard]] bool render(frame_state state, std::u8string& error);
        // 지금 렌더러다. 플랫폼이 자기 확장(Win32의 웹뷰 합성 자리)을 물을 때 쓴다.
        //  - CPU로 물러서면 다른 객체가 된다. 들고 있지 말고 쓸 때마다 묻는다.
        [[nodiscard]] skia_renderer& current() noexcept;

    private:
        renderer_host(renderer_mode mode, renderer_factories factories, std::unique_ptr<skia_renderer> renderer, bool used_fallback, int loss_after_frames) noexcept;

        [[nodiscard]] bool switch_to_cpu(std::u8string& error);
        // 주입된 device loss가 이번 frame에 오는가.
        // GPU로 그리는 동안에만 세고, 세는 값에 닿으면 실제 렌더를 부르지
        // 않고 실패로 친다 — device lost가 그리는 도중에 나는 것과 같은 자리다.
        [[nodiscard]] bool injected_loss_due() noexcept;

        renderer_mode mode_ { renderer_mode::automatic };
        renderer_factories factories_ {};
        std::unique_ptr<skia_renderer> renderer_ {};
        bool used_fallback_ { false };
        int width_ { 1 };
        int height_ { 1 };
        int loss_after_frames_ { 0 };
        int frames_drawn_ { 0 };
    };
} // namespace luil
