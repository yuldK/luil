#pragma once

#include "luil/app/taskbar.h"

#include <cstdint>
#include <string>
#include <vector>

namespace luil {
    // 진행률을 셸에 낼 때의 단계 수다. 값은 이 수로 반올림해 내므로 그보다 작은 변화는 호출이 되지 않는다.
    inline constexpr std::uint32_t taskbar_progress_total { 1000 };

    // 0..1을 1000단계 정수로 옮긴다 (docs/taskbar-design.md).
    //  - NaN은 0이다. 범위 밖은 자른다 (`progress_element::clamp_value`는 NaN을 그대로 둬서 먼저 거른다).
    [[nodiscard]] std::uint32_t taskbar_progress_units(float value) noexcept;

    // 셸에 할 호출 하나다. 어느 칸을 읽는지는 `kind`가 정한다.
    enum class taskbar_call_kind
    {
        // `units`를 `taskbar_progress_total` 중 몇 단계로 건다. 값이 있는 상태에서만 나가고 상태 호출이 곧바로 뒤따른다.
        progress_value,
        // `state`를 건다.
        progress_state,
        // 새 그림 `icon`과 `description`을 건다. 이전 오버레이는 이 호출이 바꾼다.
        overlay_set,
        // 같은 그림에 `description`만 바꿔 다시 건다.
        overlay_description,
        // 오버레이를 거둔다.
        overlay_clear,
    };

    struct taskbar_call
    {
        taskbar_call_kind kind { taskbar_call_kind::progress_state };
        taskbar_progress_state state { taskbar_progress_state::none };
        std::uint32_t units { 0 };
        ui_image icon {};
        std::u8string description {};
    };

    // 요청이 셸에 닿은 정도다. 호출이 성공했는지는 이 값이 모른다 (플랫폼이 호출 결과로 덧붙인다).
    enum class taskbar_sync_state
    {
        // 요청이 기본값이다. 셸에 걸린 것이 없다.
        off,
        // 요청이 있으나 단추가 아직 없다.
        waiting,
        // 단추가 있고 요청을 모두 냈다.
        applied,
    };

    // 한 작업 표시줄 단추의 바라는 상태와 마지막으로 낸 상태를 견줘 셸에 할 호출만 고른다.
    // OS를 모른다. 호출과 결과 보고, 창별 장부는 플랫폼이 쥔다.
    //  - 단추는 만들어진 뒤에야 호출을 듣는다. 그 전에는 요청을 쥐기만 한다.
    //  - 호출은 나가는 순간 낸 것으로 기록한다. 실패해도 다시 하지 않고, 다음 변화나 단추 재생성에서 다시 한다.
    class taskbar_sync
    {
    public:
        // 단추가 섰다. 새 단추는 아무것도 걸려 있지 않으므로 적용한 기록을 지우고 요청을 처음부터 다시 낸다.
        void button_created() noexcept;
        // 단추가 없다 (창이 숨었다). 요청은 `update`가 계속 쥔다.
        void button_lost() noexcept;
        // 오버레이만 다시 낸다 (그림 크기가 바뀌는 배율 변경). 진행률은 그대로다.
        void forget_overlay() noexcept;

        [[nodiscard]] bool ready() const noexcept
        {
            return ready_;
        }

        // 바라는 상태를 맞추려고 지금 해야 할 호출이다 (순서대로). 단추가 없으면 비었다.
        [[nodiscard]] std::vector<taskbar_call> update(const taskbar_request& wanted);

        // 마지막 `update`가 본 요청이 셸에 닿은 정도다.
        [[nodiscard]] taskbar_sync_state state() const noexcept
        {
            return state_;
        }

    private:
        bool ready_ { false };
        taskbar_sync_state state_ { taskbar_sync_state::off };
        // 마지막으로 낸 진행률이다. 값이 없는 상태에서 units는 0이다.
        taskbar_progress_state applied_state_ { taskbar_progress_state::none };
        std::uint32_t applied_units_ { 0 };
        // 마지막으로 낸 오버레이다. 그림을 복사해 쥐므로 몸통이 살아 있는 동안 포인터 비교가 같은 그림을 뜻한다.
        ui_image applied_icon_ {};
        std::u8string applied_description_ {};
    };
}
