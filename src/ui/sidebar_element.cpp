#include "luil/ui/sidebar_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/button_element.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <utility>

namespace luil {
    sidebar_element::sidebar_element(sidebar_config config, std::unique_ptr<ui_element> content)
        : ui_element { ui_element_id { ui_element_kind::sidebar, config.owner } }
        , config_ { std::move(config) }
    {
        // 접힌 폭 0으로 접혔으면 아무것도 만들지 않는다.
        if (width_for(config_) <= 0.0f)
            return;

        if (content != nullptr)
        {
            content_ = content.get();
            add_child(std::move(content));
        }

        if (config_.toggle != nullptr)
        {
            // 화살표는 눌렀을 때 판이 움직일 방향을 가리킨다.
            const bool expands { config_.collapsed };
            const bool points_right { config_.side == sidebar_side::left ? expands : expands == false };
            button_config toggle_button {};
            toggle_button.glyph = points_right ? codicons::icon_chevron_right : codicons::icon_chevron_left;
            toggle_button.corner_radius = 3.0f;
            auto toggle { std::make_unique<button_element>(ui_element_id { ui_element_kind::sidebar_toggle, config_.owner }, toggle_button) };
            toggle->set_tooltip(config_.toggle_tooltip);
            toggle->set_cursor(ui_cursor::hand);
            toggle->set_action(ui_trigger::left_click, config_.toggle);
            toggle_ = toggle.get();
            add_child(std::move(toggle));
        }

        // 손잡이는 펼쳐진 동안만 둔다.
        // 접힌 폭은 고정 값이라 토글로만 오간다.
        if (config_.resize != nullptr && config_.collapsed == false)
        {
            // 부호는 손잡이가 맞춘다. 왼쪽 판은 끝쪽으로, 오른쪽 판은 앞쪽으로 끌 때 넓어진다.
            split_handle_config handle_config {};
            handle_config.axis = split_axis::horizontal;
            handle_config.grows = config_.side == sidebar_side::left ? split_grows::toward_end : split_grows::toward_start;
            handle_config.resize = config_.resize;
            auto handle { std::make_unique<split_handle_element>(ui_element_id { ui_element_kind::sidebar_handle, config_.owner }, handle_config) };
            handle_ = handle.get();
            add_child(std::move(handle));
        }
    }

    float sidebar_element::width_for(const sidebar_config& config) noexcept
    {
        // 앱이 지금 폭을 정했으면 그것이다 (전환 중).
        // 정하지 않았으면 목표(`collapsed`)가 곧 지금이다.
        const float width { config.width.has_value() ? *config.width : (config.collapsed ? config.collapsed_width : config.expanded_width) };
        return width > 0.0f ? width : 0.0f;
    }

    void sidebar_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float width { width_for(config_) * scale };
        const float x { config_.side == sidebar_side::left ? context.slot.x : context.slot.x + context.slot.width - width };
        set_bounds({ x, context.slot.y, width, context.slot.height });
        if (width <= 0.0f)
            return;

        if (content_ != nullptr)
        {
            // 토글 버튼이 있으면 그 줄만큼 내려 겹치지 않게 한다.
            const float header { toggle_ != nullptr ? sidebar_header_height * scale : 0.0f };
            content_->arrange(context.for_child({ x, context.slot.y + header, width, context.slot.height - header }));
        }

        if (toggle_ != nullptr)
        {
            const float size { 24.0f * scale };
            const float inset { 8.0f * scale };
            // 안쪽 가장자리(내용 쪽)에 붙이고, 그만한 자리가 없으면 가운데 둔다.
            float toggle_x { config_.side == sidebar_side::left ? x + width - inset - size : x + inset };
            if (width < size + 2.0f * inset)
                toggle_x = x + (width - size) / 2.0f;
            toggle_->arrange(context.for_child({ toggle_x, context.slot.y + inset, size, size }));
        }

        if (handle_ != nullptr)
        {
            // 잡기 쉽도록 안쪽 가장자리에 절반씩 걸친다.
            // 부모가 자식을 자르지 않으므로 바깥 절반도 hit가 된다.
            const float handle_width { sidebar_handle_width * scale };
            const float inner { config_.side == sidebar_side::left ? x + width : x };
            handle_->arrange(context.for_child({ inner - handle_width / 2.0f, context.slot.y, handle_width, context.slot.height }));
        }
    }

    void sidebar_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        if (box.width <= 0.0f)
            return;

        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(context.palette.surface_background));
        // 내용과의 경계선이다.
        const float inner { config_.side == sidebar_side::left ? box.x + box.width - scale : box.x };
        context.canvas.drawRect(SkRect::MakeXYWH(inner, box.y, scale, box.height), solid_paint(context.palette.divider));
        draw_children(context, interaction);
    }
} // namespace luil
