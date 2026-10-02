#pragma once

#include <algorithm>
#include <cstdint>

namespace luil {
    // GPU fence 대기의 시간 계획이다 (밀리초).
    //
    // 무기한 대기는 render의 모든 오류 처리보다 앞에 선 단일 관문이라, driver가
    // 신호를 영영 주지 못하면 CPU fallback을 포함한 복구 코드 전체가 도달 불가가
    // 된다. 그래서 상한을 두고, 상한에 닿으면 실패로 돌려 복구에 맡긴다.
    //  - 기본 Windows에서 GPU hang은 약 2초(TdrDelay) 뒤 TDR이 device removal을
    //    일으키고 fence가 UINT64_MAX로 완료되어 저절로 풀린다. 이 예산은 그 길이
    //    막힌 환경(TDR 비활성·driver 결함)의 마지막 탈출구이므로 TDR보다 훨씬
    //    넉넉하게 잡는다 — 여기에 닿았다는 것 자체가 이미 비정상이다.
    //  - 슬라이스로 쪼개는 것은 대기 중간에 device removal을 물을 자리를 만들기
    //    위해서다. removal이 일어나고도 이벤트가 끝내 오지 않으면 예산을 다 쓰기
    //    전에 거기서 빠져나온다.
    inline constexpr std::uint32_t fence_wait_budget_ms { 10'000 };
    inline constexpr std::uint32_t fence_wait_slice_ms { 500 };

    // 다음 슬라이스의 길이를 정한다. 0이면 예산 소진 — 더 기다리지 않는다.
    // 마지막 슬라이스는 남은 예산으로 줄어들므로 총 대기가 예산을 넘지 않는다.
    [[nodiscard]] constexpr std::uint32_t next_fence_wait_slice(
        const std::uint32_t waited_ms, const std::uint32_t budget_ms = fence_wait_budget_ms, const std::uint32_t slice_ms = fence_wait_slice_ms) noexcept
    {
        if (waited_ms >= budget_ms)
            return 0;
        return std::min(slice_ms, budget_ms - waited_ms);
    }
} // namespace luil
