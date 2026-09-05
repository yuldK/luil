#pragma once

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace luil::win32 {
    // 표면 하나가 지금 그리고 있는 그림과 새 frame이 그 표면에 실은 그림이다.
    //
    // tree를 **주소로만** 잡는 이유는 게시된 tree가 불변이기 때문이다. 같은
    // 주소면 같은 그림이고, 주소가 다르면 무엇이 어떻게 달라졌는지 물을 것도
    // 없이 다시 그려야 한다. 내용을 견주지 않으므로 이 판정에는 tree 타입도
    // 창(HWND)도 필요 없다 — 표면 id와 값만 보므로 창 없이 test가 선다
    // (rendering.md).
    struct surface_content
    {
        // 표면 id다. **주 창은 빈 문자열이다.**
        std::u8string surface {};
        // 이 표면이 마지막으로 받아 든 tree다. 아직 없으면 nullptr.
        const void* current { nullptr };
        // 이 게시가 그 표면에 실은 tree다. 싣지 않았으면 nullptr.
        const void* posted { nullptr };
    };

    // 이 게시로 다시 그릴 표면들이다 (준 순서 그대로).
    //
    // `every_surface`는 "이 게시가 모든 표면의 그림을 바꾼다"는 뜻이고 둘이 그
    // 값을 참으로 만든다. 둘 다 **일부러** 고르지 않는다 (rendering.md).
    //  - **상호작용 변화**: hover·눌림·초점·메뉴 강조·끌기는 tree를 바꾸지 않고
    //    그림만 바꾼다. 스냅샷에 표면 표식이 실려 있어 원리상 가릴 수 있지만,
    //    이 선별의 실패는 "그려야 할 것을 안 그리는 것"이고 증상이 "가끔 화면이
    //    낡아 있다"라 잡기 어렵다.
    //  - **외양 변화**: 테마·키 컬러·글꼴은 tree 밖에 실려 와 모든 표면의 색과
    //    글자를 바꾼다. tree를 그대로 두고 테마만 바꾸는 frame이 유효하다.
    [[nodiscard]] std::vector<std::u8string> surfaces_to_repaint(std::span<const surface_content> surfaces, bool every_surface);

    // 표면 하나가 `next_update`로 답한 "다음에 그림이 달라지는 시각"이다.
    // 값이 없으면 그 표면은 시간이 흘러도 그대로다.
    struct surface_update_deadline
    {
        std::u8string surface {};
        std::optional<std::chrono::steady_clock::time_point> next {};
    };

    // 예고들을 모아 세운 timer 하나의 계획이다.
    struct update_timer_plan
    {
        // 걸 timer가 있는가다. 거짓이면 걸려 있던 timer를 끈다 —
        // 예고가 없으면 평소 재그리기 비용을 0으로 되돌린다.
        bool armed { false };
        // 지금부터 몇 밀리초 뒤인가다.
        unsigned int delay_milliseconds { 0 };
        // 그 시각에 깨울 표면들이다.
        // **그때 시각이 되는 표면만** 든다. `armed`면 하나 이상이다.
        std::vector<std::u8string> wake {};
    };

    // Win32 timer가 받는 지연의 상한이다 (`USER_TIMER_MAXIMUM`).
    // 아주 먼 예고가 밀리초 수로 잘리며 "지금"이 되는 것을 막는다.
    //  - 값을 여기 적는 이유는 windows.h를 들이지 않기 위해서다. 이 헤더가
    //    Win32를 모르는 것이 창 없이 test가 서는 조건이다.
    inline constexpr unsigned int maximum_update_timer_delay { 0x7FFFFFFF };

    // 표면들의 예고를 모아 timer 하나를 계획한다.
    //
    // 가장 이른 예고가 timer의 시각을 정하고, **그 시각까지 시각이 되는 표면
    // 전부**가 깨어난다 (rendering.md).
    //  - 가장 이른 하나만 깨우면 같은 tick에 함께 시각이 되는 표면(두 창이
    //    동시에 애니메이션 중)이 빠진다.
    //  - 전부를 깨우면 보조 창 하나의 회전 표시가 모든 창을 `repaint_interval`
    //    마다 다시 그린다. 이 함수가 있는 이유가 그것이다.
    //
    // "지금 이하"의 예고는 연속 애니메이션이라 `repaint_interval`로 잇고, 미래
    // 시각은 그 시각을 확실히 지나도록 살짝 늦춘다. 상한이 지연을 줄이더라도
    // 가장 이른 예고를 답한 표면은 반드시 든다.
    [[nodiscard]] update_timer_plan plan_update_timer(std::span<const surface_update_deadline> deadlines, std::chrono::steady_clock::time_point now, std::chrono::milliseconds repaint_interval);
} // namespace luil::win32
