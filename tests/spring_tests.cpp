#include "luil/ui/spring.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace {
    using time_point = std::chrono::steady_clock::time_point;

    [[nodiscard]] time_point at(const int milliseconds)
    {
        return time_point {} + std::chrono::milliseconds { milliseconds };
    }

    // `start`에서 소수 초가 지난 시각이다.
    [[nodiscard]] time_point after(const time_point start, const double seconds)
    {
        return start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
    }

    [[nodiscard]] double omega_of(const luil::spring_config& config)
    {
        return 2.0 * std::numbers::pi / (static_cast<double>(config.response.count()) / 1000.0);
    }

    // 0에서 100으로 가는 스프링 하나다. 시작은 1000ms이다.
    [[nodiscard]] luil::spring sample_spring(const float damping, const float velocity = 0.0f)
    {
        return luil::spring { 0.0f, 100.0f, at(1000), luil::spring_config { std::chrono::milliseconds { 350 }, damping }, velocity };
    }

    // 목표와의 차 y와 그 속도다.
    struct motion
    {
        double y { 0.0 };
        double v { 0.0 };
    };

    // y'' + 2ζω·y' + ω²·y = 0을 RK4로 한 걸음 적분한다. 닫힌 해와 무관한 기준이다.
    [[nodiscard]] motion rk4_step(const motion state, const double omega, const double damping, const double step)
    {
        const auto derivative = [omega, damping](const motion at_state) { return motion { at_state.v, -2.0 * damping * omega * at_state.v - omega * omega * at_state.y }; };
        const motion k1 { derivative(state) };
        const motion k2 { derivative(motion { state.y + step / 2.0 * k1.y, state.v + step / 2.0 * k1.v }) };
        const motion k3 { derivative(motion { state.y + step / 2.0 * k2.y, state.v + step / 2.0 * k2.v }) };
        const motion k4 { derivative(motion { state.y + step * k3.y, state.v + step * k3.v }) };
        return motion { state.y + step / 6.0 * (k1.y + 2.0 * k2.y + 2.0 * k3.y + k4.y), state.v + step / 6.0 * (k1.v + 2.0 * k2.v + 2.0 * k3.v + k4.v) };
    }

    // 정지에서 100만큼 떨어져 출발해 처음 끝난 밀리초다. 4초 안에 끝나지 않으면 -1이다.
    [[nodiscard]] int first_finished_millisecond(const luil::spring_config& config)
    {
        const luil::spring value { 100.0f, 0.0f, at(0), config, 0.0f };
        for (int milliseconds { 0 }; milliseconds <= 4000; ++milliseconds)
            if (luil::spring_finished(value, at(milliseconds)))
                return milliseconds;
        return -1;
    }
}

TEST_CASE("A spring starts at its value and velocity and settles on its target", "[ui][spring]")
{
    for (const float damping : { 0.4f, 1.0f, 2.5f })
    {
        CAPTURE(damping);
        const luil::spring value { sample_spring(damping, 300.0f) };
        // 시작 시각과 시작 전은 같은 출발이다.
        for (const int milliseconds : { 0, 1000 })
        {
            REQUIRE(luil::spring_value(value, at(milliseconds)) == Catch::Approx(0.0f).margin(1.0e-4));
            REQUIRE(luil::spring_velocity(value, at(milliseconds)) == Catch::Approx(300.0f).margin(1.0e-3));
            REQUIRE(luil::spring_finished(value, at(milliseconds)) == false);
        }
        // 충분히 뒤에는 목표에 서 있다.
        REQUIRE(luil::spring_value(value, at(21000)) == 100.0f);
        REQUIRE(luil::spring_velocity(value, at(21000)) == 0.0f);
        REQUIRE(luil::spring_finished(value, at(21000)));
        REQUIRE(luil::spring_next_update(value, at(21000)).has_value() == false);
    }
}

