#include "luil/app/app_host.h"
#include "luil/ui/image_element.h"
#include "luil/ui/menu_element.h"
#include "luil/win32/win32_window.h"
#include "win32/tray_icon_win32.h"
#include "win32/window_messages.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <shellapi.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
    using luil::tray_icon;
    using luil::tray_icon_action;
    using luil::tray_icon_event;
    using luil::win32::tray_icon_host;
    using luil::win32::tray_report;
    using luil::win32::tray_shell_result;
    using luil::win32::tray_status;

    // 셸의 대역이다. 받은 호출을 순서대로 적고, 시험이 정한 결과를 앞에서부터 돌려준다 (다 쓰면 성공이다).
    struct fake_shell
    {
        struct call
        {
            DWORD message { 0 };
            NOTIFYICONDATAW data {};
            // 받은 순간의 그림 너비다. 핸들은 셸이 보는 동안만 산다.
            int icon_width { 0 };
        };

        std::vector<call> calls {};
        std::vector<tray_shell_result> results {};

        [[nodiscard]] luil::win32::tray_shell_function function()
        {
            return [this](const DWORD message, const NOTIFYICONDATAW& data) {
                call received { .message = message, .data = data };
                if ((data.uFlags & NIF_ICON) != 0 && data.hIcon != nullptr)
                {
                    ICONINFO information {};
                    if (GetIconInfo(data.hIcon, &information) != FALSE)
                    {
                        BITMAP bitmap {};
                        if (GetObjectW(information.hbmColor != nullptr ? information.hbmColor : information.hbmMask, sizeof(bitmap), &bitmap) != 0)
                            received.icon_width = bitmap.bmWidth;
                        if (information.hbmColor != nullptr)
                            DeleteObject(information.hbmColor);
                        if (information.hbmMask != nullptr)
                            DeleteObject(information.hbmMask);
                    }
                }
                calls.push_back(received);
                if (results.empty())
                    return tray_shell_result { .ok = true };
                const tray_shell_result result { results.front() };
                results.erase(results.begin());
                return result;
            };
        }
    };

    // 메시지를 받지 않는 보이지 않는 창이다. 셸 대역은 창을 보지 않고 값만 적는다.
    class plain_window
    {
    public:
        plain_window()
            : window_ { CreateWindowExW(0, L"STATIC", L"tray test", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) }
        {
        }
        plain_window(const plain_window&) = delete;
        plain_window& operator=(const plain_window&) = delete;
        ~plain_window()
        {
            if (window_ != nullptr)
                DestroyWindow(window_);
        }

        [[nodiscard]] HWND get() const noexcept
        {
            return window_;
        }

    private:
        HWND window_ { nullptr };
    };

    // 1x1이 아닌 그림이 있어야 크기 맞추기가 보인다. 만들 때마다 새 몸통이다.
    [[nodiscard]] luil::ui_image make_square(const std::uint8_t red)
    {
        constexpr int size { 8 };
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4, 255);
        for (std::size_t index = 0; index < pixels.size(); index += 4)
            pixels[index] = red;
        return luil::make_rgba_image(size, size, pixels);
    }

    [[nodiscard]] tray_icon make_tray(const luil::ui_image& image, std::u8string tooltip = u8"tray test")
    {
        tray_icon icon {};
        icon.icon = image;
        icon.tooltip = std::move(tooltip);
        return icon;
    }

    [[nodiscard]] std::wstring tip_of(const NOTIFYICONDATAW& data)
    {
        return std::wstring { data.szTip };
    }

    [[nodiscard]] std::string narrow(const std::u8string_view text)
    {
        return std::string(text.begin(), text.end());
    }

    void configure(tray_icon_host& host, const HWND window, fake_shell& shell, const int icon_size = 20)
    {
        host.configure(window, GetModuleHandleW(nullptr), 0, u8"window title", shell.function());
        host.set_appearance(luil::tray_appearance { .icon_size = icon_size });
    }

    // 메뉴의 한 칸을 되읽은 것이다.
    struct menu_item
    {
        UINT type { 0 };
        UINT state { 0 };
        UINT id { 0 };
        HMENU submenu { nullptr };
        std::wstring text {};
    };

    [[nodiscard]] menu_item read_menu_item(const HMENU menu, const UINT position)
    {
        std::array<wchar_t, 256> buffer {};
        MENUITEMINFOW information {};
        information.cbSize = sizeof(information);
        information.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU | MIIM_STRING;
        information.dwTypeData = buffer.data();
        information.cch = static_cast<UINT>(buffer.size());
        REQUIRE(GetMenuItemInfoW(menu, position, TRUE, &information) != FALSE);
        return menu_item {
            .type = information.fType
            , .state = information.fState
            , .id = information.wID
            , .submenu = information.hSubMenu
            , .text = (information.fType & MFT_SEPARATOR) != 0 ? std::wstring {} : std::wstring { buffer.data() }
        };
    }

    [[nodiscard]] int width_of(const HICON icon)
    {
        ICONINFO information {};
        if (GetIconInfo(icon, &information) == FALSE)
            return 0;
        BITMAP bitmap {};
        const int width { GetObjectW(information.hbmColor != nullptr ? information.hbmColor : information.hbmMask, sizeof(bitmap), &bitmap) != 0 ? bitmap.bmWidth : 0 };
        if (information.hbmColor != nullptr)
            DeleteObject(information.hbmColor);
        if (information.hbmMask != nullptr)
            DeleteObject(information.hbmMask);
        return width;
    }
}

