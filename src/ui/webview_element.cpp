#include "luil/ui/webview_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkRect.h"

#include <utility>

namespace luil {
    webview_element::webview_element(const ui_element_id id, webview_config config)
        : ui_element { id }
        , config_ { std::move(config) }
    {
        // 이 자리의 포인터는 웹 콘텐츠의 것이다. 뒤에 있는 것이 받으면
        // 페이지 위를 눌렀는데 그 아래 목록이 반응한다.
        set_hit_opaque(true);
        // Tab이 여기 **닿을** 수는 있어야 한다. 닿으면 초점을 페이지로 넘긴다.
        // 자리표 자신에는 액션이 없어 기본 판정("누를 수 있으면 자리")이 거짓이므로
        // 명시한다.
        set_tab_stop(true);
    }

    const webview_config& webview_element::config() const noexcept
    {
        return config_;
    }

    float webview_element::scale() const noexcept
    {
        return scale_;
    }

    void webview_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;
    }

    void webview_element::draw(draw_context& context, const interaction_snapshot&) const
    {
        const rect_f box { bounds() };
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;

        // 바탕을 칠한다. 웹뷰가 실제로 서면 이 위가 구멍으로 비워지므로 보이지
        // 않고, 못 서면 이것이 그 자리의 모습이 된다.
        const SkRect area { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        context.canvas.drawRect(area, solid_paint(context.palette.surface_background));

        if (config_.placeholder.empty() || config_.placeholder_font_size <= 0.0f)
            return;

        const SkFont font { sk_ref_sp(context.ui_typeface), config_.placeholder_font_size * scale_ };
        SkFontMetrics metrics {};
        font.getMetrics(&metrics);
        // 글자의 시각적 가운데는 상자의 가운데에서 ascent·descent의 절반만큼 내려간다.
        const float baseline { box.y + (box.height - metrics.fAscent - metrics.fDescent) / 2.0f };
        const float width { measure_text(config_.placeholder, font) };
        const float left { box.x + (box.width - width) / 2.0f };
        static_cast<void>(draw_text_within(context.canvas, config_.placeholder, left, baseline, box.width, font, solid_paint(context.palette.secondary_foreground)));
    }

    access_info webview_element::accessibility() const
    {
        // 페이지 내용은 창의 UIA tree에 저절로 들어오므로 여기서 흉내 내지 않는다.
        // 이 자리가 무엇인지를 말하는 묶음 하나다.
        return { .role = access_role::group, .name = config_.name };
    }
} // namespace luil
