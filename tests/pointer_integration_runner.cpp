#include "luil/win32/win32_window.h"

// UIAutomation 헤더는 Windows의 COM 선언이 먼저 있어야 한다.
#include <windows.h>

#include <objbase.h>
#include <uiautomation.h>
#include <wrl/client.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace luil::testing {
    int run_pointer_fixture(const std::filesystem::path& profile);

    namespace {
        using Microsoft::WRL::ComPtr;
        using clock = std::chrono::steady_clock;
        using json = nlohmann::json;

        void require(const bool condition, const std::string& message)
        {
            if (!condition)
                throw std::runtime_error { message };
        }

        void check(const HRESULT result, const char* operation)
        {
            require(SUCCEEDED(result), std::string { operation } + " HRESULT=" + std::to_string(static_cast<unsigned long>(result)));
        }

        [[nodiscard]] std::wstring string_value(BSTR value)
        {
            std::wstring result { value ? value : L"" };
            SysFreeString(value);
            return result;
        }

        [[nodiscard]] std::string utf8(const std::wstring& value)
        {
            if (value.empty())
                return {};
            const int length { static_cast<int>(value.size()) };
            const int bytes { WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, nullptr, 0, nullptr, nullptr) };
            require(bytes > 0, "UTF-8 conversion failed");
            std::string result(static_cast<std::size_t>(bytes), '\0');
            require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, result.data(), bytes, nullptr, nullptr) == bytes, "UTF-8 conversion failed");
            return result;
        }

        [[nodiscard]] std::wstring desktop_name(const HDESK desktop)
        {
            wchar_t name[256] {};
            DWORD needed {};
            require(GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed), "Cannot inspect input desktop");
            return name;
        }

        // 창·프로필은 이 프로세스가 만든 것만 거둔다. 실패 경로도 같은 수명을 따른다.
        class fixture_process
        {
        public:
            fixture_process()
            {
                wchar_t executable[32768] {};
                require(GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable))) != 0, "Cannot locate executable");
                profile_ = std::filesystem::temp_directory_path() / (L"luil-pointer-" + std::to_wstring(GetCurrentProcessId()));
                require(!std::filesystem::exists(profile_), "Temporary profile already exists");
                std::filesystem::create_directory(profile_);
                std::wstring command { L"\"" + std::wstring { executable } + L"\" --fixture \"" + profile_.wstring() + L"\"" };
                STARTUPINFOW startup {};
                startup.cb = sizeof(startup);
                if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_))
                {
                    std::filesystem::remove(profile_);
                    throw std::runtime_error { "Cannot launch fixture: " + std::to_string(GetLastError()) };
                }
                CloseHandle(process_.hThread);
                process_.hThread = nullptr;
            }
            fixture_process(const fixture_process&) = delete;
            fixture_process& operator=(const fixture_process&) = delete;
            ~fixture_process()
            {
                if (window)
                    PostMessageW(window, WM_CLOSE, 0, 0);
                if (WaitForSingleObject(process_.hProcess, 5000) == WAIT_TIMEOUT)
                {
                    TerminateProcess(process_.hProcess, 1);
                    WaitForSingleObject(process_.hProcess, 2000);
                }
                CloseHandle(process_.hProcess);
                std::error_code error {};
                for (int attempt { 0 }; attempt < 30; ++attempt)
                {
                    std::filesystem::remove_all(profile_, error);
                    if (!error)
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds { 100 });
                }
                if (error)
                    std::cerr << "Profile cleanup failed: " << profile_ << ": " << error.message() << '\n';
            }
            [[nodiscard]] HWND wait_window()
            {
                const auto deadline { clock::now() + std::chrono::seconds { 15 } };
                while (clock::now() < deadline)
                {
                    EnumWindows(
                        [](const HWND candidate, const LPARAM context) -> BOOL {
                            auto* self { reinterpret_cast<fixture_process*>(context) };
                            DWORD process_id {};
                            GetWindowThreadProcessId(candidate, &process_id);
                            if (process_id == self->process_.dwProcessId && IsWindowVisible(candidate) && GetWindow(candidate, GW_OWNER) == nullptr)
                            {
                                self->window = candidate;
                                return FALSE;
                            }
                            return TRUE;
                        },
                        reinterpret_cast<LPARAM>(this));
                    if (window)
                        return window;
                    require(WaitForSingleObject(process_.hProcess, 0) == WAIT_TIMEOUT, "Fixture exited before showing a window");
                    std::this_thread::sleep_for(std::chrono::milliseconds { 30 });
                }
                throw std::runtime_error { "Fixture window timeout" };
            }
            HWND window { nullptr };

        private:
            PROCESS_INFORMATION process_ {};
            std::filesystem::path profile_ {};
        };

        class uia_client
        {
        public:
            explicit uia_client(const HWND window)
            {
                check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation_)), "Create UIA");
                check(automation_->ElementFromHandle(window, &root_), "UIA fixture root");
                check(automation_->CreateTrueCondition(&condition_), "UIA condition");
            }

            [[nodiscard]] ComPtr<IUIAutomationElement> find(const std::wstring& owner, const CONTROLTYPEID type = 0)
            {
                ComPtr<IUIAutomationElementArray> elements {};
                check(root_->FindAll(TreeScope_Descendants, condition_.Get(), &elements), "UIA FindAll");
                int count {};
                check(elements->get_Length(&count), "UIA element count");
                for (int index { 0 }; index < count; ++index)
                {
                    ComPtr<IUIAutomationElement> element {};
                    if (FAILED(elements->GetElement(index, &element)))
                        continue;
                    BSTR value {};
                    if (FAILED(element->get_CurrentAutomationId(&value)))
                        continue;
                    const std::wstring id { string_value(value) };
                    if (id.ends_with(L":" + owner))
                    {
                        CONTROLTYPEID actual_type {};
                        if (type == 0 || (SUCCEEDED(element->get_CurrentControlType(&actual_type)) && actual_type == type))
                            return element;
                    }
                }
                return {};
            }

            [[nodiscard]] ComPtr<IUIAutomationElement> element(const std::wstring& owner, const CONTROLTYPEID type = 0)
            {
                const auto deadline { clock::now() + std::chrono::seconds { 5 } };
                do
                {
                    auto found { find(owner, type) };
                    if (found)
                        return found;
                    std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
                } while (clock::now() < deadline);
                throw std::runtime_error { "UIA owner missing: " + utf8(owner) };
            }

            [[nodiscard]] json status()
            {
                BSTR value {};
                check(element(L"status")->get_CurrentName(&value), "UIA status name");
                const auto name { string_value(value) };
                return json::parse(utf8(name));
            }

            [[nodiscard]] json wait(const std::function<bool(const json&)>& predicate, const std::string& message, const std::chrono::milliseconds timeout = std::chrono::seconds { 5 })
            {
                const auto deadline { clock::now() + timeout };
                json last {};
                do
                {
                    last = status();
                    if (predicate(last))
                        return last;
                    std::this_thread::sleep_for(std::chrono::milliseconds { 15 });
                } while (clock::now() < deadline);
                throw std::runtime_error { message + ": " + last.dump() };
            }

            void invoke(const std::wstring& owner)
            {
                ComPtr<IUIAutomationInvokePattern> pattern {};
                check(element(owner)->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&pattern)), "UIA Invoke pattern");
                check(pattern->Invoke(), "UIA Invoke");
            }

            [[nodiscard]] RECT bounds(const std::wstring& owner)
            {
                RECT rectangle {};
                check(element(owner)->get_CurrentBoundingRectangle(&rectangle), "UIA physical bounds");
                require(rectangle.right > rectangle.left && rectangle.bottom > rectangle.top, "UIA target has no visible bounds");
                return rectangle;
            }

            [[nodiscard]] POINT point(const std::wstring& owner, const double x = 0.5, const double y = 0.5)
            {
                const RECT rectangle { bounds(owner) };
                return { rectangle.left + static_cast<LONG>((rectangle.right - rectangle.left) * x), rectangle.top + static_cast<LONG>((rectangle.bottom - rectangle.top) * y) };
            }

            [[nodiscard]] std::wstring selected_text()
            {
                ComPtr<IUIAutomationTextPattern> pattern {};
                check(element(L"text")->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern)), "UIA Text pattern");
                ComPtr<IUIAutomationTextRangeArray> ranges {};
                check(pattern->GetSelection(&ranges), "UIA selection");
                int length {};
                check(ranges->get_Length(&length), "UIA selection count");
                require(length == 1, "Expected one selection range");
                ComPtr<IUIAutomationTextRange> range {};
                check(ranges->GetElement(0, &range), "UIA selection range");
                BSTR value {};
                check(range->GetText(-1, &value), "UIA selection text");
                return string_value(value);
            }

        private:
            ComPtr<IUIAutomation> automation_ {};
            ComPtr<IUIAutomationElement> root_ {};
            ComPtr<IUIAutomationCondition> condition_ {};
        };

        // 주입 직전에도 창 소유자를 확인해 가려진 자리에 입력을 보내지 않는다.
        void target_window(const HWND window, const POINT point)
        {
            wchar_t foreground_class[256] {};
            GetClassNameW(GetForegroundWindow(), foreground_class, static_cast<int>(std::size(foreground_class)));
            require(GetForegroundWindow() == window, "Fixture lost foreground to " + utf8(foreground_class) + "; stop injecting input");
            const HWND hit { WindowFromPoint(point) };
            wchar_t class_name[256] {};
            GetClassNameW(hit, class_name, static_cast<int>(std::size(class_name)));
            require(GetAncestor(hit, GA_ROOT) == window, "Input target is obscured at " + std::to_string(point.x) + "," + std::to_string(point.y) + " by " + utf8(class_name));
        }

        class synthetic_device
        {
        public:
            synthetic_device(const POINTER_INPUT_TYPE type, const HWND window)
                : type_ { type }
                , window_ { window }
                , device_ { CreateSyntheticPointerDevice(type, 1, POINTER_FEEDBACK_NONE) }
            {
                require(device_ != nullptr, "CreateSyntheticPointerDevice failed: " + std::to_string(GetLastError()));
            }
            synthetic_device(const synthetic_device&) = delete;
            synthetic_device& operator=(const synthetic_device&) = delete;
            ~synthetic_device()
            {
                // 중간 assertion 실패에서도 접촉을 끝낸 뒤 장치를 없앤다.
                if (active_)
                {
                    sample_.touchInfo.pointerInfo.pointerFlags = POINTER_FLAG_UP | POINTER_FLAG_CANCELED;
                    InjectSyntheticPointerInput(device_, &sample_, 1);
                }
                if (device_ != nullptr)
                    DestroySyntheticPointerDevice(device_);
            }

            void remove()
            {
                DestroySyntheticPointerDevice(device_);
                device_ = nullptr;
                active_ = false;
            }

            void send(const POINT point, const POINTER_FLAGS flags, const PEN_FLAGS pen_flags = PEN_FLAG_NONE)
            {
                target_window(window_, point);
                if (device_ == nullptr)
                {
                    device_ = CreateSyntheticPointerDevice(type_, 1, POINTER_FEEDBACK_NONE);
                    require(device_ != nullptr, "Cannot recreate synthetic pointer device");
                }
                sample_ = {};
                sample_.type = type_;
                auto& info { type_ == PT_TOUCH ? sample_.touchInfo.pointerInfo : sample_.penInfo.pointerInfo };
                info.pointerType = type_;
                info.pointerId = 1;
                info.ptPixelLocation = point;
                info.pointerFlags = flags;
                info.dwKeyStates = ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? POINTER_MOD_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? POINTER_MOD_CTRL : 0);
                if (type_ == PT_TOUCH)
                {
                    sample_.touchInfo.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_PRESSURE | TOUCH_MASK_ORIENTATION;
                    sample_.touchInfo.rcContact = { point.x - 2, point.y - 2, point.x + 2, point.y + 2 };
                    sample_.touchInfo.pressure = 512;
                    sample_.touchInfo.orientation = 90;
                }
                else
                {
                    sample_.penInfo.penFlags = pen_flags;
                    sample_.penInfo.penMask = PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
                    sample_.penInfo.pressure = flags & POINTER_FLAG_INCONTACT ? 512 : 0;
                    sample_.penInfo.tiltX = 10;
                    sample_.penInfo.tiltY = -10;
                }
                for (int attempt { 0 }; attempt < 5; ++attempt)
                {
                    if (InjectSyntheticPointerInput(device_, &sample_, 1))
                    {
                        active_ = (flags & POINTER_FLAG_INCONTACT) != 0;
                        std::this_thread::sleep_for(std::chrono::milliseconds { 8 });
                        return;
                    }
                    const DWORD error { GetLastError() };
                    require(error == ERROR_NOT_READY, "InjectSyntheticPointerInput failed: " + std::to_string(error));
                    std::this_thread::sleep_for(std::chrono::milliseconds { 8 });
                }
                throw std::runtime_error { "InjectSyntheticPointerInput remained NOT_READY" };
            }

            void down(const POINT point, const PEN_FLAGS flags = PEN_FLAG_NONE)
            {
                send(point, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT, flags);
            }
            void move(const POINT point)
            {
                send(point, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT);
            }
            void up(const POINT point, const PEN_FLAGS flags = PEN_FLAG_NONE)
            {
                send(point, POINTER_FLAG_UP | (type_ == PT_PEN ? POINTER_FLAG_INRANGE : POINTER_FLAG_NONE), flags);
                if (type_ == PT_PEN)
                    send(point, POINTER_FLAG_UPDATE, flags);
            }

        private:
            POINTER_INPUT_TYPE type_;
            HWND window_;
            HSYNTHETICPOINTERDEVICE device_;
            POINTER_TYPE_INFO sample_ {};
            bool active_ { false };
        };

        class input_restore
        {
        public:
            input_restore()
                : foreground_ { GetForegroundWindow() }
            {
                GetCursorPos(&cursor_);
                require(!(GetAsyncKeyState(VK_SHIFT) & 0x8000) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000) && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)
                        && !(GetAsyncKeyState(VK_RBUTTON) & 0x8000),
                    "Release keyboard modifiers and mouse buttons before running");
            }
            input_restore(const input_restore&) = delete;
            input_restore& operator=(const input_restore&) = delete;
            ~input_restore()
            {
                if (shift_)
                {
                    INPUT input {};
                    input.type = INPUT_KEYBOARD;
                    input.ki.wVk = VK_LSHIFT;
                    input.ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(1, &input, sizeof(input));
                }
                if (mouse_)
                {
                    INPUT input {};
                    input.type = INPUT_MOUSE;
                    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                    SendInput(1, &input, sizeof(input));
                }
                SetCursorPos(cursor_.x, cursor_.y);
                if (IsWindow(foreground_))
                    SetForegroundWindow(foreground_);
            }
            void key(const HWND window, const bool down)
            {
                require(GetForegroundWindow() == window, "Fixture lost foreground before Shift injection");
                INPUT input {};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = VK_LSHIFT;
                input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
                require(SendInput(1, &input, sizeof(input)) == 1, "Shift injection failed");
                shift_ = down;
            }
            void mouse(const HWND window, const POINT point, const DWORD flags)
            {
                target_window(window, point);
                INPUT input {};
                input.type = INPUT_MOUSE;
                input.mi.dx = MulDiv(point.x - GetSystemMetrics(SM_XVIRTUALSCREEN), 65535, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1);
                input.mi.dy = MulDiv(point.y - GetSystemMetrics(SM_YVIRTUALSCREEN), 65535, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
                input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | flags;
                require(SendInput(1, &input, sizeof(input)) == 1, "Mouse injection failed");
                if (flags & MOUSEEVENTF_LEFTDOWN)
                    mouse_ = true;
                if (flags & MOUSEEVENTF_LEFTUP)
                    mouse_ = false;
                std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
            }

        private:
            HWND foreground_;
            POINT cursor_ {};
            bool shift_ { false };
            bool mouse_ { false };
        };

        void settle()
        {
            // 부정 assertion에는 비동기 처리가 나타날 수 있는 관찰 구간이 필요하다.
            std::this_thread::sleep_for(std::chrono::milliseconds { 200 });
        }

        int run_integration()
        {
            win32::enable_per_monitor_dpi_awareness();
            const win32::com_sta_scope com {};
            require(com.succeeded(), "COM initialization failed");
            const HDESK input_desktop { OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS) };
            require(input_desktop != nullptr, "Interactive input desktop is unavailable");
            bool same_desktop {};
            try
            {
                same_desktop = desktop_name(input_desktop) == desktop_name(GetThreadDesktop(GetCurrentThreadId()));
            }
            catch (...)
            {
                CloseDesktop(input_desktop);
                throw;
            }
            CloseDesktop(input_desktop);
            require(same_desktop, "Runner is not on the active input desktop");

            input_restore restore {};
            fixture_process process {};
            const HWND window { process.wait_window() };
            uia_client uia { window };
            static_cast<void>(uia.wait([](const json& state) { return state.value("observer_ready", false); }, "Fixture/native observer readiness"));
