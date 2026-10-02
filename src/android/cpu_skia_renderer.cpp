#include "android/cpu_skia_renderer.h"

#include "host/font_registry.h"
#include "luil/text/fonts.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"

#include <android/hardware_buffer.h>
#include <android/native_window.h>

#include <memory>
#include <utility>

namespace luil::android {
    namespace {
        class cpu_skia_renderer final : public skia_renderer
        {
        public:
            explicit cpu_skia_renderer(ANativeWindow* const window)
                : window_ { window }
                , codicon_typeface_ { load_codicon_typeface() }
            {
                ANativeWindow_acquire(window_);
            }

            cpu_skia_renderer(const cpu_skia_renderer&) = delete;
            cpu_skia_renderer(cpu_skia_renderer&&) = delete;
            cpu_skia_renderer& operator=(const cpu_skia_renderer&) = delete;
            cpu_skia_renderer& operator=(cpu_skia_renderer&&) = delete;

            ~cpu_skia_renderer() override
            {
                ANativeWindow_release(window_);
            }

            [[nodiscard]] renderer_backend backend() const noexcept override
            {
                return renderer_backend::cpu;
            }

            // 버퍼 크기는 창이 정한다 (0·0). 형식만 RGBA로 고정한다 — Skia raster가 바로
            // 쓰는 배치라 변환이 없다.
            [[nodiscard]] bool resize(const int width, const int height, std::u8string& error) override
            {
                static_cast<void>(width);
                static_cast<void>(height);
                if (ANativeWindow_setBuffersGeometry(window_, 0, 0, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM) != 0)
                {
                    error = u8"Failed to set the native window buffer format.";
                    return false;
                }
                format_set_ = true;
                return true;
            }

            [[nodiscard]] bool render(const frame_state& state, std::u8string& error) override
            {
                // `resize` 없이 먼저 불릴 수 있다. 그리다 Vulkan에서 물러서면 이 렌더러가 곧바로
                // 그 frame을 그린다 (`renderer_host::render`). 형식을 정하지 않은 창은 다른
                // 형식(RGB 565 등)의 버퍼를 줄 수 있다.
                if (format_set_ == false && resize(state.width, state.height, error) == false)
                    return false;

                ANativeWindow_Buffer buffer {};
                if (ANativeWindow_lock(window_, &buffer, nullptr) != 0)
                {
                    error = u8"Failed to lock the native window buffer.";
                    return false;
                }
                // 아래는 픽셀 하나를 4바이트로 쓴다. 다른 형식이면 버퍼 끝을 넘어 쓰게 되므로 그리지 않는다.
                if (buffer.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM && buffer.format != AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM)
                {
                    ANativeWindow_unlockAndPost(window_);
                    error = u8"The native window buffer is not RGBA.";
                    return false;
                }

                const SkImageInfo image_info {
                    SkImageInfo::Make(buffer.width, buffer.height, kRGBA_8888_SkColorType, kPremul_SkAlphaType, SkColorSpace::MakeSRGB()),
                };
                // 화면의 서브픽셀 배치를 모르므로(회전도 한다) 회색조로 그린다.
                const SkSurfaceProps surface_properties { 0, kUnknown_SkPixelGeometry };
                const std::size_t row_bytes { static_cast<std::size_t>(buffer.stride) * 4u };
                sk_sp<SkSurface> surface { SkSurfaces::WrapPixels(image_info, buffer.bits, row_bytes, &surface_properties) };
                if (surface == nullptr)
                {
                    ANativeWindow_unlockAndPost(window_);
                    error = u8"Failed to wrap the native window buffer.";
                    return false;
                }

                // UI 글꼴은 설정이 정한다. registry가 cache하므로 매 frame 물어도 된다.
                const sk_sp<SkTypeface> ui { configured_ui_typeface() };
                draw_frame(*surface->getCanvas(), codicon_typeface_.get(), ui.get(), state);
                surface.reset();
                if (ANativeWindow_unlockAndPost(window_) != 0)
                {
                    error = u8"Failed to post the native window buffer.";
                    return false;
                }
                return true;
            }

        private:
            ANativeWindow* window_ { nullptr };
            sk_sp<SkTypeface> codicon_typeface_ {};
            // 창 버퍼의 형식을 RGBA로 정했는가.
            bool format_set_ { false };
        };
    } // namespace

    renderer_factory_result create_cpu_skia_renderer(ANativeWindow* const window)
    {
        if (window == nullptr)
            return { nullptr, u8"There is no native window to render into." };
        return { std::make_unique<cpu_skia_renderer>(window), {} };
    }
} // namespace luil::android
