#pragma once

#include "host/hotkey_sync.h"
#include "luil/app/global_hotkey.h"
#include "luil/win32/win32_window.h"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace luil::win32 {
    // OS 호출 하나의 결과다. 실패면 그 순간의 `GetLastError()`를 싣는다 (충돌을 가른다).
    struct hotkey_os_result
    {
        bool ok { false };
        DWORD error { 0 };
    };

    // `RegisterHotKey`·`UnregisterHotKey`를 대신하는 seam이다. test가 가짜를 꽂아 개발자 PC의 키를 쥐지 않는다.
    struct hotkey_api
    {
        // `modifiers`는 `MOD_*`와 `MOD_NOREPEAT`의 합이다.
        std::function<hotkey_os_result(HWND window, int number, UINT modifiers, UINT virtual_key)> add {};
        std::function<bool(HWND window, int number)> remove {};
    };

    // 실제 `RegisterHotKey`·`UnregisterHotKey`를 부른다.
    [[nodiscard]] hotkey_api real_hotkey_api();

    // 새로 만드는 주 창이 쓸 OS 호출이다. 기본은 실제 OS이고, `scoped_hotkey_api`가 살아 있는 동안은 그것이다.
    //  - 주 창은 내부 구현이라 test가 seam을 넘길 자리가 없다. ctest가 실제 키를 쥐지 않도록 프로세스 전체에서 갈아 끼우는
    //    길을 둔다 (`scoped_tray_shell`과 같은 비공개 test 길이다).
    [[nodiscard]] hotkey_api hotkey_api_for_new_windows();

    // 살아 있는 동안 새 주 창의 OS 호출을 `api`로 바꾼다. 창을 만들기 전에 세우고 창이 사라진 뒤에 거둔다.
    class scoped_hotkey_api
    {
    public:
        explicit scoped_hotkey_api(hotkey_api api);
        scoped_hotkey_api(const scoped_hotkey_api&) = delete;
        scoped_hotkey_api& operator=(const scoped_hotkey_api&) = delete;
        ~scoped_hotkey_api();

    private:
        hotkey_api previous_ {};
    };

    // 조합의 수정자를 `RegisterHotKey`의 `MOD_*`로 옮긴다. `MOD_NOREPEAT`는 늘 켠다 (한 번 눌러 한 번 온다).
    [[nodiscard]] UINT hotkey_modifiers_of(const key_chord& chord) noexcept;

    // core의 결과를 공개 상태로 옮긴다. 이름과 차례가 같다.
    [[nodiscard]] hotkey_status hotkey_status_of(hotkey_state state) noexcept;

    // 주 창 하나의 전역 단축키다. UI thread에서만 부른다.
    //  - 어떤 호출을 할지는 core의 `hotkey_sync`가 고른다. 여기는 OS를 부르고 결과를 되돌리고 알릴 보고를 모은다.
    class hotkey_host
    {
    public:
        hotkey_host() = default;
        hotkey_host(const hotkey_host&) = delete;
        hotkey_host& operator=(const hotkey_host&) = delete;

        // 눌림을 받을 창과 OS 호출을 정한다.
        void configure(HWND window, hotkey_api api);

        // 바라는 목록을 OS에 맞춘다. 결과가 정해진 항목의 보고를 돌려준다.
        [[nodiscard]] std::vector<hotkey_report> apply(std::span<const global_hotkey> wanted, bool key_capture);

        // 주 창이 활성화됐다. 다음 `apply`가 걸지 못한 항목을 한 번 다시 건다.
        void reactivated() noexcept
        {
            sync_.reactivated();
        }

        // 건 것을 모두 푼다 (창이 끝날 때). 푼 뒤에는 눌림이 와도 무시한다.
        void release_all();

        // `WM_HOTKEY`의 `wParam`이 가리키는 id다. 장부에 없는 번호는 빈 값이다.
        [[nodiscard]] std::optional<std::u8string> id_of(WPARAM number) const
        {
            return sync_.id_of(static_cast<std::uintptr_t>(number));
        }

    private:
        void execute(const hotkey_call& call, std::vector<hotkey_report>& reports);

        HWND window_ { nullptr };
        hotkey_api api_ {};
        hotkey_sync sync_ {};
    };
}
