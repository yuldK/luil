#include "win32/frame_state.h"

#include "luil/theme/ui_theme.h"

#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <chrono>

namespace luil {
    void draw_frame(SkCanvas& canvas, SkTypeface* const codicon_typeface, SkTypeface* const ui_typeface, const frame_state& state)
    {
        const float scale { state.dpi_scale };
        // 고대비는 사용자가 고른 시스템 색으로 합성한다.
        // 키 컬러 경로는 색을 하드코딩하지 않은 나머지 두 테마의 것이다.
        const bool high_contrast { state.theme == color_theme::high_contrast };
        const ui_color_palette colors { high_contrast ? high_contrast_palette_for(state.high_contrast) : color_palette_for(state.theme, accent_for(state.accent_id)) };
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
        };

        // tree가 화면 전체를 그린다 (caption 포함).
        // 없으면 배경만 칠한다.
        canvas.clear(colors.window_background);
        if (state.tree != nullptr)
            state.tree->draw(context, state.interaction);

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
            canvas.drawRect(
                SkRect::MakeXYWH(static_cast<float>(hole.x), static_cast<float>(hole.y), static_cast<float>(hole.width), static_cast<float>(hole.height)), clear);
        }
    }
} // namespace luil
