#include "luil/ui/slider_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace luil {
    slider_element::slider_element(const ui_element_id id, slider_config config)
        : ui_element { id }
        , config_ { std::move(config) }
    {
        // 지점은 그리기와 이웃 간격 계산이 함께 쓰므로 한 번 다듬어 둔다.
        std::vector<float>& magnets { config_.magnets };
        if (config_.maximum > config_.minimum)
            std::erase_if(magnets, [this](const float magnet) { return (magnet >= config_.minimum && magnet <= config_.maximum) == false; });
        else
            magnets.clear();
        std::ranges::sort(magnets);
        const auto repeated { std::ranges::unique(magnets) };
        magnets.erase(repeated.begin(), repeated.end());

        if (config_.change == nullptr)
            return;

        set_cursor(ui_cursor::resize_horizontal);
        pointer_drag_target target {};
        // 누른 자리로 손잡이가 곧장 간다.
        // 트랙 아무 데나 눌러 값을 정할 수 있고, 이어지는 끌기도 같은 함수를 따르므로
        // 손잡이가 손 밑에 붙어 따라온다.
        target.on_press = [this](const ui_action_context& context) -> std::vector<input_action> {
            // 붙은 지점은 자리를 구하면서 채워지므로 자리를 먼저 받는다 — 한 호출의 인자로
            // 함께 넘기면 계산 순서가 정해져 있지 않아 빈 값이 넘어갈 수 있다.
            std::optional<std::size_t> magnet {};
            const float target_offset { knob_offset_for(context.x, context.control, &magnet) };
            return move_knob(knob_center() - bounds().x - slider_knob_size * scale_ / 2.0f, target_offset, magnet);
        };
        // 앞·뒤 포인터 자리의 손잡이 차이만 메시지로 바꾸므로 tree가 다시 빌드되어도
        // 끌기가 이어진다. 지금 값을 읽지 않는다.
        target.on_move = [this](const ui_action_context& previous, const ui_action_context& current) -> std::vector<input_action> {
            std::optional<std::size_t> magnet {};
            const float from { knob_offset_for(previous.x, previous.control) };
            const float target_offset { knob_offset_for(current.x, current.control, &magnet) };
            return move_knob(from, target_offset, magnet);
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

    float slider_element::magnet_offset(const std::size_t index) const noexcept
    {
        return (config_.magnets[index] - config_.minimum) / (config_.maximum - config_.minimum) * travel();
    }

    float slider_element::knob_offset_for(const float x, const bool free, std::optional<std::size_t>* const magnet) const noexcept
    {
        if (magnet != nullptr)
            magnet->reset();
        const float distance { travel() };
        if (distance <= 0.0f)
            return 0.0f;
        const float pointer { std::clamp(x - bounds().x - slider_knob_size * scale_ / 2.0f, 0.0f, distance) };
        if (free)
            return pointer;

        const std::vector<float>& magnets { config_.magnets };
        for (std::size_t index { 0 }; index < magnets.size(); ++index)
        {
            const float point { magnet_offset(index) };
            const float left { index > 0 ? point - magnet_offset(index - 1) : std::numeric_limits<float>::infinity() };
            const float right { index + 1 < magnets.size() ? magnet_offset(index + 1) - point : std::numeric_limits<float>::infinity() };
            const float reach { std::min(left, right) / 2.0f };
            const float stick { std::min(slider_magnet_stick_distance * scale_, reach) };
            const float release { std::min(slider_magnet_release_distance * scale_, reach) };
            const float offset { pointer - point };
            const float away { std::abs(offset) };
            if (away <= stick)
            {
                if (magnet != nullptr)
                    *magnet = index;
                return point;
            }
            // 붙는 거리에서 지점, 풀리는 거리에서 포인터에 닿는 직선이다.
            // 양 끝이 앞뒤 구간과 이어져 손잡이가 튀지 않는다.
            if (away < release)
                return point + std::copysign((away - stick) * release / (release - stick), offset);
        }
        return pointer;
    }

    std::vector<input_action> slider_element::move_knob(const float from, const float target, const std::optional<std::size_t> magnet) const
    {
        if (target == from)
            return {};
        // 델타를 이어 더한 값은 부동소수 오차로 지점에서 조금 비켜 선다.
        // 절대 메시지는 같은 것이 몇 번 겹쳐도 지점 그대로이고, 이어지는 델타가
        // 지점에서 다시 출발하므로 "손잡이 = 포인터 자리의 함수"도 어긋나지 않는다.
        if (magnet.has_value() && config_.change_to != nullptr)
            return { config_.change_to(config_.magnets[*magnet]) };
        const float delta { value_delta_for(target - from) };
        if (delta == 0.0f)
            return {};
        return { config_.change(delta) };
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

        // 지점 표식은 빈 쪽에서 `control_border`, 채운 쪽에서는 면 색으로 판 홈이다.
        // 채운 `accent` 위에 전경 알파를 얹으면 light 테마에서 대비가 무너진다.
        const float knob { slider_knob_size * scale_ };
        const float mark_width { slider_magnet_mark_width * scale_ };
        const float mark_height { slider_magnet_mark_height * scale_ };
        const float range { config_.maximum - config_.minimum };
        const float value { clamp_value(config_) };
        bool snapped { false };
        for (std::size_t index { 0 }; index < config_.magnets.size(); ++index)
        {
            const float mark { box.x + knob / 2.0f + magnet_offset(index) };
            ui_color color { mark < center ? context.palette.surface_background : context.palette.control_border };
            if (enabled() == false)
                color = context.palette.disabled_foreground;
            context.canvas.drawRRect(
                SkRRect::MakeRectXY(SkRect::MakeXYWH(mark - mark_width / 2.0f, box.y + (box.height - mark_height) / 2.0f, mark_width, mark_height), mark_width / 2.0f, mark_width / 2.0f),
                solid_paint(color));
            // `change_to`가 없으면 델타 합이라 지점에서 오차만큼 비켜 설 수 있다.
            if (std::abs(value - config_.magnets[index]) <= range * 1.0e-5f)
                snapped = true;
        }

        // 끄는 동안 지점에 붙었으면 손잡이 뒤에 고리를 둔다 — 색만이 아니라 모양이 바뀐다.
        //  - 끌지 않을 때는 그리지 않는다. 키로 지점에 닿았을 때까지 고리가 서면
        //    "붙었다"와 "그 값이다"가 섞인다.
        if (snapped && enabled() && interaction.pressed == id())
        {
            const float ring { slider_magnet_ring_size * scale_ };
            context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(center - ring / 2.0f, box.y + (box.height - ring) / 2.0f, ring, ring), ring / 2.0f, ring / 2.0f),
                solid_paint(context.palette.soft_button_background));
        }

        // 손잡이는 중심이 양 끝에 닿는다 — 그것이 값이 도는 거리를 정한다.
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
}
