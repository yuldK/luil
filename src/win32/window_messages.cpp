#include "win32/window_messages.h"

namespace luil::win32 {
    namespace {
        // 이름은 한 번만 등록한다. 같은 이름은 몇 번을 물어도 같은 번호를 돌려준다.
        [[nodiscard]] UINT taskbar_button_created_message() noexcept
        {
            static const UINT message { RegisterWindowMessageW(L"TaskbarButtonCreated") };
            return message;
        }

        [[nodiscard]] UINT taskbar_created_message() noexcept
        {
            static const UINT message { RegisterWindowMessageW(L"TaskbarCreated") };
            return message;
        }
    }

    shell_event shell_event_of(const UINT message) noexcept
    {
        // 등록 번호는 0xC000부터다. 대부분의 메시지는 여기서 걸러 등록 조회를 거치지 않는다.
        if (message < 0xC000)
            return shell_event::none;
        if (message == taskbar_button_created_message())
            return shell_event::taskbar_button_created;
        if (message == taskbar_created_message())
            return shell_event::taskbar_created;
        return shell_event::none;
    }

    UINT shell_event_message(const shell_event event) noexcept
    {
        switch (event)
        {
        case shell_event::taskbar_button_created:
            return taskbar_button_created_message();
        case shell_event::taskbar_created:
            return taskbar_created_message();
        case shell_event::none:
        default:
            return 0;
        }
    }

    void allow_shell_events(const HWND window) noexcept
    {
        if (window == nullptr)
            return;
        for (const shell_event event : { shell_event::taskbar_button_created, shell_event::taskbar_created })
        {
            if (const UINT message { shell_event_message(event) }; message != 0)
                static_cast<void>(ChangeWindowMessageFilterEx(window, message, MSGFLT_ALLOW, nullptr));
        }
    }

    void allow_instance_message(const HWND window) noexcept
    {
        if (window == nullptr)
            return;
        static_cast<void>(ChangeWindowMessageFilterEx(window, WM_COPYDATA, MSGFLT_ALLOW, nullptr));
    }
}