TEST_CASE("A spring at rest and the presets are constants", "[ui][spring]")
{
    STATIC_REQUIRE(luil::spring_at_rest(7.0f).from == 7.0f);
    STATIC_REQUIRE(luil::spring_at_rest(7.0f).to == 7.0f);
    STATIC_REQUIRE(luil::spring_at_rest(7.0f).velocity == 0.0f);
    STATIC_REQUIRE(luil::spring_smooth.response == std::chrono::milliseconds { 350 });
    STATIC_REQUIRE(luil::spring_smooth.damping_fraction == 1.0f);
    STATIC_REQUIRE(luil::spring_snappy.response == std::chrono::milliseconds { 300 });
    STATIC_REQUIRE(luil::spring_snappy.damping_fraction == 0.75f);
    STATIC_REQUIRE(luil::spring_bouncy.response == std::chrono::milliseconds { 500 });
    STATIC_REQUIRE(luil::spring_bouncy.damping_fraction == 0.5f);

    // 정지한 스프링은 시작 시각이 비어 있어도 언제 물어도 값이고, 예고가 없다.
    const luil::spring rest { luil::spring_at_rest(7.0f) };
    for (const int milliseconds : { 0, 1, 100000 })
    {
        REQUIRE(luil::spring_value(rest, at(milliseconds)) == 7.0f);
        REQUIRE(luil::spring_velocity(rest, at(milliseconds)) == 0.0f);
        REQUIRE(luil::spring_finished(rest, at(milliseconds)));
        REQUIRE(luil::spring_next_update(rest, at(milliseconds)).has_value() == false);
    }
}

TEST_CASE("A spring at or above critical damping never passes its target", "[ui][spring]")
{
    for (const float damping : { 1.0f, 1.5f, 2.5f, 8.0f })
    {
        CAPTURE(damping);
        const luil::spring value { sample_spring(damping) };
        float previous { 0.0f };
        for (int milliseconds { 1000 }; milliseconds <= 4000; ++milliseconds)
        {
            const float now_value { luil::spring_value(value, at(milliseconds)) };
            REQUIRE(now_value <= 100.0f);
            // 정지에서 출발하므로 곧장 다가가기만 한다.
            REQUIRE(now_value >= previous - 1.0e-4f);
            previous = now_value;
        }
    }
}

TEST_CASE("A half damped spring overshoots by the textbook ratio", "[ui][spring]")
{
    const luil::spring value { sample_spring(0.5f) };
    float peak { 0.0f };
    for (int step { 0 }; step <= 15000; ++step)
        peak = std::max(peak, luil::spring_value(value, after(value.started, step * 1.0e-4)));
    // 첫 마루는 이동 거리의 e^(-πζ / √(1-ζ²))배다 (약 16.3%).
    const double ratio { std::exp(-std::numbers::pi * 0.5 / std::sqrt(1.0 - 0.25)) };
    REQUIRE(peak - 100.0f == Catch::Approx(100.0 * ratio).margin(0.05));
    REQUIRE(ratio == Catch::Approx(0.163).margin(0.001));
}

TEST_CASE("The closed form matches a numerical integration in all three regimes", "[ui][spring]")
{
    for (const float damping : { 0.1f, 0.4f, 1.0f, 2.5f, 4.0f })
        for (const float velocity : { 0.0f, 500.0f, -500.0f })
        {
            CAPTURE(damping, velocity);
            const luil::spring value { sample_spring(damping, velocity) };
            const double omega { omega_of(value.config) };
            constexpr double step { 1.0e-4 };
            motion state { -100.0, velocity };
            for (int index { 1 }; index <= 15000; ++index)
            {
                state = rk4_step(state, omega, damping, step);
                if (index % 500 != 0)
                    continue;
                const time_point now { after(value.started, index * step) };
                const double rest { std::hypot(state.y, state.v / omega) };
                // 끝 판정도 같은 척도로 맞는다. 끝나면 값이 목표이고 속도가 0이다.
                if (luil::spring_finished(value, now))
                {
                    REQUIRE(rest <= 0.01 + 1.0e-6);
                    REQUIRE(luil::spring_value(value, now) == 100.0f);
                    REQUIRE(luil::spring_velocity(value, now) == 0.0f);
                    continue;
                }
                REQUIRE(rest >= 0.01 - 1.0e-6);
                REQUIRE(luil::spring_value(value, now) == Catch::Approx(100.0 + state.y).margin(1.0e-3));
                REQUIRE(luil::spring_velocity(value, now) == Catch::Approx(state.v).margin(1.0e-2));
            }
        }
}