TEST_CASE("Adding a tray icon fills the notify data and sets version 4", "[win32][tray]")
{
    const plain_window window {};
    REQUIRE(window.get() != nullptr);
    fake_shell shell {};
    tray_icon_host host {};
    configure(host, window.get(), shell, 24);

    const std::optional<tray_report> report { host.apply(make_tray(make_square(10), u8"hello tray")) };
    REQUIRE(report.has_value());
    REQUIRE(report->status == tray_status::shown);
    REQUIRE(report->error.empty());

    REQUIRE(shell.calls.size() == 2);
    const NOTIFYICONDATAW& add { shell.calls[0].data };
    REQUIRE(shell.calls[0].message == NIM_ADD);
    REQUIRE(add.cbSize == sizeof(NOTIFYICONDATAW));
    REQUIRE(add.hWnd == window.get());
    REQUIRE(add.uID == luil::win32::tray_icon_id);
    REQUIRE(add.uFlags == (NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP));
    REQUIRE(add.uCallbackMessage == luil::win32::tray_callback_message);
    REQUIRE(add.hIcon != nullptr);
    REQUIRE(shell.calls[0].icon_width == 24);
    REQUIRE(tip_of(add) == L"hello tray");
    REQUIRE((add.uFlags & NIF_GUID) == 0);

    const NOTIFYICONDATAW& version { shell.calls[1].data };
    REQUIRE(shell.calls[1].message == NIM_SETVERSION);
    REQUIRE(version.hWnd == window.get());
    REQUIRE(version.uID == luil::win32::tray_icon_id);
    REQUIRE(version.uVersion == NOTIFYICON_VERSION_4);
    REQUIRE(host.icon() != nullptr);

    // 같은 요청은 아무것도 내지 않고 보고도 없다.
    REQUIRE(host.apply(make_tray(make_square(10), u8"hello tray")).has_value() == false);
}

TEST_CASE("A tray modify carries only the changed field and the delete releases the picture", "[win32][tray]")
{
    const plain_window window {};
    fake_shell shell {};
    tray_icon_host host {};
    configure(host, window.get(), shell);
    const luil::ui_image image { make_square(10) };
    static_cast<void>(host.apply(make_tray(image)));
    shell.calls.clear();

    REQUIRE(host.apply(make_tray(image, u8"changed")).has_value() == false);
    REQUIRE(shell.calls.size() == 1);
    REQUIRE(shell.calls[0].message == NIM_MODIFY);
    REQUIRE(shell.calls[0].data.uFlags == (NIF_TIP | NIF_SHOWTIP));
    REQUIRE(tip_of(shell.calls[0].data) == L"changed");

    shell.calls.clear();
    const HICON before { host.icon() };
    REQUIRE(host.apply(make_tray(make_square(200), u8"changed")).has_value() == false);
    REQUIRE(shell.calls.size() == 1);
    REQUIRE(shell.calls[0].data.uFlags == (NIF_ICON | NIF_SHOWTIP));
    REQUIRE(host.icon() != before);

    shell.calls.clear();
    const std::optional<tray_report> removed { host.remove() };
    REQUIRE(removed.has_value());
    REQUIRE(removed->status == tray_status::off);
    REQUIRE(shell.calls.size() == 1);
    REQUIRE(shell.calls[0].message == NIM_DELETE);
    REQUIRE(shell.calls[0].data.uID == luil::win32::tray_icon_id);
    REQUIRE(host.icon() == nullptr);
    // 이미 지웠으면 할 일이 없다 (닫기와 파괴가 둘 다 부른다).
    REQUIRE(host.remove().has_value() == false);
    REQUIRE(shell.calls.size() == 1);
}

