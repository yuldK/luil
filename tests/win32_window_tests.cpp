#include "luil/ui/root_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/win32/win32_window.h"
#include "win32/caption_surface.h"

#include <catch2/catch_test_macros.hpp>

#include <shellscalingapi.h>
#include <windows.h>

#include <atomic>
#include <cwchar>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

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
    // `tree`는 캡션 띠의 자리를 정하는 게시된 tree다 (없으면 맨 위 띠).
    [[nodiscard]] LRESULT hit_test_at(const HWND window, const luil::win32::window_display_mode mode, const int x, const int y, const luil::ui_tree* const tree = nullptr)
    {
        RECT bounds {};
        GetWindowRect(window, &bounds);
        const LPARAM position { MAKELPARAM(bounds.left + x, bounds.top + y) };
        return luil::win32::caption_hit_test(window, position, 96, luil::caption_config {}, luil::win32::window_config {}, mode, tree);
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

    constexpr const wchar_t* placement_class_name { L"Luil.Test.InitialPlacement" };

    struct placement_probe
    {
        RECT before_show {};
        RECT first_show {};
        WINDOWPLACEMENT saved {};
        std::optional<luil::window_placement> reported_placement {};
        // 닫힘(`WM_CLOSE`)이 갈무리해 알린 것과 가르려고, 닫힘 전에 알린 첫 배치를 따로 둔다.
        std::optional<luil::window_placement> reported_before_close {};
        bool closing { false };
        luil::window_metrics metrics {};
        bool hidden { false };
        bool maximized { false };
        bool shown { false };
        bool activated { false };
        bool activated_before_start { false };
        UINT dpi { 0 };
    };

    placement_probe* active_probe { nullptr };

    LRESULT CALLBACK observe_placement(const int code, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        if (code >= 0 && active_probe != nullptr)
        {
            const auto* message { reinterpret_cast<const CWPSTRUCT*>(long_parameter) };
            wchar_t name[class_name_capacity] {};
            if (GetClassNameW(message->hwnd, name, class_name_capacity) > 0 && std::wcscmp(name, placement_class_name) == 0)
            {
                if (message->message == WM_WINDOWPOSCHANGED && active_probe->shown == false)
                {
                    const auto* position { reinterpret_cast<const WINDOWPOS*>(message->lParam) };
                    if ((position->flags & SWP_SHOWWINDOW) != 0)
                    {
                        GetWindowRect(message->hwnd, &active_probe->first_show);
                        active_probe->shown = true;
                    }
                }
                if (message->message == WM_ACTIVATE && LOWORD(message->wParam) != WA_INACTIVE)
                    active_probe->activated = true;
                if (message->message == WM_CLOSE)
                    active_probe->closing = true;
            }
        }
        return CallNextHookEx(nullptr, code, word_parameter, long_parameter);
    }

    BOOL CALLBACK find_placement_window(const HWND window, const LPARAM parameter)
    {
        wchar_t name[class_name_capacity] {};
        if (GetClassNameW(window, name, class_name_capacity) > 0 && std::wcscmp(name, placement_class_name) == 0)
        {
            *reinterpret_cast<HWND*>(parameter) = window;
            return FALSE;
        }
        return TRUE;
    }

    class placement_driver final : public luil::logic_driver
    {
    public:
        void handle(luil::app_message message) override
        {
            if (const auto* metrics { message.get<luil::window_metrics>() }; metrics != nullptr)
                current_metrics = *metrics;
            if (message.get<bool>() != nullptr)
                closed.store(true);
        }
        [[nodiscard]] std::shared_ptr<const luil::ui_frame> make_frame() override
        {
            if (first_metrics.has_value() == false)
                first_metrics = current_metrics;
            return std::make_shared<luil::ui_frame>();
        }
        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { true };
        }
        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed.load();
        }
        luil::window_metrics current_metrics {};
        std::optional<luil::window_metrics> first_metrics {};
        std::atomic<bool> closed { false };
    };

    class placement_delegate final : public luil::win32::window_delegate
    {
    public:
        explicit placement_delegate(placement_probe& probe)
            : probe_ { probe }
        {}
        [[nodiscard]] luil::app_message make_metrics_message(const luil::window_metrics& metrics) override
        {
            probe_.metrics = metrics;
            return luil::app_message { metrics };
        }
        [[nodiscard]] luil::app_message make_window_placement_message(const luil::window_placement& placement) override
        {
            if (probe_.reported_placement.has_value() == false)
                probe_.reported_placement = placement;
            if (probe_.closing == false && probe_.reported_before_close.has_value() == false)
                probe_.reported_before_close = placement;
            return {};
        }
        void on_started(luil::app_host&) override
        {
            HWND window { nullptr };
            EnumThreadWindows(GetCurrentThreadId(), &find_placement_window, reinterpret_cast<LPARAM>(&window));
            if (window == nullptr)
                return;
            GetWindowRect(window, &probe_.before_show);
            probe_.saved.length = sizeof(probe_.saved);
            GetWindowPlacement(window, &probe_.saved);
            probe_.dpi = GetDpiForWindow(window);
            probe_.hidden = IsWindowVisible(window) == FALSE;
            probe_.maximized = IsZoomed(window) != FALSE;
            probe_.activated_before_start = probe_.activated;
            PostMessageW(window, WM_CLOSE, 0, 0);
        }

    private:
        placement_probe& probe_;
    };

    [[nodiscard]] MONITORINFO test_monitor()
    {
        luil::win32::enable_per_monitor_dpi_awareness();
        MONITORINFO information {};
        information.cbSize = sizeof(information);
        const POINT origin {};
        GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &information);
        return information;
    }

    [[nodiscard]] luil::window_placement saved_test_placement(const MONITORINFO& monitor)
    {
        return { monitor.rcMonitor.left + 60, monitor.rcMonitor.top + 70, 960, 640 };
    }

    BOOL CALLBACK collect_monitors(const HMONITOR monitor, HDC, LPRECT, const LPARAM parameter)
    {
        reinterpret_cast<std::vector<HMONITOR>*>(parameter)->push_back(monitor);
        return TRUE;
    }

    [[nodiscard]] placement_probe run_placement_probe(luil::win32::window_config config)
    {
        luil::win32::enable_per_monitor_dpi_awareness();
        const luil::win32::com_sta_scope com {};
        placement_probe probe {};
        placement_driver driver {};
        placement_delegate delegate { probe };
        config.class_name = placement_class_name;
        config.caption.title = u8"initial placement";
        config.renderer = luil::renderer_mode::cpu;
        active_probe = &probe;
        const HHOOK hook { SetWindowsHookExW(WH_CALLWNDPROC, &observe_placement, nullptr, GetCurrentThreadId()) };
        const int result { luil::win32::run_application_window(config, { &driver, nullptr, &delegate }) };
        UnhookWindowsHookEx(hook);
        active_probe = nullptr;
        drain_quit_message();
        REQUIRE(result == 0);
        REQUIRE(probe.hidden);
        REQUIRE(driver.first_metrics.has_value());
        REQUIRE(driver.first_metrics->width == static_cast<float>(probe.before_show.right - probe.before_show.left));
        REQUIRE(driver.first_metrics->height == static_cast<float>(probe.before_show.bottom - probe.before_show.top));
        REQUIRE(driver.first_metrics->scale == static_cast<float>(probe.dpi) / 96.0f);
        return probe;
    }
} // namespace