TEST_CASE("The velocity is the slope of the value", "[ui][spring]")
{
    constexpr double half_step { 2.0e-4 };
    for (const float damping : { 0.4f, 1.0f, 2.5f })
        for (const float velocity : { 0.0f, 400.0f, -400.0f })
            for (const double seconds : { 0.01, 0.1, 0.25, 0.4 })
            {
                CAPTURE(damping, velocity, seconds);
                const luil::spring value { sample_spring(damping, velocity) };
                const time_point now { after(value.started, seconds) };
                REQUIRE(luil::spring_finished(value, now) == false);
                const double before { luil::spring_value(value, after(value.started, seconds - half_step)) };
                const double later { luil::spring_value(value, after(value.started, seconds + half_step)) };
                REQUIRE((later - before) / (2.0 * half_step) == Catch::Approx(luil::spring_velocity(value, now)).margin(0.2));
            }
}

TEST_CASE("Extreme damping stays finite over a long time", "[ui][spring]")
{
    for (const float damping : { 0.1f, 4.0f, 1000.0f })
        for (const float velocity : { 0.0f, 900.0f })
        {
            CAPTURE(damping, velocity);
            const luil::spring value { sample_spring(damping, velocity) };
            for (const int milliseconds : { 1000, 1010, 1500, 5000, 60000, 1000000 })
            {
                REQUIRE(std::isfinite(luil::spring_value(value, at(milliseconds))));
                REQUIRE(std::isfinite(luil::spring_velocity(value, at(milliseconds))));
            }
        }
    // 감쇠 0.1은 오래 지나면 끝나서 목표에 선다.
    REQUIRE(luil::spring_value(sample_spring(0.1f), at(1000000)) == 100.0f);
}

TEST_CASE("The two solutions agree on both sides of critical damping", "[ui][spring]")
{
    // 띠의 양쪽(1 ± 2e-6)은 각자 식으로, 띠 안(1 ± 5e-7)은 임계해로 풀지만 같은 운동이다.
    const auto move = [](const float damping, const float velocity) { return luil::spring { 0.0f, 10.0f, at(1000), luil::spring_config { std::chrono::milliseconds { 350 }, damping }, velocity }; };
    for (const float velocity : { 0.0f, 80.0f, -80.0f })
        for (const float damping : { 1.0f - 2.0e-6f, 1.0f - 5.0e-7f, 1.0f + 5.0e-7f, 1.0f + 2.0e-6f })
            for (const double seconds : { 0.02, 0.1, 0.2, 0.35, 0.6 })
            {
                CAPTURE(velocity, damping, seconds);
                const luil::spring critical { move(1.0f, velocity) };
                const luil::spring near { move(damping, velocity) };
                const time_point now { after(critical.started, seconds) };
                REQUIRE(luil::spring_value(near, now) == Catch::Approx(luil::spring_value(critical, now)).margin(1.0e-4));
                REQUIRE(luil::spring_velocity(near, now) == Catch::Approx(luil::spring_velocity(critical, now)).margin(5.0e-3));
            }
}

TEST_CASE("Retargeting keeps both the value and the velocity", "[ui][spring]")
{
    for (const float damping : { 0.5f, 1.0f, 2.5f })
        for (const int milliseconds : { 1000, 1030, 1100, 1250 })
        {
            CAPTURE(damping, milliseconds);
            const luil::spring running { sample_spring(damping, 250.0f) };
            const luil::spring moved { luil::spring_to(running, -50.0f, at(milliseconds), luil::spring_snappy, false) };
            REQUIRE(moved.to == -50.0f);
            REQUIRE(moved.started == at(milliseconds));
            REQUIRE(luil::spring_value(moved, at(milliseconds)) == Catch::Approx(luil::spring_value(running, at(milliseconds))).margin(1.0e-3));
            REQUIRE(luil::spring_velocity(moved, at(milliseconds)) == Catch::Approx(luil::spring_velocity(running, at(milliseconds))).margin(1.0e-2));
        }

    // 같은 목표로 매 frame 다시 불러도 한 번에 간 궤적과 같다.
    for (const float damping : { 0.5f, 1.0f, 2.5f })
    {
        CAPTURE(damping);
        const luil::spring_config config { std::chrono::milliseconds { 350 }, damping };
        const luil::spring once { 0.0f, 100.0f, at(1000), config, 0.0f };
        luil::spring again { once };
        for (int frame { 1 }; frame <= 60; ++frame)
            again = luil::spring_to(again, 100.0f, at(1000 + 16 * frame), config, false);
        for (const int extra : { 0, 100, 300, 600, 1500 })
        {
            REQUIRE(luil::spring_value(again, at(1960 + extra)) == Catch::Approx(luil::spring_value(once, at(1960 + extra))).margin(1.0e-3));
        }
    }
}

