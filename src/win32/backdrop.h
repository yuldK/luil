#pragma once

#include "luil/app/app_host.h"
#include "luil/win32/win32_window.h"

namespace luil::win32 {
    // 배경 효과를 세우는 실제 길이다 (backdrop-design.md).
    enum class backdrop_mechanism
    {
        none,
        // `DWMWA_SYSTEMBACKDROP_TYPE`이다. 표준 표시에서만 선다 — 레이어드 창에서는 단색 판이 된다 (실측).
        system_mica,
        system_mica_alt,
        system_acrylic,
        // 비공개 `SetWindowCompositionAttribute`의 acrylic blur behind다. 레이어드 창에서만 선다 — 표준 창에서는
        // 회색 판이 된다 (실측).
        accent_acrylic,
    };

    // 효과를 고를 때 보는 OS·창의 사정이다. 창 없이 판단을 test하려고 값으로 모은다.
    struct backdrop_environment
    {
        bool layered { false };
        // OS "투명 효과" 설정이다.
        bool transparency_enabled { true };
        bool high_contrast { false };
        bool remote_session { false };
        // 이 Windows가 시스템 배경 효과를 받는가 (Windows 11 22H2부터).
        bool system_backdrop_supported { false };
        // 비공개 accent API를 찾았는가.
        bool accent_supported { false };
    };

    struct backdrop_decision
    {
        backdrop_mechanism mechanism { backdrop_mechanism::none };
        backdrop_status status { backdrop_status::off };

        [[nodiscard]] bool active() const noexcept
        {
            return mechanism != backdrop_mechanism::none;
        }

        [[nodiscard]] bool operator==(const backdrop_decision&) const noexcept = default;
    };

    // 고른 효과를 이 창에서 무엇으로 세울지, 못 세우면 왜인지 정한다.
    //  - 고칠 수 없는 까닭(표시 방식·Windows 판)을 사용자가 바꿀 수 있는 까닭(투명 효과·고대비·원격)보다 먼저 답한다.
    //    투명 효과를 켜라고 안내했는데 켜도 서지 않으면 안내가 틀린 것이 된다.
    [[nodiscard]] backdrop_decision decide_backdrop(window_backdrop requested, const backdrop_environment& environment) noexcept;
} // namespace luil::win32
