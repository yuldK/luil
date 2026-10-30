#include "demo/zoom_page.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/stack_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"

#include <algorithm>
#include <utility>

namespace demo {
    namespace {
        class diagram final : public luil::ui_element
        {
        public:
            diagram()
                : ui_element { { kind_layout, u8"diagram" } }
            {
                for (int index { 0 }; index < 4; ++index)
                    add_child(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, to_u8(index) },
                        luil::label_config {
                            .text = u8"노드 " + to_u8(index + 1),
                            .font_size = 18.0f,
                            .color = luil::label_color_role::primary,
                            .background = luil::label_background_role::notice,
                            .padding = 8.0f,
                        }));
            }
            void arrange(const luil::arrange_context& context) override
            {
                set_bounds(context.slot);
                scale_ = context.scale;
                int index { 0 };
                for (const auto& child : children())
                {
                    child->arrange(
                        context.for_child({ context.slot.x + (200.0f + index * 160.0f) * scale_, context.slot.y + (index % 2 == 0 ? 250.0f : 380.0f) * scale_, 120.0f * scale_, 60.0f * scale_ }));
                    ++index;
                }
            }
            void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
            {
                SkPaint paint {};
                paint.setColor(context.palette.divider);
                paint.setStrokeWidth(scale_);
                const auto box { bounds() };
                for (int x { 0 }; x <= 1000; x += 50)
                    context.canvas.drawLine(box.x + x * scale_, box.y, box.x + x * scale_, box.y + box.height, paint);
                for (int y { 0 }; y <= 700; y += 50)
                    context.canvas.drawLine(box.x, box.y + y * scale_, box.x + box.width, box.y + y * scale_, paint);
                draw_children(context, interaction);
            }

        private:
            float scale_ { 1.0f };
        };
    }

    luil::zoom_view_config zoom_page::config() const
    {
        return {
            .owner = u8"diagram",
            .zoom = state_.zoom,
            .origin = state_.origin,
            .minimum_zoom = 0.1f,
            .maximum_zoom = 5.0f,
            .content_bounds = luil::rect_f { -500.0f, -350.0f, 1000.0f, 700.0f },
            .zoom_by = [](const float factor, const luil::zoom_point anchor) { return luil::make_app_action(zoom_intent { factor, anchor }); },
            .pan_by = [](const luil::zoom_point delta) { return luil::make_app_action(pan_intent { delta }); },
            .zoom_to = [](const float value) { return luil::make_app_action(zoom_to_intent { value }); },
        };
    }

    bool zoom_page::handle(const luil::app_message& message)
    {
        if (const auto* const zoom { message.get<zoom_intent>() }; zoom != nullptr)
            state_ = luil::zoom_about(config(), viewport_, zoom->factor, zoom->anchor);
        else if (const auto* const pan { message.get<pan_intent>() }; pan != nullptr)
            state_ = luil::pan_by(config(), viewport_, pan->delta);
        else if (const auto* const target { message.get<zoom_to_intent>() }; target != nullptr)
            state_ = luil::zoom_about(config(), viewport_, target->value / state_.zoom, {});
        else
            return false;
        return true;
    }

    std::unique_ptr<luil::ui_element> zoom_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(scale);
        viewport_ = { std::max(0.0f, width - 32.0f), std::max(0.0f, height - 110.0f) };
        state_ = luil::zoom_about(config(), viewport_, 1.0f, {});
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"zoom-page" }, luil::stack_config { .spacing = 8.0f, .padding = luil::edge_insets::all(16.0f) }) };
        column->add(
            make_label({ kind_text, u8"zoom-help" }, u8"휠·두 손가락: 확대 / 빈 곳 끌기: 이동 / 초점에서 +, -, 0, 화살표, Page / 멈추면 커서가 숨습니다", 12.0f, luil::label_color_role::primary),
            28.0f);
        // 축 맞춤 단추 둘이다 (image-frames-scaling-design.md). 계산은 라이브러리의 `zoom_fit_actions`이고 단추 글은 앱이 단다.
        //  - 맞추지 않는 축은 지금 창 가운데에 있는 도면 자리를 지킨다.
        luil::stack_config controls_config {};
        controls_config.direction = luil::stack_direction::row;
        controls_config.spacing = 8.0f;
        auto controls { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"zoom-controls" }, controls_config) };
        controls->add(std::make_unique<luil::zoom_controls_element>(luil::zoom_controls_config { .view = config(), .viewport = viewport_ }), 244.0f);
        const auto fit_button = [this](std::u8string owner, std::u8string text, const luil::zoom_fit axis) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, std::move(owner) }, luil::text_button_config { .text = std::move(text) }) };
            button->set_action(
                luil::ui_trigger::left_click, [view = config(), viewport = viewport_, axis](const luil::ui_action_context&) { return luil::zoom_fit_actions(view, viewport, 16.0f, 5.0f, axis); });
            return button;
        };
        controls->add(fit_button(u8"zoom-fit-width", u8"폭 맞춤", luil::zoom_fit::width), 72.0f);
        controls->add(fit_button(u8"zoom-fit-height", u8"높이 맞춤", luil::zoom_fit::height), 80.0f);
        column->add(std::move(controls), 34.0f);
        auto view { std::make_unique<luil::zoom_view_element>(config()) };
        view->set_access_name(u8"도면 확대 보기");
        // 그림 위에서 포인터가 1.5초 멈추면 커서를 감춘다. 움직이거나 누르면 곧바로 돌아온다.
        view->set_cursor_idle_hide(std::chrono::milliseconds { 1500 });
        view->set_content(std::make_unique<diagram>());
        column->add_flexible(std::move(view));
        return column;
    }
}
