#include "win32/secondary_surface.h"

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/win32/app_host.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cwchar>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace {
    // 프레임·캡션을 재는 최소 문맥이다. 입력 경계 test만 실제 host를 붙인다.
    // 렌더러·웹뷰는 세우지 않는다. OS 처리는 보이지 않는 HWND로 잰다.
    class fake_surface_context final : public luil::win32::surface_context
    {
    public:
        [[nodiscard]] luil::win32::app_host* host() noexcept override
        {
            return input_host;
        }

        luil::app_host* input_host { nullptr };
        bool capture_active { false };
        std::vector<std::u8string> canceled_surfaces {};

        [[nodiscard]] const luil::win32::window_config& config() const noexcept override
        {
            return config_;
        }

        [[nodiscard]] luil::interaction_policy* policy() noexcept override
        {
            return nullptr;
        }

        // 창 없는 test는 합성하지 않는다 — Direct3D 렌더러를 세우지 않으므로
        // device를 물을 일이 없다.
        [[nodiscard]] IDCompositionDevice* composition_device(std::u8string&) noexcept override
        {
            return nullptr;
        }

        // 창 없는 test는 웹뷰를 세우지 않는다.
        [[nodiscard]] std::span<const luil::pixel_rect> apply_webviews(const std::u8string&, const luil::ui_tree*, IDCompositionVisual*, int, int) override
        {
            return {};
        }

        // 창 없는 test는 웹뷰를 세우지 않는다.
        [[nodiscard]] bool relay_webview_pointer(const std::u8string&, UINT, WPARAM, int, int) override
        {
            return false;
        }

        void webview_pointer_left(const std::u8string&) override
        {}
        void cancel_webview_pointer(const std::u8string& surface) override
        {
            canceled_surfaces.push_back(surface);
        }

        [[nodiscard]] sk_sp<SkTypeface> apply_frame_appearance(luil::frame_state&, const luil::win32::ui_frame*) override
        {
            return nullptr;
        }

        bool dismiss_popups(luil::win32::popup_dismiss_reason) override
        {
            return false;
        }

        void surface_moved(const std::u8string&) override
        {}

        void dispatch_action(luil::input_action) override
        {}

        void deliver_file_drop(const std::vector<std::u8string>&) override
        {}

        void schedule_update_repaint(const std::u8string&, const luil::interaction_snapshot&) noexcept override
        {}

        void surface_focus_lost(const std::u8string&) override
        {}

        void surface_focus_gained(const std::u8string&) override
        {}

        [[nodiscard]] const luil::win32::window_surface* anchored_popup(const std::u8string&, const std::u8string&) const noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] bool pointer_capture_active() const noexcept override
        {
            return capture_active;
        }

        void set_pointer_capture_active(const bool active) noexcept override
        {
            capture_active = active;
        }

        void report_error(const std::u8string&) noexcept override
        {}

    private:
        luil::win32::window_config config_ {};
    };

    class tool_panel final : public luil::ui_element
    {
    public:
        using ui_element::add_child;
        using ui_element::ui_element;

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    // 주 창과 같은 공통 캡션 경로다. 보조 창의 실제 procedure와 함께 재어,
    // 수정이 어느 한 top-level 창에만 들어가는 회귀를 막는다.
    class test_caption_surface final : public luil::win32::caption_surface
    {
    public:
        using caption_surface::caption_surface;

    private:
        [[nodiscard]] bool execute_caption_button(WPARAM) override
        {
            return false;
        }
    };

    class test_window
    {
    public:
        test_window(luil::win32::caption_surface& surface, const bool secondary)
            : surface_ { surface }
        {
            WNDCLASSEXW type {};
            type.cbSize = sizeof(type);
            type.hInstance = GetModuleHandleW(nullptr);
            type.lpszClassName = secondary ? L"Luil.Test.CaptionSecondary" : L"Luil.Test.CaptionCommon";
            type.lpfnWndProc = secondary ? &luil::win32::secondary_surface::static_procedure : &DefWindowProcW;
            if (RegisterClassExW(&type) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return;
            // 보이지 않는 창으로 OS 제목·아이콘과 실제 기본 처리의 반환값을 잰다.
            window_ = CreateWindowExW(0, type.lpszClassName, L"original", luil::win32::custom_window_style_for(surface.caption().buttons, luil::win32::window_display_mode::normal), 0, 0, 400, 300,
                nullptr, nullptr, type.hInstance, &surface);
            if (secondary == false && window_ != nullptr)
                surface_.attach_window(window_);
        }
        test_window(const test_window&) = delete;
        test_window(test_window&&) = delete;
        test_window& operator=(const test_window&) = delete;
        test_window& operator=(test_window&&) = delete;
        ~test_window()
        {
            if (window_ != nullptr)
                DestroyWindow(window_);
            surface_.detach_window();
        }
        [[nodiscard]] HWND handle() const noexcept
        {
            return window_;
        }

    private:
        luil::win32::caption_surface& surface_;
        HWND window_ { nullptr };
    };

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> tool_tree(const std::u8string_view owner)
    {
        auto root { std::make_unique<tool_panel>(luil::ui_element_id { luil::ui_element_kind::root, std::u8string { owner } }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 120.0f }, 1.0f });
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }
} // namespace

