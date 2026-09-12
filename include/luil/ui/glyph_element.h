#pragma once

#include "luil/text/fonts.h"
#include "luil/ui/ui_element.h"

#include <optional>

namespace luil {
    struct glyph_config
    {
        // 글리프 색인이 아닌 Unicode 코드포인트다. 0이면 그리지 않는다.
        char32_t glyph { 0 };
        float font_size { 16.0f };
        // 비어 있으면 draw_context의 내장 Codicons를 쓴다. 소유권을 공유한다.
        sk_sp<SkTypeface> typeface {};
        // 비어 있으면 테마의 primary_foreground다.
        std::optional<ui_color> color {};
        // 비어 있으면 장식이다. 설명이 있으면 접근성 이미지로 노출한다.
        std::u8string description {};
    };

    // bounds 중앙에 폰트 글리프 하나를 그린다. 크기는 논리 픽셀이고 bounds로 잘린다.
    // 지정한 폰트에 없는 글리프는 그리지 않는다 (시스템 폰트로 대체하지 않는다).
    class glyph_element final : public ui_element
    {
    public:
        glyph_element(ui_element_id id, glyph_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        glyph_config config_ {};
    };
} // namespace luil
