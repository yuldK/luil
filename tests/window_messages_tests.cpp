#include "win32/window_messages.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

namespace {
    // 메시지를 받지 않는 보통 창이다 (필터 호출이 실제 창에서 도는지만 본다).
    [[nodiscard]] HWND create_plain_window()
    {
        return CreateWindowExW(0, L"STATIC", L"window messages", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
}

TEST_CASE("The WM_APP numbers keep their values and never collide", "[win32][messages]")
{
    REQUIRE(luil::win32::snapshot_wake_message == WM_APP + 1u);
    REQUIRE(luil::win32::ui_command_request_message == WM_APP + 2u);
    REQUIRE(luil::win32::app_ui_command_request_message == WM_APP + 3u);
    REQUIRE(luil::win32::clipboard_request_message == WM_APP + 4u);
    REQUIRE(luil::win32::access_dispatch_message == WM_APP + 5u);
    REQUIRE(luil::win32::notification_event_message == WM_APP + 6u);
    REQUIRE(luil::win32::tray_callback_message == WM_APP + 7u);
    REQUIRE(luil::win32::key_report_message == WM_APP + 8u);
    REQUIRE(luil::win32::text_scale_changed_message == WM_APP + 9u);
    STATIC_REQUIRE(luil::win32::app_message_numbers_valid());
}

TEST_CASE("Shell messages are registered in the registered range and ask the same twice", "[win32][messages]")
{
    using luil::win32::shell_event;
    using luil::win32::shell_event_message;

    const UINT created { shell_event_message(shell_event::taskbar_button_created) };
    const UINT recreated { shell_event_message(shell_event::taskbar_created) };
    REQUIRE(created >= 0xC000u);
    REQUIRE(created <= 0xFFFFu);
    REQUIRE(recreated >= 0xC000u);
    REQUIRE(recreated <= 0xFFFFu);
    REQUIRE(created != recreated);

    // 두 번 물어도 같다. 이름이 셸이 보내는 이름과 같아야 하므로 시스템에 같은 이름으로 물어도 같은 번호다.
    REQUIRE(shell_event_message(shell_event::taskbar_button_created) == created);
    REQUIRE(created == RegisterWindowMessageW(L"TaskbarButtonCreated"));
    REQUIRE(recreated == RegisterWindowMessageW(L"TaskbarCreated"));
    REQUIRE(shell_event_message(shell_event::none) == 0u);
}

TEST_CASE("A message maps to the shell event it announces", "[win32][messages]")
{
    using luil::win32::shell_event;
    using luil::win32::shell_event_message;
    using luil::win32::shell_event_of;

    REQUIRE(shell_event_of(shell_event_message(shell_event::taskbar_button_created)) == shell_event::taskbar_button_created);
    REQUIRE(shell_event_of(shell_event_message(shell_event::taskbar_created)) == shell_event::taskbar_created);

    REQUIRE(shell_event_of(WM_NULL) == shell_event::none);
    REQUIRE(shell_event_of(WM_SETTINGCHANGE) == shell_event::none);
    REQUIRE(shell_event_of(luil::win32::snapshot_wake_message) == shell_event::none);
    REQUIRE(shell_event_of(luil::win32::tray_callback_message) == shell_event::none);
    // 등록 번호여도 셸의 것이 아니면 아니다.
    REQUIRE(shell_event_of(RegisterWindowMessageW(L"Luil.Test.NotAShellMessage")) == shell_event::none);
}

TEST_CASE("Allowing shell events never throws, even for a window that is not there", "[win32][messages]")
{
    STATIC_REQUIRE(noexcept(luil::win32::allow_shell_events(nullptr)));
    luil::win32::allow_shell_events(nullptr);

    const HWND window { create_plain_window() };
    REQUIRE(window != nullptr);
    luil::win32::allow_shell_events(window);
    REQUIRE(DestroyWindow(window) != FALSE);
    // 이미 파괴된 창은 필터를 걸지 못하지만 조용히 넘어간다.
    luil::win32::allow_shell_events(window);
}

TEST_CASE("Allowing instance messages never throws, even for a window that is not there", "[win32][messages]")
{
    STATIC_REQUIRE(noexcept(luil::win32::allow_instance_message(nullptr)));
    luil::win32::allow_instance_message(nullptr);

    const HWND window { create_plain_window() };
    REQUIRE(window != nullptr);
    luil::win32::allow_instance_message(window);
    REQUIRE(DestroyWindow(window) != FALSE);
    // 이미 파괴된 창은 필터를 걸지 못하지만 조용히 넘어간다.
    luil::win32::allow_instance_message(window);
}

TEST_CASE("Allowing the tray callback never throws, even for a window that is not there", "[win32][messages][tray]")
{
    STATIC_REQUIRE(noexcept(luil::win32::allow_tray_callback(nullptr)));
    luil::win32::allow_tray_callback(nullptr);

    const HWND window { create_plain_window() };
    REQUIRE(window != nullptr);
    luil::win32::allow_tray_callback(window);
    REQUIRE(DestroyWindow(window) != FALSE);
    // 이미 파괴된 창은 필터를 걸지 못하지만 조용히 넘어간다.
    luil::win32::allow_tray_callback(window);
}
