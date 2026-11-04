#include "demo/zoom_page.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/stack_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace demo {
    namespace {
        // 도면 전체의 경계다 (내용 좌표, 가운데가 원점). 돌리면 앱이 이것의 `rotated_bounds`를 보기에 준다.
        const luil::rect_f page_bounds { -500.0f, -350.0f, 1000.0f, 700.0f };
        // 단추 줄의 높이와 줄 사이 간격이다 (논리 픽셀).
        constexpr float controls_height { 34.0f };
        constexpr float row_spacing { 8.0f };

        // 노드의 글자는 선 채 자리만 돌고 격자는 돌려 그린다 (rotate-gesture-design.md의 결정 1, 길 a).
        class diagram final : public luil::ui_element
        {
        public:
            explicit diagram(const float rotation)
                : ui_element { { kind_layout, u8"diagram" } }
                , rotation_ { rotation }
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
        else
            return false;
        return true;
    }

    std::unique_ptr<luil::ui_element> zoom_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(scale);
        // 위아래 여백 32, 설명 28, 단추 줄 둘, 사이 간격 셋이다.
        viewport_ = { std::max(0.0f, width - 32.0f), std::max(0.0f, height - (32.0f + 28.0f + 2.0f * controls_height + 3.0f * row_spacing)) };
        state_ = luil::zoom_about(config(), viewport_, 1.0f, {});
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"zoom-page" }, luil::stack_config { .spacing = 8.0f, .padding = luil::edge_insets::all(16.0f) }) };
        column->add(
            make_label({ kind_text, u8"zoom-help" }, u8"휠·두 손가락: 확대·회전 / 빈 곳 끌기: 이동 / 초점에서 +, -, 0, 화살표, Page, Shift+←→·Home: 회전 / 멈추면 커서가 숨습니다", 12.0f,
                luil::label_color_role::primary),
            28.0f);
        // 축 맞춤 단추 둘이다 (image-frames-scaling-design.md). 계산은 라이브러리의 `zoom_fit_actions`이고 단추 글은 앱이 단다.
        //  - 맞추지 않는 축은 지금 창 가운데에 있는 도면 자리를 지킨다.
        luil::stack_config controls_config {};
        controls_config.direction = luil::stack_direction::row;
        controls_config.spacing = 8.0f;
        auto controls { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"zoom-controls" }, controls_config) };
        // 영어 앱은 세 칸을 채운다. 비워 두면 단추 이름이 기본 문구(축소·확대·맞춤)다.
        // 단추 폭이 56dp로 정해져 있어 "Zoom out"은 잘린다. 모바일 데모와 같이 세 글자 안팎으로 짧게 쓴다.
        luil::zoom_controls_config zoom_buttons { .view = config(), .viewport = viewport_ };
        if (english_labels_)
        {
            zoom_buttons.decrease_label = u8"Out";
            zoom_buttons.increase_label = u8"In";
            zoom_buttons.fit_label = u8"Fit";
        }

        controls->add(std::make_unique<luil::zoom_controls_element>(std::move(zoom_buttons)), 244.0f);
        const auto fit_button = [this](std::u8string owner, std::u8string text, const luil::zoom_fit axis) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, std::move(owner) }, luil::text_button_config { .text = std::move(text) }) };
            button->set_action(
                luil::ui_trigger::left_click, [view = config(), viewport = viewport_, axis](const luil::ui_action_context&) { return luil::zoom_fit_actions(view, viewport, 16.0f, 5.0f, axis); });
            return button;
        };
        controls->add(fit_button(u8"zoom-fit-width", u8"폭 맞춤", luil::zoom_fit::width), 72.0f);
        controls->add(fit_button(u8"zoom-fit-height", u8"높이 맞춤", luil::zoom_fit::height), 80.0f);
        // 줌 단추 글의 언어를 바꾸는 단추와 지금 언어다. 누르면 반대 언어를 절대 값으로 보낸다.
        auto language_toggle { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"zoom-label-toggle" }, luil::text_button_config { .text = u8"줌 단추 글 바꾸기" }) };
        language_toggle->set_action(luil::ui_trigger::left_click, luil::make_message_action(zoom_labels_intent { english_labels_ == false }));
        controls->add(std::move(language_toggle), 136.0f);
        controls->add(make_label({ kind_text, u8"zoom-label-language" }, english_labels_ ? u8"줌 단추 글: English" : u8"줌 단추 글: 기본", 13.0f, luil::label_color_role::primary), 160.0f);
        column->add(std::move(controls), controls_height);
        // 회전 단추 셋과 지금 각도다. 단추는 키(Shift+←/→/Home)와 같은 `rotate_by` 메시지를 보내 UIA가 Invoke로 되풀이한다.
        //  - 0도 복귀는 지금 각도를 되돌리는 상대 각도다. 이미 0도면 할 일이 없으므로 단추를 끈다. 켜 둔 채 빈 동작을 내면
        //    보조 기술의 Invoke가 오류로 끝난다.
        luil::stack_config rotation_controls_config {};
        rotation_controls_config.direction = luil::stack_direction::row;
        rotation_controls_config.spacing = 8.0f;
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
        rotation_controls->add(rotate_button(u8"zoom-rotate-left", u8"-15°", -settings.rotation_step), 56.0f);
        rotation_controls->add(rotate_button(u8"zoom-rotate-right", u8"+15°", settings.rotation_step), 56.0f);
        rotation_controls->add(rotate_button(u8"zoom-rotate-reset", u8"0°", -state_.rotation), 48.0f);
        rotation_controls->add(
            make_label({ kind_text, u8"zoom-rotation" }, u8"회전 " + to_u8(static_cast<int>(std::lround(state_.rotation))) + u8"°", 13.0f, luil::label_color_role::primary), 120.0f);
        column->add(std::move(rotation_controls), controls_height);
        auto view { std::make_unique<luil::zoom_view_element>(config()) };
        view->set_access_name(u8"도면 확대 보기");
        // 그림 위에서 포인터가 1.5초 멈추면 커서를 감춘다. 움직이거나 누르면 곧바로 돌아온다.
        view->set_cursor_idle_hide(std::chrono::milliseconds { 1500 });
        view->set_content(std::make_unique<diagram>(state_.rotation));
        column->add_flexible(std::move(view));
        return column;
    }
}
