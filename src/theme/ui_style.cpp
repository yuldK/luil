#include "luil/theme/ui_style.h"

#include "luil/generated/ui_style.h"

namespace luil {
    const neutral_color_palette& ui_style::neutral_for(const color_theme theme) const noexcept
    {
        return theme == color_theme::light ? light : dark;
    }

    const ui_style& default_ui_style() noexcept
    {
        return generated::default_style;
    }

    ui_color_palette color_palette_for(const ui_style& style, const color_theme theme, const accent_definition& accent) noexcept
    {
        // 고대비는 접근성 설정이라 앱의 스타일보다 세다.
        // 실제 시스템 색을 아는 platform은 `high_contrast_palette_for`를 직접 부른다.
        if (theme == color_theme::high_contrast)
            return high_contrast_palette_for(high_contrast_colors {});
        return compose_palette(style.neutral_for(theme), accent.for_theme(theme), style.tones);
    }
} // namespace luil
