#include "luil/ui/page_transition.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace luil {
    namespace {
        // 0~1 진행에 쓰는 스프링 허용치의 상한이다 (결정 6). `spring.h`가 0~1 값에 권하는 값이다.
        constexpr float progress_rest_threshold { 0.001f };

        // 진행 0에서 1로 가는 스프링이다. 허용치는 앱이 준 값과 상한 중 작은 쪽이다.
        [[nodiscard]] spring progress_spring(const page_transition_spring& curve, const std::chrono::steady_clock::time_point started) noexcept
        {
            spring_config config { curve.config };
            const float given { config.rest_threshold };
            const bool valid { std::isfinite(given) && given > 0.0f };
            config.rest_threshold = valid ? std::min(given, progress_rest_threshold) : progress_rest_threshold;
            return spring { 0.0f, 1.0f, started, config, curve.velocity };
        }

        // 곡선이 움직이는가다. 길이 0 이하와 쓸 수 없는 스프링은 시작한 적도 없이 끝난 것이다.
        [[nodiscard]] bool moves(const page_transition_curve& curve, const std::chrono::steady_clock::time_point started) noexcept
        {
            if (const auto* const timing { std::get_if<page_transition_timing>(&curve) }; timing != nullptr)
                return timing->duration > std::chrono::milliseconds { 0 };
            const auto& motion { std::get<page_transition_spring>(curve) };
            return spring_finished(progress_spring(motion, started), started) == false;
        }
    }

    page_transition start_page_transition(
        std::shared_ptr<const ui_tree> prior, const page_transition_style& style, const std::chrono::steady_clock::time_point now, const bool reduce_motion)
    {
        if (reduce_motion || moves(style.curve, now) == false)
            prior.reset();
        return page_transition { std::move(prior), now, style };
    }

    std::optional<std::chrono::steady_clock::time_point> page_transition_end(const page_transition& value) noexcept
    {
        if (value.prior == nullptr)
            return std::nullopt;
        if (const auto* const timing { std::get_if<page_transition_timing>(&value.style.curve) }; timing != nullptr)
            return value.started + std::max(timing->duration, std::chrono::milliseconds { 0 });
        return spring_rest_time(progress_spring(std::get<page_transition_spring>(value.style.curve), value.started));
    }

    bool page_transition_finished(const page_transition& value, const std::chrono::steady_clock::time_point now) noexcept
    {
        const std::optional<std::chrono::steady_clock::time_point> end { page_transition_end(value) };
        return end.has_value() == false || now >= *end;
    }

    float page_transition_progress(const page_transition& value, const std::chrono::steady_clock::time_point now) noexcept
    {
        if (page_transition_finished(value, now))
            return 1.0f;
        if (now <= value.started)
            return 0.0f;
        if (const auto* const timing { std::get_if<page_transition_timing>(&value.style.curve) }; timing != nullptr)
        {
            const std::chrono::duration<float> elapsed { now - value.started };
            const std::chrono::duration<float> length { timing->duration };
            const float t { std::clamp(elapsed.count() / length.count(), 0.0f, 1.0f) };
            const page_transition_easing easing { timing->easing != nullptr ? timing->easing : ease_in_out };
            const float eased { easing(t) };
            return std::isfinite(eased) ? std::clamp(eased, 0.0f, 1.0f) : 1.0f;
        }
        const float position { spring_value(progress_spring(std::get<page_transition_spring>(value.style.curve), value.started), now) };
        return std::isfinite(position) ? std::clamp(position, 0.0f, 1.0f) : 1.0f;
    }

    std::optional<std::chrono::steady_clock::time_point> page_transition_next_update(const page_transition& value, const std::chrono::steady_clock::time_point now) noexcept
    {
        if (page_transition_finished(value, now))
            return std::nullopt;
        if (now < value.started)
            return value.started;
        return now + transition_frame;
    }
}
