#include "win32/skia_renderer.h"

#include <utility>

namespace luil::win32 {
    std::unique_ptr<renderer_host> renderer_host::create(
        const HWND window, const renderer_mode mode, const renderer_fault_injection fault, IDCompositionDevice* const composition, std::u8string& error)
    {
        if (mode == renderer_mode::cpu)
        {
            renderer_factory_result cpu_result { create_cpu_skia_renderer(window) };
            if (cpu_result.renderer == nullptr)
            {
                error = std::move(cpu_result.error);
                return nullptr;
            }

            return std::unique_ptr<renderer_host>(new renderer_host { window, mode, std::move(cpu_result.renderer), false, 0 });
        }

        renderer_factory_result direct3d_result {};
        if (fault.at_creation)
            direct3d_result.error = u8"The smoke test injected a Direct3D initialization failure.";
        else if (composition == nullptr)
            direct3d_result.error = u8"Direct3D presentation needs a DirectComposition device.";
        else
            direct3d_result = create_direct3d_skia_renderer(window, composition);

        if (direct3d_result.renderer != nullptr)
            return std::unique_ptr<renderer_host>(new renderer_host { window, mode, std::move(direct3d_result.renderer), false, fault.after_frames });
        if (mode == renderer_mode::direct3d)
        {
            error = std::move(direct3d_result.error);
            return nullptr;
        }

        renderer_factory_result cpu_result { create_cpu_skia_renderer(window) };
        if (cpu_result.renderer == nullptr)
        {
            error = u8"Both Direct3D and CPU renderer initialization failed. ";
            error += cpu_result.error;
            return nullptr;
        }

        return std::unique_ptr<renderer_host>(new renderer_host { window, mode, std::move(cpu_result.renderer), true, 0 });
    }

    renderer_host::renderer_host(const HWND window, const renderer_mode mode, std::unique_ptr<skia_renderer> renderer, const bool used_fallback, const int loss_after_frames) noexcept
        : window_ { window }
        , mode_ { mode }
        , renderer_ { std::move(renderer) }
        , used_fallback_ { used_fallback }
        , loss_after_frames_ { loss_after_frames }
    {}

    bool renderer_host::injected_loss_due() noexcept
    {
        if (loss_after_frames_ <= 0 || renderer_->backend() != renderer_backend::direct3d)
            return false;
        if (frames_drawn_ < loss_after_frames_)
        {
            ++frames_drawn_;
            return false;
        }
        return true;
    }

    renderer_backend renderer_host::backend() const noexcept
    {
        return renderer_->backend();
    }

    bool renderer_host::used_fallback() const noexcept
    {
        return used_fallback_;
    }

    bool renderer_host::resize(const int width, const int height, std::u8string& error)
    {
        width_ = width;
        height_ = height;
        if (renderer_->resize(width, height, error))
            return true;
        if (mode_ != renderer_mode::automatic || renderer_->backend() != renderer_backend::direct3d)
            return false;
        if (switch_to_cpu(error) == false)
            return false;
        return renderer_->resize(width, height, error);
    }

    bool renderer_host::render(frame_state state, std::u8string& error)
    {
        state.backend = renderer_->backend();
        state.used_fallback = used_fallback_;

        // 주입된 손실은 실제 렌더를 부르지 않고 실패로 친다 — device lost가
        // **그리는 도중에** 나는 자리를 재현하는 것이 이 주입점의 전부다.
        if (injected_loss_due())
            error = u8"The smoke test injected a Direct3D device loss.";
        else if (renderer_->render(state, error))
            return true;

        if (mode_ != renderer_mode::automatic || renderer_->backend() != renderer_backend::direct3d)
            return false;

        if (switch_to_cpu(error) == false)
            return false;

        state.backend = renderer_->backend();
        state.used_fallback = true;
        return renderer_->render(state, error);
    }

    bool renderer_host::switch_to_cpu(std::u8string& error)
    {
        renderer_factory_result cpu_result { create_cpu_skia_renderer(window_) };
        if (cpu_result.renderer == nullptr)
        {
            error += u8" CPU fallback initialization also failed: ";
            error += cpu_result.error;
            return false;
        }

        // **옛 렌더러를 먼저 놓는다.** CPU 렌더러는 `GetDC` + `StretchDIBits`로
        // 그리는데, 창이 합성 대상으로 남아 있는 동안 그 픽셀은 visual tree
        // 아래에 깔려 화면에 닿지 못한다. Direct3D 렌더러의 소멸자가 창의
        // `IDCompositionTarget`을 놓아 주어야 GDI가 드러난다
        // (webview-composition-design.md).
        //  - 대입 한 줄로도 같은 순서지만(`unique_ptr`은 새 것을 담은 뒤 옛 것을
        //    지우고, 어느 쪽이든 다음 `render`보다 앞이다), 그 순서가 화면의
        //    **정확성**이라 한 줄을 따로 세워 읽는 사람이 지우지 못하게 한다.
        renderer_.reset();
        renderer_ = std::move(cpu_result.renderer);
        used_fallback_ = true;
        return true;
    }
} // namespace luil::win32
