#pragma once

#include "luil/app/app_host.h"
#include "luil/win32/win32_window.h"

#include <windows.h>

#include <optional>

namespace luil::win32 {
    // 배경 효과를 세우는 실제 길이다 (backdrop-design.md).
    enum class backdrop_mechanism
    {
        none,
        // `DWMWA_SYSTEMBACKDROP_TYPE`이다. 활성화되는 표준 창에서만 선다 — 레이어드 창에서는 단색 판이고, 비활성 창과
        // 활성화되지 않는 popup에서는 단색 대체 색이다 (실측).
        system_mica,
        system_mica_alt,
        system_acrylic,
        // 비공개 `SetWindowCompositionAttribute`의 acrylic blur behind다. 레이어드 창과 캡션 스타일이 없는 popup
        // (`WS_POPUP`)에서 선다 — 캡션 스타일이 있는 표준 창에서는 회색 판이 된다 (실측). 활성 여부와 상관없다.
        accent_acrylic,
    };

    // 효과를 고를 때 보는 OS·표면의 사정이다. 창 없이 판단을 test하려고 값으로 모은다.
    struct backdrop_environment
    {
        bool layered { false };
        // 이 표면이 활성 창이 되는가다. popup(`WS_EX_NOACTIVATE`)은 거짓이다.
        //  - 시스템 배경 효과는 비활성 창에서 단색 대체 색이라, 활성화되지 않는 표면에서는 늘 단색 판이다 (실측).
        //  - **지금** 활성인지는 보지 않는다. 활성화되는 표면이 비활성일 때 단색이 되는 것은 OS의 비활성 창 모습이고,
        //    활성 전환마다 길을 바꾸면 바탕이 전환마다 다시 깔린다.
        bool activates { true };
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

    // 고른 효과를 이 표면에서 무엇으로 세울지, 못 세우면 왜인지 정한다.
    //  - 레이어드 창과 활성화되지 않는 표면은 accent acrylic뿐이다. mica 계열은 `unsupported_presentation`이다.
    //  - 고칠 수 없는 까닭(표시 방식·Windows 판)을 사용자가 바꿀 수 있는 까닭(투명 효과·고대비·원격)보다 먼저 답한다.
    //    투명 효과를 켜라고 안내했는데 켜도 서지 않으면 안내가 틀린 것이 된다.
    [[nodiscard]] backdrop_decision decide_backdrop(window_backdrop requested, const backdrop_environment& environment) noexcept;

    // 표면 하나의 배경 효과 상태다. 세워 둔 길과 알린 상태를 들고, 새 선택·OS 사정에서 무엇을 다시 걸고 무엇을 알릴지
    // 정한다. 주 창·보조 창·popup이 같은 규칙을 쓰려고 창 없이 모았다.
    //  - 표면의 표시 방식은 만든 뒤 바뀌지 않으므로 효과가 바뀌어도 창을 다시 만들 일이 없다. 길을 다시 걸 뿐이다.
    class surface_backdrop
    {
    public:
        struct update
        {
            // 새로 세울 길이다. 세워 둔 길과 같으면 비어 있다 — DWM에 아무것도 말하지 않는다.
            std::optional<backdrop_mechanism> apply {};
            // 바로 앞까지 세워 둔 길이다. 새 길을 세울 때 이 길을 거둔다.
            backdrop_mechanism previous { backdrop_mechanism::none };
            // 알릴 보고다. 지난번에 알린 것과 같으면 비어 있다. 표면 id(`surface`)는 부른 쪽이 채운다.
            std::optional<backdrop_report> report {};
        };

        // `quiet_off`면 효과를 고르지 않은 처음 상태(`off`)를 이미 알린 것으로 친다 — 보조 창마다 "효과 없음"이 가면
        // 앱이 모든 창의 보고를 걸러야 한다. 주 창은 처음 상태도 알린다.
        explicit surface_backdrop(bool quiet_off = false) noexcept;

        [[nodiscard]] update refresh(window_backdrop requested, const backdrop_environment& environment) noexcept;

        [[nodiscard]] const backdrop_decision& decision() const noexcept
        {
            return decision_;
        }

    private:
        backdrop_decision decision_ {};
        std::optional<backdrop_report> posted_ {};
    };

    // 이 프로세스에서 비공개 accent API를 찾았는가다. 문서화되지 않은 API라 찾지 못할 수 있다.
    [[nodiscard]] bool accent_backdrop_available() noexcept;

    // 창 하나에 길을 세우고 앞의 길을 거둔다. 두 길(시스템·accent)을 함께 걸면 판이 겹친다.
    //  - 시스템 길은 `system_supported`일 때만, 그리고 앞이나 뒤의 길이 시스템일 때만 말한다. accent도 같다 — 효과를
    //    고른 적 없는 창에는 아무것도 말하지 않는다.
    //  - 같은 accent 길을 다시 넘기면 색 막만 다시 건다. `dark`는 그 색 막의 명암이다 (바탕 밝기, `is_dark_background`).
    //  - DWM 프레임 넓히기(`MARGINS` -1)는 부른 쪽의 몫이다. 표면마다 프레임을 다시 거는 자리가 달라서다.
    void apply_backdrop_mechanism(HWND window, backdrop_mechanism mechanism, backdrop_mechanism previous, bool system_supported, bool dark) noexcept;
} // namespace luil::win32
