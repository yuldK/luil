#include "win32/tray_icon_win32.h"

#include "ui/access_key.h"
#include "win32/utf8.h"
#include "win32/win32_error.h"
#include "win32/window_messages.h"

#include <shellscalingapi.h>
#include <windowsx.h>

#include <cwchar>

namespace luil::win32 {
    namespace {
        // `scoped_tray_shell`이 세운 셸 호출이다. UI thread의 창 생성과 test만 만진다.
        [[nodiscard]] tray_shell_function& tray_shell_override()
        {
            static tray_shell_function shell {};
            return shell;
        }

        [[nodiscard]] tray_status status_of(const tray_sync_state state) noexcept
        {
            switch (state)
            {
            case tray_sync_state::waiting:
                return tray_status::waiting;
            case tray_sync_state::shown:
                return tray_status::shown;
            case tray_sync_state::failed:
                return tray_status::failed;
            case tray_sync_state::off:
                break;
            }
            return tray_status::off;
        }

        // 셸이 시간 안에 답하지 않았는가다. 셸은 마지막 오류를 Win32 코드가 아니라 HRESULT 꼴로 남기기도 해서
        // (실측: 없는 아이콘을 지우면 0x80004005) 두 꼴을 다 받는다.
        [[nodiscard]] bool timed_out(const DWORD error) noexcept
        {
            return error == ERROR_TIMEOUT
                || error == static_cast<DWORD>(HRESULT_FROM_WIN32(ERROR_TIMEOUT))
            ;
        }

        // 작업 표시줄이 밝은 테마인가다. 앱 모드(`AppsUseLightTheme`)와 따로 정해진다. 값이 없으면 어둡다.
        [[nodiscard]] bool read_light_taskbar() noexcept
        {
            DWORD value { 0 };
            DWORD size { sizeof(value) };
            const LSTATUS status {
                RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size),
            };
            return status == ERROR_SUCCESS && value != 0;
        }