TEST_CASE("A secondary window adopts the caption and size limits of every frame", "[win32][window][caption]")
{
    // 캡션과 최소 크기는 창을 만들 때 한 번만 실렸다. 그 둘을 읽는 자리는 매번
    // 오는 OS 질문(비클라이언트 hit test·`WM_GETMINMAXINFO`)이라, 앱이 다음
    // frame에서 캡션을 바꾸면 그리기와 판정이 갈라섰다.
    fake_surface_context context {};
    luil::win32::secondary_surface surface { context, u8"tool" };

    luil::win32::ui_window source {};
    source.id = u8"tool";
    source.caption.title = u8"검사 결과";
    source.caption.buttons = { .minimize = true, .maximize = true, .close = true };
    source.minimum_width = 240.0f;
    source.minimum_height = 160.0f;
    source.tree = tool_tree(u8"first");

    // 창을 만들 때의 값이 실리고 첫 tree가 붙는다
    // (실물에서는 `create_secondary`가 이 순서로 한다).
    surface.set_caption(source.caption);
    surface.set_minimum_client_size(source.minimum_width, source.minimum_height);
    REQUIRE(surface.adopt(source));
    REQUIRE(surface.caption().title == u8"검사 결과");

    // 다음 frame이 캡션과 한계를 바꾼다.
    luil::win32::ui_window next { source };
    next.caption.title = u8"검사 결과 (2)";
    next.caption.buttons = { .minimize = false, .maximize = false, .close = true };
    next.caption.metrics.height = 28;
    next.minimum_width = 320.0f;
    next.minimum_height = 200.0f;
    next.tree = source.tree;

    // tree가 그대로라 다시 그릴 것은 없다 — 보이는 캡션은 앱이 tree에 담은
    // `caption_element`가 그리므로 픽셀이 같다. 그래도 값은 닿아야 한다.
    REQUIRE(surface.adopt(next) == false);
    REQUIRE(surface.caption().title == u8"검사 결과 (2)");
    REQUIRE(surface.caption().buttons.maximize == false);
    REQUIRE(surface.caption().buttons.minimize == false);
    // 비클라이언트 hit test가 이 치수로 캡션 자리를 판정한다.
    REQUIRE(surface.caption().metrics.height == 28);
    // `WM_GETMINMAXINFO`가 이 값으로 답한다.
    REQUIRE(surface.minimum_client_width() == 320.0f);
    REQUIRE(surface.minimum_client_height() == 200.0f);

    // tree가 바뀌면 다시 그리기가 필요하다고 보고한다.
    luil::win32::ui_window redrawn { next };
    redrawn.tree = tool_tree(u8"second");
    REQUIRE(surface.adopt(redrawn));
}

