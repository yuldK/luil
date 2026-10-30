#include "luil/theme/ui_theme.h"
#include "luil/ui/image_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"

#include <catch2/catch_test_macros.hpp>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

// GPU 렌더러에서 최대 텍스처보다 큰 그림이 비지 않는지를 픽셀로 본다 (image-frames-scaling-design.md).
// 창 없이 Direct3D 12 문맥 하나를 세워 그 위 render target에 `image_element`를 그린다. 하드웨어 어댑터가 없는
// 기계(가상 머신·CI)에서는 건너뛴다 — 그것을 실패로 적으면 GPU가 없는 기계마다 test가 붉다.

namespace {
    struct headless_direct3d
    {
        gr_cp<IDXGIFactory4> factory {};
        gr_cp<IDXGIAdapter1> adapter {};
        gr_cp<ID3D12Device> device {};
        gr_cp<ID3D12CommandQueue> queue {};
        sk_sp<GrDirectContext> context {};

        // 렌더러(`direct3d_skia_renderer.cpp`)와 같은 차례다: 하드웨어 어댑터 → 장치 → 명령 큐 → Skia 문맥.
        [[nodiscard]] bool start()
        {
            if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
                return false;
            for (UINT index { 0 };; ++index)
            {
                IDXGIAdapter1* raw { nullptr };
                if (factory->EnumAdapters1(index, &raw) == DXGI_ERROR_NOT_FOUND)
                    break;
                gr_cp<IDXGIAdapter1> candidate { raw };
                DXGI_ADAPTER_DESC1 description {};
                if (FAILED(candidate->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                    continue;
                if (SUCCEEDED(D3D12CreateDevice(candidate.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
                {
                    adapter = std::move(candidate);
                    break;
                }
            }
            if (!adapter)
                return false;
            D3D12_COMMAND_QUEUE_DESC queue_description {};
            queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            if (FAILED(device->CreateCommandQueue(&queue_description, IID_PPV_ARGS(&queue))))
                return false;
            GrD3DBackendContext backend {};
            backend.fAdapter = adapter;
            backend.fDevice = device;
            backend.fQueue = queue;
            context = GrDirectContexts::MakeD3D(backend);
            return context != nullptr;
        }

        ~headless_direct3d()
        {
            if (context != nullptr)
                context->releaseResourcesAndAbandonContext();
        }

        headless_direct3d() = default;
        headless_direct3d(const headless_direct3d&) = delete;
        headless_direct3d(headless_direct3d&&) = delete;
        headless_direct3d& operator=(const headless_direct3d&) = delete;
        headless_direct3d& operator=(headless_direct3d&&) = delete;
    };

    // 한 픽셀 세로 줄무늬 띠다 (짝수 열 빨강, 홀수 열 파랑). 줄인 그림이나 흐린 텍스처로는 같은 무늬가 나오지 않는다.
    [[nodiscard]] luil::ui_image make_strip(const int width, const int height)
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0u);
        for (std::size_t index { 0 }; index < pixels.size(); index += 4u)
        {
            const bool even { (index / 4u) % static_cast<std::size_t>(width) % 2u == 0u };
            pixels[index + (even ? 0u : 2u)] = 255u;
            pixels[index + 3u] = 255u;
        }
        return luil::make_rgba_image(width, height, pixels);
    }

    // 띠의 **오른쪽 끝** `width`열을 1:1로, 섞지 않는 표본으로 그리는 tree다. 한 장 텍스처의 한도 밖 열이 그 자리에 든다.
    [[nodiscard]] luil::ui_tree make_picture_tree(const luil::ui_image& image, const int width, const int height)
    {
        luil::image_config config {};
        config.image = image;
        config.fit = luil::image_fit::fill;
        config.source = { static_cast<float>(image.width() - width), 0.0f, static_cast<float>(width), static_cast<float>(height) };
        config.sampling = luil::image_sampling::sharp;
        auto picture { std::make_unique<luil::image_element>(luil::ui_element_id { luil::ui_element_kind::root }, std::move(config)) };
        picture->arrange({ { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) }, 1.0f });
        return luil::ui_tree { std::move(picture) };
    }

    // 표면에 그림 tree를 그리고 한 줄을 읽어 온다 (RGBA, straight).
    [[nodiscard]] std::vector<std::uint8_t> draw_and_read(SkSurface& surface, GrDirectContext* context, const luil::ui_tree& tree, const int width, const int row)
    {
        SkCanvas& canvas { *surface.getCanvas() };
        canvas.clear(SK_ColorBLACK);
        const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
        luil::draw_context draw {
            .canvas = canvas,
            .palette = palette,
            .scale = 1.0f,
        };
        tree.draw(draw, luil::interaction_snapshot {});
        if (context != nullptr)
            context->flushAndSubmit(GrSyncCpu::kYes);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * 4u, 0u);
        const SkImageInfo info { SkImageInfo::Make(width, 1, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType) };
        REQUIRE(surface.readPixels(info, pixels.data(), static_cast<std::size_t>(width) * 4u, 0, row));
        return pixels;
    }