#if LUIL_POINTER_TEST_WEBVIEW
            static_cast<void>(uia.wait([](const json& state) { return state["web"].value("ready", false); }, "WebView runtime/page readiness", std::chrono::seconds { 20 }));
#endif
            require(SetForegroundWindow(window), "Windows refused fixture activation");
            synthetic_device touch { PT_TOUCH, window };
            synthetic_device pen { PT_PEN, window };
            int passed { 0 };
            auto test = [&](const char* name, const std::function<void()>& action) {
                action();
                ++passed;
                std::cout << "PASS " << name << '\n';
            };
            auto click = [&](synthetic_device& device, const POINT point, const PEN_FLAGS flags = PEN_FLAG_NONE) {
                device.down(point, flags);
                device.up(point, flags);
            };
            auto hold = [&](synthetic_device& device, const POINT point) {
                // 합성 접촉도 digitizer처럼 프레임을 계속 보내야 OS가 누름을 유지한다.
                const auto deadline { clock::now() + std::chrono::milliseconds { 750 } };
                while (clock::now() < deadline)
                {
                    device.move(point);
                    std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
                }
            };
            auto no_change = [&](const json& before, const bool scroll) {
                settle();
                const auto after { uia.status() };
                require(after["left"] == before["left"] && after["right"] == before["right"], "Unexpected click/context action: " + after.dump());
                if (scroll)
                    require(after["scroll"] == before["scroll"], "Non-touch input started pan: " + after.dump());
            };
            auto reset_scroll = [&] {
                ComPtr<IUIAutomationRangeValuePattern> pattern {};
                check(uia.element(L"scroll", UIA_ScrollBarControlTypeId)->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&pattern)), "UIA scroll RangeValue");
                check(pattern->SetValue(0.0), "UIA reset scroll");
                static_cast<void>(uia.wait([](const json& state) { return state["scroll"] == 0.0f; }, "Scroll reset"));
            };
            const POINT button { uia.point(L"click") };
            const POINT pan_start { uia.point(L"scroll-content", 0.5, 0.8) };
            const POINT pan_end { uia.point(L"scroll-content", 0.5, 0.25) };

            test("touch tap / one activation / native WM_POINTER", [&] {
                const auto before { uia.status() };
                click(touch, button);
                static_cast<void>(uia.wait([&](const json& state) { return state["left"] == before["left"].get<int>() + 1 && state["touch_down"] > before["touch_down"]; }, "Touch tap"));
                settle();
                require(uia.status()["left"] == before["left"].get<int>() + 1, "Promoted mouse duplicated touch click");
            });
            test("touch long press / right click only", [&] {
                const auto before { uia.status() };
                touch.down(button);
                hold(touch, button);
                touch.up(button);
                const auto after { uia.wait([&](const json& state) { return state["right"] == before["right"].get<int>() + 1; }, "Touch long press") };
                require(after["left"] == before["left"], "Long press also clicked");
            });
            test("touch pan / scroll / no click", [&] {
                reset_scroll();
                const auto before { uia.status() };
                touch.down(pan_start);
                touch.move(pan_end);
                touch.up(pan_end);
                static_cast<void>(uia.wait([](const json& state) { return state["scroll"] > 20.0f; }, "Touch pan"));
                no_change(before, false);
            });
            test("runtime pan disabled during contact", [&] {
                reset_scroll();
                touch.down(pan_start);
                touch.move(pan_end);
                static_cast<void>(uia.wait([](const json& state) { return state["scroll"] > 20.0f; }, "Pan started"));
                uia.invoke(L"pan");
                const auto stopped { uia.wait([](const json& state) { return state["pan"] == false && state["config_ok"] == true; }, "Disable pan") };
                touch.move(pan_start);
                touch.up(pan_start);
                no_change(stopped, true);
                uia.invoke(L"pan");
                static_cast<void>(uia.wait([](const json& state) { return state["pan"] == true; }, "Restore pan"));
            });
            test("runtime long press disabled / no context action", [&] {
                const auto before { uia.status() };
                touch.down(button);
                uia.invoke(L"hold");
                static_cast<void>(uia.wait([](const json& state) { return state["hold"] == false; }, "Disable hold"));
                hold(touch, button);
                touch.up(button);
                settle();
                require(uia.status()["right"] == before["right"], "Disabled hold invoked context action");
                uia.invoke(L"hold");
                static_cast<void>(uia.wait([](const json& state) { return state["hold"] == true; }, "Restore hold"));
            });
            test("target disabled during touch", [&] {
                const auto before { uia.status() };
                touch.down(button);
                uia.invoke(L"target");
                static_cast<void>(uia.wait([](const json& state) { return state["enabled"] == false; }, "Disable target"));
                touch.up(button);
                no_change(before, false);
                uia.invoke(L"target");
                static_cast<void>(uia.wait([](const json& state) { return state["enabled"] == true; }, "Restore target"));
            });
            test("pen tip click / native WM_POINTER", [&] {
                const auto before { uia.status() };
                click(pen, button);
                static_cast<void>(uia.wait([&](const json& state) { return state["left"] == before["left"].get<int>() + 1 && state["pen_down"] > before["pen_down"]; }, "Pen tip"));
                settle();
                require(uia.status()["left"] == before["left"].get<int>() + 1, "Promoted mouse duplicated pen click");
            });
            test("pen barrel / right click", [&] {
                const auto before { uia.status() };
                click(pen, button, PEN_FLAG_BARREL);
                const auto after { uia.wait([&](const json& state) { return state["right"] == before["right"].get<int>() + 1; }, "Pen barrel") };
                require(after["left"] == before["left"], "Barrel also left clicked");
            });
            test("pen hover / no action", [&] {
                const auto before { uia.status() };
                pen.send(button, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE);
                static_cast<void>(uia.wait([&](const json& state) { return state["pen_hover"] > before["pen_hover"]; }, "Pen hover delivered"));
                no_change(before, false);
            });
            test("pen long hold / ordinary left click", [&] {
                const auto before { uia.status() };
                pen.down(button);
                hold(pen, button);
                pen.up(button);
                const auto after { uia.wait([&](const json& state) { return state["left"] == before["left"].get<int>() + 1; }, "Pen hold") };
                require(after["right"] == before["right"], "Pen hold became touch long press");
            });
            test("pen drag / no touch pan", [&] {
                reset_scroll();
                const auto before { uia.status() };
                pen.down(pan_start);
                pen.move(pan_end);
                pen.up(pan_end);
                no_change(before, true);
            });
            test("pen eraser / no ordinary click", [&] {
                const auto before { uia.status() };
                click(pen, button, PEN_FLAG_ERASER | PEN_FLAG_INVERTED);
                static_cast<void>(uia.wait([&](const json& state) { return state["pen_down"] == before["pen_down"].get<int>() + 1; }, "Eraser contact delivered"));
                no_change(before, false);
            });
            test("pen Shift click / UIA text selection", [&] {
                pen.remove();
                click(pen, uia.point(L"text", 0.1));
                const auto anchored { uia.wait([](const json& state) { return state["caret"] > 0 && state["caret"] == state["anchor"]; }, "Text anchor") };
                restore.key(window, true);
                click(pen, uia.point(L"text", 0.45));
                restore.key(window, false);
                const auto selected { uia.wait([](const json& state) { return state["caret"] != state["anchor"]; }, "Shift selection") };
                require(selected["anchor"] == anchored["anchor"], "Shift click lost anchor");
                require(!uia.selected_text().empty(), "UIA reported empty selection");
            });
            test("mouse long hold / ordinary left click", [&] {
                const auto before { uia.status() };
                restore.mouse(window, button, MOUSEEVENTF_LEFTDOWN);
                std::this_thread::sleep_for(std::chrono::milliseconds { 750 });
                restore.mouse(window, button, MOUSEEVENTF_LEFTUP);
                const auto after { uia.wait([&](const json& state) { return state["left"] == before["left"].get<int>() + 1; }, "Mouse hold") };
                require(after["right"] == before["right"] && after["mouse_down"] > before["mouse_down"], "Mouse hold became a gesture");
            });
            test("mouse drag / no touch pan", [&] {
                reset_scroll();
                const auto before { uia.status() };
                restore.mouse(window, pan_start, MOUSEEVENTF_LEFTDOWN);
                restore.mouse(window, pan_end, 0);
                restore.mouse(window, pan_end, MOUSEEVENTF_LEFTUP);
                no_change(before, true);
            });
