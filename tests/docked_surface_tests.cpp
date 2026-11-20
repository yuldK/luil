#include "win32/docked_surface.h"
#include "win32/window_messages.h"

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    // 도킹 표면의 창 처리를 재는 최소 문맥이다. 렌더러·웹뷰·host는 세우지 않는다.
    class fake_surface_context final : public luil::win32::surface_context
    {
    public:
        std::vector<luil::popup_dismiss_reason> dismissed {};
        std::vector<std::u8string> moved {};
        int display_hints { 0 };

        [[nodiscard]] luil::app_host* host() noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] const luil::win32::window_config& config() const noexcept override
        {
            return config_;
        }

        [[nodiscard]] luil::interaction_policy* policy() noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] IDCompositionDevice* composition_device(std::u8string&) noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] std::span<const luil::pixel_rect> apply_webviews(const std::u8string&, const luil::ui_tree*, luil::win32::composition_renderer*, int, int) override
        {
            return {};
        }

        [[nodiscard]] bool relay_webview_pointer(const std::u8string&, UINT, WPARAM, int, int) override
        {
            return false;
        }

        void webview_pointer_left(const std::u8string&) override
        {
        }
        void cancel_webview_pointer(const std::u8string&) override
        {
        }

        [[nodiscard]] sk_sp<SkTypeface> apply_frame_appearance(luil::frame_state&, const luil::ui_frame*) override
        {
            return nullptr;
        }

        bool dismiss_popups(const luil::popup_dismiss_reason reason) override
        {
            dismissed.push_back(reason);
            return false;
        }

        void surface_moved(const std::u8string&) override
        {
            dismissed.push_back(luil::popup_dismiss_reason::surface_moved);
        }

        void surface_scrolled(const std::u8string& surface) override
        {
            moved.push_back(surface);
        }

        void dispatch_action(luil::input_action) override
        {
        }

        void deliver_file_drop(const std::vector<std::u8string>&) override
        {
        }

        void schedule_update_repaint(const std::u8string&, const luil::interaction_snapshot&) noexcept override
        {
        }

        void surface_focus_lost(const std::u8string&) override
        {
        }

        void surface_focus_gained(const std::u8string&) override
        {
        }

        [[nodiscard]] const luil::win32::window_surface* anchored_popup(const std::u8string&, const std::u8string&) const noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] bool pointer_capture_active() const noexcept override
        {
            return false;
        }

        void set_pointer_capture_active(bool) noexcept override
        {
        }

        void report_error(const std::u8string&) noexcept override
        {
        }

        void displays_changed() override
        {
            ++display_hints;
        }

    private:
        luil::win32::window_config config_ {};
    };

    class empty_panel final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
    };

    // 실제 도킹 표면 창 클래스로 숨은 창을 세운다. 보이게 하는 시험은 화면 밖 자리(-30000)에만 건다 — 맨 위 창이 사람의
    // 화면을 덮지 않게.
    class docked_test_window final
    {
    public:
        explicit docked_test_window(luil::win32::docked_surface& surface)
            : surface_ { surface }
        {
            WNDCLASSEXW type {};
            type.cbSize = sizeof(type);
            type.hInstance = GetModuleHandleW(nullptr);
            type.lpszClassName = L"Luil.Test.Docked";
            type.lpfnWndProc = &luil::win32::docked_surface::static_procedure;
            if (RegisterClassExW(&type) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return;
            window_ = CreateWindowExW(luil::win32::docked_extended_style(), type.lpszClassName, L"", luil::win32::docked_window_style(), -30000, -30000, 1, 1, nullptr, nullptr, type.hInstance, &surface);
        }
        docked_test_window(const docked_test_window&) = delete;
        docked_test_window& operator=(const docked_test_window&) = delete;
        ~docked_test_window()
        {
            if (window_ != nullptr && IsWindow(window_) != FALSE)
                DestroyWindow(window_);
            surface_.detach_window();
        }
        [[nodiscard]] HWND handle() const noexcept
        {
            return window_;
        }

    private:
        luil::win32::docked_surface& surface_;
        HWND window_ { nullptr };
    };

    [[nodiscard]] RECT window_rect(const HWND window)
    {
        RECT rectangle {};
        GetWindowRect(window, &rectangle);
        return rectangle;
    }

    [[nodiscard]] auto create_test_popup(const HWND owner, const luil::screen_rect& bounds)
    {
        const HWND window { CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"BUTTON", L"", WS_POPUP
            , bounds.left, bounds.top, bounds.width(), bounds.height(), owner, nullptr, GetModuleHandleW(nullptr), nullptr)
        };
        return std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)> { window, &DestroyWindow };
    }
}

