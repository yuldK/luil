#include "win32/skia_renderer.h"

#include "host/fence_wait.h"
#include "host/font_registry.h"
#include "win32/embedded_assets.h"
#include "win32/win32_error.h"

#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendSurface.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"

#include <d3d12.h>
#include <dcomp.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

namespace luil::win32 {
    namespace {
        constexpr std::size_t frame_count { 2 };

        gr_cp<IDXGIAdapter1> find_hardware_adapter(IDXGIFactory4& factory)
        {
            for (UINT adapter_index { 0 };; ++adapter_index)
            {
                IDXGIAdapter1* raw_adapter { nullptr };
                if (factory.EnumAdapters1(adapter_index, &raw_adapter) == DXGI_ERROR_NOT_FOUND)
                    break;

                gr_cp<IDXGIAdapter1> adapter { raw_adapter };
                DXGI_ADAPTER_DESC1 description {};
                if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                    continue;
                if (SUCCEEDED(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
                    return adapter;
            }
            return nullptr;
        }

        class direct3d_skia_renderer final : public skia_renderer
        {
        public:
            direct3d_skia_renderer(const HWND window, IDCompositionDevice& composition)
                : window_(window)
                , composition_(&composition)
                , codicon_typeface_(load_codicon_typeface())
                , ui_typeface_(load_ui_typeface())
            {}

            ~direct3d_skia_renderer() override
            {
                // CPU fallback 전에 창을 합성 대상에서 분리한다.
                // visual tree가 붙어 있으면 GDI 픽셀이 그 아래에 가려지므로,
                // root를 분리하고 Commit한 뒤 target을 해제한다.
                if (target_ != nullptr)
                {
                    static_cast<void>(target_->SetRoot(nullptr));
                    static_cast<void>(composition_->Commit());
                }
                if (visual_ != nullptr)
                    visual_->Release();
                if (underlay_ != nullptr)
                    underlay_->Release();
                if (root_ != nullptr)
                    root_->Release();
                if (target_ != nullptr)
                    target_->Release();

                // 대기가 실패해도(예산 소진·device removal) 그대로 진행한다.
                // 여기서 더 기다릴 길이 없고, 아래 정리는 abandon 계열이라 GPU에
                // 새 일을 시키지 않는다.
                static_cast<void>(wait_for_gpu());
                release_surfaces();
                if (context_ != nullptr)
                    context_->releaseResourcesAndAbandonContext();
                context_.reset();
                if (fence_event_ != nullptr)
                    CloseHandle(fence_event_);
            }

            [[nodiscard]] bool initialize(std::u8string& error)
            {
                HRESULT result { CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)) };
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to create the DXGI factory", result);
                    return false;
                }

                adapter_ = find_hardware_adapter(*factory_.get());
                if (!adapter_)
                {
                    error = u8"No hardware adapter supports Direct3D 12.";
                    return false;
                }

                result = D3D12CreateDevice(adapter_.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to create the Direct3D 12 device", result);
                    return false;
                }

                D3D12_COMMAND_QUEUE_DESC queue_description {};
                queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                queue_description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
                result = device_->CreateCommandQueue(&queue_description, IID_PPV_ARGS(&queue_));
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to create the Direct3D command queue", result);
                    return false;
                }

                GrD3DBackendContext backend_context {};
                backend_context.fAdapter = adapter_;
                backend_context.fDevice = device_;
                backend_context.fQueue = queue_;
                context_ = GrDirectContexts::MakeD3D(backend_context);
                if (context_ == nullptr)
                {
                    error = u8"Failed to create the Skia Direct3D context.";
                    return false;
                }

                RECT client_rectangle {};
                if (GetClientRect(window_, &client_rectangle) == FALSE)
                {
                    error = u8"The Direct3D renderer failed to read the initial client size.";
                    return false;
                }
                width_ = std::max(1L, client_rectangle.right - client_rectangle.left);
                height_ = std::max(1L, client_rectangle.bottom - client_rectangle.top);

                DXGI_SWAP_CHAIN_DESC1 swap_chain_description {};
                swap_chain_description.BufferCount = static_cast<UINT>(frame_count);
                swap_chain_description.Width = static_cast<UINT>(width_);
                swap_chain_description.Height = static_cast<UINT>(height_);
                swap_chain_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                swap_chain_description.SampleDesc.Count = 1;
                // **합성에서 알파가 뜻을 갖는다.** 이 값이 없으면(UNSPECIFIED)
                // 알파 0인 픽셀이 불투명 검정으로 합성돼, 아래 visual을 비추는
                // 길이 오류 없이 사라진다 (webview-composition-design.md).
                // 화면 전체는 여전히 불투명하게 칠해지므로(`draw_frame`의
                // `canvas.clear`) 지금 화면은 달라지지 않는다.
                swap_chain_description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

