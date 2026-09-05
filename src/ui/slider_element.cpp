#include "luil/ui/slider_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <optional>
#include <utility>
#include <vector>

namespace luil {
    slider_element::slider_element(const ui_element_id id, slider_config config)
        : ui_element { id }
        , config_ { std::move(config) }
    {
        if (config_.change == nullptr)
            return;

        set_cursor(ui_cursor::resize_horizontal);
        pointer_drag_target target {};
        // 누른 자리로 손잡이가 곧장 간다.
        // 트랙 아무 데나 눌러 값을 정할 수 있고, 이어지는 끌기는 그 자리에서
        // 상대로 흐르므로 손잡이가 손 밑에 붙어 따라온다.
        target.on_press = [this](const ui_action_context& context) -> std::vector<input_action> {
            const float delta { value_delta_for(context.x - knob_center()) };
            if (delta == 0.0f)
                return {};
            return { config_.change(delta) };
        };
        // 좌표 변화량만 메시지로 바꾸므로 tree가 다시 빌드되어도 끌기가 이어진다.
        target.on_move = [this](const ui_action_context& previous, const ui_action_context& current) -> std::vector<input_action> {
            const float delta { value_delta_for(current.x - previous.x) };
            if (delta == 0.0f)
                return {};
            return { config_.change(delta) };
        };
        set_pointer_drag_target(std::move(target));

        // 마우스로 갈 수 있는 자리는 키보드로도 갈 수 있어야 한다.
        // 조작할 수 없는 표시 전용 막대(factory 없음)는 위에서 이미 돌아섰다.
        set_tab_stop(true);
        key_step_target steps {};
        steps.axis = focus_axis::horizontal;
        steps.on_step = [this](const value_step step) -> std::optional<std::vector<input_action>> {
            const float range { config_.maximum - config_.minimum };
            const float current { clamp_value(config_) };
            float delta { 0.0f };
            switch (step)
            {
            case value_step::decrease:
                delta = -range * slider_key_step_ratio;
                break;
            case value_step::increase:
                delta = range * slider_key_step_ratio;
                break;
            case value_step::decrease_page:
                delta = -range * slider_key_page_ratio;
                break;
            case value_step::increase_page:
                delta = range * slider_key_page_ratio;
                break;
            case value_step::minimum:
                delta = config_.minimum - current;
                break;
            case value_step::maximum:
                delta = config_.maximum - current;
                break;
            }
            // 이미 끝에 닿았거나 범위가 없으면 **삼킨다.** 흘려보내면 최소인
            // 막대의 Home이 앱 단축키로 새어 화면이 함께 맨 위로 뛴다
            // (묶음의 Home/End가 같은 이유로 그렇게 한다).
            if (range <= 0.0f || delta == 0.0f)
                return std::vector<input_action> {};
            return std::vector<input_action> { config_.change(delta) };
        };
        set_key_step_target(std::move(steps));
    }

    void slider_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;
    }

    float slider_element::travel() const noexcept
    {
        const float distance { bounds().width - slider_knob_size * scale_ };
        return distance > 0.0f ? distance : 0.0f;
    }

    float slider_element::knob_center() const noexcept
    {
        const float range { config_.maximum - config_.minimum };
        const float ratio { range > 0.0f ? (clamp_value(config_) - config_.minimum) / range : 0.0f };
        return bounds().x + slider_knob_size * scale_ / 2.0f + ratio * travel();
    }

    float slider_element::value_delta_for(const float pixels) const noexcept
    {
        const float distance { travel() };
        if (distance <= 0.0f || config_.maximum <= config_.minimum)
            return 0.0f;
        return pixels / distance * (config_.maximum - config_.minimum);
    }

    void slider_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;

        const float thickness { slider_track_thickness * scale_ };
        const float top { box.y + (box.height - thickness) / 2.0f };
        const float radius { thickness / 2.0f };
        const bool hot { enabled() && (interaction.pressed == id() || interaction.hovered == id()) };
        const ui_color filled { enabled() == false ? context.palette.disabled_foreground : (hot ? context.palette.accent_hover : context.palette.accent) };

        context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, top, box.width, thickness), radius, radius), solid_paint(context.palette.input_background));

        // 채움은 트랙 왼쪽 끝에서 손잡이 중심까지다.
        const float center { knob_center() };
        const float fill_width { center - box.x };
        if (fill_width > 0.0f)
            context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, top, fill_width, thickness), radius, radius), solid_paint(filled));

        // 손잡이는 중심이 양 끝에 닿는다 — 그것이 값이 도는 거리를 정한다.
        const float knob { slider_knob_size * scale_ };
        context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(center - knob / 2.0f, box.y + (box.height - knob) / 2.0f, knob, knob), knob / 2.0f, knob / 2.0f), solid_paint(filled));
    }
    access_info slider_element::accessibility() const
    {
        // 곁에 선 라벨과 잇는 어휘가 없어 이름은 tooltip에서만 나온다
        // (accessibility-design.md의 LabeledBy).
        return { .role = access_role::slider, .name = tooltip(), .range = access_range { config_.minimum, config_.maximum, clamp_value(config_) } };
    }

    std::optional<std::vector<input_action>> slider_element::access_actions(const access_request& request) const
    {
        if (request.command != access_command::set_value)
            return ui_element::access_actions(request);
        // 값 정하기는 **절대 메시지**만 탄다. 델타 환산은 오래된 발행본 기준의
        // 변화량이 겹쳐 쌓여, 다음 frame 전에 같은 목표가 두 번 오면 두 배로
        // 움직인다 (accessibility-action-design.md).
        // 표시 전용 막대(factory 없음)는 값을 받지 않는다.
        if (config_.change_to == nullptr || config_.maximum <= config_.minimum)
            return std::nullopt;
        // 자르기는 그리기와 같은 함수를 쓴다. 식이 두 벌이 되지 않는다.
        const float target { clamp_value(slider_config { .minimum = config_.minimum, .maximum = config_.maximum, .value = request.value }) };
        if (target == clamp_value(config_))
            return std::vector<input_action> {};
        return std::vector<input_action> { config_.change_to(target) };
    }
} // namespace luil
