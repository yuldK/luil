#include "host/frame_state.h"

#include "luil/theme/ui_style.h"
#include "luil/theme/ui_theme.h"

#include "include/core/SkBlendMode.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <chrono>

namespace luil {
    const ui_style& frame_style(const frame_state& state) noexcept
    {
        return state.style != nullptr ? *state.style : default_ui_style();
    }

    ui_color_palette frame_palette(const frame_state& state) noexcept
    {
        // 고대비는 사용자가 고른 시스템 색으로 합성한다 — 앱의 스타일보다 세다.
        // 키 컬러 경로는 스타일의 중립 색 위에 accent를 얹는 나머지 두 테마의 것이다.
        if (state.theme == color_theme::high_contrast)
            return high_contrast_palette_for(state.high_contrast);
        return color_palette_for(frame_style(state), state.theme, accent_for(state.accent_id));
    }

    namespace {
        // 표면 둘레에 1px 테두리를 긋는다. 획의 중심을 반 픽셀 안으로 들여 획 전체가 자리 안에 든다.
        void draw_border(SkCanvas& canvas, const ui_color_palette& colors, const float scale, const SkRect& bounds)
        {
            SkPaint border {};
            border.setColor(colors.tooltip_border);
            border.setStyle(SkPaint::kStroke_Style);
            border.setStrokeWidth(scale);
            const float inset { scale / 2.0f };
            canvas.drawRect(bounds.makeInset(inset, inset), border);
        }

        // popup 하나를 주 tree 위에 겹쳐 그린다.
        // 데스크톱은 popup이 자기 창이라 창의 배경·OS 그림자가 경계를 세운다. 여기서는 표면이
        // 그 셋을 대신한다: 아래로 번지는 그림자, 창 배경, 그리고 tree와 테두리다.
        void draw_overlay(SkCanvas& canvas, draw_context& context, const ui_color_palette& colors, const float scale, const overlay_layer& layer)
        {
            if (layer.bounds.width <= 0 || layer.bounds.height <= 0)
                return;
            const pixel_rect& area { layer.bounds };
            const SkRect bounds { SkRect::MakeXYWH(static_cast<float>(area.x), static_cast<float>(area.y), static_cast<float>(area.width), static_cast<float>(area.height)) };
            SkPaint shadow {};
            shadow.setColor(SkColorSetARGB(96, 0, 0, 0));
            shadow.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, 6.0f * scale));
            canvas.drawRect(bounds.makeOffset(0.0f, 3.0f * scale), shadow);

            SkAutoCanvasRestore restore { &canvas, true };
            canvas.clipRect(bounds);
            SkPaint background {};
            background.setColor(colors.window_background);
            canvas.drawRect(bounds, background);
            if (layer.tree != nullptr)
            {
                SkAutoCanvasRestore restore_origin { &canvas, true };
                canvas.translate(bounds.x(), bounds.y());
                layer.tree->draw(context, layer.interaction);
            }
            if (layer.border)
                draw_border(canvas, colors, scale, bounds);
        }
    } // namespace

    void draw_frame(SkCanvas& canvas, SkTypeface* const codicon_typeface, SkTypeface* const ui_typeface, const frame_state& state)
    {
        const float scale { state.dpi_scale };
        const ui_color_palette colors { frame_palette(state) };
        draw_context context {
            .canvas = canvas,
            .codicon_typeface = codicon_typeface,
            .ui_typeface = ui_typeface,
            .code_typeface = state.code_typeface != nullptr ? state.code_typeface : ui_typeface,
            .fonts = state.fonts,
            .palette = colors,
            .scale = scale,
            .now = std::chrono::steady_clock::now(),
            .maximized = state.maximized,
            .fullscreen = state.fullscreen,
            .metrics = frame_style(state).metrics,
        };

        // tree가 화면 전체를 그린다 (caption 포함).
        // 없으면 배경만 칠한다.
        canvas.clear(colors.window_background);
        // 내용은 안전 영역의 원점에서 시작한다. 배경은 위에서 표면 전체에 칠했다.
        //  - 옮긴 원점은 tree에만 쓴다. 아래의 테두리와 웹뷰 구멍은 표면 좌표다.
        if (state.tree != nullptr)
        {
            SkAutoCanvasRestore restore_origin { &canvas, true };
            canvas.translate(static_cast<float>(state.origin_x), static_cast<float>(state.origin_y));
            state.tree->draw(context, state.interaction);
        }

        // 주 tree 위의 popup이다 (모바일). 뒤의 것이 위다.
        //  - 주 tree의 tooltip·끌기 표시는 위에서 이미 그렸으므로 popup이 그 위를 덮는다. 터치
        //    화면에는 주 tree의 hover tooltip이 서지 않아 겹칠 일이 드물다.
        for (const overlay_layer& layer : state.overlays)
            draw_overlay(canvas, context, colors, scale, layer);

        // popup의 테두리다. tree 위에 긋는다 — 가장자리까지 채운 내용에도 경계가 남는다.
        if (state.border)
            draw_border(canvas, colors, scale, SkRect::MakeWH(static_cast<float>(state.width), static_cast<float>(state.height)));

        // 웹뷰가 드러날 자리를 비운다.
        //
        // **다 그린 뒤다.** 그리기 도중에 비우면 뒤에 그려지는 것이 도로 덮는다.
        // 여기서 비우면 그 자리에 그려진 것이 전부 지워지는데, 그래도 되는 frame만
        // 목록에 담겨 온다 (`plan_webview_layout`의 판정).
        //  - 알파 0이 합성에서 뜻을 가지려면 스왑체인이 `PREMULTIPLIED`여야 한다.
        //    그 값이 없으면 여기서 지운 자리가 **불투명 검정**이 된다.
        if (state.holes.empty())
            return;
        SkPaint clear {};
        clear.setBlendMode(SkBlendMode::kClear);
        for (const pixel_rect& hole : state.holes)
        {
            if (hole.width <= 0 || hole.height <= 0)
                continue;
            canvas.drawRect(SkRect::MakeXYWH(static_cast<float>(hole.x), static_cast<float>(hole.y), static_cast<float>(hole.width), static_cast<float>(hole.height)), clear);
        }
    }
} // namespace luil
