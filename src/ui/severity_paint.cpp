#include "ui/severity_paint.h"

#include "luil/generated/codicons.h"

namespace luil {
    ui_color severity_color(const ui_color_palette& palette, const ui_severity severity) noexcept
    {
        switch (severity)
        {
        case ui_severity::success:
            return palette.accent;
        case ui_severity::warning:
            return palette.warning_accent;
        case ui_severity::error:
            return palette.error_accent;
        case ui_severity::info:
            break;
        }
        return palette.secondary_foreground;
    }

    char32_t severity_glyph(const ui_severity severity) noexcept
    {
        switch (severity)
        {
        case ui_severity::success:
            return codicons::icon_pass;
        case ui_severity::warning:
            return codicons::icon_warning;
        case ui_severity::error:
            return codicons::icon_error;
        case ui_severity::info:
            break;
        }
        return codicons::icon_info;
    }
}
