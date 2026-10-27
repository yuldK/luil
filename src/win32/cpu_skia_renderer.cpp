#include "win32/skia_renderer.h"

#include "host/font_registry.h"
#include "win32/embedded_assets.h"

#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

namespace luil::win32 {
    namespace {
        [[nodiscard]] SkImageInfo raster_info(const int width, const int height)
        {
            return SkImageInfo::MakeN32Premul(width, height, SkColorSpace::MakeSRGB());
        }

        // 픽셀 배치(RGB 가로)를 알려야 LCD 서브픽셀 글자가 실제로 켜진다.
        // 회색조 설정에서는 있어도 아무 일도 하지 않는다.
        [[nodiscard]] SkSurfaceProps raster_properties()
        {
            return SkSurfaceProps { 0, kRGB_H_SkPixelGeometry };
        }

        class cpu_skia_renderer final : public skia_renderer
        {
        public:
            explicit cpu_skia_renderer(const HWND window)
                : window_ { window }
                , codicon_typeface_ { load_codicon_typeface() }
                , ui_typeface_ { load_ui_typeface() }
            {}

            [[nodiscard]] renderer_backend backend() const noexcept override
            {
                return renderer_backend::cpu;
            }

            [[nodiscard]] bool resize(const int width, const int height, std::u8string& error) override
            {
                width_ = std::max(1, width);
                height_ = std::max(1, height);
                const SkSurfaceProps surface_properties { raster_properties() };
                surface_ = SkSurfaces::Raster(raster_info(width_, height_), &surface_properties);
                if (surface_ == nullptr)
                {
                    error = u8"Failed to create the CPU raster surface.";
                    return false;
                }
                return true;
            }

            [[nodiscard]] bool render(const frame_state& state, std::u8string& error) override
            {
                if (surface_ == nullptr && resize(state.width, state.height, error) == false)
                    return false;

                // UI 글꼴은 설정이 정한다.
                // registry가 cache하므로 매 frame 물어도 된다.
                const sk_sp<SkTypeface> ui { configured_ui_typeface() };
                draw_frame(*surface_->getCanvas(), codicon_typeface_.get(), ui.get(), state);

                SkPixmap pixels {};
                if (surface_->peekPixels(&pixels) == false)
                {
                    error = u8"Failed to read CPU surface pixels.";
                    return false;
                }

                BITMAPINFO bitmap_info {};
                bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bitmap_info.bmiHeader.biWidth = width_;
                bitmap_info.bmiHeader.biHeight = -height_;
                bitmap_info.bmiHeader.biPlanes = 1;
                bitmap_info.bmiHeader.biBitCount = 32;
                bitmap_info.bmiHeader.biCompression = BI_RGB;

                const HDC device_context { GetDC(window_) };
                if (device_context == nullptr)
                {
                    error = u8"The CPU renderer failed to acquire the window device context.";
                    return false;
                }

                const int copied_lines {
                    StretchDIBits(device_context, 0, 0, width_, height_, 0, 0, width_, height_, pixels.addr(), &bitmap_info, DIB_RGB_COLORS, SRCCOPY),
                };
                ReleaseDC(window_, device_context);
                if (copied_lines == GDI_ERROR || copied_lines == 0)
                {
                    error = u8"The CPU renderer failed to copy the raster frame to the window.";
                    return false;
                }
                return true;
            }

        private:
            HWND window_ { nullptr };
            int width_ { 1 };
            int height_ { 1 };
            sk_sp<SkSurface> surface_ {};
            sk_sp<SkTypeface> codicon_typeface_ {};
            sk_sp<SkTypeface> ui_typeface_ {};
        };

        // 레이어드 창에 `UpdateLayeredWindow`로 내는 CPU 렌더러다.
        //
        // Skia가 DIB section의 픽셀에 바로 그린다 (premultiplied BGRA — Windows의 N32가 그 순서다). 그 DIB를 고른
        // memory DC를 `UpdateLayeredWindow`에 넘기면 DWM이 그림과 함께 창의 위치·크기를 **한 합성에** 바꾼다.
        // 일반 창은 크기가 먼저 바뀌고 이전 frame이 새 창의 왼쪽 위에 붙어 한 합성 동안 보이는데, 이 호출에는 그
        // 사이가 없다 (layered-presentation-design.md).
        class cpu_layered_renderer final : public layered_renderer
        {
        public:
            cpu_layered_renderer(const HWND window, const bool click_through)
                : window_ { window }
                , click_through_ { click_through }
                , codicon_typeface_ { load_codicon_typeface() }
                , ui_typeface_ { load_ui_typeface() }
            {}

            cpu_layered_renderer(const cpu_layered_renderer&) = delete;
            cpu_layered_renderer(cpu_layered_renderer&&) = delete;
            cpu_layered_renderer& operator=(const cpu_layered_renderer&) = delete;
            cpu_layered_renderer& operator=(cpu_layered_renderer&&) = delete;

            ~cpu_layered_renderer() override
            {
                release_bitmap();
                if (memory_dc_ != nullptr)
                    DeleteDC(memory_dc_);
            }

            [[nodiscard]] renderer_backend backend() const noexcept override
            {
                return renderer_backend::cpu;
            }