#if LUIL_POINTER_TEST_WEBVIEW
            static_cast<void>(uia.wait([](const json& state) { return state["web"].value("ready", false); }, "WebView runtime/page readiness", std::chrono::seconds { 20 }));
            const POINT web_point { uia.point(L"web") };
            auto web_click = [&](synthetic_device& device, const char* type) {
                const auto before { uia.status()["web"] };
                click(device, web_point);
                json state_after {};
                state_after = uia.wait([&](const json& state) { return state["web"].value("down", 0) == before["down"].get<int>() + 1 && state["web"].value("up", 0) == before["up"].get<int>() + 1; },
                    "Web pointer sequence");
                const auto after { state_after["web"] };
                require(after["type"] == type && after["orphan"] == 0, "WebView pointer identity/sequence mismatch");
            };
            test("WebView touch pointerType / paired down-up", [&] { web_click(touch, "touch"); });
            test("WebView pen pointerType / paired down-up", [&] { web_click(pen, "pen"); });
            test("WebView hide during touch / terminal event / late UP swallowed", [&] {
                const auto before { uia.status() };
                touch.down(web_point);
                static_cast<void>(uia.wait([&](const json& state) { return state["web"].value("down", 0) == before["web"]["down"].get<int>() + 1; }, "Web touch down"));
                uia.invoke(L"web-visible");
                json hidden {};
                hidden = uia.wait(
                    [&](const json& state) {
                        return state["web_visible"] == false
                            && state["web"].value("up", 0) + state["web"].value("cancel", 0) == before["web"]["up"].get<int>() + before["web"]["cancel"].get<int>() + 1;
                    },
                    "Hidden WebView contact termination");
                touch.up(web_point);
                settle();
                require(uia.status()["web"] == hidden["web"], "Late UP delivered after WebView cancellation");
                no_change(before, false);
                uia.invoke(L"web-visible");
                static_cast<void>(uia.wait([](const json& state) { return state["web_visible"] == true; }, "Restore WebView"));
                web_click(touch, "touch");
            });
#else
            std::cout << "SKIP WebView coverage: LUIL_ENABLE_WEBVIEW=OFF\n";
#endif
            std::cout << passed << " native pointer integration scenarios passed\n";
            return 0;
        }
    } // namespace
} // namespace luil::testing

int wmain(const int count, wchar_t** arguments)
{
    try
    {
        if (count == 3 && std::wstring_view { arguments[1] } == L"--fixture")
            return luil::testing::run_pointer_fixture(arguments[2]);
        if (count == 2 && std::wstring_view { arguments[1] } == L"--interactive")
            return luil::testing::run_integration();
        if (count != 1)
            throw std::runtime_error { "Usage: luil_pointer_integration [--interactive]" };
        std::cout << "SKIP: pass --interactive on a dedicated active Windows test desktop.\n"
                     "This runner injects actual touch, pen, mouse and Shift input.\n";
        return 77;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
