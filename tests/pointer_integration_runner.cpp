#include "luil/win32/win32_window.h"

// UIAutomation 헤더는 Windows의 COM 선언이 먼저 있어야 한다.
#include <windows.h>

#include <objbase.h>
#include <uiautomation.h>
#include <wrl/client.h>

#include <nlohmann/json.hpp>

#include <array>
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

            void activate()
            {
                check(element(L"click")->SetFocus(), "UIA fixture focus");
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

        void activate_window(const HWND window)
        {
            SetLastError(0);
            const BOOL requested { SetForegroundWindow(window) };
            const DWORD error { GetLastError() };
            DWORD_PTR result {};
            require(SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &result) != 0, "Fixture stopped processing activation messages");
            if (GetForegroundWindow() == window)
                return;
            const HWND foreground { GetForegroundWindow() };
            wchar_t class_name[256] {};
            GetClassNameW(foreground, class_name, static_cast<int>(std::size(class_name)));
            wchar_t target_class[256] {};
            GetClassNameW(window, target_class, static_cast<int>(std::size(target_class)));
            require(foreground == window,
                "Windows refused fixture activation: target=" + utf8(target_class) + ", foreground=" + utf8(class_name) + ", requested=" + std::to_string(requested)
                    + ", enabled=" + std::to_string(IsWindowEnabled(window)) + ", visible=" + std::to_string(IsWindowVisible(window)) + ", error=" + std::to_string(error));
        }

        class synthetic_device
        {
        public:
            synthetic_device(const POINTER_INPUT_TYPE type, const HWND window)
                : type_ { type }
                , window_ { window }
                , device_ { CreateSyntheticPointerDevice(type, type == PT_TOUCH ? 2 : 1, POINTER_FEEDBACK_NONE) }
            {
                require(device_ != nullptr, "CreateSyntheticPointerDevice failed: " + std::to_string(GetLastError()));
            }
            synthetic_device(const synthetic_device&) = delete;
            synthetic_device& operator=(const synthetic_device&) = delete;
            ~synthetic_device()
            {
                // 중간 assertion 실패에서도 접촉을 끝낸 뒤 장치를 없앤다.
                std::array<POINTER_TYPE_INFO, 2> endings {};
                UINT32 count { 0 };
                for (std::size_t index { 0 }; index < active_.size(); ++index)
                {
                    if (active_[index])
                    {
                        endings[count] = samples_[index];
                        endings[count++].touchInfo.pointerInfo.pointerFlags = POINTER_FLAG_UP | POINTER_FLAG_CANCELED;
                    }
                }
                if (count != 0)
                    InjectSyntheticPointerInput(device_, endings.data(), count);
                if (device_ != nullptr)
                    DestroySyntheticPointerDevice(device_);
            }

            void remove()
            {
                DestroySyntheticPointerDevice(device_);
                device_ = nullptr;
                active_ = {};
            }

            void send(const POINT point, const POINTER_FLAGS flags, const PEN_FLAGS pen_flags = PEN_FLAG_NONE, const std::size_t index = 0)
            {
                target_window(window_, point);
                require(index < (type_ == PT_TOUCH ? 2u : 1u), "Invalid synthetic pointer index");
                if (device_ == nullptr)
                {
                    device_ = CreateSyntheticPointerDevice(type_, type_ == PT_TOUCH ? 2 : 1, POINTER_FEEDBACK_NONE);
                    require(device_ != nullptr, "Cannot recreate synthetic pointer device");
                }
                auto& sample { samples_[index] };
                sample = {};
                sample.type = type_;
                auto& info { type_ == PT_TOUCH ? sample.touchInfo.pointerInfo : sample.penInfo.pointerInfo };
                info.pointerType = type_;
                info.pointerId = static_cast<UINT32>(index + 1);
                info.ptPixelLocation = point;
                info.pointerFlags = flags;
                info.dwKeyStates = ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? POINTER_MOD_SHIFT : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? POINTER_MOD_CTRL : 0);
                if (type_ == PT_TOUCH)
                {
                    sample.touchInfo.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_PRESSURE | TOUCH_MASK_ORIENTATION;
                    sample.touchInfo.rcContact = { point.x - 2, point.y - 2, point.x + 2, point.y + 2 };
                    sample.touchInfo.pressure = 512;
                    sample.touchInfo.orientation = 90;
                }
                else
                {
                    sample.penInfo.penFlags = pen_flags;
                    sample.penInfo.penMask = PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
                    sample.penInfo.pressure = flags & POINTER_FLAG_INCONTACT ? 512 : 0;
                    sample.penInfo.tiltX = 10;
                    sample.penInfo.tiltY = -10;
                }
                // 같은 장치의 다른 접촉도 매 프레임에 실어 OS의 접촉 수명을 유지한다.
                std::array<POINTER_TYPE_INFO, 2> frame {};
                UINT32 count { 0 };
                for (std::size_t contact { 0 }; contact < samples_.size(); ++contact)
                {
                    if (contact == index)
                        frame[count++] = sample;
                    else if (active_[contact])
                    {
                        frame[count] = samples_[contact];
                        frame[count++].touchInfo.pointerInfo.pointerFlags = POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
                    }
                }
                for (int attempt { 0 }; attempt < 5; ++attempt)
                {
                    if (InjectSyntheticPointerInput(device_, frame.data(), count))
                    {
                        active_[index] = (flags & POINTER_FLAG_INCONTACT) != 0;
                        std::this_thread::sleep_for(std::chrono::milliseconds { 8 });
                        return;
                    }
                    const DWORD error { GetLastError() };
                    require(error == ERROR_NOT_READY, "InjectSyntheticPointerInput failed: " + std::to_string(error));
                    std::this_thread::sleep_for(std::chrono::milliseconds { 8 });
                }
                throw std::runtime_error { "InjectSyntheticPointerInput remained NOT_READY" };
            }

            void down(const POINT point, const PEN_FLAGS flags = PEN_FLAG_NONE, const std::size_t index = 0)
            {
                send(point, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT, flags, index);
            }
            void move(const POINT point, const std::size_t index = 0, const PEN_FLAGS flags = PEN_FLAG_NONE)
            {
                send(point, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT, flags, index);
            }
            void up(const POINT point, const PEN_FLAGS flags = PEN_FLAG_NONE, const std::size_t index = 0)
            {
                send(point, POINTER_FLAG_UP | (type_ == PT_PEN ? POINTER_FLAG_INRANGE : POINTER_FLAG_NONE), flags, index);
                if (type_ == PT_PEN)
                    send(point, POINTER_FLAG_UPDATE, flags);
            }

        private:
            POINTER_INPUT_TYPE type_;
            HWND window_;
            HSYNTHETICPOINTERDEVICE device_;
            std::array<POINTER_TYPE_INFO, 2> samples_ {};
            std::array<bool, 2> active_ {};
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
            uia.activate();
            activate_window(window);
            std::cout << "ENV window DPI=" << GetDpiForWindow(window) << ", virtual screen=" << GetSystemMetrics(SM_XVIRTUALSCREEN) << ',' << GetSystemMetrics(SM_YVIRTUALSCREEN) << ' '
                      << GetSystemMetrics(SM_CXVIRTUALSCREEN) << 'x' << GetSystemMetrics(SM_CYVIRTUALSCREEN) << '\n';
            synthetic_device touch { PT_TOUCH, window };
            synthetic_device pen { PT_PEN, window };
            int passed { 0 };
            int skipped { 0 };
            auto test = [&](const char* name, const std::function<void()>& action) {
                action();
                ++passed;
                std::cout << "PASS " << name << '\n';
            };
            auto skip = [&](const std::string& message) {
                ++skipped;
                std::cout << "SKIP " << message << '\n';
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
            {
                const auto before { uia.status() };
                uia.invoke(L"zoom-visible");
                static_cast<void>(uia.wait([](const json& state) { return state["zoom_visible"] == true; }, "Show zoom target"));
                const POINT first { uia.point(L"zoom", 0.25) };
                const POINT second { uia.point(L"zoom", 0.75) };
                touch.down(first);
                static_cast<void>(uia.wait([&](const json& state) { return state["touch_down"] > before["touch_down"]; }, "First zoom contact"));
                touch.down(second, PEN_FLAG_NONE, 1);
                const auto deadline { clock::now() + std::chrono::seconds { 1 } };
                json during {};
                do
                {
                    during = uia.status();
                    if (during["touch_down"] >= before["touch_down"].get<int>() + 2)
                        break;
                    touch.move(second, 1);
                } while (clock::now() < deadline);
                if (during["touch_down"] != before["touch_down"].get<int>() + 2 || during["native_touch_frame"].size() != 2)
                {
                    touch.up(first);
                    touch.up(second, PEN_FLAG_NONE, 1);
                    skip("Zoom pinch: injection did not reach host as two native contacts; down=" + during["touch_down"].dump() + ", before=" + before["touch_down"].dump()
                        + ", frame=" + during["native_touch_frame"].dump() + ", trace=" + during["native_trace"].dump());
                }
                else
                    test("zoom pinch / expand contract / remaining finger pan", [&] {
                        const POINT outer_first { uia.point(L"zoom", 0.1) };
                        const POINT outer_second { uia.point(L"zoom", 0.9) };
                        touch.move(outer_first);
                        touch.move(outer_second, 1);
                        const auto expanded { uia.wait([&](const json& state) { return state["zoom"].get<float>() > before["zoom"].get<float>() * 1.3f; }, "Pinch expand") };
                        const POINT inner_first { uia.point(L"zoom", 0.3) };
                        const POINT inner_second { uia.point(L"zoom", 0.7) };
                        touch.move(inner_first);
                        touch.move(inner_second, 1);
                        const auto contracted { uia.wait([&](const json& state) { return state["zoom"].get<float>() < expanded["zoom"].get<float>() * 0.8f; }, "Pinch contract") };
                        touch.up(inner_first);
                        POINT moved { inner_second };
                        moved.y += 24;
                        touch.move(moved, 1);
                        const auto panned { uia.wait([&](const json& state) { return state["zoom_y"].get<float>() > contracted["zoom_y"].get<float>() + 8.0f; }, "Remaining finger pan") };
                        touch.up(moved, PEN_FLAG_NONE, 1);
                        require(panned["zoom"] == contracted["zoom"], "Remaining finger changed zoom");
                        no_change(before, true);
                    });
                uia.invoke(L"zoom-visible");
                static_cast<void>(uia.wait([](const json& state) { return state["zoom_visible"] == false; }, "Hide zoom target"));
            }
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
            test("second finger released first / only owner clicks", [&] {
                reset_scroll();
                const auto before { uia.status() };
                touch.down(button);
                touch.down(pan_start, PEN_FLAG_NONE, 1);
                touch.move(pan_end, 1);
                touch.up(pan_end, PEN_FLAG_NONE, 1);
                no_change(before, true);
                touch.up(button);
                const auto after { uia.wait([&](const json& state) { return state["left"] == before["left"].get<int>() + 1; }, "Owner finger click") };
                require(after["touch_down"] == before["touch_down"].get<int>() + 2, "Second native contact did not arrive");
                require(after["right"] == before["right"] && after["scroll"] == before["scroll"], "Second contact took over controls");
            });
            test("owner released first / remaining finger never takes over", [&] {
                reset_scroll();
                const auto before { uia.status() };
                touch.down(button);
                touch.down(pan_start, PEN_FLAG_NONE, 1);
                touch.up(button);
                const auto released { uia.wait([&](const json& state) { return state["left"] == before["left"].get<int>() + 1; }, "First finger release") };
                touch.move(pan_end, 1);
                touch.up(pan_end, PEN_FLAG_NONE, 1);
                no_change(released, true);
            });
            {
                const auto before { uia.status() };
                touch.down(button);
                static_cast<void>(uia.wait([&](const json& state) { return state["touch_down"] > before["touch_down"]; }, "Touch before mouse takeover"));
                restore.mouse(window, button, MOUSEEVENTF_LEFTDOWN);
                restore.mouse(window, button, MOUSEEVENTF_LEFTUP);
                const auto taken { uia.wait([&](const json& state) { return state["mouse_down"] > before["mouse_down"] && state["left"] > before["left"]; }, "Mouse after touch") };
                touch.up(button);
                if (taken["touch_up_at_mouse_down"] > before["touch_up"] && taken["touch_canceled"] == before["touch_canceled"])
                    skip("touch/mouse overlap: Windows ended synthetic touch normally before delivering mouse DOWN");
                else
                    test("touch contact then mouse / touch late UP swallowed", [&] {
                        no_change(taken, false);
                        require(taken["left"] == before["left"].get<int>() + 1, "Mouse takeover did not produce exactly one click");
                    });
            }
            {
                const auto before { uia.status() };
                touch.down(button);
                static_cast<void>(uia.wait([&](const json& state) { return state["touch_down"] > before["touch_down"]; }, "Touch before pen takeover"));
                click(pen, button);
                const auto taken { uia.wait([&](const json& state) { return state["pen_down"] > before["pen_down"] && state["left"] > before["left"]; }, "Pen after touch") };
                touch.up(button);
                if (taken["touch_up_at_pen_down"] > before["touch_up"] && taken["touch_canceled"] == before["touch_canceled"])
                    skip("touch/pen overlap: Windows ended synthetic touch normally before delivering pen DOWN");
                else
                    test("touch contact then pen / only pen clicks", [&] {
                        no_change(taken, false);
                        require(taken["left"] == before["left"].get<int>() + 1, "Pen takeover did not produce exactly one click");
                    });
            }
            test("modal covers pending touch / no click or context action", [&] {
                const auto before { uia.status() };
                touch.down(button);
                uia.invoke(L"modal");
                static_cast<void>(uia.wait([](const json& state) { return state["modal"] == true; }, "Modal appeared"));
                touch.up(button);
                no_change(before, false);
                uia.invoke(L"modal");
                static_cast<void>(uia.wait([](const json& state) { return state["modal"] == false; }, "Modal dismissed"));
                click(touch, button);
                static_cast<void>(uia.wait([&](const json& state) { return state["left"] == before["left"].get<int>() + 1; }, "Touch after modal"));
            });
            test("modal covers active touch pan / remaining moves swallowed", [&] {
                reset_scroll();
                touch.down(pan_start);
                touch.move(pan_end);
                static_cast<void>(uia.wait([](const json& state) { return state["scroll"] > 20.0f; }, "Pan before modal"));
                uia.invoke(L"modal");
                const auto blocked { uia.wait([](const json& state) { return state["modal"] == true; }, "Pan modal appeared") };
                touch.move(pan_start);
                touch.up(pan_start);
                no_change(blocked, true);
                uia.invoke(L"modal");
                static_cast<void>(uia.wait([](const json& state) { return state["modal"] == false; }, "Pan modal dismissed"));
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
            test("pen barrel changes during contact / right click only", [&] {
                const auto before { uia.status() };
                pen.down(button);
                pen.move(button, 0, PEN_FLAG_BARREL);
                pen.up(button, PEN_FLAG_BARREL);
                const auto after { uia.wait([&](const json& state) { return state["right"] == before["right"].get<int>() + 1; }, "Pen barrel switch") };
                require(after["left"] == before["left"], "Barrel switch completed old left click");
            });
            test("pen hover / no action", [&] {
                const auto before { uia.status() };
                pen.send(button, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE);
                static_cast<void>(uia.wait([&](const json& state) { return state["pen_hover"] > before["pen_hover"]; }, "Pen hover delivered"));
                no_change(before, false);
            });
            test("pen hover over text / I-beam without WM_SETCURSOR", [&] {
                const auto before { uia.status() };
                const POINT text_point { uia.point(L"text", 0.3) };
                pen.send(text_point, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE);
                static_cast<void>(uia.wait([&](const json& state) { return state["pen_hover"] > before["pen_hover"]; }, "Pen hover over text"));
                settle();
                CURSORINFO cursor { sizeof(CURSORINFO) };
                require(GetCursorInfo(&cursor), "Cannot inspect pen hover cursor");
                require(cursor.hCursor == LoadCursorW(nullptr, IDC_IBEAM),
                    "Pen hover did not select text cursor at " + std::to_string(text_point.x) + "," + std::to_string(text_point.y) + "; system cursor=" + std::to_string(cursor.ptScreenPos.x) + ","
                        + std::to_string(cursor.ptScreenPos.y));
                require(uia.status()["set_cursor"] == before["set_cursor"], "Pen cursor test received WM_SETCURSOR");
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
            {
                const auto before { uia.status() };
                touch.down(button);
                static_cast<void>(uia.wait([&](const json& state) { return state["touch_down"] > before["touch_down"]; }, "Contact before minimize"));
                ShowWindow(window, SW_MINIMIZE);
                require(IsIconic(window), "Fixture did not minimize");
                settle();
                ShowWindow(window, SW_RESTORE);
                uia.activate();
                activate_window(window);
                settle();
                const auto restored { uia.status() };
                touch.up(button);
                if (restored["capture_changed"] > before["capture_changed"])
                {
                    test("OS pointer capture lost on minimize / late UP swallowed", [&] { no_change(before, false); });
                    const auto ended { uia.status() };
                    test("fresh contact after OS capture loss", [&] {
                        click(touch, button);
                        static_cast<void>(uia.wait([&](const json& state) { return state["left"] == ended["left"].get<int>() + 1; }, "Touch after capture loss"));
                    });
                }
                else
                {
                    ++skipped;
                    std::cout << "SKIP OS pointer capture loss: minimizing did not generate WM_POINTERCAPTURECHANGED\n";
                }
            }
            {
                const auto before { uia.status() };
                touch.down(button);
                touch.send(button, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_CANCELED);
                touch.up(button);
                settle();
                const auto after { uia.status() };
                if (after["canceled_message"] > before["canceled_message"] || after["touch_canceled"] > before["touch_canceled"])
                    test("native canceled touch / no click or context action", [&] { no_change(before, false); });
                else
                {
                    ++skipped;
                    std::cout << "SKIP native cancellation: Windows did not forward the injected CANCELED flag\n";
                }
            }
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
            test("WebView modal during touch / terminal event / late UP swallowed", [&] {
                const auto before { uia.status() };
                touch.down(web_point);
                static_cast<void>(uia.wait([&](const json& state) { return state["web"].value("down", 0) == before["web"]["down"].get<int>() + 1; }, "Web contact before modal"));
                uia.invoke(L"modal");
                json blocked {};
                blocked = uia.wait(
                    [&](const json& state) {
                        return state["modal"] == true && state["web"].value("up", 0) + state["web"].value("cancel", 0) == before["web"]["up"].get<int>() + before["web"]["cancel"].get<int>() + 1;
                    },
                    "Modal terminated WebView contact");
                touch.up(web_point);
                settle();
                require(uia.status()["web"] == blocked["web"], "Late UP leaked into modal-blocked WebView");
                no_change(before, false);
                uia.invoke(L"modal");
                static_cast<void>(uia.wait([](const json& state) { return state["modal"] == false; }, "Dismiss web modal"));
                web_click(touch, "touch");
            });
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
            {
                const auto before { uia.status() };
                const POINT first { uia.point(L"web", 0.25) };
                const POINT second { uia.point(L"web", 0.75) };
                touch.down(first);
                touch.down(second, PEN_FLAG_NONE, 1);
                const auto deadline { clock::now() + std::chrono::seconds { 1 } };
                json during {};
                do
                {
                    during = uia.status();
                    if (during["touch_down"] >= before["touch_down"].get<int>() + 2)
                        break;
                    touch.move(second, 1);
                } while (clock::now() < deadline);
                touch.up(first);
                touch.up(second, PEN_FLAG_NONE, 1);
                if (during["touch_down"] != before["touch_down"].get<int>() + 2 || during["native_touch_frame"].size() != 2)
                    skip("WebView multi-touch: injection did not reach host as exactly two native DOWNs; verify with physical fingers");
                else
                    test("WebView two simultaneous contacts / paired terminal events", [&] {
                        json after {};
                        after = uia.wait(
                            [&](const json& state) {
                                return state["web"].value("down", 0) == before["web"]["down"].get<int>() + 2
                                    && state["web"].value("up", 0) + state["web"].value("cancel", 0) == before["web"]["up"].get<int>() + before["web"]["cancel"].get<int>() + 2;
                            },
                            "Web multi-touch");
                        require(after["web"]["orphan"] == 0 && after["web"]["type"] == "touch", "Web multi-touch identity/sequence mismatch");
                    });
            }
#else
            std::cout << "SKIP WebView coverage: LUIL_ENABLE_WEBVIEW=OFF\n";
#endif
            std::cout << passed << " native pointer integration scenarios passed; " << skipped << " native probes skipped\n";
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