TEST_CASE("Caption activation permits both transitions and preserves window metadata", "[win32][window][caption]")
{
    for (const bool secondary : { false, true })
        for (const bool full_buttons : { false, true })
        {
            DYNAMIC_SECTION("secondary=" << secondary << ", full buttons=" << full_buttons)
            {
                fake_surface_context context {};
                test_caption_surface common { context };
                luil::win32::secondary_surface tool { context, u8"tool" };
                luil::win32::caption_surface& surface { secondary ? static_cast<luil::win32::caption_surface&>(tool) : common };
                luil::caption_config caption {};
                caption.buttons = { .minimize = full_buttons, .maximize = full_buttons, .close = true };
                surface.set_caption(caption);
                const test_window window { surface, secondary };
                REQUIRE(window.handle() != nullptr);
                std::u8string error {};
                REQUIRE(luil::win32::caption_surface::remove_system_caption(window.handle(), error));
                const LONG_PTR style { GetWindowLongPtrW(window.handle(), GWL_STYLE) };
                const HICON icon { LoadIconW(nullptr, IDI_APPLICATION) };
                SendMessageW(window.handle(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
                REQUIRE(SetWindowTextW(window.handle(), L"updated"));

                for (const auto dpi : std::array<std::uint32_t, 3> { 96, 144, 192 })
                {
                    surface.set_dpi(dpi);
                    for (int repeat { 0 }; repeat < 4; ++repeat)
                        for (const WPARAM active : { WPARAM { FALSE }, WPARAM { TRUE } })
                        {
                            const auto handled { surface.handle_caption_message(WM_NCACTIVATE, active, reinterpret_cast<LPARAM>(window.handle())) };
                            REQUIRE(handled.has_value());
                            if (active == FALSE)
                                REQUIRE(*handled == TRUE);
                            if (secondary)
                                REQUIRE(SendMessageW(window.handle(), WM_NCACTIVATE, active, 0) == *handled);
                        }
                }
                REQUIRE(GetWindowLongPtrW(window.handle(), GWL_STYLE) == style);
                wchar_t title[32] {};
                REQUIRE(GetWindowTextW(window.handle(), title, 32) == 7);
                REQUIRE(std::wcscmp(title, L"updated") == 0);
                REQUIRE(reinterpret_cast<HICON>(SendMessageW(window.handle(), WM_GETICON, ICON_SMALL, 0)) == icon);
                REQUIRE(GetSystemMenu(window.handle(), FALSE) != nullptr);
                // 실제 창을 화면에 띄우지 않고 최소화 분기도 기본 처리와 비교한다.
                SetWindowLongPtrW(window.handle(), GWL_STYLE, style | WS_MINIMIZE);
                REQUIRE(IsIconic(window.handle()));
                for (const WPARAM active : { WPARAM { FALSE }, WPARAM { TRUE } })
                    REQUIRE(surface.handle_caption_message(WM_NCACTIVATE, active, 0) == DefWindowProcW(window.handle(), WM_NCACTIVATE, active, 0));
                SetWindowLongPtrW(window.handle(), GWL_STYLE, style);
                // 필요성이 확인되지 않은 그리기·제목·아이콘 메시지는 가로채지 않는다.
                REQUIRE(surface.handle_caption_message(WM_NCPAINT, 1, 0).has_value() == false);
                REQUIRE(surface.handle_caption_message(WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"next")).has_value() == false);
                REQUIRE(surface.handle_caption_message(WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon)).has_value() == false);
            }
        }
}

namespace {
    struct capture_close
    {};