TEST_CASE("A docked surface window has no caption, no taskbar button and never activates", "[win32][docked]")
{
    const DWORD style { luil::win32::docked_window_style() };
    const DWORD extended { luil::win32::docked_extended_style() };
    REQUIRE((style & WS_POPUP) != 0u);
    REQUIRE((style & (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU)) == 0u);
    REQUIRE((extended & WS_EX_TOOLWINDOW) != 0u);
    REQUIRE((extended & WS_EX_TOPMOST) != 0u);
    REQUIRE((extended & WS_EX_NOACTIVATE) != 0u);
    REQUIRE((extended & WS_EX_APPWINDOW) == 0u);

    fake_surface_context context {};
    luil::win32::docked_surface surface { context, u8"widget" };
    docked_test_window window { surface };
    REQUIRE(window.handle() != nullptr);
    REQUIRE(GetWindow(window.handle(), GW_OWNER) == nullptr);
    // 누름이 활성화를 부르지 않는다.
    REQUIRE(SendMessageW(window.handle(), WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(window.handle()), MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) == MA_NOACTIVATE);
    // Alt+F4도 창을 없애지 않는다. 표면을 없애는 것은 앱이 목록에서 빼는 것뿐이다.
    SendMessageW(window.handle(), WM_CLOSE, 0, 0);
    REQUIRE(IsWindow(window.handle()) != FALSE);
}

