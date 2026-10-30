#include "mobile_demo/zoom_page.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/stack_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mobile_demo {
    namespace {
        class diagram final : public luil::ui_element
        {
        public:
            // 쪽마다 노드 이름이 달라 넘긴 것이 보인다.
            explicit diagram(const int page)
                : ui_element { { kind_layout, u8"diagram" } }
            {
                for (int index { 0 }; index < 4; ++index)
                    add_child(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, to_u8(index) },
                        luil::label_config {
                            .text = u8"노드 " + to_u8(page) + u8"-" + to_u8(index + 1),
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

        constexpr int page_count { 5 };
        // 쪽 머리의 줄 셋(설명·쪽·쓸기)과 단추 줄이다 (논리 픽셀).
        constexpr float controls_height { 34.0f };
        constexpr float row_spacing { 8.0f };

        [[nodiscard]] std::u8string direction_name(const int horizontal, const int vertical)
        {
            if (horizontal < 0)
                return u8"왼쪽";
            if (horizontal > 0)
                return u8"오른쪽";
            return vertical < 0 ? u8"위" : u8"아래";
        }

        // 받은 경계 쓸기 한 줄이다: 방향, 막힌 양, 뗄 때의 속도.
        [[nodiscard]] std::u8string describe(const luil::zoom_edge_swipe& swipe)
        {
            const bool horizontal { swipe.horizontal != 0 };
            const float blocked { std::abs(horizontal ? swipe.blocked.x : swipe.blocked.y) };
            const float speed { std::abs(horizontal ? swipe.velocity.x : swipe.velocity.y) };
            return u8"경계 쓸기: " + direction_name(swipe.horizontal, swipe.vertical) + u8" · 막힘 " + to_u8(static_cast<int>(std::lround(blocked))) + u8" · 속도 "
                + to_u8(static_cast<int>(std::lround(speed))) + u8"/s";
        }
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
            // 쪽 보기라 맞춤 상태에서는 옮길 여지가 없다. 그래서 쓸기가 전부 경계에 막혀 쪽 넘기기가 된다.
            .bounds = luil::zoom_bounds::contain,
            .edge_swipe = [](const luil::zoom_edge_swipe& swipe) { return luil::make_app_action(edge_swipe_intent { swipe }); },
            // 도면에는 탭 동작이 없어 두 번 탭을 확대에 쓴다.
            .double_tap = { .enabled = true },
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
        else if (const auto* const edge { message.get<edge_swipe_intent>() }; edge != nullptr)
        {
            last_swipe_ = edge->swipe;
            // 왼쪽으로 쓸면 다음 쪽, 오른쪽이면 앞 쪽이다. 넘긴 쪽은 맞춤으로 연다.
            const int next { std::clamp(page_ - edge->swipe.horizontal, 1, page_count) };
            if (next != page_)
            {
                page_ = next;
                state_ = luil::zoom_to_fit(config(), viewport_);
            }
        }
        else
            return false;
        return true;
    }

    std::unique_ptr<luil::ui_element> zoom_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(scale);
        const float header { 2.0f * note_line_height + body_line_height + controls_height + 4.0f * row_spacing };
        viewport_ = { std::max(0.0f, width - 2.0f * page_padding), std::max(0.0f, height - (2.0f * page_padding + header)) };
        if (fitted_ == false && viewport_.x > 0.0f && viewport_.y > 0.0f)
        {
            fitted_ = true;
            state_ = luil::zoom_to_fit(config(), viewport_);
        }
        state_ = luil::zoom_about(config(), viewport_, 1.0f, {});
        const luil::stack_config column_config { .spacing = row_spacing, .padding = luil::edge_insets::all(page_padding) };
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"zoom-page" }, column_config) };
        column->add(make_label({ kind_text, u8"zoom-help" }, u8"두 번 탭·두 손가락으로 키우고, 끝에서 쓸면 넘긴다.", note_text_size, luil::label_color_role::dim), note_line_height);
        column->add(make_label({ kind_text, u8"zoom-page-number" }, u8"쪽 " + to_u8(page_) + u8" / " + to_u8(page_count), body_text_size, luil::label_color_role::primary), body_line_height);
        column->add(
            make_label({ kind_text, u8"zoom-edge-swipe" }, last_swipe_.has_value() ? describe(*last_swipe_) : std::u8string { u8"경계 쓸기: 아직 없음" }, note_text_size, luil::label_color_role::dim),
            note_line_height);
        column->add(std::make_unique<luil::zoom_controls_element>(luil::zoom_controls_config { .view = config(), .viewport = viewport_ }), controls_height);
        auto view { std::make_unique<luil::zoom_view_element>(config()) };
        view->set_access_name(u8"도면 확대 보기");
        view->set_content(std::make_unique<diagram>(page_));
        column->add_flexible(std::move(view));
        return column;
    }
}
