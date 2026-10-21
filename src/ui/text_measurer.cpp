#include "luil/ui/ui_element.h"

#include "host/font_registry.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

// 기본 측정기는 글꼴 등록부를 부른다. `ui_element.cpp`와 떼어 둔 것은 링크 때문이다 — 등록부는 플랫폼의
// 글꼴 hook을 부르므로, 배치 측정기를 쓰지 않는 실행 파일(플랫폼 계층 없이 core만 링크하는 test)이
// element 하나만 써도 그 hook까지 끌려오지 않게 한다.
namespace luil {
    namespace {
        // 그리기와 같은 글꼴이다 — 렌더러가 `draw_context::ui_typeface`에 넣는 것이 이 등록부의 값이다.
        [[nodiscard]] float measure_in_ui_typeface(const std::u8string_view text, const float pixel_size)
        {
            const SkFont font { configured_ui_typeface(), pixel_size };
            return measure_text(text, font);
        }
    } // namespace

    const text_measurer& default_text_measurer()
    {
        static const text_measurer measurer { measure_in_ui_typeface };
        return measurer;
    }

    const text_measurer& arrange_context::measure_text() const
    {
        return measurer != nullptr ? *measurer : default_text_measurer();
    }

    const text_measurer& measure_context::measure_text() const
    {
        return measurer != nullptr ? *measurer : default_text_measurer();
    }

    const ui_metrics& measure_context::style_metrics() const noexcept
    {
        return metrics != nullptr ? *metrics : default_ui_style().metrics;
    }

    float measure_context::text_width(const std::u8string_view text, const float font_size) const
    {
        if (text.empty())
            return 0.0f;
        // 그리기와 같은 글자 크기다 — 시스템 글자 크기 설정(`scaled_font_size`)까지 같아야 잰 폭과 그린 글이 맞는다.
        const float ratio { scale > 0.0f ? scale : 1.0f };
        return measure_text()(text, scaled_font_size(font_size, ratio)) / ratio;
    }
} // namespace luil