TEST_CASE("A release starts from the given velocity and ignores the one before it", "[ui][spring]")
{
    const luil::spring running { sample_spring(0.75f, 900.0f) };
    const luil::spring released { luil::spring_release(10.0f, -700.0f, 80.0f, at(1100), luil::spring_snappy, false) };
    REQUIRE(released.from == 10.0f);
    REQUIRE(released.to == 80.0f);
    REQUIRE(released.velocity == -700.0f);
    REQUIRE(luil::spring_value(released, at(1100)) == Catch::Approx(10.0f).margin(1.0e-3));
    REQUIRE(luil::spring_velocity(released, at(1100)) == Catch::Approx(-700.0f).margin(1.0e-2));
    // 처음에는 속도의 방향으로 나아가고, 곧 돌아서 목표에 선다.
    REQUIRE(luil::spring_value(released, at(1110)) < 10.0f);
    REQUIRE(luil::spring_value(released, at(21000)) == 80.0f);
    static_cast<void>(running);

    // 같은 값에서 속도만 주는 놓기(from == to)도 움직인다.
    const luil::spring flick { luil::spring_release(40.0f, 600.0f, 40.0f, at(0), luil::spring_snappy, false) };
    REQUIRE(luil::spring_finished(flick, at(0)) == false);
    REQUIRE(luil::spring_value(flick, at(20)) > 40.0f);
    REQUIRE(luil::spring_finished(flick, at(5000)));
}

TEST_CASE("Ending is decided once and a longer tolerance ends sooner", "[ui][spring]")
{
    struct preset
    {
        luil::spring_config config;
        int earliest;
        int latest;
    };
    // 이동 100, 허용치 0.01에서 끝나기까지의 시간이다 (약 0.67초, 0.61초, 1.5초).
    for (const preset& entry : { preset { luil::spring_smooth, 660, 690 }, preset { luil::spring_snappy, 600, 620 }, preset { luil::spring_bouncy, 1500, 1540 } })
    {
        CAPTURE(entry.config.response.count(), entry.config.damping_fraction);
        const luil::spring value { 100.0f, 0.0f, at(0), entry.config, 0.0f };
        const double omega { omega_of(entry.config) };
        double previous { std::numeric_limits<double>::infinity() };
        bool ended { false };
        for (int milliseconds { 0 }; milliseconds <= 4000; ++milliseconds)
        {
            const bool finished { luil::spring_finished(value, at(milliseconds)) };
            // 한 번 끝나면 다시 움직이지 않는다.
            if (ended)
                REQUIRE(finished);
            if (finished)
            {
                REQUIRE(luil::spring_value(value, at(milliseconds)) == 0.0f);
                REQUIRE(luil::spring_velocity(value, at(milliseconds)) == 0.0f);
                REQUIRE(luil::spring_next_update(value, at(milliseconds)).has_value() == false);
                if (ended == false)
                {
                    REQUIRE(milliseconds >= entry.earliest);
                    REQUIRE(milliseconds <= entry.latest);
                }
                ended = true;
                continue;
            }
            // 값과 속도를 합친 척도는 줄기만 한다 (목표가 0이라 값이 곧 차이다).
            const double rest { std::hypot(luil::spring_value(value, at(milliseconds)), luil::spring_velocity(value, at(milliseconds)) / omega) };
            REQUIRE(rest <= previous * (1.0 + 1.0e-5));
            previous = rest;
        }
        REQUIRE(ended);

        // 허용치를 키우면 일찍 끝나고, 0 이하는 기본값이다.
        luil::spring_config loose { entry.config };
        loose.rest_threshold = 0.1f;
        REQUIRE(first_finished_millisecond(loose) < first_finished_millisecond(entry.config));
        luil::spring_config unset { entry.config };
        unset.rest_threshold = 0.0f;
        REQUIRE(first_finished_millisecond(unset) == first_finished_millisecond(entry.config));
        unset.rest_threshold = -1.0f;
        REQUIRE(first_finished_millisecond(unset) == first_finished_millisecond(entry.config));
    }
}

