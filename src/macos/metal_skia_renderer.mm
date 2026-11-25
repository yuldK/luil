#include "macos/metal_skia_renderer.h"

#include "host/font_registry.h"
#include "luil/text/fonts.h"
#include "macos/frame_capture.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendContext.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendSurface.h"
#include "include/gpu/ganesh/mtl/GrMtlDirectContext.h"
#include "include/gpu/ganesh/mtl/GrMtlTypes.h"

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <cstddef>
#include <memory>

namespace luil::macos {
    namespace {
        void release_pixels(void* const info, const void*, std::size_t)
        {
            static_cast<SkData*>(info)->unref();
        }

        // GPU의 그림을 되읽어 갈무리에 넘긴다 (frame_capture.h). 갈무리를 원할 때만 부른다 — 한 frame을 기다리게 한다.
        void publish_readback(SkSurface& surface, const int width, const int height)
        {
            const SkImageInfo info { SkImageInfo::MakeN32Premul(width, height, SkColorSpace::MakeSRGB()) };
            const std::size_t row_bytes { info.minRowBytes() };
            sk_sp<SkData> pixels { SkData::MakeUninitialized(row_bytes * static_cast<std::size_t>(height)) };
            if (surface.readPixels(info, pixels->writable_data(), row_bytes, 0, 0) == false)
                return;
            SkData* const owned { pixels.release() };
            CGDataProviderRef const provider { CGDataProviderCreateWithData(owned, owned->data(), owned->size(), &release_pixels) };
            if (provider == nullptr)
            {
                owned->unref();
                return;
            }
            CGColorSpaceRef const space { CGColorSpaceCreateWithName(kCGColorSpaceSRGB) };
            const CGBitmapInfo bitmap { static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little) | static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) };
            CGImageRef const image { CGImageCreate(static_cast<std::size_t>(width), static_cast<std::size_t>(height), 8, 32, row_bytes, space, bitmap, provider, nullptr, false,
                kCGRenderingIntentDefault) };
            CGColorSpaceRelease(space);
            CGDataProviderRelease(provider);
            if (image != nullptr)
            {
                publish_frame(image);
                CGImageRelease(image);
            }
        }

        class metal_skia_renderer final : public skia_renderer
        {
        public:
            metal_skia_renderer(CALayer* const parent, id<MTLDevice> const device, id<MTLCommandQueue> const queue, sk_sp<GrDirectContext> context)
                : parent_ { parent }
                , device_ { device }
                , queue_ { queue }
                , context_ { std::move(context) }
                , codicon_typeface_ { load_codicon_typeface() }
            {
                layer_ = [CAMetalLayer layer];
                layer_.device = device_;
                layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
                // 갈무리가 drawable을 되읽으므로 표시 전용으로 묶지 않는다.
                layer_.framebufferOnly = NO;
                layer_.opaque = NO;
                layer_.anchorPoint = CGPointZero;
                layer_.contentsGravity = kCAGravityTopLeft;
                CGColorSpaceRef const space { CGColorSpaceCreateWithName(kCGColorSpaceSRGB) };
                layer_.colorspace = space;
                CGColorSpaceRelease(space);
                [CATransaction begin];
                [CATransaction setDisableActions:YES];
                layer_.frame = parent_.bounds;
                layer_.contentsScale = parent_.contentsScale;
                [parent_ addSublayer:layer_];
                [CATransaction commit];
            }

            metal_skia_renderer(const metal_skia_renderer&) = delete;
            metal_skia_renderer(metal_skia_renderer&&) = delete;
            metal_skia_renderer& operator=(const metal_skia_renderer&) = delete;
            metal_skia_renderer& operator=(metal_skia_renderer&&) = delete;

            ~metal_skia_renderer() override
            {
                // GPU가 끝내기를 기다린 뒤 놓는다. 그리던 drawable이 사라진 layer를 가리키지 않게 한다.
                if (context_ != nullptr && context_->abandoned() == false)
                    static_cast<void>(context_->flushAndSubmit(GrSyncCpu::kYes));
                context_.reset();
                [CATransaction begin];
                [CATransaction setDisableActions:YES];
                [layer_ removeFromSuperlayer];
                [CATransaction commit];
            }

            [[nodiscard]] renderer_backend backend() const noexcept override
            {
                return renderer_backend::metal;
            }

            [[nodiscard]] bool resize(const int width, const int height, std::u8string& error) override
            {
                static_cast<void>(error);
                width_ = width;
                height_ = height;
                if (width > 0 && height > 0)
                    layer_.drawableSize = CGSizeMake(width, height);
                return true;
            }

            [[nodiscard]] bool render(const frame_state& state, std::u8string& error) override
            {
                // drawable은 표면 전체다. 안전 영역(iOS)의 오른쪽·아래 몫이 빠지면 layer가 그림을 늘리거나 한쪽에 붙인다
                // (iOS의 layer는 좌표가 뒤집혀 "위 왼쪽" 붙임이 아래에 붙는다). 배경은 표면 끝까지 칠한다.
                const int width { std::max(state.origin_x + state.width, width_) };
                const int height { std::max(state.origin_y + state.height, height_) };
                if (width <= 0 || height <= 0)
                    return true;
                if (context_ == nullptr || context_->abandoned())
                {
                    error = u8"The Metal device was lost.";
                    return false;
                }

                [CATransaction begin];
                [CATransaction setDisableActions:YES];
                layer_.frame = parent_.bounds;
                layer_.contentsScale = state.dpi_scale;
                if (layer_.drawableSize.width != width || layer_.drawableSize.height != height)
                    layer_.drawableSize = CGSizeMake(width, height);
                [CATransaction commit];

                // drawable이 잠깐 없는 것(창을 가렸다, 시스템이 늦다)은 장치 실패가 아니다. 이 frame만 건너뛴다.
                id<CAMetalDrawable> const drawable { [layer_ nextDrawable] };
                if (drawable == nil)
                    return true;

                GrMtlTextureInfo texture {};
                texture.fTexture.retain((__bridge GrMTLHandle)drawable.texture);
                const GrBackendRenderTarget target { GrBackendRenderTargets::MakeMtl(width, height, texture) };
                // 레티나 화면은 서브픽셀 배치를 쓰지 않는다. 아래층 웹뷰의 구멍과도 맞는 회색조다.
                const SkSurfaceProps properties { 0, kUnknown_SkPixelGeometry };
                sk_sp<SkSurface> surface { SkSurfaces::WrapBackendRenderTarget(context_.get(), target, kTopLeft_GrSurfaceOrigin, kBGRA_8888_SkColorType, SkColorSpace::MakeSRGB(), &properties) };
                if (surface == nullptr)
                {
                    error = u8"Failed to wrap the Metal drawable as a Skia surface.";
                    return false;
                }

                const sk_sp<SkTypeface> ui { configured_ui_typeface() };
                draw_frame(*surface->getCanvas(), codicon_typeface_.get(), ui.get(), state);
                if (frame_readback_wanted())
                    publish_readback(*surface, width, height);
                static_cast<void>(context_->flushAndSubmit(surface.get(), GrSyncCpu::kNo));
                surface.reset();
                if (context_->abandoned())
                {
                    error = u8"The Metal device was lost while drawing.";
                    return false;
                }

                id<MTLCommandBuffer> const present { [queue_ commandBuffer] };
                [present presentDrawable:drawable];
                [present commit];
                return true;
            }

        private:
            CALayer* parent_ { nil };
            CAMetalLayer* layer_ { nil };
            id<MTLDevice> device_ { nil };
            id<MTLCommandQueue> queue_ { nil };
            sk_sp<GrDirectContext> context_ {};
            sk_sp<SkTypeface> codicon_typeface_ {};
            int width_ { 0 };
            int height_ { 0 };
        };
    }

    renderer_factory_result create_metal_skia_renderer(void* const layer)
    {
        if (layer == nullptr)
            return { nullptr, u8"There is no layer to render into." };
        id<MTLDevice> const device { MTLCreateSystemDefaultDevice() };
        if (device == nil)
            return { nullptr, u8"This Mac has no Metal device." };
        id<MTLCommandQueue> const queue { [device newCommandQueue] };
        if (queue == nil)
            return { nullptr, u8"Failed to create the Metal command queue." };
        GrMtlBackendContext backend {};
        backend.fDevice.retain((__bridge GrMTLHandle)device);
        backend.fQueue.retain((__bridge GrMTLHandle)queue);
        sk_sp<GrDirectContext> context { GrDirectContexts::MakeMetal(backend) };
        if (context == nullptr)
            return { nullptr, u8"Failed to create the Skia Metal context." };
        return { std::make_unique<metal_skia_renderer>((__bridge CALayer*)layer, device, queue, std::move(context)), {} };
    }
}
