#pragma once

#include "host/taskbar_sync.h"
#include "luil/win32/win32_window.h"
#include "win32/icon_from_image.h"

#include <windows.h>

#include <shobjidl.h>
#include <wrl/client.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace luil::win32 {
    // 작업 표시줄 단추에 진행률과 오버레이를 거는 Win32 쪽이다 (docs/taskbar-design.md).
    // `ITaskbarList3` 개체 하나를 모든 창이 나눠 쓰고, 창(표면)마다 적용한 상태와 오버레이 아이콘 핸들을 쥔다.
    // UI thread의 STA에서만 부른다.
    //  - 어떤 호출을 할지는 core의 `taskbar_sync`가 고른다. 여기는 그 호출을 셸에 옮기고 결과를 보고로 낸다.
    //  - 표면은 id로 가른다 (주 창은 빈 문자열). 같은 id의 창이 다시 서면(`HWND`가 달라지면) 기록을 새로 시작한다.
    class taskbar_button
    {
    public:
        taskbar_button() = default;
        taskbar_button(const taskbar_button&) = delete;
        taskbar_button(taskbar_button&&) = delete;
        taskbar_button& operator=(const taskbar_button&) = delete;
        taskbar_button& operator=(taskbar_button&&) = delete;
        ~taskbar_button() = default;

        // 셸의 작업 표시줄 개체를 만든다. 만들지 못하면 거짓이고 `available()`도 거짓이다.
        //  - `com_sta_scope`가 없는 스레드에서는 COM이 서지 않아 만들지 못한다.
        bool open() noexcept;
        // 이미 만든 개체를 쥔다 (test가 가짜 개체를 꽂는다). `HrInit`을 부른다.
        bool open(Microsoft::WRL::ComPtr<ITaskbarList3> shell) noexcept;

        // 이 프로세스가 요청을 셸에 낼 수 있는가다 (`ui_platform_capabilities::taskbar`).
        [[nodiscard]] bool available() const noexcept
        {
            return shell_ != nullptr;
        }

        // 셸이 이 창의 단추를 만들었다고 알렸다 (`TaskbarButtonCreated`). 적용 기록을 지우고 오버레이 핸들을 놓는다.
        // 요청은 부른 쪽이 `apply`로 다시 낸다.
        void button_created(const std::u8string& surface, HWND window);
        // 이 창의 단추가 없다 (창이 숨었다). 요청을 쥐는 일은 `apply`가 계속한다.
        void button_lost(const std::u8string& surface) noexcept;

        // 이 창의 바라는 상태를 적용한 상태와 견줘 바뀐 것만 셸에 낸다.
        // `dpi`는 오버레이 그림의 크기를 정한다. 바뀌면 오버레이만 새 크기로 다시 낸다.
        // 보고할 상태가 바뀌었으면 그 보고를 돌려준다.
        [[nodiscard]] std::optional<taskbar_report> apply(const std::u8string& surface, HWND window, std::uint32_t dpi, const taskbar_request& wanted);

        // 목록에 없는 표면의 기록과 아이콘을 놓는다.
        void retain(const std::vector<std::u8string>& surfaces);
        void forget(const std::u8string& surface) noexcept;
        void forget_all() noexcept;

        // 이 표면에 단추가 섰다고 받았는가다 (test).
        [[nodiscard]] bool ready(const std::u8string& surface) const noexcept;

    private:
        struct record
        {
            std::u8string surface {};
            HWND window { nullptr };
            taskbar_sync sync {};
            // 지금 걸려 있는 오버레이 아이콘이다. 교체·해제·창 파괴까지 쥔다.
            owned_icon icon {};
            std::uint32_t dpi { 96 };
            // 마지막으로 낸 호출 묶음이 실패했다. 다음 변화나 단추 재생성에서 다시 한다.
            bool failed { false };
            std::u8string error {};
            // 마지막으로 알린 보고다.
            taskbar_report reported {};
        };

        [[nodiscard]] record& record_for(const std::u8string& surface, HWND window);
        [[nodiscard]] record* find(const std::u8string& surface) noexcept;
        [[nodiscard]] const record* find(const std::u8string& surface) const noexcept;
        // 호출 하나를 셸에 낸다. 실패하면 첫 실패의 글을 `error`에 남긴다 (이미 있으면 그대로 둔다).
        void execute(record& item, const taskbar_call& call, std::u8string& error);

        Microsoft::WRL::ComPtr<ITaskbarList3> shell_ {};
        std::vector<record> records_ {};
    };
}
