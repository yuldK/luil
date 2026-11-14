#pragma once

#include "luil/app/app_host.h"

#include <optional>
#include <span>
#include <variant>

namespace luil {
    // 도킹 표면의 자리를 물리 화면 사각형으로 푼다 (docked-surface-design.md).
    // 창을 모르는 순수 계산이라 모니터 목록만 받는다 — 플랫폼이 OS에서 읽어 넘기고, 모니터가 바뀌면 같은 값으로 다시 부른다.
    //  - 물리 사각형이면 그대로다. 빈 사각형이면 빈 값이다.
    //  - 가장자리 자리면 그 모니터(비어 있으면 주 모니터)의 배율로 논리 값을 곱한다. 그 모니터가 없거나 두께가 0 이하면
    //    빈 값이다 (표면을 숨긴다).
    //  - 가장자리를 따라 잰 길이는 가장자리 길이로 줄이고, 띄운 거리 때문에 가장자리 밖으로 나가는 것은 막지 않는다 —
    //    앱이 바란 자리다.
    [[nodiscard]] std::optional<screen_rect> resolve_docked_bounds(const std::variant<screen_rect, docked_edge_placement>& placement, std::span<const display_info> displays) noexcept;
}
