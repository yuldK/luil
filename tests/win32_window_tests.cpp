#include "luil/win32/win32_window.h"
#include "win32/caption_surface.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <cwchar>

namespace {
    // 두 test가 함께 쓰는 창 클래스 이름이다.
    // 실패한 시도와 그 뒤의 성공이 같은 이름을 써야 남은 창이 다음 시도를 막는지
    // 함께 볼 수 있다 — 클래스 등록 자체는 남아도 된다.
    constexpr const wchar_t* test_class_name { L"Luil.Test.WindowLifetime" };

    // smoke에서 Direct3D를 얻지 못했을 때의 종료 코드다.
    // CTest의 SKIP_RETURN_CODE와 짝이고 win32_window.h가 그 계약을 적어 둔다.
    constexpr int direct3d_unavailable_exit_code { 77 };

    // Win32 창 클래스 이름의 상한이다.
    constexpr int class_name_capacity { 256 };

    struct class_window_count
    {
        const wchar_t* class_name { nullptr };
        int count { 0 };
    };

    BOOL CALLBACK count_class_window(const HWND window, const LPARAM parameter)
    {
        auto* const state { reinterpret_cast<class_window_count*>(parameter) };
        wchar_t name[class_name_capacity] {};
        if (GetClassNameW(window, name, class_name_capacity) > 0 && std::wcscmp(name, state->class_name) == 0)
            ++state->count;
        return TRUE;
    }

    // 이 스레드에 살아 있는 그 클래스의 창 수다.
    // FindWindowW는 이 PC에서 답을 놓친 적이 있어 스레드의 창을 직접 훑는다.
    [[nodiscard]] int windows_of_class(const wchar_t* const class_name)
    {
        class_window_count state { class_name, 0 };
        EnumThreadWindows(GetCurrentThreadId(), &count_class_window, reinterpret_cast<LPARAM>(&state));
        return state.count;
    }

