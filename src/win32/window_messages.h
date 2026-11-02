#pragma once

#include <windows.h>

#include <cstddef>
#include <iterator>

namespace luil::win32 {
    // 창이 앱 안에서 주고받는 `WM_APP` 번호표다 (docs/taskbar-design.md).
    // 번호가 한 창에서 겹치면 안 되므로 받는 창이 같은 번호끼리 한 곳에 모은다.
    //  - 새 번호는 +6부터 표 끝에만 더하고, 받는 창을 아래 주석에 적는다.
    //  - `SetTimer` 번호는 여기 없다 (win32_window.cpp의 1·2·4, window_surface.cpp의 3).

    // 주 창. wake 신호다. frame slot과 interaction slot의 signal callback이 게시한다.
    inline constexpr UINT snapshot_wake_message { WM_APP + 1 };
    // 주 창. input thread가 큐에 넣은 창 명령(`ui_command`)의 신호다. 내용은 host의 큐가 나르고 창 메시지는 신호만 싣는다.
    inline constexpr UINT ui_command_request_message { WM_APP + 2 };
    // 주 창. input thread가 큐에 넣은 앱 UI 명령의 신호다.
    inline constexpr UINT app_ui_command_request_message { WM_APP + 3 };
    // 주 창. 클립보드 요청(복사·붙여넣기)을 UI thread로 넘기는 신호다.
    inline constexpr UINT clipboard_request_message { WM_APP + 4 };
    // 모든 표면. 접근 실행이 낸 액션을 창의 **자기 차례**에 배분하는 신호다.
    // UIA 호출은 COM 마샬링을 타고 thread가 메시지를 푸는 아무 자리에나 도착한다(크기 조절 modal 루프·시스템 메뉴·
    // `UiaRaiseAutomationEvent` 안까지). 거기서 곧바로 창 명령을 실행하면 그리는 도중에 창이 죽을 수 있어, 큐에 담고
    // 신호만 보낸다 (accessibility-action-design.md).
    inline constexpr UINT access_dispatch_message { WM_APP + 5 };

    // 번호가 서로 다르고 앱이 쓸 수 있는 대역(`WM_APP`~0xBFFF) 안인가다.
    [[nodiscard]] constexpr bool app_message_numbers_valid() noexcept
    {
        constexpr UINT numbers[] {
            snapshot_wake_message,
            ui_command_request_message,
            app_ui_command_request_message,
            clipboard_request_message,
            access_dispatch_message,
        };
        for (std::size_t index = 0; index < std::size(numbers); ++index)
        {
            if (numbers[index] < WM_APP || numbers[index] > 0xBFFF)
                return false;
            for (std::size_t other = index + 1; other < std::size(numbers); ++other)
            {
                if (numbers[index] == numbers[other])
                    return false;
            }
        }
        return true;
    }

    static_assert(app_message_numbers_valid());

    // 셸이 창에 보내는 등록 메시지(`RegisterWindowMessage`)가 알리는 일이다.
    enum class shell_event
    {
        // 등록 메시지가 아니다.
        none,
        // 작업 표시줄이 이 창의 단추를 만들었다 (`TaskbarButtonCreated`). 탐색기가 다시 뜨면 단추가 새로 서고 다시 온다.
        taskbar_button_created,
        // 작업 표시줄이 새로 생겼다 (`TaskbarCreated`). 탐색기가 다시 뜰 때 최상위 창마다 방송으로 온다.
        taskbar_created,
    };

    // 메시지가 어느 셸 사건인가다. 등록 번호(0xC000 이상)가 아니면 `none`이다.
    //  - 등록 번호는 실행마다 다르므로 상수 switch가 아니라 이 함수로 가른다.
    [[nodiscard]] shell_event shell_event_of(UINT message) noexcept;

    // 그 셸 사건의 등록 번호다. `none`이나 등록에 실패했으면 0이다.
    [[nodiscard]] UINT shell_event_message(shell_event event) noexcept;

    // 관리자 권한으로 도는 앱이 낮은 권한의 셸이 보내는 등록 메시지를 받게 한다 (UIPI).
    // 두 메시지 모두 값을 싣지 않아 열어도 얻는 것이 없다. 실패해도 메시지가 안 올 뿐이라 조용히 넘어간다.
    void allow_shell_events(HWND window) noexcept;
}
