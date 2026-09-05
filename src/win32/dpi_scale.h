#pragma once

#include <cmath>

namespace luil::win32 {
    // Windows가 100%로 보는 DPI다. 배율은 이 값에 대한 비다.
    inline constexpr int dpi_base { 96 };

    // DPI를 배율로 옮긴다 (96 → 1.0, 144 → 1.5).
    [[nodiscard]] constexpr float dpi_scale(const int dpi) noexcept
    {
        return static_cast<float>(dpi) / static_cast<float>(dpi_base);
    }

    // 논리 픽셀을 물리 픽셀로 옮긴다.
    //
    // **반올림이지 잘라내기가 아니다.** `static_cast<int>`는 0 쪽으로 자르므로
    // 양수는 내려가고 음수는 올라간다 — 같은 논리 값이 **화면의 어느 쪽에 있느냐로**
    // 다르게 놓인다. 가상 화면 좌표는 왼쪽·위 모니터에서 음수라 그 비대칭이
    // 실제로 드러나는 자리다. `MulDiv`는 이미 반올림하고 있었으므로, 두 방식이
    // 섞여 있던 것을 이 하나로 모은다.
    //  - 절반(.5)은 0에서 먼 쪽으로 간다 (`std::lround`와 같은 규칙).
    //  - 정수를 정수로 옮기는 자리(`MulDiv`)는 이미 이 규칙이라 그대로 둔다.
    //    고칠 것은 **잘라내던 쪽**이었다.
    [[nodiscard]] inline int scaled_pixels(const float logical, const float scale) noexcept
    {
        return static_cast<int>(std::lround(static_cast<double>(logical) * static_cast<double>(scale)));
    }
} // namespace luil::win32
