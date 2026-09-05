#include "luil/ui/transition.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace {
    [[nodiscard]] constexpr std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    // 길이 100의 전환 하나다 (0 → 200).
    constexpr luil::transition sample { 0.0f, 200.0f, at(1000), std::chrono::milliseconds { 100 } };
} // namespace

TEST_CASE("Easing stays inside the unit square and is symmetric about the middle", "[ui][transition]")
{
    STATIC_REQUIRE(luil::ease_in_out(0.0f) == 0.0f);
    STATIC_REQUIRE(luil::ease_in_out(1.0f) == 1.0f);
    STATIC_REQUIRE(luil::ease_in_out(0.5f) == 0.5f);
    // 범위 밖은 끝값으로 잘린다.
    STATIC_REQUIRE(luil::ease_in_out(-1.0f) == 0.0f);
    STATIC_REQUIRE(luil::ease_in_out(2.0f) == 1.0f);
    // 완만하게 시작한다 — 앞의 4분의 1이 선형(0.25)보다 적게 간다.
    STATIC_REQUIRE(luil::ease_in_out(0.25f) < 0.25f);
    // 그리고 완만하게 멈춘다 — 뒤의 4분의 1이 선형보다 많이 가 있다.
    STATIC_REQUIRE(luil::ease_in_out(0.75f) > 0.75f);
}

TEST_CASE("A transition is a function of the clock alone", "[ui][transition]")
{
    // 시작 전·시작·중간·끝·끝난 뒤.
    STATIC_REQUIRE(luil::transition_value(sample, at(0)) == 0.0f);
    STATIC_REQUIRE(luil::transition_value(sample, at(1000)) == 0.0f);
    STATIC_REQUIRE(luil::transition_value(sample, at(1050)) == 100.0f);
    STATIC_REQUIRE(luil::transition_value(sample, at(1100)) == 200.0f);
    STATIC_REQUIRE(luil::transition_value(sample, at(5000)) == 200.0f);

    // 같은 시각을 다시 물어도 같은 값이다 (그 사이의 상태가 없다).
    STATIC_REQUIRE(luil::transition_value(sample, at(1050)) == luil::transition_value(sample, at(1050)));

    // 길이가 0이면 언제 물어도 끝값이다 — 전환이 없다는 뜻이다.
    constexpr luil::transition instant { 5.0f, 9.0f, at(1000), std::chrono::milliseconds { 0 } };
    STATIC_REQUIRE(luil::transition_value(instant, at(0)) == 9.0f);
    STATIC_REQUIRE(luil::transition_finished(instant, at(0)));
}

TEST_CASE("A running transition asks for the next frame and a finished one sleeps", "[ui][transition]")
{
    STATIC_REQUIRE(luil::transition_finished(sample, at(1099)) == false);
    STATIC_REQUIRE(luil::transition_finished(sample, at(1100)));

    // 아직 시작 전이면 시작 시각을 예고한다 (그때까지는 잠잔다).
    STATIC_REQUIRE(luil::transition_next_update(sample, at(0)) == at(1000));
    // 도는 중이면 한 frame 뒤다.
    STATIC_REQUIRE(luil::transition_next_update(sample, at(1050)) == at(1050) + luil::transition_frame);
    // 끝났으면 답이 없다 — logic thread를 계속 깨우지 않는다.
    STATIC_REQUIRE(luil::transition_next_update(sample, at(1100)).has_value() == false);
}

TEST_CASE("Turning back mid-flight starts from where it is now", "[ui][transition]")
{
    // 절반쯤 간 자리(100)에서 0으로 되돌린다.
    constexpr luil::transition back { luil::transition_to(sample, 0.0f, at(1050), std::chrono::milliseconds { 100 }) };
    STATIC_REQUIRE(back.from == 100.0f);
    STATIC_REQUIRE(back.to == 0.0f);
    STATIC_REQUIRE(back.started == at(1050));
    // 끝값으로 튀지 않고 지금 값에서 이어진다.
    STATIC_REQUIRE(luil::transition_value(back, at(1050)) == 100.0f);
    STATIC_REQUIRE(luil::transition_value(back, at(1150)) == 0.0f);
}
