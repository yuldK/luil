#include "macos/cpu_skia_renderer.h"

#include "host/font_registry.h"
#include "luil/text/fonts.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"

#import <QuartzCore/QuartzCore.h>

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cstdlib>
#include <cstring>

#include <cstddef>
#include <memory>

namespace luil::macos {
    namespace {
        // CGImage가 픽셀을 쥐는 동안 SkData를 살려 둔다. 다음 frame은 새 버퍼에 그리므로 layer가 아직 보이는
        // 이전 그림을 덮어쓰지 않는다.
        void release_pixels(void* const info, const void*, std::size_t)
        {
            static_cast<SkData*>(info)->unref();
        }

        // 개발용 frame 갈무리다. 환경 변수 `LUIL_MACOS_FRAME_CAPTURE`에 PNG 경로를 주면 그린 frame마다 그 파일을 덮어쓴다.
        //  - 화면 기록 권한 없이 그린 결과를 그대로 보려는 것이다 (자동 검증·버그 보고). 정하지 않으면 아무 일도 없다.
        void capture_frame(CGImageRef const image)
        {
            static const char* const path { std::getenv("LUIL_MACOS_FRAME_CAPTURE") };
            if (path == nullptr || *path == '\0')
                return;
            CFURLRef const url { CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path), static_cast<CFIndex>(std::strlen(path)), false) };
            if (url == nullptr)
                return;
            if (CGImageDestinationRef const destination { CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr) }; destination != nullptr)
            {
                CGImageDestinationAddImage(destination, image, nullptr);
                static_cast<void>(CGImageDestinationFinalize(destination));
                CFRelease(destination);
            }
            CFRelease(url);
        }

        class cpu_skia_renderer final : public skia_renderer
        {
        public:
            explicit cpu_skia_renderer(CALayer* const layer)
                : layer_ { layer }
                , codicon_typeface_ { load_codicon_typeface() }
                , color_space_ { CGColorSpaceCreateWithName(kCGColorSpaceSRGB) }
            {
            }

            cpu_skia_renderer(const cpu_skia_renderer&) = delete;
            cpu_skia_renderer(cpu_skia_renderer&&) = delete;
            cpu_skia_renderer& operator=(const cpu_skia_renderer&) = delete;
            cpu_skia_renderer& operator=(cpu_skia_renderer&&) = delete;

            ~cpu_skia_renderer() override
            {
                CGColorSpaceRelease(color_space_);
            }

            [[nodiscard]] renderer_backend backend() const noexcept override
            {
                return renderer_backend::cpu;
            }

            // 버퍼는 frame마다 그 크기로 새로 짓는다. 크기만 적어 둔다.
            [[nodiscard]] bool resize(const int width, const int height, std::u8string& error) override
            {
                static_cast<void>(error);
                width_ = width;
                height_ = height;
                return true;
            }

            [[nodiscard]] bool render(const frame_state& state, std::u8string& error) override
            {
                // 안전 영역이 없어 그릴 자리가 곧 표면이다.
                const int width { state.origin_x + state.width };
                const int height { state.origin_y + state.height };
                if (width <= 0 || height <= 0)
                    return true;

                const SkImageInfo image_info { SkImageInfo::MakeN32Premul(width, height, SkColorSpace::MakeSRGB()) };
                const std::size_t row_bytes { image_info.minRowBytes() };
                sk_sp<SkData> pixels { SkData::MakeUninitialized(row_bytes * static_cast<std::size_t>(height)) };
                // 레티나 화면은 서브픽셀 배치를 쓰지 않는다 (macOS가 서브픽셀 AA를 끈 지 오래다). 회색조로 그린다.
                const SkSurfaceProps surface_properties { 0, kUnknown_SkPixelGeometry };
                sk_sp<SkSurface> surface { SkSurfaces::WrapPixels(image_info, pixels->writable_data(), row_bytes, &surface_properties) };
                if (surface == nullptr)
                {
                    error = u8"Failed to wrap the raster buffer.";
                    return false;
                }

                const sk_sp<SkTypeface> ui { configured_ui_typeface() };
                draw_frame(*surface->getCanvas(), codicon_typeface_.get(), ui.get(), state);
                surface.reset();

                SkData* const owned { pixels.release() };
                CGDataProviderRef const provider { CGDataProviderCreateWithData(owned, owned->data(), owned->size(), &release_pixels) };
                if (provider == nullptr)
                {
                    owned->unref();
                    error = u8"Failed to create the image data provider.";
                    return false;
                }
                // N32는 little-endian BGRA다. CoreGraphics에서는 32-bit little + premultiplied first다.
                const CGBitmapInfo info { static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little) | static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) };
                CGImageRef const image { CGImageCreate(static_cast<std::size_t>(width), static_cast<std::size_t>(height), 8, 32, row_bytes, color_space_, info, provider, nullptr, false,
                    kCGRenderingIntentDefault) };
                CGDataProviderRelease(provider);
                if (image == nullptr)
                {
                    error = u8"Failed to create the frame image.";
                    return false;
                }

                // 내용을 바꾸는 암묵적 애니메이션(0.25초의 교차 페이드)을 끈다.
                [CATransaction begin];
                [CATransaction setDisableActions:YES];
                layer_.contentsScale = state.dpi_scale;
                layer_.contents = (__bridge id)image;
                [CATransaction commit];
                capture_frame(image);
                CGImageRelease(image);
                return true;
            }

        private:
            CALayer* layer_ { nil };
            sk_sp<SkTypeface> codicon_typeface_ {};
            CGColorSpaceRef color_space_ { nullptr };
            int width_ { 0 };
            int height_ { 0 };
        };
    }

    renderer_factory_result create_cpu_skia_renderer(void* const layer)
    {
        if (layer == nullptr)
            return { nullptr, u8"There is no layer to render into." };
        return { std::make_unique<cpu_skia_renderer>((__bridge CALayer*)layer), {} };
    }
}