    // 창 파괴가 남긴 WM_QUIT을 걷어낸다.
    // `WM_DESTROY`가 `PostQuitMessage(0)`을 부르므로 이 스레드의 큐에 남는다 —
    // 두면 뒤에 오는 test가 오염된 큐를 본다.
    void drain_quit_message() noexcept
    {
        MSG message {};
        // WM_QUIT은 큐에 하나뿐이라 한 번 걷으면 된다.
        static_cast<void>(PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
    }

    // 비클라이언트 판정만 재는 창의 클래스 이름이다.
    // 메시지는 기본 처리에 맡긴다 — 재는 것은 `caption_hit_test` 하나다.
    constexpr const wchar_t* hit_test_class_name { L"Luil.Test.WindowMode" };

    // 그 클래스로 창 하나를 만든다.
    // 보이지 않는 창이라 화면을 건드리지 않는다 (`WS_VISIBLE`을 주지 않는다).
    [[nodiscard]] HWND create_hit_test_window()
    {
        WNDCLASSEXW window_class {};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &DefWindowProcW;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = hit_test_class_name;
        if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return nullptr;
        const DWORD style { luil::win32::custom_window_style_for({}, luil::win32::window_display_mode::normal) };
        return CreateWindowExW(WS_EX_APPWINDOW, hit_test_class_name, L"hit test", style, 100, 100, 1000, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    // 창 왼쪽 위에서 (x, y)만큼 떨어진 자리를 이 모드로 판정한다.
    // `WM_NCHITTEST`의 lParam은 **화면 좌표**라 창 자리를 더해 넘긴다.
    [[nodiscard]] LRESULT hit_test_at(const HWND window, const luil::win32::window_display_mode mode, const int x, const int y)
    {
        RECT bounds {};
        GetWindowRect(window, &bounds);
        const LPARAM position { MAKELPARAM(bounds.left + x, bounds.top + y) };
        return luil::win32::caption_hit_test(window, position, 96, luil::caption_config {}, luil::win32::window_config {}, mode);
    }

    // 두 test가 함께 쓰는 설정이다.
    // 파일 끌기를 켜야 표면이 IDropTarget을 등록하고, 그래야 실패 정리가
    // 등록 해제까지 도는지 실제 OLE 위에서 확인된다.
    [[nodiscard]] luil::win32::window_config make_test_config()
    {
        luil::win32::window_config config {};
        config.class_name = test_class_name;
        // 대체 글꼴을 부르지 않도록 ASCII만 쓴다 (한 frame을 실제로 그린다).
        config.caption.title = u8"window lifetime";
        config.smoke_test = true;
        config.accept_file_drop = true;
        return config;
    }
} // namespace

TEST_CASE("A fullscreen window has no non-client area at all", "[win32][window]")
{
    // 순수 판정은 window_mode_tests가 잰다.
    // 여기서 재는 것은 **창을 든 경로**다: 화면 좌표를 창 좌표로 옮기고, 답을 Win32
    // `HT*`로 옮기고, 전체 화면이면 DWM에게 묻지도 않는 그 자리다.
    const HWND window { create_hit_test_window() };
    REQUIRE(window != nullptr);

    // 통상 창의 모서리와 캡션은 살아 있다.
    REQUIRE(hit_test_at(window, luil::win32::window_display_mode::normal, 1, 1) == HTTOPLEFT);
    REQUIRE(hit_test_at(window, luil::win32::window_display_mode::normal, 300, 10) == HTCAPTION);

    // 전체 화면에서는 같은 자리가 전부 client다.
    // 가장자리가 남으면 화면 끝을 노려 누르다 창 크기가 바뀌고, 캡션 띠가 남으면
    // 끌기 한 번에 전체 화면이 통째로 딸려 나온다.
    REQUIRE(hit_test_at(window, luil::win32::window_display_mode::fullscreen, 1, 1) == HTCLIENT);
    REQUIRE(hit_test_at(window, luil::win32::window_display_mode::fullscreen, 10, 10) == HTCLIENT);
    REQUIRE(hit_test_at(window, luil::win32::window_display_mode::fullscreen, 300, 10) == HTCLIENT);
    REQUIRE(hit_test_at(window, luil::win32::window_display_mode::fullscreen, 980, 10) == HTCLIENT);
    REQUIRE(hit_test_at(window, luil::win32::window_display_mode::fullscreen, 500, 400) == HTCLIENT);

    DestroyWindow(window);
}

TEST_CASE("A failed renderer leaves no window behind", "[win32][window]")
{
    // RegisterDragDrop이 실제로 서려면 STA와 OLE가 있어야 한다.
    // 그래야 실패 경로가 등록 해제까지 도는지가 껍데기 없이 검증된다.
    const luil::win32::com_sta_scope com {};
    REQUIRE(com.succeeded());

    luil::win32::window_config config { make_test_config() };
    config.renderer = luil::renderer_mode::direct3d;
    config.simulate_direct3d_failure = true;

    // 창은 만들어지고 renderer만 실패한다 — 정리를 빠뜨리면 여기서 창이 샌다.
    REQUIRE(luil::win32::run_application_window(config, {}) == direct3d_unavailable_exit_code);
    drain_quit_message();
    REQUIRE(windows_of_class(test_class_name) == 0);
}

TEST_CASE("A window can be created again after a failed attempt", "[win32][window]")
{
    const luil::win32::com_sta_scope com {};
    REQUIRE(com.succeeded());

    // 먼저 renderer를 실패시킨다.
    luil::win32::window_config failing { make_test_config() };
    failing.renderer = luil::renderer_mode::direct3d;
    failing.simulate_direct3d_failure = true;
    REQUIRE(luil::win32::run_application_window(failing, {}) == direct3d_unavailable_exit_code);
    drain_quit_message();

    // 같은 이름으로 다시 만든다.
    // 클래스 등록은 남아 있어도 되지만(ERROR_CLASS_ALREADY_EXISTS를 받아들인다)
    // 창이 남아 있으면 안 된다.
    luil::win32::window_config working { make_test_config() };
    working.renderer = luil::renderer_mode::cpu;
    REQUIRE(luil::win32::run_application_window(working, {}) == 0);
    // smoke는 한 frame만 그리고 돌아오므로 이 창을 거두는 것은 소멸자의 안전망이다.
    drain_quit_message();
    REQUIRE(windows_of_class(test_class_name) == 0);
}
