#include "luil/ui/toast_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <memory>
#include <utility>

namespace luil {
    namespace {
        // 심각도의 역할색이다.
        // info는 중립 알림이라 강조하지 않고, success가 "정상"의 키 컬러를 쓴다.
        [[nodiscard]] ui_color severity_color(const ui_color_palette& palette, const toast_severity severity) noexcept
        {
            switch (severity)
            {
            case toast_severity::success:
                return palette.accent;
            case toast_severity::warning:
                return palette.warning_accent;
            case toast_severity::error:
                return palette.error_accent;
            case toast_severity::info:
                break;
            }
            return with_alpha(palette.primary_foreground, 0.75f);
        }

        [[nodiscard]] char32_t severity_glyph(const toast_severity severity) noexcept
        {
            switch (severity)
            {
            case toast_severity::success:
                return codicons::icon_pass;
            case toast_severity::warning:
                return codicons::icon_warning;
            case toast_severity::error:
                return codicons::icon_error;
            case toast_severity::info:
                break;
            }
            return codicons::icon_info;
        }
    } // namespace

    toast_element::toast_element(toast_config config)
        : ui_element { ui_element_id { ui_element_kind::toast, config.id } }
        , config_ { std::move(config) }
    {
        // 토스트는 내용 위에 뜨는 표면이다.
        // 액션 버튼이 없어도 몸통 클릭이 아래 element로 새지 않는다.
        set_hit_opaque(true);
        if (config_.action_label.empty() || config_.action == nullptr)
            return;

        auto action { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::toast_action, config_.id }, text_button_config { .text = config_.action_label }) };
        // 스스로 사라지는 알림이다. 초점이 얹히면 그 초점이 증발한다 —
        // Tab이 여기 서면 다음 Tab이 갈 곳을 잃는다.
        action->set_tab_stop(false);
        action->set_cursor(ui_cursor::hand);
        action->set_action(ui_trigger::left_click, config_.action);
        action_ = action.get();
        add_child(std::move(action));
    }

    float toast_element::opacity_at(const std::chrono::steady_clock::time_point now) const noexcept
    {
        if (config_.duration.count() <= 0)
            return 1.0f;
        const auto remaining { config_.shown_at + config_.duration - now };
        if (remaining <= std::chrono::steady_clock::duration::zero())
            return 0.0f;
        if (remaining >= toast_fade)
            return 1.0f;
        return std::chrono::duration<float, std::milli>(remaining).count() / std::chrono::duration<float, std::milli>(toast_fade).count();
    }

    void toast_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        if (action_ == nullptr)
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float button_width { toast_action_width * scale };
        const float button_height { 24.0f * scale };
        const float inset { 8.0f * scale };
        action_->arrange(context.for_child({ context.slot.x + context.slot.width - inset - button_width, context.slot.y + (context.slot.height - button_height) / 2.0f, button_width, button_height }));
    }

    void toast_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float opacity { opacity_at(context.now) };
        if (opacity <= 0.0f)
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        // 흐려짐은 토스트 전체(액션 버튼 포함)에 함께 적용한다.
        const bool faded { opacity < 1.0f };
        if (faded)
            context.canvas.saveLayerAlphaf(&body, opacity);

        const float radius { 6.0f * scale };
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.notice_background));
        SkPaint border { solid_paint(context.palette.tooltip_border) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.0f * scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

        const float inset { 10.0f * scale };
        const float glyph_size { 16.0f * scale };
        const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 14.0f * scale };
        const SkPaint severity { solid_paint(severity_color(context.palette, config_.severity)) };
        draw_centered_glyph(context.canvas, severity_glyph(config_.severity), { box.x + inset, box.y, glyph_size, box.height }, glyph_font, severity);

        const float text_left { box.x + inset + glyph_size + 8.0f * scale };
        const float reserved { action_ != nullptr ? (toast_action_width + 16.0f) * scale : inset };
        const float text_width { box.x + box.width - reserved - text_left };
        if (text_width > 0.0f)
        {
            const SkFont font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
            const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
            static_cast<void>(draw_text_within(context.canvas, config_.text, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
        }

        draw_children(context, interaction);
        if (faded)
            context.canvas.restore();
    }

    std::optional<std::chrono::steady_clock::time_point> toast_element::next_update(const update_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        if (config_.duration.count() <= 0)
            return std::nullopt;

        // 흐려지기 전에는 그 시작 시각까지 그대로다.
        const auto fade_begin { config_.shown_at + config_.duration - toast_fade };
        if (context.now < fade_begin)
            return fade_begin;
        // 흐려지는 동안은 계속 움직인다.
        if (context.now < config_.shown_at + config_.duration)
            return context.now;
        // 다 사라졌다.
        // 목록에서 지우는 것은 앱 몫이라 다시 그릴 것이 없다.
        return std::nullopt;
    }

    toast_stack_element::toast_stack_element(toast_stack_config config, std::vector<toast_config> toasts)
        : ui_element { ui_element_id { ui_element_kind::toast_stack, config.owner } }
        , config_ { std::move(config) }
    {
        // 최대 개수를 넘는 설정은 element로 만들지 않는다.
        const std::size_t count { toasts.size() < config_.maximum_count ? toasts.size() : config_.maximum_count };
        for (std::size_t index = 0; index < count; ++index)
        {
            auto toast { std::make_unique<toast_element>(std::move(toasts[index])) };
            toasts_.push_back(toast.get());
            add_child(std::move(toast));
        }
    }

    void toast_stack_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float width { config_.width * scale };
        const float height { toast_height * scale };
        const float margin { config_.margin * scale };
        const float stride { (toast_height + config_.spacing) * scale };
        const bool at_left { config_.corner == toast_corner::top_left || config_.corner == toast_corner::bottom_left };
        const bool at_top { config_.corner == toast_corner::top_left || config_.corner == toast_corner::top_right };
        const float x { at_left ? context.slot.x + margin : context.slot.x + context.slot.width - margin - width };

        for (std::size_t index = 0; index < toasts_.size(); ++index)
        {
            const float offset { margin + static_cast<float>(index) * stride };
            const float y { at_top ? context.slot.y + offset : context.slot.y + context.slot.height - offset - height };
            toasts_[index]->arrange(context.for_child({ x, y, width, height }));
        }
    }

    void toast_stack_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }
    access_info toast_element::accessibility() const
    {
        return { .role = access_role::alert, .name = config_.text };
    }
} // namespace luil