TEST_CASE("Initial placement is physical and precedes first layout and display", "[win32][window]")
{
    const MONITORINFO monitor { test_monitor() };
    const luil::window_placement saved { saved_test_placement(monitor) };
    luil::win32::window_config config {};
    config.initial_position = luil::win32::window_position { 10, 20 };
    config.initial_width = 700;
    config.initial_height = 500;
    config.initial_placement = saved;
    const placement_probe probe { run_placement_probe(config) };
    REQUIRE(probe.maximized == false);
    REQUIRE(probe.saved.rcNormalPosition.left == saved.x);
    REQUIRE(probe.saved.rcNormalPosition.top == saved.y);
    REQUIRE(probe.before_show.right - probe.before_show.left == saved.width);
    REQUIRE(probe.before_show.bottom - probe.before_show.top == saved.height);
    REQUIRE(probe.shown);
    REQUIRE(EqualRect(&probe.before_show, &probe.first_show));
}

TEST_CASE("Initial maximization keeps the restore rectangle before layout", "[win32][window]")
{
    const MONITORINFO monitor { test_monitor() };
    luil::window_placement saved { saved_test_placement(monitor) };
    saved.maximized = true;
    luil::win32::window_config config {};
    config.initial_placement = saved;
    const placement_probe probe { run_placement_probe(config) };
    REQUIRE(probe.maximized);
    INFO("window: " << probe.before_show.left << ',' << probe.before_show.top << ',' << probe.before_show.right << ',' << probe.before_show.bottom);
    INFO("work: " << monitor.rcWork.left << ',' << monitor.rcWork.top << ',' << monitor.rcWork.right << ',' << monitor.rcWork.bottom);
    REQUIRE(EqualRect(&probe.before_show, &monitor.rcWork));
    REQUIRE(probe.saved.rcNormalPosition.left == saved.x);
    REQUIRE(probe.saved.rcNormalPosition.top == saved.y);
    REQUIRE(probe.saved.rcNormalPosition.right - probe.saved.rcNormalPosition.left == saved.width);
    REQUIRE(probe.saved.rcNormalPosition.bottom - probe.saved.rcNormalPosition.top == saved.height);
    REQUIRE(probe.shown);
    REQUIRE(EqualRect(&probe.before_show, &probe.first_show));
}