    class capture_driver final : public luil::logic_driver
    {
    public:
        explicit capture_driver(const bool payload_drag)
        {
            auto root { std::make_unique<tool_panel>(luil::ui_element_id { luil::ui_element_kind::root, u8"root" }) };
            root->arrange({ { 0.0f, 0.0f, 400.0f, 300.0f }, 1.0f });
            auto source { std::make_unique<tool_panel>(luil::ui_element_id { luil::application_element_kind(0), u8"source" }) };
            source->arrange({ { 10.0f, 10.0f, 100.0f, 40.0f }, 1.0f });
            source->set_action(luil::ui_trigger::left_click, [this](const luil::ui_action_context&) {
                ++clicks;
                return std::vector<luil::input_action> {};
            });
            if (payload_drag)
                source->set_drag_source(luil::drag_source {
                    [](const luil::ui_action_context&) {
                        luil::drag_payload payload {};
                        payload.dragged_owner = u8"file";
                        return payload;
                    },
                });
            else
            {
                luil::pointer_drag_target target {};
                target.start_distance = 8.0f;
                target.on_press = [this](const luil::ui_action_context&) {
                    ++drag_starts;
                    return std::vector<luil::input_action> {};
                };
                target.on_release = [this](const luil::ui_action_context& context, luil::drag_velocity) {
                    // 취소용 좌표가 보통 해제로 전달되면 Win32 경계에서 cancelled가 빠진 것이다.
                    // 마지막 자리와 수식 키 보존은 core test가 따로 검증한다.
                    if (context.x == -10000.0f || context.y == -10000.0f)
                        ++sentinel_releases;
                    ++drag_releases;
                    return std::vector<luil::input_action> {};
                };
                source->set_pointer_drag_target(std::move(target));
            }
            root->add_child(std::move(source));
            auto folder { std::make_unique<tool_panel>(luil::ui_element_id { luil::application_element_kind(0), u8"folder" }) };
            folder->arrange({ { 10.0f, 80.0f, 100.0f, 40.0f }, 1.0f });
            folder->set_drop_target(luil::drop_target {
                [](const luil::drag_payload&) { return true; },
                [this](const luil::drag_payload&, const luil::ui_action_context&) {
                    ++drops;
                    return std::vector<luil::input_action> {};
                },
            });
            root->add_child(std::move(folder));
            tree = std::make_shared<const luil::ui_tree>(std::move(root));
        }
        void handle(luil::app_message message) override
        {
            if (message.get<capture_close>() != nullptr)
                closed.store(true);
        }
        [[nodiscard]] std::shared_ptr<const luil::ui_frame> make_frame() override
        {
            auto frame { std::make_shared<luil::ui_frame>() };
            // 끌기는 주 tree가 아닌 이름 있는 보조 표면에 속한다.
            frame->windows.push_back(luil::ui_window { .id = u8"tool", .tree = tree });
            return frame;
        }
        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { capture_close {} };
        }
        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed.load();
        }
        std::shared_ptr<const luil::ui_tree> tree {};
        std::atomic<int> clicks { 0 };
        std::atomic<int> drops { 0 };
        std::atomic<int> drag_starts { 0 };
        std::atomic<int> drag_releases { 0 };
        std::atomic<int> sentinel_releases { 0 };
        std::atomic<bool> closed { false };
    };

    // 앞선 Win32 입력을 모두 처리한 뒤 UI thread가 카운터와 끌기 상태를 읽게 한다.
    class capture_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot& snapshot) override
        {
            dragging.store(snapshot.drag.has_value());
            ++fences;
            return {};
        }
        std::atomic<int> fences { 0 };
        std::atomic<bool> dragging { false };
    };

    void input_fence(luil::app_host& host, capture_policy& policy)
    {
        const int expected { policy.fences.load() + 1 };
        host.post_raw_input(luil::key_pressed_event { luil::key_code::f5 });
        const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 3 } };
        while (policy.fences.load() < expected && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds { 1 });
        REQUIRE(policy.fences.load() == expected);
        REQUIRE_FALSE(host.faulted());
    }
} // namespace

