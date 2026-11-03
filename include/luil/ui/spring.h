#pragma once

#include "luil/ui/transition.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace luil {
    // 스프링의 성질이다. 값의 단위와 무관하다 (질량은 1로 둔다).
    struct spring_config
    {
        // 감쇠가 없을 때 한 번 진동하는 시간이다. 짧을수록 빠르다. 0 이하면 전환이 없다.
        std::chrono::milliseconds response { 350 };
        // 감쇠 비율이다. 1이면 넘치지 않고 가장 빨리 선다. 작을수록 목표를 넘었다 돌아온다. 0 이하면 전환이 없다.
        float damping_fraction { 1.0f };
        // 이만큼 가까워지면 끝이다 (값의 단위, 값과 속도를 합친 척도다). 0 이하면 기본값으로 본다.
        //  - 기본 0.01은 논리 픽셀 값에 맞췄다. 불투명도나 배율처럼 0~1 쪽 값은 0.001쯤으로 낮춘다.
        float rest_threshold { 0.01f };
    };

    // 넘치지 않고 가장 빨리 선다.
    inline constexpr spring_config spring_smooth { std::chrono::milliseconds { 350 }, 1.0f };
    // 살짝 넘었다 돌아온다.
    inline constexpr spring_config spring_snappy { std::chrono::milliseconds { 300 }, 0.75f };
    // 눈에 띄게 튕긴다.
    inline constexpr spring_config spring_bouncy { std::chrono::milliseconds { 500 }, 0.5f };

    // 값 하나가 목표로 가는 중이다. 앱이 들고, 지금 값을 물어 element에 넘긴다 (`transition`과 같다).
    //  - 어느 시각에 물어도 같은 값이 나오는 시각만의 함수다 (닫힌 해라서 단계 적분의 상태가 없다).
    //  - 끝은 길이가 아니라 허용치로 정해진다 (`spring_config::rest_threshold`).
    struct spring
    {
        float from { 0.0f };
        float to { 0.0f };
        // 시작 시각이다. `now`가 이보다 이르면 아직 `from`이다.
        std::chrono::steady_clock::time_point started {};
        spring_config config {};
        // 출발할 때의 속도다 (값의 단위 / 초, 값이 커지는 쪽이 양수).
        float velocity { 0.0f };
    };

    // 정지한 스프링이다. 언제 물어도 `value`이고 끝나 있다.
    [[nodiscard]] constexpr spring spring_at_rest(const float value) noexcept
    {
        return spring { value, value, {}, {}, 0.0f };
    }

    namespace spring_detail {
        // 감쇠 비율이 1에 이만큼 가까우면 임계해를 쓴다.
        // 두 쪽 식이 나눗셈에서 불안정해지는 띠이고, 띠 안의 오차는 띠의 폭과 같은 비율이다.
        inline constexpr double critical_band { 1.0e-6 };
        inline constexpr float default_rest_threshold { 0.01f };
        // `spring_from_physics`가 밀리초로 바꿀 때 정수 변환이 넘치지 않게 자르는 상한이다.
        inline constexpr double longest_response_milliseconds { 1.0e9 };

        // 어느 시각의 값과 속도다.
        struct sample
        {
            double value { 0.0 };
            double velocity { 0.0 };
            bool finished { false };
        };

        // 계산할 수 있는 스프링인가. 아니면 전환이 없는 것으로 본다.
        [[nodiscard]] inline bool usable(const spring& value) noexcept
        {
            return std::isfinite(value.from)
                && std::isfinite(value.to)
                && std::isfinite(value.velocity)
                && value.config.response > std::chrono::milliseconds { 0 }
                && std::isfinite(value.config.damping_fraction)
                && value.config.damping_fraction > 0.0f;
        }

        // 전환이 없는 스프링이 내놓는 값이다. 목표가 유한하지 않으면 from, 그것도 아니면 0이다. NaN은 내지 않는다.
        [[nodiscard]] inline float settled_value(const spring& value) noexcept
        {
            if (std::isfinite(value.to))
                return value.to;
            return std::isfinite(value.from) ? value.from : 0.0f;
        }

        [[nodiscard]] inline double elapsed_seconds(const spring& value, const std::chrono::steady_clock::time_point now) noexcept
        {
            if (now <= value.started)
                return 0.0;
            return std::chrono::duration<double>(now - value.started).count();
        }

        // `usable`인 스프링의 값과 속도를 닫힌 해로 구한다. 목표와의 차 y가 y'' + 2ζω·y' + ω²·y = 0을 따른다.
        [[nodiscard]] inline sample evaluate(const spring& value, const double seconds) noexcept
        {
            const double omega { 2.0 * std::numbers::pi / (static_cast<double>(value.config.response.count()) / 1000.0) };
            const double zeta { value.config.damping_fraction };
            const double y0 { static_cast<double>(value.from) - static_cast<double>(value.to) };
            const double v0 { value.velocity };
            double y { 0.0 };
            double dy { 0.0 };
            if (std::abs(zeta - 1.0) < critical_band)
            {
                const double decay { std::exp(-omega * seconds) };
                const double slope { v0 + omega * y0 };
                y = decay * (y0 + slope * seconds);
                dy = decay * (v0 - omega * slope * seconds);
            }
            else if (zeta < 1.0)
            {
                const double rate { zeta * omega };
                const double damped { omega * std::sqrt((1.0 - zeta) * (1.0 + zeta)) };
                const double sine_part { (v0 + rate * y0) / damped };
                const double decay { std::exp(-rate * seconds) };
                const double cosine { std::cos(damped * seconds) };
                const double sine { std::sin(damped * seconds) };
                y = decay * (y0 * cosine + sine_part * sine);
                dy = decay * ((sine_part * damped - rate * y0) * cosine - (y0 * damped + rate * sine_part) * sine);
            }
            else
            {
                // 느린 뿌리는 큰 ζ에서 두 수의 차가 0으로 뭉개지지 않게 곱의 역수(1 / (ζ + q))로 구한다.
                const double root { std::sqrt((zeta - 1.0) * (zeta + 1.0)) };
                const double fast { -omega * (zeta + root) };
                const double slow { -omega / (zeta + root) };
                const double fast_weight { (v0 - slow * y0) / (fast - slow) };
                const double slow_weight { y0 - fast_weight };
                const double fast_decay { std::exp(fast * seconds) };
                const double slow_decay { std::exp(slow * seconds) };
                y = slow_weight * slow_decay + fast_weight * fast_decay;
                dy = slow_weight * slow * slow_decay + fast_weight * fast * fast_decay;
            }

            const float configured { value.config.rest_threshold };
            const double threshold { std::isfinite(configured) && configured > 0.0f ? configured : default_rest_threshold };
            // 값과 속도를 한 척도로 합친다. 에너지에 비례하고 감쇠로는 줄기만 해서, 한 번 끝난 스프링은 다시 움직이지 않는다.
            const double scale { std::hypot(y, dy / omega) };
            return sample { static_cast<double>(value.to) + y, dy, scale <= threshold };
        }
    }

    // `now` 시점의 값이다. 끝났으면 정확히 `to`다.
    [[nodiscard]] inline float spring_value(const spring& value, const std::chrono::steady_clock::time_point now) noexcept
    {
        if (spring_detail::usable(value) == false)
            return spring_detail::settled_value(value);
        const spring_detail::sample at { spring_detail::evaluate(value, spring_detail::elapsed_seconds(value, now)) };
        return at.finished ? value.to : static_cast<float>(at.value);
    }

    // 값의 시간 미분이다 (값의 단위 / 초). 끝났으면 0이다.
    [[nodiscard]] inline float spring_velocity(const spring& value, const std::chrono::steady_clock::time_point now) noexcept
    {
        if (spring_detail::usable(value) == false)
            return 0.0f;
        const spring_detail::sample at { spring_detail::evaluate(value, spring_detail::elapsed_seconds(value, now)) };
        return at.finished ? 0.0f : static_cast<float>(at.velocity);
    }

    // 끝났는가. 한 번 끝난 스프링은 다시 움직이지 않는다.
    // 잘못된 값이면 시작한 적도 없이 끝난 것이다 (`transition_finished`의 길이 0과 같다).
    [[nodiscard]] inline bool spring_finished(const spring& value, const std::chrono::steady_clock::time_point now) noexcept
    {
        if (spring_detail::usable(value) == false)
            return true;
        return spring_detail::evaluate(value, spring_detail::elapsed_seconds(value, now)).finished;
    }

    // `transition_next_update`와 같은 계약이다. 시작 전이면 시작 시각, 도는 중이면 `transition_frame` 뒤, 끝났으면 nullopt.
    [[nodiscard]] inline std::optional<std::chrono::steady_clock::time_point> spring_next_update(const spring& value, const std::chrono::steady_clock::time_point now) noexcept
    {
        if (spring_finished(value, now))
            return std::nullopt;
        if (now < value.started)
            return value.started;
        return now + transition_frame;
    }

    // 도는 중이면 지금 값과 속도에서 `target`으로 이어 출발한다.
    // 이어받는 속도를 쓰므로 속도 인자가 없다. 같은 목표로 매 frame 불러도 궤적이 같다.
    // `reduce_motion`(`window_metrics::reduce_motion`)이 참이면 목표에 정지한 스프링이다.
    [[nodiscard]] inline spring spring_to(const spring& value, const float target, const std::chrono::steady_clock::time_point now, const spring_config& config, const bool reduce_motion) noexcept
    {
        if (reduce_motion)
            return spring_at_rest(target);
        return spring { spring_value(value, now), target, now, config, spring_velocity(value, now) };
    }

    // 손을 뗀 값과 속도에서 `target`으로 출발한다 (`velocity`는 값의 단위 / 초, 값이 커지는 쪽이 양수).
    // 이어받는 속도를 실수로 덮지 않도록 `spring_to`와 따로 둔다.
    // `reduce_motion`이 참이면 놓은 속도도 쓰지 않고 목표에 정지한 스프링이다.
    [[nodiscard]] inline spring spring_release(
        const float value, const float velocity, const float target, const std::chrono::steady_clock::time_point now, const spring_config& config, const bool reduce_motion) noexcept
    {
        if (reduce_motion)
            return spring_at_rest(target);
        return spring { value, target, now, config, velocity };
    }

    // (질량, 강성, 감쇠)와 같은 운동의 config다. 하나라도 0 이하이거나 유한하지 않으면 전환 없는 config다.
    //  - 질량은 운동을 바꾸지 않고 시간 척도와 감쇠로 흡수된다.
    //  - `response`는 밀리초로 반올림한다 (1ms보다 짧아지지 않는다).
    [[nodiscard]] inline spring_config spring_from_physics(const float mass, const float stiffness, const float damping, const float rest_threshold = spring_detail::default_rest_threshold) noexcept
    {
        const bool usable { std::isfinite(mass) && std::isfinite(stiffness) && std::isfinite(damping) && mass > 0.0f && stiffness > 0.0f && damping > 0.0f };
        if (usable == false)
            return spring_config { std::chrono::milliseconds { 0 }, 0.0f, rest_threshold };
        const double m { mass };
        const double k { stiffness };
        const double c { damping };
        const double seconds { 2.0 * std::numbers::pi * std::sqrt(m / k) };
        const double milliseconds { std::clamp(std::round(seconds * 1000.0), 1.0, spring_detail::longest_response_milliseconds) };
        const double fraction { std::min(c / (2.0 * std::sqrt(k * m)), static_cast<double>(std::numeric_limits<float>::max())) };
        return spring_config { std::chrono::milliseconds { static_cast<std::chrono::milliseconds::rep>(milliseconds) }, static_cast<float>(fraction), rest_threshold };
    }

    // 처음 끝난 시각이다. `started`부터 `transition_frame` 간격으로 재어 `spring_finished`가 처음 참인 시각을 답한다.
    //  - 앱이 끝을 시각 하나로 예고하려고 쓴다 (`next_tick`). 끝 판정의 척도는 줄기만 하므로 처음 끝난 frame이 하나로 정해진다.
    //  - `limit` 안에 끝나지 않으면 `started + limit`이다. 기본 10초면 평가는 많아야 625번이다.
    //  - 잘못된 값이면 시작한 적도 없이 끝난 것이라 `started`다.
    [[nodiscard]] inline std::chrono::steady_clock::time_point spring_rest_time(const spring& value, const std::chrono::milliseconds limit = std::chrono::seconds { 10 }) noexcept
    {
        if (spring_detail::usable(value) == false)
            return value.started;
        const std::chrono::steady_clock::time_point last { value.started + std::max(limit, std::chrono::milliseconds { 0 }) };
        for (std::chrono::steady_clock::time_point at { value.started }; at < last; at += transition_frame)
        {
            if (spring_detail::evaluate(value, spring_detail::elapsed_seconds(value, at)).finished)
                return at;
        }
        return last;
    }
}