TEST_CASE("A spring is a function of the clock alone", "[ui][spring]")
{
    const luil::spring value { sample_spring(0.75f, 200.0f) };
    const float early { luil::spring_value(value, at(1100)) };
    const float early_velocity { luil::spring_velocity(value, at(1100)) };
    // 늦은 시각을 먼저 물어도 이른 시각의 답은 그대로다.
    static_cast<void>(luil::spring_value(value, at(5000)));
    static_cast<void>(luil::spring_finished(value, at(5000)));
    REQUIRE(luil::spring_value(value, at(1100)) == early);
    REQUIRE(luil::spring_velocity(value, at(1100)) == early_velocity);

    // 예고: 시작 전이면 시작 시각, 도는 중이면 한 frame 뒤, 끝났으면 없다.
    REQUIRE(luil::spring_next_update(value, at(0)) == at(1000));
    REQUIRE(luil::spring_next_update(value, at(1000)) == at(1000) + luil::transition_frame);
    REQUIRE(luil::spring_next_update(value, at(1100)) == at(1100) + luil::transition_frame);
    REQUIRE(luil::spring_next_update(value, at(60000)).has_value() == false);
}

TEST_CASE("Reduced motion ignores the velocity and stands on the target", "[ui][spring]")
{
    const luil::spring running { sample_spring(0.75f, 300.0f) };
    const luil::spring retargeted { luil::spring_to(running, 50.0f, at(1100), luil::spring_smooth, true) };
    const luil::spring released { luil::spring_release(10.0f, 9000.0f, 80.0f, at(1100), luil::spring_snappy, true) };
    for (const int milliseconds : { 0, 1000, 1100, 1110, 5000 })
    {
        CAPTURE(milliseconds);
        REQUIRE(luil::spring_value(retargeted, at(milliseconds)) == 50.0f);
        REQUIRE(luil::spring_velocity(retargeted, at(milliseconds)) == 0.0f);
        REQUIRE(luil::spring_finished(retargeted, at(milliseconds)));
        REQUIRE(luil::spring_next_update(retargeted, at(milliseconds)).has_value() == false);
        REQUIRE(luil::spring_value(released, at(milliseconds)) == 80.0f);
        REQUIRE(luil::spring_velocity(released, at(milliseconds)) == 0.0f);
        REQUIRE(luil::spring_finished(released, at(milliseconds)));
        REQUIRE(luil::spring_next_update(released, at(milliseconds)).has_value() == false);
    }
}

TEST_CASE("Invalid values never produce NaN and stand on the target", "[ui][spring]")
{
    constexpr float nan { std::numeric_limits<float>::quiet_NaN() };
    constexpr float infinity { std::numeric_limits<float>::infinity() };
    const luil::spring_config invalid_configs[] {
        luil::spring_config { std::chrono::milliseconds { 0 }, 1.0f },
        luil::spring_config { std::chrono::milliseconds { -5 }, 1.0f },
        luil::spring_config { std::chrono::milliseconds { 350 }, 0.0f },
        luil::spring_config { std::chrono::milliseconds { 350 }, -0.5f },
        luil::spring_config { std::chrono::milliseconds { 350 }, nan },
        luil::spring_config { std::chrono::milliseconds { 350 }, infinity },
    };
    for (const luil::spring_config& config : invalid_configs)
    {
        CAPTURE(config.response.count(), config.damping_fraction);
        const luil::spring value { 0.0f, 100.0f, at(1000), config, 250.0f };
        for (const int milliseconds : { 0, 1000, 1050, 9999 })
        {
            REQUIRE(luil::spring_value(value, at(milliseconds)) == 100.0f);
            REQUIRE(luil::spring_velocity(value, at(milliseconds)) == 0.0f);
            REQUIRE(luil::spring_finished(value, at(milliseconds)));
            REQUIRE(luil::spring_next_update(value, at(milliseconds)).has_value() == false);
        }
        // 이어 출발해도 NaN이 번지지 않는다.
        const luil::spring next { luil::spring_to(value, 30.0f, at(1050), luil::spring_smooth, false) };
        REQUIRE(luil::spring_value(next, at(1050)) == 100.0f);
        REQUIRE(luil::spring_value(next, at(20000)) == 30.0f);
    }

    // 값이 유한하지 않으면 목표다. 목표가 유한하지 않으면 시작 값이고, 둘 다 아니면 0이다.
    const luil::spring bad_from { nan, 100.0f, at(1000), luil::spring_smooth, 0.0f };
    const luil::spring bad_velocity { 0.0f, 100.0f, at(1000), luil::spring_smooth, infinity };
    const luil::spring bad_to { 5.0f, nan, at(1000), luil::spring_smooth, 0.0f };
    const luil::spring bad_both { nan, infinity, at(1000), luil::spring_smooth, nan };
    for (const int milliseconds : { 0, 1000, 1100, 9999 })
    {
        REQUIRE(luil::spring_value(bad_from, at(milliseconds)) == 100.0f);
        REQUIRE(luil::spring_value(bad_velocity, at(milliseconds)) == 100.0f);
        REQUIRE(luil::spring_value(bad_to, at(milliseconds)) == 5.0f);
        REQUIRE(luil::spring_value(bad_both, at(milliseconds)) == 0.0f);
        REQUIRE(luil::spring_velocity(bad_both, at(milliseconds)) == 0.0f);
        REQUIRE(luil::spring_finished(bad_both, at(milliseconds)));
    }
}

