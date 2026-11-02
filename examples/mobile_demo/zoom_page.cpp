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
        // 도면 한 쪽의 경계다 (내용 좌표, 가운데가 원점). 돌리면 앱이 이것의 `rotated_bounds`를 보기에 준다.
        const luil::rect_f page_bounds { -500.0f, -350.0f, 1000.0f, 700.0f };

        // 노드의 글자는 선 채 자리만 돌고 격자는 돌려 그린다 (rotate-gesture-design.md의 결정 1, 길 a).
        class diagram final : public luil::ui_element
        {
        public:
            // 쪽마다 노드 이름이 달라 넘긴 것이 보인다.
            diagram(const int page, const float rotation)
                : ui_element { { kind_layout, u8"diagram" } }
                , rotation_ { rotation }
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
                // slot은 돌린 경계의 자리다. 내용 원점의 화면 자리는 거기서 돌린 경계의 왼쪽 위를 뺀 곳이다.
                const luil::rect_f turned { luil::rotated_bounds(page_bounds, rotation_) };
                origin_ = { context.slot.x - turned.x * scale_, context.slot.y - turned.y * scale_ };
                int index { 0 };
                for (const auto& child : children())
                {
                    // 가운데만 돌리고 글자는 세운 채 둔다. 회전한 자리의 축 정렬 slot이라 hit test와 접근성 bounds가 그대로 맞는다.
                    const luil::zoom_point center { luil::rotate_point({ -240.0f + index * 160.0f, index % 2 == 0 ? -70.0f : 60.0f }, rotation_) };
                    child->arrange(context.for_child({ origin_.x + center.x * scale_ - 60.0f * scale_, origin_.y + center.y * scale_ - 30.0f * scale_, 120.0f * scale_, 60.0f * scale_ }));
                    ++index;
                }
            }
            void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
            {
                SkPaint paint {};
                paint.setColor(context.palette.divider);
                paint.setStrokeWidth(scale_);
                // 격자만 돌려 그린다: 원점을 옮기고 돌린 캔버스 위에 내용 좌표대로 긋는다.
                context.canvas.save();
                context.canvas.translate(origin_.x, origin_.y);
                context.canvas.rotate(rotation_);
                const float right { page_bounds.x + page_bounds.width };
                const float bottom { page_bounds.y + page_bounds.height };
                for (float x { page_bounds.x }; x <= right; x += 50.0f)
                    context.canvas.drawLine(x * scale_, page_bounds.y * scale_, x * scale_, bottom * scale_, paint);
                for (float y { page_bounds.y }; y <= bottom; y += 50.0f)
                    context.canvas.drawLine(page_bounds.x * scale_, y * scale_, right * scale_, y * scale_, paint);
                context.canvas.restore();
                draw_children(context, interaction);
            }

        private:
            float rotation_ { 0.0f };
            float scale_ { 1.0f };
            luil::zoom_point origin_ {};
        };

        constexpr int page_count { 5 };
        // 쪽 머리의 줄 셋(설명·쪽·쓸기)과 단추 줄 셋(확대·회전·줌 단추 글)이다 (논리 픽셀).
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
            .content_bounds = luil::rotated_bounds(page_bounds, state_.rotation),
            .zoom_by = [](const float factor, const luil::zoom_point anchor) { return luil::make_app_action(zoom_intent { factor, anchor }); },
            .pan_by = [](const luil::zoom_point delta) { return luil::make_app_action(pan_intent { delta }); },
            .zoom_to = [](const float value) { return luil::make_app_action(zoom_to_intent { value }); },
            // 쪽 보기라 맞춤 상태에서는 옮길 여지가 없다. 그래서 쓸기가 전부 경계에 막혀 쪽 넘기기가 된다.
            .bounds = luil::zoom_bounds::contain,
            .edge_swipe = [](const luil::zoom_edge_swipe& swipe) { return luil::make_app_action(edge_swipe_intent { swipe }); },
            // 도면에는 탭 동작이 없어 두 번 탭을 확대에 쓴다.
            .double_tap = { .enabled = true },
            .rotate_by = [](const float degrees, const luil::zoom_point anchor) { return luil::make_app_action(rotate_intent { degrees, anchor }); },
            .rotation = state_.rotation,
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
        else if (const auto* const rotate { message.get<rotate_intent>() }; rotate != nullptr)
            state_ = luil::rotate_about(config(), viewport_, rotate->degrees, rotate->anchor);
        else if (const auto* const labels { message.get<zoom_labels_intent>() }; labels != nullptr)
            english_labels_ = labels->english;
        else if (const auto* const edge { message.get<edge_swipe_intent>() }; edge != nullptr)
        {
            last_swipe_ = edge->swipe;
            // 왼쪽으로 쓸면 다음 쪽, 오른쪽이면 앞 쪽이다. 넘긴 쪽은 곧게 세워 맞춤으로 연다.
            const int next { std::clamp(page_ - edge->swipe.horizontal, 1, page_count) };
            if (next != page_)
            {
                page_ = next;
                state_.rotation = 0.0f;
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
        const float header { 2.0f * note_line_height + body_line_height + 3.0f * controls_height + 6.0f * row_spacing };
        viewport_ = { std::max(0.0f, width - 2.0f * page_padding), std::max(0.0f, height - (2.0f * page_padding + header)) };
        if (fitted_ == false && viewport_.x > 0.0f && viewport_.y > 0.0f)
        {
            fitted_ = true;
            state_ = luil::zoom_to_fit(config(), viewport_);
        }
        state_ = luil::zoom_about(config(), viewport_, 1.0f, {});
        const luil::stack_config column_config { .spacing = row_spacing, .padding = luil::edge_insets::all(page_padding) };
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"zoom-page" }, column_config) };
        column->add(make_label({ kind_text, u8"zoom-help" }, u8"두 번 탭·두 손가락: 키우고 돌린다 / 끝에서 쓸면 쪽 넘김", note_text_size, luil::label_color_role::dim), note_line_height);
        column->add(make_label({ kind_text, u8"zoom-page-number" }, u8"쪽 " + to_u8(page_) + u8" / " + to_u8(page_count), body_text_size, luil::label_color_role::primary), body_line_height);
        column->add(
            make_label({ kind_text, u8"zoom-edge-swipe" }, last_swipe_.has_value() ? describe(*last_swipe_) : std::u8string { u8"경계 쓸기: 아직 없음" }, note_text_size, luil::label_color_role::dim),
            note_line_height);
        // 영어 앱은 세 칸을 채운다. 비워 두면 단추 이름이 기본 문구(축소·확대·맞춤)다.
        luil::zoom_controls_config zoom_buttons { .view = config(), .viewport = viewport_ };
        if (english_labels_)
        {
            zoom_buttons.decrease_label = u8"Zoom out";
            zoom_buttons.increase_label = u8"Zoom in";
            zoom_buttons.fit_label = u8"Fit";
        }

        column->add(std::make_unique<luil::zoom_controls_element>(std::move(zoom_buttons)), controls_height);
        // 회전 단추 셋과 지금 각도다. 단추는 키(Shift+←/→/Home)와 같은 `rotate_by` 메시지를 보내 UIA가 Invoke로 되풀이한다.
        //  - 0도 복귀는 지금 각도를 되돌리는 상대 각도다. 이미 0도면 할 일이 없으므로 단추를 끈다. 켜 둔 채 빈 동작을 내면
        //    보조 기술의 Invoke가 오류로 끝난다.
        luil::stack_config rotation_controls_config {};
        rotation_controls_config.direction = luil::stack_direction::row;
        rotation_controls_config.spacing = row_spacing;
        auto rotation_controls { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"zoom-rotation-controls" }, rotation_controls_config) };
        const luil::zoom_view_config settings { config() };
        const auto rotate_button = [&settings](std::u8string owner, std::u8string text, const float degrees) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, std::move(owner) }, luil::text_button_config { .text = std::move(text) }) };
            button->set_action(luil::ui_trigger::left_click, [view = settings, degrees](const luil::ui_action_context&) {
                return std::vector<luil::input_action> { view.rotate_by(degrees, {}) };
            });
            button->set_enabled(degrees != 0.0f);
            return button;
        };
        rotation_controls->add(rotate_button(u8"zoom-rotate-left", u8"-15°", -settings.rotation_step), 64.0f);
        rotation_controls->add(rotate_button(u8"zoom-rotate-right", u8"+15°", settings.rotation_step), 64.0f);
        rotation_controls->add(rotate_button(u8"zoom-rotate-reset", u8"0°", -state_.rotation), 56.0f);
        rotation_controls->add_flexible(
            make_label({ kind_text, u8"zoom-rotation" }, u8"회전 " + to_u8(static_cast<int>(std::lround(state_.rotation))) + u8"°", body_text_size, luil::label_color_role::primary));
        column->add(std::move(rotation_controls), controls_height);
        // 줌 단추 글의 언어를 바꾸는 단추와 지금 언어다. 누르면 반대 언어를 절대 값으로 보낸다.
        luil::stack_config language_controls_config {};
        language_controls_config.direction = luil::stack_direction::row;
        language_controls_config.spacing = row_spacing;
        auto language_controls { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"zoom-label-controls" }, language_controls_config) };
        auto language_toggle { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"zoom-label-toggle" }, luil::text_button_config { .text = u8"줌 단추 글 바꾸기" }) };
        language_toggle->set_action(luil::ui_trigger::left_click, luil::make_message_action(zoom_labels_intent { english_labels_ == false }));
        language_controls->add(std::move(language_toggle), 148.0f);
        language_controls->add_flexible(
            make_label({ kind_text, u8"zoom-label-language" }, english_labels_ ? u8"줌 단추 글: English" : u8"줌 단추 글: 기본", body_text_size, luil::label_color_role::primary));
        column->add(std::move(language_controls), controls_height);
        auto view { std::make_unique<luil::zoom_view_element>(config()) };
        view->set_access_name(u8"도면 확대 보기");
        view->set_content(std::make_unique<diagram>(page_, state_.rotation));
        column->add_flexible(std::move(view));
        return column;
    }
}