            [[nodiscard]] bool resize(const int width, const int height, std::u8string& error) override
            {
                const int next_width { std::max(1, width) };
                const int next_height { std::max(1, height) };
                if (surface_ != nullptr && next_width == width_ && next_height == height_)
                    return true;

                if (memory_dc_ == nullptr)
                {
                    memory_dc_ = CreateCompatibleDC(nullptr);
                    if (memory_dc_ == nullptr)
                    {
                        error = u8"The layered renderer failed to create a memory device context.";
                        return false;
                    }
                }

                BITMAPINFO bitmap_info {};
                bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bitmap_info.bmiHeader.biWidth = next_width;
                bitmap_info.bmiHeader.biHeight = -next_height;
                bitmap_info.bmiHeader.biPlanes = 1;
                bitmap_info.bmiHeader.biBitCount = 32;
                bitmap_info.bmiHeader.biCompression = BI_RGB;
                void* bits { nullptr };
                const HBITMAP bitmap { CreateDIBSection(memory_dc_, &bitmap_info, DIB_RGB_COLORS, &bits, nullptr, 0) };
                if (bitmap == nullptr || bits == nullptr)
                {
                    if (bitmap != nullptr)
                        DeleteObject(bitmap);
                    error = u8"The layered renderer failed to create the frame bitmap.";
                    return false;
                }

                const SkSurfaceProps surface_properties { raster_properties() };
                // 위로 자라는 DIB가 아니므로(높이가 음수) 한 줄은 폭 × 4바이트다.
                sk_sp<SkSurface> surface { SkSurfaces::WrapPixels(raster_info(next_width, next_height), bits, static_cast<std::size_t>(next_width) * 4, &surface_properties) };
                if (surface == nullptr)
                {
                    DeleteObject(bitmap);
                    error = u8"The layered renderer failed to wrap the frame bitmap.";
                    return false;
                }

                // surface가 픽셀을 빌려 쓰므로 bitmap보다 먼저 놓는다.
                surface_.reset();
                release_bitmap();
                previous_bitmap_ = SelectObject(memory_dc_, bitmap);
                bitmap_ = bitmap;
                bits_ = static_cast<std::uint32_t*>(bits);
                surface_ = std::move(surface);
                width_ = next_width;
                height_ = next_height;
                return true;
            }

            void place_next_frame(const std::optional<screen_bounds> bounds) noexcept override
            {
                next_bounds_ = bounds;
            }

            [[nodiscard]] bool render(const frame_state& state, std::u8string& error) override
            {
                // 크기는 frame이 정한다. 미룬 배치를 내는 frame은 창보다 먼저 새 크기다.
                //  - `renderer_host`가 적어 둔 크기는 낡지만, 그 값은 GPU에서 CPU로 물러설 때만 쓰인다. 레이어드
                //    표시는 처음부터 CPU라 물러설 일이 없다.
                if (resize(state.width, state.height, error) == false)
                    return false;

                const sk_sp<SkTypeface> ui { configured_ui_typeface() };
                draw_frame(*surface_->getCanvas(), codicon_typeface_.get(), ui.get(), state);
                // 알파 0 픽셀은 바탕을 알파 0으로 칠했을 때만 생긴다 — 바탕이 표면 전체를 먼저 칠한다. 그 밖의 frame은
                // 전체를 훑지 않는다.
                const ui_color background { frame_palette(state).window_background };
                if (click_through_ == false && (background >> 24) == 0)
                    raise_transparent_pixels();

                const std::optional<screen_bounds> bounds { std::exchange(next_bounds_, std::nullopt) };
                POINT position {};
                if (bounds.has_value())
                    position = { bounds->x, bounds->y };
                SIZE size { width_, height_ };
                POINT origin {};
                BLENDFUNCTION blend { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                if (UpdateLayeredWindow(window_, nullptr, bounds.has_value() ? &position : nullptr, &size, memory_dc_, &origin, 0, &blend, ULW_ALPHA) == FALSE)
                {
                    error = u8"The layered renderer failed to update the layered window.";
                    return false;
                }
                return true;
            }

        private:
            // 알파 0 픽셀을 알파 1로 올린다 — 레이어드 창은 알파 0인 자리의 클릭을 뒤 창에 넘긴다.
            // premultiplied라 색 성분도 0이므로 1/255 검정이 된다. 눈에는 그대로 투명하다.
            void raise_transparent_pixels() noexcept
            {
                const std::size_t count { static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) };
                for (std::size_t index = 0; index < count; ++index)
                    if ((bits_[index] >> 24) == 0)
                        bits_[index] = 0x01000000u;
            }

            void release_bitmap() noexcept
            {
                if (bitmap_ == nullptr)
                    return;
                SelectObject(memory_dc_, previous_bitmap_);
                DeleteObject(bitmap_);
                bitmap_ = nullptr;
                bits_ = nullptr;
                previous_bitmap_ = nullptr;
            }

            HWND window_ { nullptr };
            bool click_through_ { false };
            int width_ { 1 };
            int height_ { 1 };
            HDC memory_dc_ { nullptr };
            HBITMAP bitmap_ { nullptr };
            HGDIOBJ previous_bitmap_ { nullptr };
            std::uint32_t* bits_ { nullptr };
            sk_sp<SkSurface> surface_ {};
            std::optional<screen_bounds> next_bounds_ {};
            sk_sp<SkTypeface> codicon_typeface_ {};
            sk_sp<SkTypeface> ui_typeface_ {};
        };
    } // namespace

    renderer_factory_result create_cpu_skia_renderer(const HWND window, const cpu_presentation& presentation)
    {
        RECT client_rectangle {};
        if (GetClientRect(window, &client_rectangle) == FALSE)
            return { nullptr, u8"The CPU renderer failed to read the initial client size." };

        std::unique_ptr<skia_renderer> renderer {};
        if (presentation.layered)
            renderer = std::make_unique<cpu_layered_renderer>(window, presentation.click_through);
        else
            renderer = std::make_unique<cpu_skia_renderer>(window);
        std::u8string error {};
        if (false == renderer->resize(client_rectangle.right - client_rectangle.left, client_rectangle.bottom - client_rectangle.top, error))
            return { nullptr, std::move(error) };
        return { std::move(renderer), {} };
    }
} // namespace luil::win32
