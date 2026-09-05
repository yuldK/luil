#include "win32/fence_wait.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

TEST_CASE("Fence wait slices stop exactly at the budget", "[win32][renderer]")
{
    // 예산 안에서는 슬라이스 길이 그대로다.
    STATIC_REQUIRE(luil::win32::next_fence_wait_slice(0) == luil::win32::fence_wait_slice_ms);

    // 마지막 슬라이스는 남은 예산으로 줄어든다 — 총 대기가 예산을 넘지 않는다.
    STATIC_REQUIRE(luil::win32::next_fence_wait_slice(900, 1'000, 400) == 100);
    // 슬라이스가 예산보다 크면 예산이 곧 슬라이스다.
    STATIC_REQUIRE(luil::win32::next_fence_wait_slice(0, 300, 500) == 300);

    // 예산이 소진되면 0 — 호출하는 loop의 유일한 종료 조건이다.
    STATIC_REQUIRE(luil::win32::next_fence_wait_slice(luil::win32::fence_wait_budget_ms) == 0);
    STATIC_REQUIRE(luil::win32::next_fence_wait_slice(1'000, 1'000, 400) == 0);
    STATIC_REQUIRE(luil::win32::next_fence_wait_slice(2'000, 1'000, 400) == 0);
}

TEST_CASE("Fence wait loop spends the budget and terminates", "[win32][renderer]")
{
    // wait_for_fence의 loop를 그대로 흉내 낸다. 매 슬라이스가 timeout으로
    // 끝나는 최악(신호 없는 fence)에서도 예산만큼 기다린 뒤 반드시 끝난다 —
    // 무기한 대기 회귀를 여기서 잡는다.
    std::uint32_t waited_ms { 0 };
    while (true)
    {
        const std::uint32_t slice_ms { luil::win32::next_fence_wait_slice(waited_ms) };
        if (slice_ms == 0)
            break;
        waited_ms += slice_ms;
    }
    REQUIRE(waited_ms == luil::win32::fence_wait_budget_ms);
}

TEST_CASE("Fence wait budget clears the default TDR window", "[win32][renderer]")
{
    // 기본 Windows에서는 TDR(TdrDelay 2초)이 device removal로 대기를 풀어 준다.
    // 예산이 그보다 짧으면 정상 복구 경로를 가로채는 것이 되므로, 예산은 TDR이
    // 일할 시간을 넉넉히 준 뒤에야 포기하는 값이어야 한다.
    STATIC_REQUIRE(luil::win32::fence_wait_budget_ms > 2'000);
    // 슬라이스는 removal 감지 지연의 상한이다. 0이면 loop가 공회전한다.
    STATIC_REQUIRE(luil::win32::fence_wait_slice_ms > 0);
}