TEST_CASE("Physical parameters convert to the same motion", "[ui][spring]")
{
    // 질량 1, 강성 100, 감쇠 20은 ω = 10, ζ = 1이다 (2π / 10 = 628.3ms).
    const luil::spring_config critical { luil::spring_from_physics(1.0f, 100.0f, 20.0f) };
    REQUIRE(critical.response == std::chrono::milliseconds { 628 });
    REQUIRE(critical.damping_fraction == Catch::Approx(1.0f));
    REQUIRE(critical.rest_threshold == 0.01f);
    // 질량 2, 강성 200, 감쇠 8도 ω = 10이고 ζ = 8 / (2√400) = 0.2다. 질량은 운동을 바꾸지 않는다.
    const luil::spring_config light { luil::spring_from_physics(2.0f, 200.0f, 8.0f, 0.5f) };
    REQUIRE(light.response == std::chrono::milliseconds { 628 });
    REQUIRE(light.damping_fraction == Catch::Approx(0.2f));
    REQUIRE(light.rest_threshold == 0.5f);

    // m·y'' + c·y' + k·y = 0을 직접 적분한 운동과 반올림 안에서 맞는다.
    const luil::spring value { 100.0f, 0.0f, at(0), luil::spring_from_physics(2.0f, 200.0f, 8.0f), 0.0f };
    motion state { 100.0, 0.0 };
    constexpr double step { 1.0e-4 };
    for (int index { 1 }; index <= 5000; ++index)
    {
        state = rk4_step(state, 10.0, 0.2, step);
        if (index % 1000 == 0)
            REQUIRE(luil::spring_value(value, after(value.started, index * step)) == Catch::Approx(state.y).margin(0.5));
    }

    // 0 이하이거나 유한하지 않으면 전환이 없다.
    constexpr float nan { std::numeric_limits<float>::quiet_NaN() };
    const luil::spring_config none[] {
        luil::spring_from_physics(0.0f, 100.0f, 20.0f),
        luil::spring_from_physics(1.0f, -100.0f, 20.0f),
        luil::spring_from_physics(1.0f, 100.0f, 0.0f),
        luil::spring_from_physics(nan, 100.0f, 20.0f),
        luil::spring_from_physics(1.0f, std::numeric_limits<float>::infinity(), 20.0f),
    };
    for (const luil::spring_config& config : none)
    {
        REQUIRE(config.response == std::chrono::milliseconds { 0 });
        const luil::spring value_without_motion { 0.0f, 100.0f, at(1000), config, 0.0f };
        REQUIRE(luil::spring_value(value_without_motion, at(1001)) == 100.0f);
        REQUIRE(luil::spring_finished(value_without_motion, at(1001)));
    }

    // 아주 빠르거나 아주 느린 값도 1ms 이상, 상한 이하로 자른다.
    REQUIRE(luil::spring_from_physics(1.0f, 1.0e30f, 1.0f).response == std::chrono::milliseconds { 1 });
    REQUIRE(luil::spring_from_physics(1.0f, 1.0e-30f, 1.0f).response > std::chrono::milliseconds { 0 });
}
