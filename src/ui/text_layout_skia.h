#pragma once

#include "luil/ui/text_layout.h"
#include "include/core/SkFont.h"

#include <cstdint>
#include <optional>

namespace luil {
    [[nodiscard]] text_layout make_font_text_layout(text_layout_request request, const SkFont& font);
    [[nodiscard]] SkFont text_layout_font(const SkFont& font);
    [[nodiscard]] sk_sp<SkTypeface> text_layout_fallback(char32_t codepoint);
    // 모든 cache key가 같은 hash를 받게 한다. 충돌을 일부러 내야 hash만으로 같은 요청이라 단정하지 않는지 잴 수 있다.
    // 저장된 hash가 낡으므로 cache를 비운다. std::nullopt로 되돌린다.
    void set_text_layout_cache_hash_for_test(std::optional<std::uint64_t> hash);
} // namespace luil
