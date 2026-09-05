#include "win32/frame_state.h"

#include "luil/theme/ui_theme.h"

#include "include/core/SkCanvas.h"

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
    }
} // namespace luil