    // 읽은 줄이 띠의 무늬 그대로인가. `first`는 그린 첫 열이 띠에서 몇 번째 열인지다.
    [[nodiscard]] bool matches_stripes(const std::vector<std::uint8_t>& row, const int first)
    {
        for (std::size_t x { 0 }; x * 4u < row.size(); ++x)
        {
            const bool even { (static_cast<std::size_t>(first) + x) % 2u == 0u };
            const std::uint8_t* const pixel { row.data() + x * 4u };
            if (pixel[0] != (even ? 255u : 0u) || pixel[1] != 0u || pixel[2] != (even ? 0u : 255u) || pixel[3] != 255u)
                return false;
        }
        return true;
    }
}

TEST_CASE("An image wider than the GPU texture limit still reaches a Direct3D surface", "[gpu][image][tiling]")
{
    headless_direct3d gpu {};
    if (gpu.start() == false)
    {
        WARN("No hardware Direct3D 12 adapter; the GPU tiling check is skipped.");
        return;
    }
    // 한도를 손으로 적지 않는다 — 장치마다 다르다. 한도보다 한 픽셀 넓은 그림이면 한 장으로는 올리지 못한다.
    const int limit { gpu.context->maxTextureSize() };
    const luil::ui_image strip { make_strip(limit + 1, 8) };
    REQUIRE(strip.valid());

    constexpr int width { 256 };
    constexpr int height { 8 };
    const SkImageInfo info { SkImageInfo::MakeN32Premul(width, height, SkColorSpace::MakeSRGB()) };
    const sk_sp<SkSurface> surface { SkSurfaces::RenderTarget(gpu.context.get(), skgpu::Budgeted::kNo, info) };
    REQUIRE(surface != nullptr);

    const luil::ui_tree tree { make_picture_tree(strip, width, height) };
    const std::vector<std::uint8_t> row { draw_and_read(*surface, gpu.context.get(), tree, width, height / 2) };
    // 비어 보이면(검정 바탕) 그림이 GPU에 닿지 못한 것이고, 무늬가 흐리면 줄인 텍스처로 대신 그린 것이다.
    REQUIRE(matches_stripes(row, limit + 1 - width));
}

TEST_CASE("The same oversized image draws the same colours on the CPU", "[gpu][image][tiling]")
{
    // CPU에는 텍스처 한도가 없다. 타일 그리기가 CPU 경로를 바꾸지 않았는지 같은 띠로 본다.
    const luil::ui_image strip { make_strip(16385, 8) };
    REQUIRE(strip.valid());
    constexpr int width { 256 };
    constexpr int height { 8 };
    const sk_sp<SkSurface> surface { SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height, SkColorSpace::MakeSRGB())) };
    REQUIRE(surface != nullptr);
    const luil::ui_tree tree { make_picture_tree(strip, width, height) };
    const std::vector<std::uint8_t> row { draw_and_read(*surface, nullptr, tree, width, height / 2) };
    REQUIRE(matches_stripes(row, 16385 - width));
}