TEST_CASE("Initial placement uses the target monitor DPI and its minimum size", "[win32][window]")
{
    luil::win32::enable_per_monitor_dpi_awareness();
    std::vector<HMONITOR> monitors {};
    REQUIRE(EnumDisplayMonitors(nullptr, nullptr, &collect_monitors, reinterpret_cast<LPARAM>(&monitors)) != FALSE);
    for (const HMONITOR monitor : monitors)
    {
        MONITORINFO information {};
        information.cbSize = sizeof(information);
        REQUIRE(GetMonitorInfoW(monitor, &information) != FALSE);
        UINT dpi_x { 96 };
        UINT dpi_y { 96 };
        REQUIRE(SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)));
        luil::win32::window_config config {};
        config.minimum_client_width = 600;
        config.minimum_client_height = 400;
        config.initial_position = luil::win32::window_position { information.rcWork.left + 100, information.rcWork.top + 100 };
        luil::window_placement saved { saved_test_placement(information) };
        saved.width = MulDiv(config.minimum_client_width, static_cast<int>(dpi_x), 96);
        saved.height = MulDiv(config.minimum_client_height, static_cast<int>(dpi_x), 96);
        config.initial_placement = saved;
        const placement_probe accepted { run_placement_probe(config) };
        REQUIRE(accepted.dpi == dpi_x);
        REQUIRE(accepted.before_show.right - accepted.before_show.left == saved.width);
        REQUIRE(accepted.before_show.bottom - accepted.before_show.top == saved.height);
        REQUIRE(accepted.saved.rcNormalPosition.left == saved.x);
        REQUIRE(accepted.saved.rcNormalPosition.top == saved.y);
        config.initial_placement->maximized = true;
        const placement_probe maximized { run_placement_probe(config) };
        REQUIRE(maximized.maximized);
        REQUIRE(EqualRect(&maximized.before_show, &information.rcWork));
        REQUIRE(maximized.saved.rcNormalPosition.right - maximized.saved.rcNormalPosition.left == saved.width);
        REQUIRE(maximized.saved.rcNormalPosition.bottom - maximized.saved.rcNormalPosition.top == saved.height);
        --config.initial_placement->width;
        config.initial_placement->maximized = true;
        const placement_probe rejected { run_placement_probe(config) };
        REQUIRE(rejected.maximized == false);
        REQUIRE(rejected.before_show.left == config.initial_position->x);
        REQUIRE(rejected.before_show.top == config.initial_position->y);
    }
}

TEST_CASE("Initial fullscreen keeps its normal placement before first display", "[win32][window]")
{
    const MONITORINFO monitor { test_monitor() };
    for (const bool maximized : { false, true })
    {
        luil::win32::window_config config {};
        config.initial_placement = saved_test_placement(monitor);
        config.initial_placement->maximized = maximized;
        config.initial_placement->fullscreen = true;
        const placement_probe probe { run_placement_probe(config) };
        REQUIRE(EqualRect(&probe.before_show, &monitor.rcMonitor));
        REQUIRE(EqualRect(&probe.first_show, &monitor.rcMonitor));
        REQUIRE(probe.reported_placement == config.initial_placement);
    }
}

TEST_CASE("Invalid initial placements use the complete default placement", "[win32][window]")
{
    const MONITORINFO monitor { test_monitor() };
    const std::vector<luil::window_placement> invalid {
        { 0, 0, 0, 600, true },
        { 0, 0, -1, 600, true },
        { 0, 0, 10, 10, true },
        { std::numeric_limits<int>::max(), 0, 960, 640, true },
        { 1'000'000, 1'000'000, 960, 640, true },
    };
    for (const luil::window_placement& saved : invalid)
    {
        luil::win32::window_config config {};
        config.initial_position = luil::win32::window_position { monitor.rcWork.left + 100, monitor.rcWork.top + 100 };
        config.initial_width = 700;
        config.initial_height = 500;
        config.initial_placement = saved;
        const placement_probe probe { run_placement_probe(config) };
        REQUIRE(probe.maximized == false);
        REQUIRE(probe.before_show.left == config.initial_position->x);
        REQUIRE(probe.before_show.top == config.initial_position->y);
        REQUIRE(probe.before_show.right - probe.before_show.left == MulDiv(config.initial_width, static_cast<int>(probe.dpi), 96));
        REQUIRE(probe.before_show.bottom - probe.before_show.top == MulDiv(config.initial_height, static_cast<int>(probe.dpi), 96));
    }
}

