#include "macos/tray_icon_macos.h"

#include <catch2/catch_test_macros.hpp>

// macOS 상태 항목의 누름 풀이와 좌표·상태 옮기기다 (src/macos/tray_icon_macos.h). AppKit을 모르는 inline 도우미라 core test에서 돈다.
namespace {
    [[nodiscard]] luil::macos::tray_click click(const luil::macos::tray_mouse_button button, const int count = 1, const bool control = false)
    {
        luil::macos::tray_click value {};
        value.button = button;
        value.click_count = count;
        value.control = control;
        value.point = luil::screen_point { 120, 4 };
        return value;
    }
}

TEST_CASE("macOS tray clicks become tray events", "[macos][tray]")
{
    using luil::tray_icon_action;
    using luil::macos::tray_mouse_button;

    const luil::tray_icon_event left { luil::macos::tray_event_of_click(click(tray_mouse_button::left)) };
    CHECK(left == luil::tray_icon_event { tray_icon_action::select, false, luil::screen_point { 120, 4 } });

    // 두 번 누름은 둘째 누름이 `double_click`이다. 첫 누름은 `select`로 먼저 나갔다.
    CHECK(luil::macos::tray_event_of_click(click(tray_mouse_button::left, 2)).action == tray_icon_action::double_click);
    CHECK(luil::macos::tray_event_of_click(click(tray_mouse_button::left, 3)).action == tray_icon_action::double_click);

    // 오른쪽 누름과 Control+왼쪽 누름은 메뉴 요청이다. 두 번 누름보다 Control이 앞선다.
    CHECK(luil::macos::tray_event_of_click(click(tray_mouse_button::right)).action == tray_icon_action::context);
    CHECK(luil::macos::tray_event_of_click(click(tray_mouse_button::right, 2)).action == tray_icon_action::context);
    CHECK(luil::macos::tray_event_of_click(click(tray_mouse_button::left, 1, true)).action == tray_icon_action::context);
    CHECK(luil::macos::tray_event_of_click(click(tray_mouse_button::left, 2, true)).action == tray_icon_action::context);
    CHECK(luil::macos::tray_event_of_click(click(tray_mouse_button::right)).keyboard == false);

    // 마우스가 아닌 누름(VoiceOver)은 키보드 고름이다.
    const luil::tray_icon_event spoken { luil::macos::tray_event_of_click(click(tray_mouse_button::none)) };
    CHECK(spoken.action == tray_icon_action::select);
    CHECK(spoken.keyboard);
    CHECK(spoken.point == luil::screen_point { 120, 4 });
}

TEST_CASE("macOS tray points use the primary screen pixel convention", "[macos][tray]")
{
    // 주 화면 높이 900점, 배율 2. AppKit의 (100, 890)은 위에서 10점이다.
    CHECK(luil::macos::tray_screen_point(100.0, 890.0, 900.0, 2.0) == luil::screen_point { 200, 20 });
    // 주 화면 밖(왼쪽·위)은 음수다.
    CHECK(luil::macos::tray_screen_point(-50.0, 950.0, 900.0, 2.0) == luil::screen_point { -100, -100 });
    // 배율을 모르면 1이다.
    CHECK(luil::macos::tray_screen_point(10.25, 800.0, 900.0, 0.0) == luil::screen_point { 10, 100 });
}

TEST_CASE("macOS tray reports have no waiting state", "[macos][tray]")
{
    using luil::win32::tray_status;
    CHECK(luil::macos::tray_status_of(luil::tray_sync_state::off) == tray_status::off);
    CHECK(luil::macos::tray_status_of(luil::tray_sync_state::shown) == tray_status::shown);
    CHECK(luil::macos::tray_status_of(luil::tray_sync_state::failed) == tray_status::failed);
    CHECK(luil::macos::tray_status_of(luil::tray_sync_state::waiting) == tray_status::failed);
}