                gr_cp<IDXGISwapChain1> initial_swap_chain {};
                // 창이 아니라 **합성**에 붙는다. HWND를 인자로 받지 않으므로 그
                // 창에는 flip model 스왑체인이 연결되지 않고, 첫 Present가 창의
                // GDI 경로를 영구히 죽이던 일도 함께 사라진다.
                result = factory_->CreateSwapChainForComposition(queue_.get(), &swap_chain_description, nullptr, &initial_swap_chain);
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to create the Direct3D composition swapchain", result);
                    return false;
                }
                factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
                result = initial_swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain_));
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to query IDXGISwapChain3", result);
                    return false;
                }
                if (attach_composition(error) == false)
                    return false;

                result = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to create the Direct3D fence", result);
                    return false;
                }
                fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (fence_event_ == nullptr)
                {
                    error = u8"Failed to create the Direct3D fence event.";
                    return false;
                }
                return setup_surfaces(error);
            }

            [[nodiscard]] renderer_backend backend() const noexcept override
            {
                return renderer_backend::direct3d;
            }

            [[nodiscard]] IDCompositionVisual* underlay() noexcept override
            {
                return underlay_;
            }

            [[nodiscard]] bool resize(const int width, const int height, std::u8string& error) override
            {
                const int safe_width { std::max(1, width) };
                const int safe_height { std::max(1, height) };
                if (safe_width == width_ && safe_height == height_)
                    return true;
                if (wait_for_gpu() == false)
                {
                    error = fence_wait_error(u8"Failed to wait for the GPU before resizing Direct3D resources");
                    return false;
                }

                context_->flush();
                context_->submit(GrSyncCpu::kYes);
                release_surfaces();
                const HRESULT result {
                    swap_chain_->ResizeBuffers(static_cast<UINT>(frame_count), static_cast<UINT>(safe_width), static_cast<UINT>(safe_height), DXGI_FORMAT_R8G8B8A8_UNORM, 0),
                };

                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to resize the Direct3D swapchain", result);
                    return false;
                }
                width_ = safe_width;
                height_ = safe_height;
                frame_fence_values_.fill(0);
                return setup_surfaces(error);
            }

            [[nodiscard]] bool render(const frame_state& state, std::u8string& error) override
            {
                // 투명한 구멍이 있는 frame에서는 LCD 서브픽셀 글자를 끈다.
                // 알파 0인 배경에 채널별 커버리지가 섞이면 경계에 잘못된 색이 생긴다.
                // 구멍 유무가 바뀔 때만 같은 백버퍼를 다른 픽셀 배치로 다시 감싼다.
                // 구멍이 없는 frame은 LCD 표시를 유지한다.
                if (const bool subpixel { state.holes.empty() }; subpixel != subpixel_text_)
                {
                    subpixel_text_ = subpixel;
                    if (wait_for_gpu() == false)
                    {
                        error = fence_wait_error(u8"Failed to wait for the GPU before changing the text pixel geometry");
                        return false;
                    }
                    context_->flush();
                    context_->submit(GrSyncCpu::kYes);
                    release_surfaces();
                    if (setup_surfaces(error) == false)
                        return false;
                }

                const UINT frame_index { swap_chain_->GetCurrentBackBufferIndex() };
                if (wait_for_fence(frame_fence_values_[frame_index]) == false)
                {
                    error = fence_wait_error(u8"Failed to wait for the Direct3D back buffer fence");
                    return false;
                }

                // UI 글꼴은 설정이 정한다.
                // registry가 cache하므로 매 frame 물어도 된다.
                const sk_sp<SkTypeface> ui { configured_ui_typeface() };
                draw_frame(*surfaces_[frame_index]->getCanvas(), codicon_typeface_.get(), ui.get(), state);
                GrFlushInfo flush_info {};
                context_->flush(surfaces_[frame_index].get(), SkSurfaces::BackendSurfaceAccess::kPresent, flush_info);
                context_->submit();

                HRESULT result { swap_chain_->Present(1, 0) };
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to present the Direct3D frame", result);
                    return false;
                }

                const std::uint64_t signal_value { ++next_fence_value_ };
                result = queue_->Signal(fence_.get(), signal_value);
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to signal the Direct3D queue", result);
                    return false;
                }
                frame_fence_values_[frame_index] = signal_value;
                return true;
            }

        private:
            // 창을 합성 대상으로 만들고 스왑체인을 그 visual에 얹는다.
            // `IDCompositionTarget`은 HWND당 하나뿐이라 이 렌더러가 사는 동안만
            // 창이 합성 대상이다 — 소멸자가 놓으면 GDI가 다시 드러난다.
            [[nodiscard]] bool attach_composition(std::u8string& error)
            {
                // topmost는 redirection bitmap과의 위아래를 바꾸지 않는다.
                // GDI는 언제나 visual tree **아래**이므로 값의 뜻은 다른 합성
                // 대상과의 순서뿐이고, 우리는 창마다 target 하나라 어느 쪽이든
                // 같다 (webview-composition-design.md).
                HRESULT result { composition_->CreateTargetForHwnd(window_, TRUE, &target_) };
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to create the DirectComposition target", result);
                    return false;
                }
                // visual 셋을 세운다.
                //
                //   root
                //     ├─ underlay   ← 웹뷰가 여기 들어간다 (아래)
                //     └─ visual     ← 우리 스왑체인 (위)
                //
                // 우리가 위에 있어야 알파 0으로 비운 자리에 **아래**가 비친다.
                // 컨테이너를 따로 두는 이유: `IDCompositionTarget`은 root를 하나만
                // 받고, 그 하나가 스왑체인이면 형제를 놓을 자리가 없다.
                for (IDCompositionVisual** slot : { &root_, &underlay_, &visual_ })
                {
                    result = composition_->CreateVisual(slot);
                    if (FAILED(result))
                    {
                        error = make_hresult_error(u8"Failed to create a DirectComposition visual", result);
                        return false;
                    }
                }
                result = visual_->SetContent(swap_chain_.get());
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to attach the swapchain to the DirectComposition visual", result);
                    return false;
                }
                // 먼저 넣은 visual이 아래에 놓이도록 두 번째 삽입의 기준 visual을 명시한다.
                result = root_->AddVisual(underlay_, FALSE, nullptr);
                if (SUCCEEDED(result))
                    result = root_->AddVisual(visual_, TRUE, underlay_);
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to order the DirectComposition visuals", result);
                    return false;
                }
                result = target_->SetRoot(root_);
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to set the DirectComposition root visual", result);
                    return false;
                }
                result = composition_->Commit();
                if (FAILED(result))
                {
                    error = make_hresult_error(u8"Failed to commit the DirectComposition tree", result);
                    return false;
                }
                return true;
            }

            [[nodiscard]] bool setup_surfaces(std::u8string& error)
            {
                GrD3DTextureResourceInfo resource_info {
                    nullptr,
                    nullptr,
                    D3D12_RESOURCE_STATE_PRESENT,
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    1,
                    1,
                    0,
                };
                for (std::size_t index = 0; index < frame_count; ++index)
                {
                    HRESULT result {
                        swap_chain_->GetBuffer(static_cast<UINT>(index), IID_PPV_ARGS(&buffers_[index])),
                    };

                    if (FAILED(result))
                    {
                        error = make_hresult_error(u8"Failed to retrieve a Direct3D back buffer", result);
                        return false;
                    }
                    resource_info.fResource = buffers_[index];
                    const GrBackendRenderTarget render_target {
                        GrBackendRenderTargets::MakeD3D(width_, height_, resource_info),
                    };

                    // 픽셀 배치(RGB 가로)를 알려야 LCD 서브픽셀 글자가 실제로 켜진다.
                    // 구멍을 뚫는 frame은 `kUnknown`으로 감싼다 — 그러면 Skia가 회색조로
                    // 물러서므로 그리는 쪽의 ClearType 판정은 건드리지 않는다.
                    const SkSurfaceProps surface_properties { 0, subpixel_text_ ? kRGB_H_SkPixelGeometry : kUnknown_SkPixelGeometry };
                    surfaces_[index] = SkSurfaces::WrapBackendRenderTarget(context_.get(), render_target, kTopLeft_GrSurfaceOrigin, kRGBA_8888_SkColorType, nullptr, &surface_properties);
                    if (surfaces_[index] == nullptr)
                    {
                        error = u8"Skia failed to wrap a Direct3D back buffer as a surface.";
                        return false;
                    }
                }
                return true;
            }

            void release_surfaces() noexcept
            {
                for (std::size_t index = 0; index < frame_count; ++index)
                {
                    surfaces_[index].reset();
                    buffers_[index].reset();
                }
            }

            // 대기 실패의 글을 만든다. device removal이면 removal의 HRESULT
            // (DXGI_ERROR_DEVICE_HUNG 등)가 진짜 원인이므로 그것을 붙인다.
            [[nodiscard]] std::u8string fence_wait_error(const std::u8string_view message) const
            {
                const HRESULT removed_reason { device_->GetDeviceRemovedReason() };
                if (removed_reason != S_OK)
                    return make_hresult_error(message, removed_reason);
                std::u8string output { message };
                output += u8'.';
                return output;
            }

            [[nodiscard]] bool wait_for_fence(const std::uint64_t value) noexcept
            {
                if (value == 0 || fence_->GetCompletedValue() >= value)
                    return true;
                if (FAILED(fence_->SetEventOnCompletion(value, fence_event_)))
                    return false;

                // 예산(fence_wait.h)까지만 기다린다. 슬라이스 사이마다 removal을
                // 물어, 이벤트가 끝내 오지 않는 device removal에서도 예산을 다
                // 쓰기 전에 빠져나온다.
                std::uint32_t waited_ms { 0 };
                while (true)
                {
                    const std::uint32_t slice_ms { next_fence_wait_slice(waited_ms) };
                    if (slice_ms == 0)
                        return false;
                    const DWORD wait_result { WaitForSingleObject(fence_event_, slice_ms) };
                    if (wait_result == WAIT_OBJECT_0)
                        return true;
                    if (wait_result != WAIT_TIMEOUT)
                        return false;
                    if (device_->GetDeviceRemovedReason() != S_OK)
                        return false;
                    waited_ms += slice_ms;
                }
            }

            [[nodiscard]] bool wait_for_gpu() noexcept
            {
                if (!queue_ || !fence_ || fence_event_ == nullptr)
                    return true;
                const std::uint64_t signal_value { ++next_fence_value_ };
                if (FAILED(queue_->Signal(fence_.get(), signal_value)))
                    return false;
                return wait_for_fence(signal_value);
            }

            HWND window_ { nullptr };
            // 프로세스가 소유한다 (composition_device.h). 렌더러는 빌려 쓴다.
            IDCompositionDevice* composition_ { nullptr };
            // 창이 소유하는 쪽이다. 렌더러와 수명을 같이한다.
            IDCompositionTarget* target_ { nullptr };
            // root 아래에 underlay(웹뷰)와 visual(우리 스왑체인)이 이 순서로 든다.
            IDCompositionVisual* root_ { nullptr };
            IDCompositionVisual* underlay_ { nullptr };
            IDCompositionVisual* visual_ { nullptr };
            int width_ { 1 };
            int height_ { 1 };
            gr_cp<IDXGIFactory4> factory_ {};
            gr_cp<IDXGIAdapter1> adapter_ {};
            gr_cp<ID3D12Device> device_ {};
            gr_cp<ID3D12CommandQueue> queue_ {};
            gr_cp<IDXGISwapChain3> swap_chain_ {};
            gr_cp<ID3D12Fence> fence_ {};
            HANDLE fence_event_ { nullptr };
            std::uint64_t next_fence_value_ { 0 };
            std::array<std::uint64_t, frame_count> frame_fence_values_ {};
            std::array<gr_cp<ID3D12Resource>, frame_count> buffers_ {};
            std::array<sk_sp<SkSurface>, frame_count> surfaces_ {};
            // 지금 표면이 LCD 서브픽셀 글자를 켜고 감싸졌는가. 구멍을 뚫는 frame은 끈다
            // (`render`).
            bool subpixel_text_ { true };
            sk_sp<GrDirectContext> context_ {};
            sk_sp<SkTypeface> codicon_typeface_ {};
            sk_sp<SkTypeface> ui_typeface_ {};
        };
    } // namespace

    bool direct3d_renderer_built() noexcept
    {
        return true;
    }

    renderer_factory_result create_direct3d_skia_renderer(const HWND window, IDCompositionDevice* const composition)
    {
        if (composition == nullptr)
            return { nullptr, u8"Direct3D presentation needs a DirectComposition device." };

        auto renderer { std::make_unique<direct3d_skia_renderer>(window, *composition) };
        std::u8string error {};
        if (renderer->initialize(error) == false)
            return { nullptr, std::move(error) };
        return { std::move(renderer), {} };
    }
} // namespace luil::win32