        [[nodiscard]] bool read_high_contrast() noexcept
        {
            HIGHCONTRASTW high_contrast {};
            high_contrast.cbSize = sizeof(high_contrast);
            return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high_contrast), &high_contrast, 0) != FALSE && (high_contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        }

        // 알림 영역은 주 작업 표시줄에 있다. 주 창은 숨었거나 다른 모니터에 있을 수 있어 주 모니터의 배율을 읽는다.
        [[nodiscard]] int read_icon_size() noexcept
        {
            const POINT origin {};
            UINT dpi_x { 96 };
            UINT dpi_y { 96 };
            if (FAILED(GetDpiForMonitor(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)))
                dpi_x = 96;
            const int size { GetSystemMetricsForDpi(SM_CXSMICON, dpi_x) };
            return size > 0 ? size : 16;
        }

        void append_menu_items(const HMENU menu, const std::vector<menu_item_config>& items, std::vector<std::u8string>& keys)
        {
            UINT position { 0 };
            for (const menu_item_config& item : items)
            {
                if (item.separator_above && position > 0)
                {
                    MENUITEMINFOW separator {};
                    separator.cbSize = sizeof(separator);
                    separator.fMask = MIIM_FTYPE;
                    separator.fType = MFT_SEPARATOR;
                    if (InsertMenuItemW(menu, position, TRUE, &separator) != FALSE)
                        ++position;
                }
                keys.push_back(item.key);
                std::wstring text { tray_menu_text(item.label, item.shortcut_text, item.access_key) };
                MENUITEMINFOW information {};
                information.cbSize = sizeof(information);
                information.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_STRING | MIIM_ID;
                information.fType = MFT_STRING | (item.kind == menu_item_kind::radio ? MFT_RADIOCHECK : 0u);
                information.fState = (item.enabled ? MFS_ENABLED : MFS_GRAYED) | (item.kind != menu_item_kind::action && item.checked ? MFS_CHECKED : 0u);
                information.wID = static_cast<UINT>(keys.size());
                information.dwTypeData = text.data();
                HMENU submenu { nullptr };
                if (item.children.empty() == false)
                {
                    submenu = CreatePopupMenu();
                    if (submenu != nullptr)
                    {
                        append_menu_items(submenu, item.children, keys);
                        information.fMask |= MIIM_SUBMENU;
                        information.hSubMenu = submenu;
                    }
                }
                if (InsertMenuItemW(menu, position, TRUE, &information) != FALSE)
                    ++position;
                else if (submenu != nullptr)
                    static_cast<void>(DestroyMenu(submenu));
            }
        }
    }

    tray_shell_result call_tray_shell(const DWORD message, const NOTIFYICONDATAW& data) noexcept
    {
        // 셸 함수는 구조체를 고치지 않지만 const가 아닌 포인터를 받는다.
        NOTIFYICONDATAW copy { data };
        SetLastError(ERROR_SUCCESS);
        const BOOL ok { Shell_NotifyIconW(message, &copy) };
        return tray_shell_result { .ok = ok != FALSE, .error = ok != FALSE ? 0 : GetLastError() };
    }

    tray_shell_function tray_shell_for_new_windows()
    {
        if (const tray_shell_function& shell { tray_shell_override() }; shell != nullptr)
            return shell;
        return [](const DWORD message, const NOTIFYICONDATAW& data) { return call_tray_shell(message, data); };
    }

    scoped_tray_shell::scoped_tray_shell(tray_shell_function shell)
        : previous_ { std::exchange(tray_shell_override(), std::move(shell)) }
    {
    }

    scoped_tray_shell::~scoped_tray_shell()
    {
        tray_shell_override() = std::move(previous_);
    }

    std::wstring tray_tooltip_text(const std::u8string_view tooltip, const std::u8string_view title, std::u8string& error)
    {
        std::wstring text {};
        if (tooltip.empty() == false)
        {
            if (auto converted { utf8_to_utf16(tooltip) }; converted.value.has_value())
                text = std::move(*converted.value);
            else
                error = u8"The tray tooltip is not valid UTF-8, so the window title is shown instead.";
        }
        if (text.empty())
        {
            if (auto converted { utf8_to_utf16(title) }; converted.value.has_value())
                text = std::move(*converted.value);
        }
        if (text.empty())
            text = L"luil";
        if (text.size() > tray_tooltip_limit)
        {
            // 자른 끝이 서로게이트 쌍의 앞쪽이면 그것까지 뺀다. 반쪽 글자는 셸이 깨진 상자로 그린다.
            std::size_t length { tray_tooltip_limit };
            if (IS_HIGH_SURROGATE(text[length - 1]))
                --length;
            text.resize(length);
        }
        return text;
    }

    std::optional<tray_icon_event> tray_event_of(const WPARAM word_parameter, const LPARAM long_parameter) noexcept
    {
        if (HIWORD(long_parameter) != tray_icon_id)
            return std::nullopt;
        tray_icon_event event {};
        event.point = screen_point { GET_X_LPARAM(word_parameter), GET_Y_LPARAM(word_parameter) };
        switch (LOWORD(long_parameter))
        {
        case NIN_SELECT:
            event.action = tray_icon_action::select;
            return event;
        case NIN_KEYSELECT:
            event.action = tray_icon_action::select;
            event.keyboard = true;
            return event;
        case WM_LBUTTONDBLCLK:
            event.action = tray_icon_action::double_click;
            return event;
        case WM_CONTEXTMENU:
            event.action = tray_icon_action::context;
            return event;
        default:
            return std::nullopt;
        }
    }

    bool tray_keyselect_gate::pass(const std::uint64_t now, const std::uint32_t window) noexcept
    {
        if (last_.has_value() && now >= *last_ && now - *last_ < window)
            return false;
        last_ = now;
        return true;
    }

    std::optional<std::u8string> tray_menu::key_of(const UINT command) const
    {
        if (command == 0 || command > keys.size())
            return std::nullopt;
        return keys[command - 1];
    }

    std::wstring tray_menu_text(const std::u8string_view label, const std::u8string_view shortcut, const char32_t access_key)
    {
        // `&`와 접근 키는 ASCII라 UTF-8 글에서 바로 끼운다. 접근 키 글자 앞의 `&` 하나만 밑줄 표시다.
        const std::optional<std::size_t> marked { access_key_offset(label, access_key) };
        const char32_t glyph { access_key_glyph(access_key) };
        std::u8string joined {};
        joined.reserve(label.size() + shortcut.size() + 8);
        for (std::size_t index = 0; index < label.size(); ++index)
        {
            if (marked == index)
                joined += u8'&';
            joined += label[index];
            if (label[index] == u8'&')
                joined += u8'&';
        }
        // 글자가 라벨에 없으면 막대의 메뉴처럼 라벨 뒤에 "(F)"를 붙이고 그 글자에 밑줄을 둔다.
        if (glyph != 0 && marked.has_value() == false)
        {
            joined += u8"(&";
            joined += static_cast<char8_t>(glyph);
            joined += u8')';
        }
        if (shortcut.empty() == false)
        {
            joined += u8'\t';
            for (const char8_t character : shortcut)
            {
                joined += character;
                if (character == u8'&')
                    joined += u8'&';
            }
        }
        auto converted { utf8_to_utf16(joined) };
        if (converted.value.has_value() == false)
            return {};
        return std::move(*converted.value);
    }

    tray_menu make_tray_menu(const menu_config& config)
    {
        tray_menu result {};
        result.menu = owned_menu { CreatePopupMenu() };
        if (result.menu.get() != nullptr)
            append_menu_items(result.menu.get(), config.items, result.keys);
        return result;
    }

    tray_appearance read_tray_appearance() noexcept
    {
        return tray_appearance {
            .high_contrast = read_high_contrast()
            , .light_taskbar = read_light_taskbar()
            , .icon_size = read_icon_size()
        };
    }

    owned_icon load_window_icon(const HINSTANCE instance, const int icon_resource, const int size) noexcept
    {
        if (icon_resource != 0)
        {
            if (const HANDLE loaded { LoadImageW(instance, MAKEINTRESOURCEW(icon_resource), IMAGE_ICON, size, size, 0) }; loaded != nullptr)
                return owned_icon { static_cast<HICON>(loaded) };
        }
        const HICON shared { LoadIconW(nullptr, IDI_APPLICATION) };
        if (shared == nullptr)
            return {};
        return owned_icon { static_cast<HICON>(CopyImage(shared, IMAGE_ICON, size, size, 0)) };
    }

    void tray_icon_host::configure(const HWND window, const HINSTANCE instance, const int icon_resource, std::u8string title, tray_shell_function shell)
    {
        window_ = window;
        instance_ = instance;
        icon_resource_ = icon_resource;
        title_ = std::move(title);
        shell_ = std::move(shell);
        appearance_ = read_tray_appearance();
    }

    std::optional<tray_report> tray_icon_host::apply(const std::optional<tray_icon>& wanted)
    {
        if (window_ == nullptr || shell_ == nullptr)
            return std::nullopt;
        std::optional<tray_call> call { sync_.update(wanted, appearance_) };
        if (call.has_value())
        {
            error_.clear();
            while (call.has_value())
            {
                const tray_call_outcome outcome { execute(*call) };
                call = sync_.completed(*call, outcome);
            }
        }

        tray_report report { .status = status_of(sync_.state()) };
        // 기다리는 동안의 실패(더하기 실패, 시간 초과 뒤의 떠 보기)는 `waiting`이 말한다. 그 밖의 실패만 까닭을 싣는다.
        if (error_.empty() == false && report.status != tray_status::waiting)
        {
            report.status = tray_status::failed;
            report.error = error_;
        }
        if (report == reported_)
            return std::nullopt;
        reported_ = report;
        return report;
    }

    std::optional<tray_icon_event> tray_icon_host::receive(const WPARAM word_parameter, const LPARAM long_parameter)
    {
        std::optional<tray_icon_event> event { tray_event_of(word_parameter, long_parameter) };
        if (event.has_value() && event->keyboard && gate_.pass(GetTickCount64(), GetDoubleClickTime()) == false)
            return std::nullopt;
        return event;
    }

    NOTIFYICONDATAW tray_icon_host::base_data() const noexcept
    {
        NOTIFYICONDATAW data {};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = tray_icon_id;
        data.uCallbackMessage = tray_callback_message;
        return data;
    }

    owned_icon tray_icon_host::make_call_icon(const tray_call& call)
    {
        if (call.image.valid())
        {
            std::u8string error {};
            owned_icon icon { make_icon(call.image, call.icon_size, error) };
            if (icon.valid() == false && error_.empty())
                error_ = error;
            return icon;
        }
        owned_icon icon { load_window_icon(instance_, icon_resource_, call.icon_size) };
        if (icon.valid() == false && error_.empty())
            error_ = u8"Cannot load the window icon for the tray.";
        return icon;
    }

    void tray_icon_host::fail(const std::u8string_view what, const DWORD error)
    {
        if (error_.empty())
            error_ = make_hresult_error(what, HRESULT_FROM_WIN32(error));
    }

    tray_call_outcome tray_icon_host::execute(const tray_call& call)
    {
        NOTIFYICONDATAW data { base_data() };
        if (call.kind == tray_call_kind::remove)
        {
            const tray_shell_result result { shell_(NIM_DELETE, data) };
            if (result.ok == false)
            {
                fail(u8"Failed to delete the tray icon.", result.error);
                return tray_call_outcome::failed;
            }
            icon_.reset();
            return tray_call_outcome::succeeded;
        }

        const bool adding { call.kind == tray_call_kind::add };
        owned_icon next {};
        if (call.icon)
        {
            next = make_call_icon(call);
            // 더하는 아이콘에는 그림이 있어야 한다. 앱의 그림을 옮기지 못했으면 창 아이콘으로 세우고 실패는 보고가 말한다.
            if (next.valid() == false && adding)
                next = load_window_icon(instance_, icon_resource_, call.icon_size);
            if (next.valid())
            {
                data.uFlags |= NIF_ICON;
                data.hIcon = next.get();
            }
        }
        if (call.tooltip)
        {
            const std::wstring tip { tray_tooltip_text(call.text, title_, error_) };
            static_cast<void>(wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE));
            data.uFlags |= NIF_TIP;
        }
        // NOTIFYICON_VERSION_4는 이 표지가 없으면 표준 툴팁을 띄우지 않는다. 바꾸기에서 빠지면 툴팁이 꺼질 수 있어 늘 싣는다.
        data.uFlags |= NIF_SHOWTIP;
        if (adding)
            data.uFlags |= NIF_MESSAGE;
        // 그림만 바꾸려다 변환이 실패했다. 걸린 그림을 그대로 둔다.
        if (adding == false && (data.uFlags & (NIF_ICON | NIF_TIP)) == 0)
            return tray_call_outcome::failed;

        const tray_shell_result result { shell_(adding ? NIM_ADD : NIM_MODIFY, data) };
        if (result.ok == false)
        {
            // 더하기 실패는 탐색기가 아직 없거나 바쁜 것이라 `waiting`이 말한다. 까닭은 바꾸기 실패만 적는다.
            if (adding == false)
                fail(u8"Failed to modify the tray icon.", result.error);
            return timed_out(result.error) ? tray_call_outcome::timed_out : tray_call_outcome::failed;
        }
        if (next.valid())
            icon_ = std::move(next);
        if (adding)
        {
            NOTIFYICONDATAW version { base_data() };
            version.uVersion = NOTIFYICON_VERSION_4;
            if (const tray_shell_result set { shell_(NIM_SETVERSION, version) }; set.ok == false)
                fail(u8"Failed to set the tray callback version.", set.error);
        }
        return tray_call_outcome::succeeded;
    }
}
