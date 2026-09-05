#include "luil/ui/caption_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/button_element.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <memory>
#include <utility>

namespace luil {
    namespace {
        ui_action make_command_action(const ui_command command)
        {
            return [command](const ui_action_context&) -> std::vector<input_action> { return { input_action { command } }; };
        }

        std::unique_ptr<button_element> make_caption_button(const ui_element_kind kind, const button_config config, const ui_command command, std::u8string tooltip)
        {
            auto button { std::make_unique<button_element>(ui_element_id { kind }, config) };
            // 창 chrome이다. 시스템 창도 Tab으로 창 버튼에 가지 않는다
            // (Alt+Space의 시스템 메뉴가 그 자리다).
            button->set_tab_stop(false);
            button->set_action(ui_trigger::left_click, make_command_action(command));
            button->set_tooltip(std::move(tooltip));
            return button;
        }
    } // namespace

    ui_element_id caption_button_element_id(const caption_button_hover hover) noexcept
    {
        switch (hover)
        {
        case caption_button_hover::minimize:
            return { ui_element_kind::caption_minimize };
        case caption_button_hover::maximize:
            return { ui_element_kind::caption_maximize };
        case caption_button_hover::close:
            return { ui_element_kind::caption_close };
        case caption_button_hover::none:
            break;
        }
        return {};
    }

    caption_element::caption_element(caption_config config)
        : ui_element { ui_element_id { ui_element_kind::caption } }
        , config_ { std::move(config) }
    {
        const button_config window_button {
            .glyph = codicons::icon_chrome_minimize,
            .icon_size = static_cast<float>(config_.metrics.button_icon_size),
            .corner_radius = 0.0f,
            .role = button_visual_role::caption,
        };

        // 설정에서 빠진 버튼은 아예 만들지 않는다.
        // 자리도 차지하지 않고 그 창 스타일도 함께 빠진다
        // (caption-button-design.md).
        if (config_.buttons.minimize)
        {
            button_config minimize_config { window_button };
            auto minimize { make_caption_button(ui_element_kind::caption_minimize, minimize_config, ui_command::window_minimize, config_.minimize_tooltip) };
            minimize_ = minimize.get();
            add_child(std::move(minimize));
        }

        if (config_.buttons.maximize)
        {
            button_config maximize_config { window_button };
            maximize_config.glyph = codicons::icon_chrome_maximize;
            maximize_config.maximized_glyph = codicons::icon_chrome_restore;
            auto maximize { make_caption_button(ui_element_kind::caption_maximize, maximize_config, ui_command::window_toggle_maximize, config_.maximize_tooltip) };
            maximize_ = maximize.get();
            add_child(std::move(maximize));
        }

        if (config_.buttons.close)
        {
            button_config close_config { window_button };
            close_config.glyph = codicons::icon_chrome_close;
            close_config.role = button_visual_role::caption_close;
            auto close { make_caption_button(ui_element_kind::caption_close, close_config, ui_command::window_close, config_.close_tooltip) };
            close_ = close.get();
            add_child(std::move(close));
        }
    }

    // 오른쪽 끝에서 닫기 → 최대화 → 최소화 순으로 쌓는다.
    // 빠진 버튼은 자리를 차지하지 않아 남은 버튼이 오른쪽으로 당겨진다.
    const ui_element* caption_element::leftmost_button() const noexcept
    {
        if (minimize_ != nullptr)
            return minimize_;
        if (maximize_ != nullptr)
            return maximize_;
        return close_;
    }

    void caption_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float caption_height { height_for(config_) * scale };
        set_bounds({ context.slot.x, context.slot.y, context.slot.width, caption_height });

        const float button_width { static_cast<float>(config_.metrics.button_width) * scale };
        float edge { context.slot.x + context.slot.width };
        const auto place = [&edge, &context, button_width, caption_height, scale](ui_element* const button) {
            if (button == nullptr)
                return;
            edge -= button_width;
            button->arrange(context.for_child({ edge, context.slot.y, button_width, caption_height }));
        };
        place(close_);
        place(maximize_);
        place(minimize_);
    }

    void caption_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const SkPaint fill { solid_paint(context.palette.caption.background) };
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), fill);

        const SkPaint foreground { solid_paint(context.palette.caption.foreground) };
        const SkFont title_font { sk_ref_sp(context.ui_typeface), static_cast<float>(config_.metrics.title_font_size) * scale };

        float title_left { box.x + static_cast<float>(config_.metrics.title_left_padding) * scale };
        if (context.codicon_typeface != nullptr && config_.application_icon != 0)
        {
            const SkFont icon_font { sk_ref_sp(context.codicon_typeface), static_cast<float>(config_.metrics.application_icon_size) * scale };
            const rect_f icon_slot { box.x, box.y, static_cast<float>(config_.metrics.application_icon_slot_width) * scale, box.height };
            draw_centered_glyph(context.canvas, config_.application_icon, icon_slot, icon_font, foreground);
            title_left = box.x + static_cast<float>(config_.metrics.application_icon_slot_width + config_.metrics.title_icon_gap) * scale;
        }
        // 제목이 창 버튼을 침범하지 않도록 버튼 왼쪽까지만 그린다.
        // 버튼이 하나도 없으면 캡션 오른쪽 끝까지다.
        const ui_element* const leftmost { leftmost_button() };
        const float title_right { leftmost != nullptr ? leftmost->bounds().x - static_cast<float>(config_.metrics.title_icon_gap) * scale : box.x + box.width };
        const float title_limit { title_right - title_left };
        static_cast<void>(draw_text_within(context.canvas, config_.title, title_left, box.y + centered_text_baseline(title_font, box.height), title_limit, title_font, foreground));

        draw_children(context, interaction);
    }

    float caption_element::height_for(const caption_config& config) noexcept
    {
        return static_cast<float>(config.metrics.height);
    }

    ui_tree make_caption_tree(const float window_width, const float scale, caption_config config)
    {
        auto caption { std::make_unique<caption_element>(std::move(config)) };
        caption->arrange({ { 0.0f, 0.0f, window_width, 0.0f }, scale });
        return ui_tree { std::move(caption) };
    }
    access_info caption_element::accessibility() const
    {
        return { .role = access_role::title_bar, .name = config_.title };
    }
} // namespace luil