TEST_CASE("A tray add without an explorer waits and a restart adds again", "[win32][tray]")
{
    const plain_window window {};
    fake_shell shell {};
    shell.results.push_back(tray_shell_result { .ok = false, .error = ERROR_FILE_NOT_FOUND });
    tray_icon_host host {};
    configure(host, window.get(), shell);

    const std::optional<tray_report> waiting { host.apply(make_tray(make_square(10))) };
    REQUIRE(waiting.has_value());
    REQUIRE(waiting->status == tray_status::waiting);
    REQUIRE(waiting->error.empty());
    REQUIRE(shell.calls.size() == 1);

    host.shell_restarted();
    const std::optional<tray_report> shown { host.apply(make_tray(make_square(10))) };
    REQUIRE(shown.has_value());
    REQUIRE(shown->status == tray_status::shown);
    REQUIRE(shell.calls.size() == 3);
    REQUIRE(shell.calls[1].message == NIM_ADD);
    REQUIRE(shell.calls[2].message == NIM_SETVERSION);
}

TEST_CASE("A timed out tray add is probed with a modify", "[win32][tray]")
{
    const plain_window window {};
    fake_shell shell {};
    // 셸은 마지막 오류를 Win32 코드로도, HRESULT 꼴로도 남긴다. 둘 다 시간 초과다.
    DWORD timeout { ERROR_TIMEOUT };
    SECTION("A Win32 code")
    {
        timeout = ERROR_TIMEOUT;
    }
    SECTION("An HRESULT")
    {
        timeout = static_cast<DWORD>(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    }
    shell.results.push_back(tray_shell_result { .ok = false, .error = timeout });
    tray_icon_host host {};
    configure(host, window.get(), shell);

    const std::optional<tray_report> report { host.apply(make_tray(make_square(10), u8"busy shell")) };
    REQUIRE(report.has_value());
    REQUIRE(report->status == tray_status::shown);
    REQUIRE(shell.calls.size() == 2);
    REQUIRE(shell.calls[0].message == NIM_ADD);
    REQUIRE(shell.calls[1].message == NIM_MODIFY);
    REQUIRE(shell.calls[1].data.uFlags == (NIF_ICON | NIF_TIP | NIF_SHOWTIP));
    REQUIRE(tip_of(shell.calls[1].data) == L"busy shell");
}

TEST_CASE("A failed tray modify is reported with its code and not retried", "[win32][tray]")
{
    const plain_window window {};
    fake_shell shell {};
    tray_icon_host host {};
    configure(host, window.get(), shell);
    const luil::ui_image image { make_square(10) };
    static_cast<void>(host.apply(make_tray(image)));
    shell.calls.clear();
    shell.results.push_back(tray_shell_result { .ok = false, .error = ERROR_ACCESS_DENIED });

    const std::optional<tray_report> failed { host.apply(make_tray(image, u8"changed")) };
    REQUIRE(failed.has_value());
    REQUIRE(failed->status == tray_status::failed);
    INFO(narrow(failed->error));
    REQUIRE(failed->error.find(u8"0x80070005") != std::u8string::npos);
    REQUIRE(host.apply(make_tray(image, u8"changed")).has_value() == false);
    REQUIRE(shell.calls.size() == 1);

    // 다음 변화가 성공하면 실패를 거둔다.
    const std::optional<tray_report> recovered { host.apply(make_tray(image, u8"again")) };
    REQUIRE(recovered.has_value());
    REQUIRE(recovered->status == tray_status::shown);
    REQUIRE(recovered->error.empty());
}

TEST_CASE("The tray tooltip is cut at 127 code units without splitting a surrogate pair", "[win32][tray]")
{
    std::u8string error {};
    REQUIRE(luil::win32::tray_tooltip_text(u8"plain", u8"title", error) == L"plain");
    REQUIRE(error.empty());
    REQUIRE(luil::win32::tray_tooltip_text(u8"", u8"title", error) == L"title");
    REQUIRE(luil::win32::tray_tooltip_text(u8"", u8"", error) == L"luil");
    REQUIRE(error.empty());

    const std::u8string exact(127, u8'a');
    REQUIRE(luil::win32::tray_tooltip_text(exact, u8"title", error).size() == 127);
    const std::u8string longer(200, u8'b');
    REQUIRE(luil::win32::tray_tooltip_text(longer, u8"title", error) == std::wstring(127, L'b'));

    // 126글자 뒤의 이모지는 두 코드 단위다. 127에서 자르면 반쪽이 남으므로 이모지째 뺀다.
    const std::u8string split { std::u8string(126, u8'c') + u8"\U0001F600" + u8"tail" };
    const std::wstring cut { luil::win32::tray_tooltip_text(split, u8"title", error) };
    REQUIRE(cut == std::wstring(126, L'c'));
    // 125글자 뒤면 쌍이 통째로 들어간다.
    const std::u8string fits { std::u8string(125, u8'd') + u8"\U0001F600" + u8"tail" };
    const std::wstring kept { luil::win32::tray_tooltip_text(fits, u8"title", error) };
    REQUIRE(kept.size() == 127);
    REQUIRE(IS_LOW_SURROGATE(kept.back()));
    REQUIRE(error.empty());
}

TEST_CASE("An invalid tray tooltip falls back to the window title and is reported", "[win32][tray]")
{
    std::u8string error {};
    std::u8string broken { u8"bad" };
    broken.push_back(static_cast<char8_t>(0xFF));
    REQUIRE(luil::win32::tray_tooltip_text(broken, u8"window title", error) == L"window title");
    REQUIRE(error.empty() == false);

    const plain_window window {};
    fake_shell shell {};
    tray_icon_host host {};
    configure(host, window.get(), shell);
    const std::optional<tray_report> report { host.apply(make_tray(make_square(10), broken)) };
    REQUIRE(report.has_value());
    REQUIRE(report->status == tray_status::failed);
    REQUIRE(report->error.empty() == false);
    REQUIRE(tip_of(shell.calls[0].data) == L"window title");
}

TEST_CASE("An empty tray picture reads the window icon at the tray size", "[win32][tray]")
{
    const plain_window window {};
    fake_shell shell {};
    tray_icon_host host {};
    configure(host, window.get(), shell, 32);
    static_cast<void>(host.apply(tray_icon {}));
    REQUIRE(shell.calls.size() == 2);
    REQUIRE(shell.calls[0].data.hIcon != nullptr);
    REQUIRE(shell.calls[0].icon_width == 32);
    // 빈 툴팁은 창 제목이다.
    REQUIRE(tip_of(shell.calls[0].data) == L"window title");

    // 리소스가 없는 id도 시스템 기본으로 물러선다. 공유 아이콘이 아니라 사본이라 거둬도 된다.
    luil::win32::owned_icon missing { luil::win32::load_window_icon(GetModuleHandleW(nullptr), 32001, 20) };
    REQUIRE(missing.valid());
    REQUIRE(width_of(missing.get()) == 20);
    missing.reset();
    REQUIRE(LoadIconW(nullptr, IDI_APPLICATION) != nullptr);
}

TEST_CASE("Tray callbacks map to tray events", "[win32][tray]")
{
    using luil::win32::tray_event_of;
    const WPARAM point { MAKEWPARAM(static_cast<WORD>(-20), 300) };
    const auto event_for = [point](const UINT event, const UINT id = luil::win32::tray_icon_id) { return tray_event_of(point, MAKELPARAM(event, id)); };

    const std::optional<tray_icon_event> select { event_for(NIN_SELECT) };
    REQUIRE(select.has_value());
    REQUIRE(select->action == tray_icon_action::select);
    REQUIRE(select->keyboard == false);
    // 좌표는 부호가 있다. 왼쪽 모니터의 아이콘은 음수다.
    REQUIRE(select->point == luil::screen_point { -20, 300 });

    const std::optional<tray_icon_event> key { event_for(NIN_KEYSELECT) };
    REQUIRE(key.has_value());
    REQUIRE(key->action == tray_icon_action::select);
    REQUIRE(key->keyboard);

    REQUIRE(event_for(WM_LBUTTONDBLCLK)->action == tray_icon_action::double_click);
    REQUIRE(event_for(WM_CONTEXTMENU)->action == tray_icon_action::context);

    REQUIRE(event_for(WM_MOUSEMOVE).has_value() == false);
    REQUIRE(event_for(NIN_POPUPOPEN).has_value() == false);
    REQUIRE(event_for(WM_LBUTTONUP).has_value() == false);
    REQUIRE(event_for(NIN_SELECT, 2).has_value() == false);
}

TEST_CASE("A repeated keyboard select inside the double click time is dropped", "[win32][tray]")
{
    luil::win32::tray_keyselect_gate gate {};
    REQUIRE(gate.pass(1000, 500));
    REQUIRE(gate.pass(1200, 500) == false);
    // 버린 것은 기준을 옮기지 않는다.
    REQUIRE(gate.pass(1400, 500) == false);
    REQUIRE(gate.pass(1500, 500));
    REQUIRE(gate.pass(3000, 500));
}

TEST_CASE("The tray menu becomes a system menu with escaped text, states and nested items", "[win32][tray]")
{
    luil::menu_config config {};
    config.items.push_back({ .key = u8"show", .label = u8"Show & hide", .icon = U'x', .shortcut_text = u8"Ctrl+S" });
    config.items.push_back({ .key = u8"grey", .label = u8"Disabled", .enabled = false });
    config.items.push_back({ .key = u8"check", .label = u8"Check", .separator_above = true, .kind = luil::menu_item_kind::check, .checked = true });
    config.items.push_back({ .key = u8"radio", .label = u8"Radio", .kind = luil::menu_item_kind::radio, .checked = true });
    config.items.push_back({ .key = u8"unchecked", .label = u8"Off", .kind = luil::menu_item_kind::check });
    luil::menu_item_config more { .key = u8"more", .label = u8"More" };
    more.children.push_back({ .key = u8"inner", .label = u8"Inner" });
    config.items.push_back(std::move(more));
    config.items.push_back({ .key = u8"exit", .label = u8"Exit" });

    const luil::win32::tray_menu built { luil::win32::make_tray_menu(config) };
    const HMENU menu { built.menu.get() };
    REQUIRE(menu != nullptr);
    // 구분선 하나가 더해진다.
    REQUIRE(GetMenuItemCount(menu) == 8);

    const menu_item show { read_menu_item(menu, 0) };
    REQUIRE(show.text == L"Show && hide\tCtrl+S");
    REQUIRE(show.id == 1);
    REQUIRE((show.state & MFS_GRAYED) == 0);
    REQUIRE((show.state & MFS_CHECKED) == 0);

    const menu_item grey { read_menu_item(menu, 1) };
    REQUIRE((grey.state & MFS_GRAYED) != 0);
    REQUIRE(grey.id == 2);

    REQUIRE((read_menu_item(menu, 2).type & MFT_SEPARATOR) != 0);

    const menu_item check { read_menu_item(menu, 3) };
    REQUIRE((check.state & MFS_CHECKED) != 0);
    REQUIRE((check.type & MFT_RADIOCHECK) == 0);

    const menu_item radio { read_menu_item(menu, 4) };
    REQUIRE((radio.state & MFS_CHECKED) != 0);
    REQUIRE((radio.type & MFT_RADIOCHECK) != 0);

    REQUIRE((read_menu_item(menu, 5).state & MFS_CHECKED) == 0);

    const menu_item nested { read_menu_item(menu, 6) };
    REQUIRE(nested.text == L"More");
    REQUIRE(nested.submenu != nullptr);
    REQUIRE(GetMenuItemCount(nested.submenu) == 1);
    const menu_item inner { read_menu_item(nested.submenu, 0) };
    REQUIRE(inner.text == L"Inner");

    const menu_item exit { read_menu_item(menu, 7) };
    // 번호는 하위 메뉴까지 펼친 순서다: more가 6, inner가 7, exit가 8이다.
    REQUIRE(nested.id == 6);
    REQUIRE(inner.id == 7);
    REQUIRE(exit.id == 8);
    REQUIRE(built.key_of(inner.id) == std::optional<std::u8string> { u8"inner" });
    REQUIRE(built.key_of(exit.id) == std::optional<std::u8string> { u8"exit" });
    REQUIRE(built.key_of(0).has_value() == false);
    REQUIRE(built.key_of(9).has_value() == false);
}

TEST_CASE("A leading separator does not open the tray menu", "[win32][tray]")
{
    luil::menu_config config {};
    config.items.push_back({ .key = u8"first", .label = u8"First", .separator_above = true });
    const luil::win32::tray_menu built { luil::win32::make_tray_menu(config) };
    REQUIRE(GetMenuItemCount(built.menu.get()) == 1);
    REQUIRE(read_menu_item(built.menu.get(), 0).text == L"First");
}

namespace {
    constexpr const wchar_t* tray_window_class_name { L"Luil.Test.TrayCallback" };

    BOOL CALLBACK find_tray_window(const HWND window, const LPARAM parameter)
    {
        std::array<wchar_t, 256> name {};
        if (GetClassNameW(window, name.data(), static_cast<int>(name.size())) > 0 && std::wcscmp(name.data(), tray_window_class_name) == 0)
        {
            *reinterpret_cast<HWND*>(parameter) = window;
            return FALSE;
        }
        return TRUE;
    }

    // 한 번의 실행에서 본 것이다. 창 thread가 적고, 실행이 끝나고 thread를 거둔 뒤에 읽는다.
    struct tray_probe
    {
        std::vector<DWORD> messages {};
        std::vector<tray_icon_event> events {};
        std::vector<tray_report> reports {};
        std::atomic<bool> added { false };
        std::atomic<int> activations { 0 };
        std::atomic<int> handled { 0 };
    };

    // frame마다 같은 아이콘을 싣는다. `activate`는 사건을 적고 앱 메시지로 돌려준다 (logic thread에 닿는지 본다).
    class tray_driver final : public luil::logic_driver
    {
    public:
        explicit tray_driver(tray_probe& probe)
            : probe_ { probe }
            , image_ { make_square(30) }
        {
        }

        void handle(luil::app_message message) override
        {
            if (message.get<tray_icon_event>() != nullptr)
                ++probe_.handled;
            if (message.get<bool>() != nullptr)
                closed_.store(true);
        }

        [[nodiscard]] std::shared_ptr<const luil::ui_frame> make_frame() override
        {
            auto frame { std::make_shared<luil::ui_frame>() };
            frame->main_hidden = true;
            tray_icon icon { make_tray(image_, u8"tray callback") };
            icon.activate = [this](const tray_icon_event& event) {
                probe_.events.push_back(event);
                ++probe_.activations;
                return luil::make_app_action(event);
            };
            frame->tray = std::move(icon);
            return frame;
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { true };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed_.load();
        }

    private:
        tray_probe& probe_;
        luil::ui_image image_ {};
        std::atomic<bool> closed_ { false };
    };

    // 아이콘이 서면 다른 thread에서 셸인 양 콜백을 보내고, 사건이 닿으면 창을 닫는다.
    class tray_delegate final : public luil::win32::window_delegate
    {
    public:
        explicit tray_delegate(tray_probe& probe)
            : probe_ { probe }
        {
        }
        tray_delegate(const tray_delegate&) = delete;
        tray_delegate& operator=(const tray_delegate&) = delete;
        ~tray_delegate()
        {
            join();
        }

        void join()
        {
            if (poster_.joinable())
                poster_.join();
        }

        void on_started(luil::app_host&) override
        {
            EnumThreadWindows(GetCurrentThreadId(), &find_tray_window, reinterpret_cast<LPARAM>(&window_));
            if (window_ == nullptr)
                return;
            poster_ = std::thread { [this] { post(); } };
        }

        [[nodiscard]] luil::app_message make_tray_message(const tray_report& report) override
        {
            probe_.reports.push_back(report);
            return {};
        }

    private:
        template<typename condition_type>
        static void wait_for(condition_type condition)
        {
            const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 5 } };
            while (condition() == false && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
        }

        void post()
        {
            wait_for([this] { return probe_.added.load(); });
            // 다른 아이콘 번호는 사건이 아니다.
            PostMessageW(window_, luil::win32::tray_callback_message, MAKEWPARAM(5, 6), MAKELPARAM(NIN_SELECT, 2));
            PostMessageW(window_, luil::win32::tray_callback_message, MAKEWPARAM(120, 340), MAKELPARAM(NIN_SELECT, luil::win32::tray_icon_id));
            wait_for([this] { return probe_.activations.load() >= 1; });
            // 메뉴가 없으면 오른쪽 클릭은 `context` 사건이다.
            PostMessageW(window_, luil::win32::tray_callback_message, MAKEWPARAM(121, 341), MAKELPARAM(WM_CONTEXTMENU, luil::win32::tray_icon_id));
            wait_for([this] { return probe_.activations.load() >= 2 && probe_.handled.load() >= 2; });
            PostMessageW(window_, WM_CLOSE, 0, 0);
        }

        tray_probe& probe_;
        HWND window_ { nullptr };
        std::thread poster_ {};
    };
}