TEST_CASE("A first run without a saved placement reports where the window landed", "[win32][window]")
{
    // 저장한 배치가 없어도 첫 표시 직후 실제 배치를 한 번 알린다. 닫힘이 알리는 것을 기다리지 않는다.
    const MONITORINFO monitor { test_monitor() };
    luil::win32::window_config config {};
    config.initial_position = luil::win32::window_position { monitor.rcWork.left + 120, monitor.rcWork.top + 90 };
    config.initial_width = 700;
    config.initial_height = 500;
    const placement_probe probe { run_placement_probe(config) };
    REQUIRE(probe.reported_before_close.has_value());
    REQUIRE(probe.reported_before_close->width == MulDiv(config.initial_width, static_cast<int>(probe.dpi), 96));
    REQUIRE(probe.reported_before_close->height == MulDiv(config.initial_height, static_cast<int>(probe.dpi), 96));
    REQUIRE(probe.reported_before_close->maximized == false);
}

TEST_CASE("Initial placement respects inactive startup policies", "[win32][window]")
{
    wchar_t executable[32768] {};
    REQUIRE(GetModuleFileNameW(nullptr, executable, 32768) > 0);
    for (const int show : { SW_SHOWNOACTIVATE, SW_SHOWMINNOACTIVE, SW_HIDE })
    {
        std::wstring command { L"\"" };
        command += executable;
        command += L"\" \"Initial placement startup probe\" --reporter compact";
        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = static_cast<WORD>(show);
        PROCESS_INFORMATION process {};
        REQUIRE(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process) != FALSE);
        CloseHandle(process.hThread);
        const DWORD waited { WaitForSingleObject(process.hProcess, 30000) };
        DWORD exit_code { 1 };
        GetExitCodeProcess(process.hProcess, &exit_code);
        CloseHandle(process.hProcess);
        INFO("startup show command: " << show);
        REQUIRE(waited == WAIT_OBJECT_0);
        REQUIRE(exit_code == 0);
    }
}

TEST_CASE("Initial placement startup probe", "[.][win32][window]")
{
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    GetStartupInfoW(&startup);
    if ((startup.dwFlags & STARTF_USESHOWWINDOW) == 0 || (startup.wShowWindow != SW_SHOWNOACTIVATE && startup.wShowWindow != SW_SHOWMINNOACTIVE && startup.wShowWindow != SW_HIDE))
        SKIP("This probe runs in the inactive startup policy subprocess.");
    const MONITORINFO monitor { test_monitor() };
    for (const bool maximized : { false, true })
    {
        luil::win32::window_config config {};
        config.initial_placement = saved_test_placement(monitor);
        config.initial_placement->maximized = maximized;
        const placement_probe probe { run_placement_probe(config) };
        INFO("maximized: " << maximized << ", activated before start: " << probe.activated_before_start);
        REQUIRE(probe.maximized == maximized);
        REQUIRE(probe.activated == false);
        REQUIRE(probe.shown == (startup.wShowWindow != SW_HIDE));
        if (startup.wShowWindow == SW_SHOWNOACTIVATE)
            REQUIRE(EqualRect(&probe.before_show, &probe.first_show));
    }
}

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

TEST_CASE("The window hit test reads the caption band from the published tree", "[win32][window]")
{
    // 앱이 캡션을 창 아래에 세운 tree다. 판정은 그 tree가 그린 자리를 본다.
    const HWND window { create_hit_test_window() };
    REQUIRE(window != nullptr);
    RECT bounds {};
    GetWindowRect(window, &bounds);
    const float width { static_cast<float>(bounds.right - bounds.left) };
    const float height { static_cast<float>(bounds.bottom - bounds.top) };

    auto root { std::make_unique<luil::root_element>() };
    root->arrange({ { 0.0f, 0.0f, width, height }, 1.0f });
    auto caption { std::make_unique<luil::caption_element>(luil::caption_config {}) };
    caption->arrange({ { 0.0f, height - 36.0f, width, 0.0f }, 1.0f });
    root->add(std::move(caption));
    const luil::ui_tree tree { std::move(root) };

    const int bottom_row { static_cast<int>(height) - 20 };
    REQUIRE(hit_test_at(window, luil::win32::window_display_mode::normal, 300, bottom_row, &tree) == HTCAPTION);
    REQUIRE(hit_test_at(window, luil::win32::window_display_mode::normal, static_cast<int>(width) - 20, bottom_row, &tree) == HTCLOSE);
    REQUIRE(hit_test_at(window, luil::win32::window_display_mode::normal, 300, 10, &tree) == HTCLIENT);
    // tree가 없으면 예전처럼 맨 위가 캡션이다.
    REQUIRE(hit_test_at(window, luil::win32::window_display_mode::normal, 300, 10) == HTCAPTION);

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
