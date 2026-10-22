#include "win32/drag_ghost_window.h"

#include "host/font_registry.h"
#include "luil/ui/ui_tree.h"
#include "win32/dpi_scale.h"
#include "win32/embedded_assets.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurfaceProps.h"
#include "include/core/SkTypeface.h"

#include <shellscalingapi.h>

#include <chrono>
#include <cstring>

namespace luil::win32 {
    namespace {
        constexpr wchar_t drag_ghost_class_name[] { L"Luil.DragGhost" };
    } // namespace

    POINT drag_ghost_position(const POINT cursor, const std::uint32_t dpi) noexcept
    {
        const int offset { scaled_pixels(drag_ghost_offset, dpi_scale(static_cast<int>(dpi))) };
        return POINT { cursor.x + offset, cursor.y + offset };
    }

    drag_ghost_window::~drag_ghost_window()
    {
        destroy();
    }

    void drag_ghost_window::show(const HINSTANCE instance, const drag_payload& payload, const frame_state& appearance, const POINT cursor) noexcept
    {
        if (ensure_window(instance) == false)
            return;
        // 그 점이 있는 모니터의 배율이다 — 창이 걸친 모니터가 아니라 포인터가 가리키는 모니터다.
        UINT dpi_x { 96 };
        UINT dpi_y { 96 };
        if (FAILED(GetDpiForMonitor(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)))
            dpi_x = 96;
        const POINT position { drag_ghost_position(cursor, dpi_x) };
        const ui_color_palette palette { frame_palette(appearance) };
        if (painted_ == false || painted_dpi_ != dpi_x || painted_payload_ != payload || painted_palette_ != palette)
        {
            if (paint(payload, appearance, dpi_x, position) == false)
            {
                hide();
                return;
            }
            painted_ = true;
            painted_dpi_ = dpi_x;
            painted_payload_ = payload;
            painted_palette_ = palette;
        }
        else
            SetWindowPos(window_, HWND_TOPMOST, position.x, position.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        if (visible_ == false)
        {
            ShowWindow(window_, SW_SHOWNOACTIVATE);
            visible_ = true;
        }
    }

    void drag_ghost_window::hide() noexcept
    {
        if (window_ != nullptr && visible_)
            ShowWindow(window_, SW_HIDE);
        visible_ = false;
    }

    void drag_ghost_window::destroy() noexcept
    {
        if (window_ != nullptr)
            DestroyWindow(window_);
        window_ = nullptr;
        visible_ = false;
        painted_ = false;
    }

    bool drag_ghost_window::ensure_window(const HINSTANCE instance) noexcept
    {
        if (window_ != nullptr)
            return true;
        WNDCLASSEXW type {};
        type.cbSize = sizeof(type);
        type.lpfnWndProc = &DefWindowProcW;
        type.hInstance = instance;
        type.lpszClassName = drag_ghost_class_name;
        if (RegisterClassExW(&type) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
        // 클릭이 통과하는(`WS_EX_TRANSPARENT`) 층 창이라 OS hit test가 지나친다. 작업 표시줄·Alt+Tab에 서지 않는다.
        window_ = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, drag_ghost_class_name, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
        return window_ != nullptr;
    }

    bool drag_ghost_window::paint(const drag_payload& payload, const frame_state& appearance, const std::uint32_t dpi, const POINT position) noexcept
    {
        const float scale { dpi_scale(static_cast<int>(dpi)) };
        const int width { scaled_pixels(detached_drag_ghost_width, scale) };
        const int height { scaled_pixels(detached_drag_ghost_height, scale) };
        if (width <= 0 || height <= 0)
            return false;

        // 위에서 아래로 내려가는 32비트 BGRA다 — Skia의 N32(Windows)와 같은 배치라 그대로 옮긴다. 층 창은 미리 곱한
        // 알파를 받는다.
        BITMAPINFO information {};
        information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        information.bmiHeader.biWidth = width;
        information.bmiHeader.biHeight = -height;
        information.bmiHeader.biPlanes = 1;
        information.bmiHeader.biBitCount = 32;
        information.bmiHeader.biCompression = BI_RGB;
        void* bits { nullptr };
        const HDC screen { GetDC(nullptr) };
        if (screen == nullptr)
            return false;
        const HDC memory { CreateCompatibleDC(screen) };
        const HBITMAP section { memory != nullptr ? CreateDIBSection(memory, &information, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr };
        bool painted { false };
        if (section != nullptr && bits != nullptr)
        {
            const HGDIOBJ previous { SelectObject(memory, section) };
            try
            {
                SkBitmap image {};
                if (image.installPixels(SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType), bits, static_cast<std::size_t>(width) * 4u))
                {
                    const SkSurfaceProps properties { 0, kUnknown_SkPixelGeometry };
                    SkCanvas canvas { image, properties };
                    canvas.clear(SK_ColorTRANSPARENT);
                    const ui_color_palette palette { frame_palette(appearance) };
                    const sk_sp<SkTypeface> codicon { load_codicon_typeface() };
                    const sk_sp<SkTypeface> ui { configured_ui_typeface() };
                    draw_context context {
                        .canvas = canvas,
                        .codicon_typeface = codicon.get(),
                        .ui_typeface = ui.get(),
                        .code_typeface = ui.get(),
                        .palette = palette,
                        .scale = scale,
                        .now = std::chrono::steady_clock::now(),
                        .metrics = frame_style(appearance).metrics,
                    };
                    draw_detached_drag_ghost(context, payload);
                    POINT origin { 0, 0 };
                    POINT destination { position };
                    SIZE size { width, height };
                    BLENDFUNCTION blend { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                    painted = UpdateLayeredWindow(window_, screen, &destination, &size, memory, &origin, 0, &blend, ULW_ALPHA) != FALSE;
                }
            }
            catch (...)
            {
                painted = false;
            }
            SelectObject(memory, previous);
        }
        if (section != nullptr)
            DeleteObject(section);
        if (memory != nullptr)
            DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        if (painted)
            SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return painted;
    }
} // namespace luil::win32
