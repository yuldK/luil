#include "luil/ui/app_bar_element.h"

#include "luil/ui/button_element.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <memory>
#include <string>
#include <utility>

namespace luil {
    namespace {
        [[nodiscard]] std::unique_ptr<button_element> make_app_bar_button(std::u8string owner, const app_bar_button& source, const app_bar_metrics& metrics)
        {
            const button_config config {
                .glyph = source.glyph,
                .icon_size = static_cast<float>(metrics.icon_size),
                // 앱 바의 아이콘 버튼은 둥근 터치 자리다 (지름 = 버튼 크기).
                .corner_radius = static_cast<float>(metrics.button_size) / 2.0f,
                .role = button_visual_role::toolbar,
            };
            auto button { std::make_unique<button_element>(ui_element_id { ui_element_kind::app_bar_button, std::move(owner) }, config) };
            button->set_tooltip(source.label);
            if (source.action != nullptr)
                button->set_action(ui_trigger::left_click, source.action);
            return button;
        }
    } // namespace

    app_bar_element::app_bar_element(app_bar_config config)
        : ui_element { ui_element_id { ui_element_kind::app_bar } }
        , config_ { std::move(config) }
    {
        if (config_.navigation.has_value())
        {
            auto navigation { make_app_bar_button(u8"navigation", *config_.navigation, config_.metrics) };
            navigation_ = navigation.get();
            add_child(std::move(navigation));
        }
        actions_.reserve(config_.actions.size());
        for (std::size_t index { 0 }; index < config_.actions.size(); ++index)
        {
            const std::string number { std::to_string(index) };
            auto action { make_app_bar_button(u8"action:" + std::u8string { number.begin(), number.end() }, config_.actions[index], config_.metrics) };
            actions_.push_back(action.get());
            add_child(std::move(action));
        }
    }

    // 선행 버튼은 왼쪽 끝, 동작 버튼은 오른쪽 끝부터 쌓는다. 버튼은 바의 세로 가운데에 놓인다.
    void app_bar_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float bar_height { height_for(config_) * scale };
        set_bounds({ context.slot.x, context.slot.y, context.slot.width, bar_height });

        const float button_size { static_cast<float>(config_.metrics.button_size) * scale };
        const float edge_padding { static_cast<float>(config_.metrics.edge_padding) * scale };
        const float button_top { context.slot.y + (bar_height - button_size) / 2.0f };
        if (navigation_ != nullptr)
            navigation_->arrange(context.for_child({ context.slot.x + edge_padding, button_top, button_size, button_size }));

        float edge { context.slot.x + context.slot.width - edge_padding };
        for (auto action { actions_.rbegin() }; action != actions_.rend(); ++action)
        {
            edge -= button_size;
            (*action)->arrange(context.for_child({ edge, button_top, button_size, button_size }));
        }
    }

    void app_bar_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const SkPaint fill { solid_paint(context.palette.caption.background) };
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), fill);

        // 제목은 선행 버튼 오른쪽에서 시작해 첫 동작 버튼 왼쪽에서 끝난다.
        const float gap { static_cast<float>(config_.metrics.title_gap) * scale };
        const float title_left { navigation_ != nullptr ? navigation_->bounds().x + navigation_->bounds().width + gap : box.x + static_cast<float>(config_.metrics.title_left_padding) * scale };
        const float title_right { actions_.empty() ? box.x + box.width - static_cast<float>(config_.metrics.title_left_padding) * scale : actions_.front()->bounds().x - gap };
        if (title_right > title_left && config_.title.empty() == false)
        {
            const SkPaint foreground { solid_paint(context.palette.caption.foreground) };
            const SkFont title_font { sk_ref_sp(context.ui_typeface), static_cast<float>(config_.metrics.title_font_size) * scale };
            static_cast<void>(draw_text_within(context.canvas, config_.title, title_left, box.y + centered_text_baseline(title_font, box.height), title_right - title_left, title_font, foreground));
        }

        draw_children(context, interaction);
    }

    access_info app_bar_element::accessibility() const
    {
        return { .role = access_role::title_bar, .name = config_.title };
    }

    float app_bar_element::height_for(const app_bar_config& config) noexcept
    {
        return static_cast<float>(config.metrics.height);
    }
} // namespace luil