TEST_CASE("A tray callback on the main window reaches the frame's activate and the icon goes with the window", "[win32][window][tray]")
{
    tray_probe probe {};
    // 실제 알림 영역을 만지지 않는다. 새 주 창이 이 대역을 쓴다.
    const auto record = [&probe](const DWORD message, const NOTIFYICONDATAW&) {
        probe.messages.push_back(message);
        if (message == NIM_ADD)
            probe.added.store(true);
        return tray_shell_result { .ok = true };
    };
    const luil::win32::scoped_tray_shell shell { record };
    {
        luil::win32::enable_per_monitor_dpi_awareness();
        const luil::win32::com_sta_scope com {};
        tray_driver driver { probe };
        tray_delegate delegate { probe };
        luil::win32::window_config config {};
        config.class_name = tray_window_class_name;
        config.caption.title = u8"tray callback";
        config.renderer = luil::renderer_mode::cpu;
        REQUIRE(luil::win32::run_application_window(config, { &driver, nullptr, &delegate }) == 0);
        delegate.join();
        // 창 파괴가 남긴 WM_QUIT을 걷어낸다. 뒤 test가 오염된 큐를 보지 않게 한다.
        MSG message {};
        static_cast<void>(PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
    }

    REQUIRE(probe.events.size() == 2);
    REQUIRE(probe.events[0].action == tray_icon_action::select);
    REQUIRE(probe.events[0].point == luil::screen_point { 120, 340 });
    REQUIRE(probe.events[1].action == tray_icon_action::context);
    REQUIRE(probe.handled.load() == 2);
    REQUIRE(probe.messages.size() >= 3);
    REQUIRE(probe.messages[0] == NIM_ADD);
    REQUIRE(probe.messages[1] == NIM_SETVERSION);
    // 창이 닫히면 아이콘을 지운다.
    REQUIRE(probe.messages.back() == NIM_DELETE);
    REQUIRE(probe.reports.empty() == false);
    REQUIRE(probe.reports.front().status == tray_status::shown);
    REQUIRE(luil::current_ui_platform().capabilities.tray);
}

// 실제 셸에 아이콘을 세우고 곧바로 지운다 (docs/tray-icon-design.md의 X-9 실측). ctest는 돌리지 않는다.
//  - `luil_tests "[.tray-live]"`로 올린다. 결과와 상관없이 지우고, 지운 뒤 같은 아이콘을 다시 지우면 실패하는지 본다.
TEST_CASE("A live tray icon is added and deleted", "[.tray-live]")
{
    const plain_window window {};
    REQUIRE(window.get() != nullptr);
    tray_icon_host host {};
    host.configure(window.get(), GetModuleHandleW(nullptr), 0, u8"luil tray live", [](const DWORD message, const NOTIFYICONDATAW& data) { return luil::win32::call_tray_shell(message, data); });
    const luil::tray_appearance appearance { host.appearance() };
    const std::optional<tray_report> added { host.apply(make_tray(make_square(40), u8"luil tray live test")) };

    // 지우기는 결과와 상관없이 늘 한다. 실측이 남긴 아이콘이 알림 영역에 남지 않게 한다.
    const std::optional<tray_report> removed { host.remove() };
    NOTIFYICONDATAW again {};
    again.cbSize = sizeof(again);
    again.hWnd = window.get();
    again.uID = luil::win32::tray_icon_id;
    const tray_shell_result second_delete { luil::win32::call_tray_shell(NIM_DELETE, again) };

    WARN("appearance high contrast " << appearance.high_contrast << ", light taskbar " << appearance.light_taskbar << ", icon size " << appearance.icon_size);
    WARN("add status " << (added.has_value() ? static_cast<int>(added->status) : -1) << " " << (added.has_value() ? narrow(added->error) : std::string {}));
    WARN("delete status " << (removed.has_value() ? static_cast<int>(removed->status) : -1) << " " << (removed.has_value() ? narrow(removed->error) : std::string {}));
    WARN("second delete ok " << second_delete.ok << ", error " << second_delete.error);

    REQUIRE(added.has_value());
    REQUIRE(added->status == tray_status::shown);
    REQUIRE(removed.has_value());
    REQUIRE(removed->status == tray_status::off);
    REQUIRE(second_delete.ok == false);
}
