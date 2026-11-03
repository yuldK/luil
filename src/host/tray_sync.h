#pragma once

#include "luil/app/tray_icon.h"

#include <optional>
#include <string>

namespace luil {
    // 알림 영역 그림을 고르는 셸의 환경이다 (docs/tray-icon-design.md의 "그림").
    struct tray_appearance
    {
        bool high_contrast { false };
        // 작업 표시줄이 밝은 테마다 (앱 모드가 아니다).
        bool light_taskbar { false };
        // 아이콘 한 변의 물리 픽셀이다. 주 작업 표시줄이 있는 모니터의 배율을 따른다.
        int icon_size { 16 };

        [[nodiscard]] bool operator==(const tray_appearance&) const noexcept = default;
    };

    // 환경에 맞는 그림을 고른다. 고대비면 `high_contrast_icon`, 밝은 작업 표시줄이면 `light_icon`, 그 밖은 `icon`이고
    // 빈 칸은 다음 순서로 물러선다. 셋이 다 비면 빈 그림이고, 플랫폼이 창 아이콘으로 채운다.
    [[nodiscard]] ui_image choose_tray_image(const tray_icon& icon, bool high_contrast, bool light_taskbar);

    enum class tray_call_kind
    {
        // 아이콘을 더한다. 플랫폼은 곧이어 콜백 판(NOTIFYICON_VERSION_4)을 건다.
        add,
        // 선 아이콘의 그림이나 툴팁을 바꾼다. 바뀐 칸만 싣는다 (`icon`·`tooltip`).
        modify,
        // 아이콘을 지운다.
        remove,
    };

    struct tray_call
    {
        tray_call_kind kind { tray_call_kind::add };
        // 그림 칸과 툴팁 칸을 싣는가다. 더하기는 둘 다 참이고 지우기는 둘 다 거짓이다.
        bool icon { false };
        bool tooltip { false };
        // 고른 그림이다. 비면 창 아이콘이다.
        ui_image image {};
        int icon_size { 0 };
        // 앱이 준 툴팁이다. 비면 창 제목이다 (플랫폼이 채운다).
        std::u8string text {};
    };

    // 플랫폼이 호출 하나를 낸 결과다.
    enum class tray_call_outcome
    {
        succeeded,
        failed,
        // 셸이 바빠 시간 안에 답하지 않았다. 더하기에서는 아이콘이 섰을 수도 있다.
        timed_out,
    };

    // 요청이 셸에 닿은 정도다. 플랫폼 보고(`win32::tray_status`)가 이 값을 옮긴다.
    enum class tray_sync_state
    {
        off,
        waiting,
        shown,
        failed,
    };

    // 알림 영역 아이콘 하나의 바라는 상태와 셸에 낸 상태를 견줘 할 호출을 고른다. OS를 모른다.
    //  - 작업 표시줄(`taskbar_sync`)과 달리 호출 결과가 다음 할 일을 바꾼다. 더하기가 실패하면 기다리고, 시간 초과면
    //    바꾸기로 섰는지 떠 본다. 그래서 플랫폼은 호출마다 `completed`로 결과를 되돌린다 (곧바로, 다음 `update` 전에).
    //  - 호출은 나가는 순간 낸 것으로 기록한다. 바꾸기·지우기가 실패해도 같은 요청으로 다시 하지 않는다.
    //  - 그림은 몸통(`ui_image::backing`)과 크기로, 툴팁은 값으로 견준다.
    class tray_sync
    {
    public:
        // 지금 해야 할 호출이다. 없으면 빈 값이다.
        [[nodiscard]] std::optional<tray_call> update(const std::optional<tray_icon>& wanted, const tray_appearance& appearance);

        // `call`을 낸 결과다. 이어서 해야 할 호출이 있으면 돌려준다 (시간 초과 뒤 섰는지 떠 보는 바꾸기).
        [[nodiscard]] std::optional<tray_call> completed(const tray_call& call, tray_call_outcome outcome);

        // 탐색기가 다시 떴다 (`TaskbarCreated`). 새 알림 영역에는 아무것도 없으므로 적용 기록을 지운다. 요청이 있으면
        // 다음 `update`가 처음부터 더한다. 여러 번 와도 같다.
        void shell_restarted() noexcept;

        // 셸에 아이콘이 걸려 있다고 보는가다 (지우기를 낼 대상이 있는가).
        [[nodiscard]] bool added() const noexcept;

        [[nodiscard]] tray_sync_state state() const noexcept
        {
            return state_;
        }

    private:
        enum class phase
        {
            // 셸에 낸 것이 없다.
            absent,
            // 더하기를 냈고 결과를 기다린다.
            adding,
            // 시간 초과 뒤 섰는지 떠 보는 바꾸기를 냈고 결과를 기다린다.
            probing,
            // 섰다.
            present,
            // 더하기가 실패했다. 탐색기가 다시 뜰 때까지 요청 변화에도 다시 하지 않는다.
            waiting_restart,
            // 시간 초과 뒤 떠 보기도 실패했다. 아이콘이 섰는지 모르고 방송도 오지 않으므로 다음 요청 변화에서 다시 더한다.
            waiting_change,
        };

        void record(const tray_call& call);
        [[nodiscard]] bool differs(const ui_image& image, int size, const std::u8string& text) const noexcept;

        phase phase_ { phase::absent };
        tray_sync_state state_ { tray_sync_state::off };
        // 마지막으로 낸 그림·크기·툴팁이다. 그림을 복사해 쥐므로 몸통이 살아 있는 동안 포인터 비교가 같은 그림을 뜻한다.
        ui_image applied_image_ {};
        int applied_size_ { 0 };
        std::u8string applied_text_ {};
    };
}
