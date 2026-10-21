#pragma once

#include "luil/ui/text_layout.h"
#include "include/core/SkFont.h"

namespace luil {
    [[nodiscard]] text_layout make_font_text_layout(text_layout_request request, const SkFont& font);
    [[nodiscard]] SkFont text_layout_font(const SkFont& font);
    [[nodiscard]] sk_sp<SkTypeface> text_layout_fallback(char32_t codepoint);
} // namespace luil