TEST_CASE("A docked surface recovers when Windows lowers its owned popup", "[win32][docked]")
{
    fake_surface_context context {};
    luil::win32::docked_surface surface { context, u8"widget" };
    docked_test_window window { surface };
    REQUIRE(window.handle() != nullptr);
    const luil::screen_rect bounds { -25100, -25100, -25000, -25000 };
    surface.apply_bounds(bounds);
    const auto popup { create_test_popup(window.handle(), { bounds.left, bounds.top, bounds.left + 8, bounds.top + 8 }) };
    const auto ordinary { create_test_popup(nullptr, bounds) };
    REQUIRE(popup.get() != nullptr);
    REQUIRE(ordinary.get() != nullptr);
    ShowWindow(popup.get(), SW_SHOWNOACTIVATE);

    // 소유 팝업을 내리면 Windows가 도킹 창까지 함께 내린다. 겹친 자리 중 팝업 바깥에서 어느 창이 앞인지 잰다.
    const POINT overlap { bounds.left + 50, bounds.top + 50 };
    const UINT flags { SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE };
    REQUIRE(SetWindowPos(popup.get(), HWND_NOTOPMOST, 0, 0, 0, 0, flags));
    REQUIRE((GetWindowLongW(window.handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);
    REQUIRE(SetWindowPos(ordinary.get(), HWND_TOP, 0, 0, 0, 0, flags | SWP_SHOWWINDOW));
    REQUIRE(WindowFromPoint(overlap) == ordinary.get());

    const auto dispatch_restore = [&window] {
        MSG message {};
        const UINT restore { luil::win32::restore_docked_topmost_message };
        while (PeekMessageW(&message, window.handle(), restore, restore, PM_REMOVE) != FALSE)
            DispatchMessageW(&message);
    };

    SECTION("The next window message restores the visible dock above the ordinary window")
    {
        const HWND active { GetActiveWindow() };
        dispatch_restore();
        REQUIRE(GetActiveWindow() == active);
        REQUIRE((GetWindowLongW(window.handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
        REQUIRE((GetWindowLongW(ordinary.get(), GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);
        REQUIRE(WindowFromPoint(overlap) == window.handle());
    }

    SECTION("A queued restore keeps a subsequently hidden dock hidden")
    {
        surface.apply_bounds(std::nullopt);
        dispatch_restore();
        REQUIRE(IsWindowVisible(window.handle()) == FALSE);
        REQUIRE((GetWindowLongW(window.handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);
        REQUIRE(WindowFromPoint(overlap) == ordinary.get());
    }

    SECTION("An already restored dock keeps its place behind another topmost window")
    {
        REQUIRE(SetWindowPos(window.handle(), HWND_TOPMOST, 0, 0, 0, 0, flags));
        REQUIRE(SetWindowPos(ordinary.get(), HWND_TOPMOST, 0, 0, 0, 0, flags));
        dispatch_restore();
        REQUIRE(WindowFromPoint(overlap) == ordinary.get());
    }
}

TEST_CASE("A docked surface takes the bounds the app gives, down to a thin strip", "[win32][docked]")
{
    fake_surface_context context {};
    luil::win32::docked_surface surface { context, u8"strip" };
    docked_test_window window { surface };
    REQUIRE(window.handle() != nullptr);
    REQUIRE(IsWindowVisible(window.handle()) == FALSE);

    // 3px 두께의 띠다. 최소 크기에 잘리지 않는다.
    const luil::screen_rect strip { -30000, -30000, -29200, -29997 };
    surface.apply_bounds(strip);
    RECT placed { window_rect(window.handle()) };
    REQUIRE(placed.left == strip.left);
    REQUIRE(placed.top == strip.top);
    REQUIRE(placed.right - placed.left == 800);
    REQUIRE(placed.bottom - placed.top == 3);
    REQUIRE(IsWindowVisible(window.handle()) != FALSE);
    REQUIRE((GetWindowLongW(window.handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    REQUIRE(surface.bounds() == strip);
    // 옮겨지면 붙은 popup이 따라온다. 앱이 정한 이동이라 닫힘 계기는 아니다.
    REQUIRE(context.moved.empty() == false);
    REQUIRE(context.moved.back() == u8"strip");
    REQUIRE(context.dismissed.empty());

    // 실행 중에 바로 바꾼다.
    const luil::screen_rect wider { -30000, -30000, -29000, -29960 };
    surface.apply_bounds(wider);
    placed = window_rect(window.handle());
    REQUIRE(placed.right - placed.left == 1000);
    REQUIRE(placed.bottom - placed.top == 40);

    // 풀리지 않은 자리(빠진 모니터)는 숨긴다.
    surface.apply_bounds(std::nullopt);
    REQUIRE(IsWindowVisible(window.handle()) == FALSE);
    REQUIRE(surface.bounds().has_value() == false);
}

TEST_CASE("A docked surface ignores the DPI suggestion and reports display changes", "[win32][docked]")
{
    fake_surface_context context {};
    luil::win32::docked_surface surface { context, u8"panel" };
    docked_test_window window { surface };
    REQUIRE(window.handle() != nullptr);
    const luil::screen_rect bounds { -30000, -30000, -29700, -29900 };
    surface.apply_bounds(bounds);

    // OS가 제안하는 사각형(지금 창을 새 배율로 곱한 것)을 쓰지 않는다. 자리와 크기는 앱의 것이다.
    RECT suggested { -30000, -30000, -29550, -29850 };
    SendMessageW(window.handle(), WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&suggested));
    const RECT placed { window_rect(window.handle()) };
    REQUIRE(placed.right - placed.left == 300);
    REQUIRE(placed.bottom - placed.top == 100);
    REQUIRE(surface.dpi() == 144u);
    REQUIRE(context.display_hints == 1);

    // 작업 영역·모니터 변화도 소유자에게 알린다 (소유자가 목록을 견주어 거른다).
    SendMessageW(window.handle(), WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
    SendMessageW(window.handle(), WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080));
    REQUIRE(context.display_hints == 3);
}

TEST_CASE("A docked surface closes its menus only when it loses a requested activation", "[win32][docked]")
{
    fake_surface_context context {};
    luil::win32::docked_surface surface { context, u8"widget" };
    docked_test_window window { surface };
    REQUIRE(window.handle() != nullptr);
    // 메뉴를 열며 잠깐 활성화를 청한 앱의 메뉴가 서자마자 닫히면 안 된다.
    SendMessageW(window.handle(), WM_ACTIVATE, WA_ACTIVE, 0);
    REQUIRE(context.dismissed.empty());
    SendMessageW(window.handle(), WM_ACTIVATE, WA_INACTIVE, 0);
    REQUIRE(context.dismissed == std::vector<luil::popup_dismiss_reason> { luil::popup_dismiss_reason::activation_changed });
}

TEST_CASE("A docked surface adopts the title and the tree of every frame", "[win32][docked]")
{
    fake_surface_context context {};
    luil::win32::docked_surface surface { context, u8"widget" };
    docked_test_window window { surface };
    REQUIRE(window.handle() != nullptr);

    luil::ui_docked_surface source {};
    source.id = u8"widget";
    source.title = u8"날씨 위젯";
    source.tree = std::make_shared<const luil::ui_tree>(std::make_unique<empty_panel>(luil::ui_element_id { luil::ui_element_kind::root }));
    REQUIRE(surface.adopt(source));
    std::wstring title(32, L'\0');
    title.resize(static_cast<std::size_t>(GetWindowTextW(window.handle(), title.data(), static_cast<int>(title.size()))));
    REQUIRE(title == L"날씨 위젯");
    // tree가 그대로면 다시 그릴 것이 없다.
    REQUIRE(surface.adopt(source) == false);
}

TEST_CASE("Connected displays are listed with their work areas and scales", "[win32][docked]")
{
    const std::vector<luil::display_info> displays { luil::win32::enumerate_displays() };
    REQUIRE(displays.empty() == false);
    int primaries { 0 };
    for (const luil::display_info& display : displays)
    {
        REQUIRE(display.id.empty() == false);
        REQUIRE(display.bounds.empty() == false);
        REQUIRE(display.work_area.empty() == false);
        REQUIRE(display.scale >= 1.0f);
        if (display.primary)
            ++primaries;
    }
    REQUIRE(primaries == 1);
}