TEST_CASE("Win32 mouse capture loss cancels the owning surface through the input pump", "[win32][capture][surface-input]")
{
    for (const bool payload_drag : { false, true })
    {
        DYNAMIC_SECTION("payload drag=" << payload_drag)
        {
            capture_driver driver { payload_drag };
            capture_policy policy {};
            luil::app_host host { {}, driver, &policy };
            fake_surface_context context {};
            context.input_host = &host;
            luil::win32::secondary_surface surface { context, u8"tool" };
            luil::win32::secondary_surface thief { context, u8"other" };
            const test_window window { surface, true };
            const test_window other { thief, true };
            REQUIRE(window.handle() != nullptr);
            REQUIRE(other.handle() != nullptr);
            const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 3 } };
            while (host.acquire_frame() == nullptr && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds { 1 });
            REQUIRE(host.acquire_frame() != nullptr);

            // DOWN/MOVE만 보낸다. SetCapture가 이전 HWND에 WM_CAPTURECHANGED를
            // 동기 전달하게 하여 실제 캡처 상실 경로를 검증한다.
            SendMessageW(window.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 30));
            REQUIRE(GetCapture() == window.handle());
            REQUIRE(context.capture_active);
            SendMessageW(window.handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(30, 100));
            input_fence(host, policy);
            if (payload_drag)
                REQUIRE(policy.dragging.load());
            else
                REQUIRE(driver.drag_starts.load() == 1);

            REQUIRE(SetCapture(other.handle()) == window.handle());
            REQUIRE(GetCapture() == other.handle());
            REQUIRE_FALSE(context.capture_active);
            REQUIRE(context.canceled_surfaces == std::vector<std::u8string> { u8"tool" });
            input_fence(host, policy);
            REQUIRE_FALSE(policy.dragging.load());
            REQUIRE(driver.drops.load() == 0);
            REQUIRE(driver.clicks.load() == 0);
            REQUIRE(driver.sentinel_releases.load() == 0);
            if (payload_drag == false)
                REQUIRE(driver.drag_releases.load() == 1);

            // 빼앗은 캡처를 풀고, 놓기를 받는 폴더 위에 뒤늦은 UP을 보낸다.
            REQUIRE(ReleaseCapture());
            SendMessageW(window.handle(), WM_LBUTTONUP, 0, MAKELPARAM(30, 100));
            input_fence(host, policy);
            REQUIRE(driver.drops.load() == 0);
            REQUIRE(context.canceled_surfaces.size() == 1u);
            if (payload_drag == false)
                REQUIRE(driver.drag_releases.load() == 1);

            // 다음 보통 클릭이 캡처를 풀 때 중첩되는 WM_CAPTURECHANGED는
            // 클릭을 취소하거나 해제를 하나 더 보내면 안 된다.
            SendMessageW(window.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 30));
            REQUIRE(GetCapture() == window.handle());
            SendMessageW(window.handle(), WM_LBUTTONUP, 0, MAKELPARAM(30, 30));
            REQUIRE(GetCapture() == nullptr);
            input_fence(host, policy);
            REQUIRE(driver.clicks.load() == 1);
            REQUIRE(driver.drops.load() == 0);
            REQUIRE(context.canceled_surfaces.size() == 1u);
            if (payload_drag == false)
                REQUIRE(driver.drag_releases.load() == 1);
            if (payload_drag)
            {
                // 같은 Win32 경로에서 정상 끌어 놓기는 여전히 실행되어야 한다.
                SendMessageW(window.handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 30));
                SendMessageW(window.handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(30, 100));
                input_fence(host, policy);
                REQUIRE(policy.dragging.load());
                SendMessageW(window.handle(), WM_LBUTTONUP, 0, MAKELPARAM(30, 100));
                input_fence(host, policy);
                REQUIRE(driver.drops.load() == 1);
                REQUIRE_FALSE(policy.dragging.load());
                REQUIRE(context.canceled_surfaces.size() == 1u);
            }
            host.shutdown();
        }
    }
}
