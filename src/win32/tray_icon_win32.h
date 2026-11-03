#pragma once

#include "host/tray_sync.h"
#include "luil/app/tray_icon.h"
#include "luil/ui/menu_element.h"
#include "luil/win32/win32_window.h"
#include "win32/icon_from_image.h"

#include <windows.h>

#include <shellapi.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace luil::win32 {
    // 셸에 알리는 아이콘 번호다. 한 앱에 아이콘이 하나라 1로 고정한다 (docs/tray-icon-design.md 결정 3·6).
    inline constexpr UINT tray_icon_id { 1 };
    // 툴팁 칸(`szTip`)에 넣는 UTF-16 코드 단위의 상한이다. 끝 NUL을 넣어 128칸이다.
    inline constexpr std::size_t tray_tooltip_limit { 127 };

    // 셸 호출 하나의 결과다. 실패면 그 순간의 `GetLastError()`를 싣는다 (시간 초과를 가른다).
    struct tray_shell_result
    {
        bool ok { false };
        DWORD error { 0 };
    };

    // `Shell_NotifyIconW`를 대신하는 seam이다. test가 가짜를 꽂아 실제 알림 영역을 만지지 않는다.
    using tray_shell_function = std::function<tray_shell_result(DWORD message, const NOTIFYICONDATAW& data)>;

    // 실제 `Shell_NotifyIconW`를 부른다.
    [[nodiscard]] tray_shell_result call_tray_shell(DWORD message, const NOTIFYICONDATAW& data) noexcept;

    // 새로 만드는 주 창이 쓸 셸 호출이다. 기본은 실제 셸이고, `scoped_tray_shell`이 살아 있는 동안은 그것이다.
    //  - 주 창은 내부 구현이라 test가 seam을 넘길 자리가 없다. ctest가 실제 알림 영역에 아이콘을 세우지 않도록
    //    프로세스 전체에서 갈아 끼우는 길을 둔다 (`post_notification_event`와 같은 비공개 test 길이다).
    [[nodiscard]] tray_shell_function tray_shell_for_new_windows();

    // 살아 있는 동안 새 주 창의 셸 호출을 `shell`로 바꾼다. 창을 만들기 전에 세우고 창이 사라진 뒤에 거둔다.
    class scoped_tray_shell
    {
    public:
        explicit scoped_tray_shell(tray_shell_function shell);
        scoped_tray_shell(const scoped_tray_shell&) = delete;
        scoped_tray_shell& operator=(const scoped_tray_shell&) = delete;
        ~scoped_tray_shell();

    private:
        tray_shell_function previous_ {};
    };

    // 툴팁 칸에 넣을 글이다. 비면 `title`, 그것도 비면 `luil`이다.
    //  - 127 UTF-16 코드 단위에서 자르되 서로게이트 쌍을 가르지 않는다.
    //  - `tooltip`이 올바른 UTF-8이 아니면 `title`로 물러서고 `error`에 까닭을 적는다.
    [[nodiscard]] std::wstring tray_tooltip_text(std::u8string_view tooltip, std::u8string_view title, std::u8string& error);

    // 셸 콜백(NOTIFYICON_VERSION_4)을 사건으로 푼다. 다른 아이콘 번호와 모르는 사건은 빈 값이다.
    //  - `WM_CONTEXTMENU`는 `context`다. 메뉴를 띄울지는 부른 쪽이 그 순간의 frame으로 정한다.
    [[nodiscard]] std::optional<tray_icon_event> tray_event_of(WPARAM word_parameter, LPARAM long_parameter) noexcept;

    // Enter 한 번에 `NIN_KEYSELECT`가 두 번 온다고 알려져 있다 (실측 전). 두 번 누름 시간 안의 두 번째를 버린다.
    class tray_keyselect_gate
    {
    public:
        // `now`(밀리초)에 온 키보드 고름을 내보낼 것인가다. 버린 것은 기준 시각을 옮기지 않는다.
        [[nodiscard]] bool pass(std::uint64_t now, std::uint32_t window) noexcept;

    private:
        std::optional<std::uint64_t> last_ {};
    };

    // `HMENU` 하나를 쥐고 없어질 때 `DestroyMenu`로 거둔다 (하위 메뉴도 함께 사라진다).
    class owned_menu
    {
    public:
        owned_menu() noexcept = default;
        explicit owned_menu(const HMENU menu) noexcept
            : menu_ { menu }
        {
        }
        owned_menu(const owned_menu&) = delete;
        owned_menu& operator=(const owned_menu&) = delete;
        owned_menu(owned_menu&& other) noexcept
            : menu_ { std::exchange(other.menu_, nullptr) }
        {
        }
        owned_menu& operator=(owned_menu&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                menu_ = std::exchange(other.menu_, nullptr);
            }
            return *this;
        }
        ~owned_menu()
        {
            reset();
        }

        [[nodiscard]] HMENU get() const noexcept
        {
            return menu_;
        }

        void reset() noexcept
        {
            if (menu_ != nullptr)
                static_cast<void>(DestroyMenu(menu_));
            menu_ = nullptr;
        }

    private:
        HMENU menu_ { nullptr };
    };

    // 시스템 메뉴로 옮긴 `menu_config`다. 명령 번호는 하위 메뉴까지 펼친 항목 순서 + 1이다.
    struct tray_menu
    {
        owned_menu menu {};
        std::vector<std::u8string> keys {};

        // 고른 명령 번호의 키다. 0(고르지 않음)이나 모르는 번호는 빈 값이다.
        [[nodiscard]] std::optional<std::u8string> key_of(UINT command) const;
    };

    // 메뉴 항목 하나의 글이다. `&`는 `&&`로 바꿔 밑줄 글자로 읽히지 않게 하고, 단축키 글은 탭 뒤에 둔다.
    [[nodiscard]] std::wstring tray_menu_text(std::u8string_view label, std::u8string_view shortcut);

    // `menu_config::items`를 `CreatePopupMenu`·`InsertMenuItemW`로 옮긴다 (docs/tray-icon-design.md의 "메뉴").
    //  - 앞 항목이 있을 때만 `separator_above`가 구분선이다. 맨 위 구분선은 시스템 메뉴에서 빈 줄로만 보인다.
    //  - 항목 아이콘 글리프와 열림 상태 칸은 쓰지 않는다.
    [[nodiscard]] tray_menu make_tray_menu(const menu_config& config);

    // 지금 셸 환경을 읽는다: 고대비, 작업 표시줄 밝기(`SystemUsesLightTheme`), 주 모니터 배율의 작은 아이콘 크기.
    [[nodiscard]] tray_appearance read_tray_appearance() noexcept;

    // 창 아이콘 리소스를 `size`×`size`로 읽는다. 0이거나 읽지 못하면 시스템 기본(`IDI_APPLICATION`)의 사본이다.
    //  - 시스템 기본은 공유 아이콘이라 그대로 쥐면 거둘 때 `DestroyIcon`이 공유 핸들을 지운다. 사본을 지어 쥔다.
    [[nodiscard]] owned_icon load_window_icon(HINSTANCE instance, int icon_resource, int size) noexcept;

    // 주 창 하나의 알림 영역 아이콘이다. UI thread에서만 부른다.
    //  - 어떤 호출을 할지는 core의 `tray_sync`가 고른다. 여기는 `NOTIFYICONDATAW`를 짓고 셸에 내고 결과를 되돌린다.
    //  - 그림 핸들은 교체·지우기·이 객체가 사라질 때까지 쥔다.
    class tray_icon_host
    {
    public:
        tray_icon_host() = default;
        tray_icon_host(const tray_icon_host&) = delete;
        tray_icon_host& operator=(const tray_icon_host&) = delete;

        // 콜백을 받을 창과 창 아이콘·제목(물러설 자리), 셸 호출을 정한다. 환경도 이때 처음 읽는다.
        void configure(HWND window, HINSTANCE instance, int icon_resource, std::u8string title, tray_shell_function shell);

        // 셸 환경을 바꾼다 (설정·배율 변경 뒤 다시 읽은 값, test가 정한 값). 다음 `apply`가 고른 그림·크기를 견준다.
        void set_appearance(const tray_appearance& appearance) noexcept
        {
            appearance_ = appearance;
        }

        [[nodiscard]] const tray_appearance& appearance() const noexcept
        {
            return appearance_;
        }

        // 바라는 아이콘을 셸에 맞춘다. 보고할 상태가 바뀌었으면 그 보고다.
        [[nodiscard]] std::optional<tray_report> apply(const std::optional<tray_icon>& wanted);

        // 탐색기가 다시 떴다 (`TaskbarCreated`). 다음 `apply`가 처음부터 더한다.
        void shell_restarted() noexcept
        {
            sync_.shell_restarted();
        }

        // 걸린 아이콘을 지운다 (창이 끝날 때). 유령 아이콘이 마우스가 지나갈 때까지 남지 않게 한다.
        [[nodiscard]] std::optional<tray_report> remove()
        {
            return apply(std::nullopt);
        }

        // 셸 콜백을 사건으로 푼다. 겹친 키보드 고름은 버린다.
        [[nodiscard]] std::optional<tray_icon_event> receive(WPARAM word_parameter, LPARAM long_parameter);

        // 지금 셸에 건 그림 핸들이다 (test).
        [[nodiscard]] HICON icon() const noexcept
        {
            return icon_.get();
        }

    private:
        [[nodiscard]] tray_call_outcome execute(const tray_call& call);
        [[nodiscard]] NOTIFYICONDATAW base_data() const noexcept;
        // 이 호출에 실을 새 그림이다. 변환이 실패하면 빈 값과 `error_`이고, 부른 쪽은 걸린 그림을 그대로 둔다.
        [[nodiscard]] owned_icon make_call_icon(const tray_call& call);
        void fail(std::u8string_view what, DWORD error);

        HWND window_ { nullptr };
        HINSTANCE instance_ { nullptr };
        int icon_resource_ { 0 };
        std::u8string title_ {};
        tray_shell_function shell_ {};
        tray_sync sync_ {};
        tray_appearance appearance_ {};
        owned_icon icon_ {};
        // 마지막 호출 묶음의 첫 실패다. 다음 호출 묶음까지 남는다.
        std::u8string error_ {};
        tray_report reported_ {};
        tray_keyselect_gate gate_ {};
    };
}
