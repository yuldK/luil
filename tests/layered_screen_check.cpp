// 위·왼쪽 끝이 움직이는 크기 변경에서 아래·오른쪽에 붙은 내용이 한 합성이라도 제자리를 벗어나는지 **합성된 화면**으로
// 잰다 (layered-presentation-design.md의 검증).
//
// 창 DC에서 픽셀을 읽는 검사는 이 어긋남을 보지 못한다 — 어긋남은 DWM이 새 창 영역에 이전 frame을 붙여 합성할 때만
// 있다. 그래서 화면 DC를 `DwmFlush`마다 캡처해 본다. 사람의 입력 데스크톱에 창을 띄우므로 일반 ctest에서는 건너뛴다
// (`LUIL_SCREEN_TESTS=1`일 때만 돈다).
//
// 창 아래에 자홍 막대, 오른쪽에 청록 막대를 둔다. 크기 변경은 위·왼쪽 끝만 옮기므로 두 막대는 화면에서 늘 같은 자리여야
// 한다. 크기를 바꾼 직후 frame들에서 그 자리의 색이 막대가 아니면 어긋난 frame으로 센다.
//  - 크기 변경 두 길을 잰다. `SetWindowPos`(시스템 크기 조절 루프·최대화와 같은 길)와 앱의 배치 요청
//    (`ui_frame::window_placement_request`)이다.
//  - 표준 표시에서 어긋난 frame이 나와야 검사 자체가 맞다. 레이어드 표시는 0이어야 한다.
//  - `--drag`를 주면 실제 마우스로 위 테두리를 끄는 길도 잰다. 사람의 마우스를 가져가므로 기본은 끈다.

#include "luil/luil.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <windows.h>

#include <dwmapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {
    using clock = std::chrono::steady_clock;

    constexpr wchar_t window_class_name[] { L"Luil.LayeredScreenCheck" };
    // 막대 두께다 (물리 픽셀).
    constexpr int bar_thickness { 48 };
    // 한 번에 옮기는 끝의 거리다 (물리 픽셀).
    constexpr int edge_step { 120 };
    // 크기 변경 하나 뒤에 캡처하는 시간이다. 레이어드 표시는 새 tree를 기다리므로 넉넉히 둔다.
    constexpr std::chrono::milliseconds capture_window { 320 };
    constexpr std::uint32_t bottom_color { 0xFFFF00FFu };
    constexpr std::uint32_t right_color { 0xFF00FFFFu };

    struct metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 1.0f };
    };

    struct close_intent
    {
    };

    // 칠한 사각형 하나다.
    class color_block final : public luil::ui_element
    {
    public:
        color_block(const luil::ui_element_id id, const std::uint32_t color)
            : ui_element { id }
            , color_ { color }
        {
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context& context, const luil::interaction_snapshot&) const override
        {
            SkPaint paint {};
            paint.setColor(color_);
            const luil::rect_f& box { bounds() };
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), paint);
        }

    private:
        std::uint32_t color_ { 0 };
    };

    // 앱의 배치 요청 길이다. 검사 thread가 요청을 넣으면 logic이 다음 tick에 frame에 싣는다.
    class request_box
    {
    public:
        void put(const luil::win32::window_placement& placement)
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            pending_ = placement;
        }

        [[nodiscard]] std::optional<luil::win32::window_placement> take()
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            return std::exchange(pending_, std::nullopt);
        }

    private:
        std::mutex mutex_ {};
        std::optional<luil::win32::window_placement> pending_ {};
    };

    class check_driver final : public luil::win32::logic_driver
    {
    public:
        explicit check_driver(request_box& requests)
            : requests_ { requests }
        {
        }

        void handle(luil::app_message message) override
        {
            if (message.get<close_intent>() != nullptr)
            {
                closed_.store(true);
                return;
            }
            if (const auto* const metrics { message.get<metrics_intent>() }; metrics != nullptr)
                metrics_ = *metrics;
        }

        [[nodiscard]] std::optional<clock::time_point> next_tick() override
        {
            return clock::now() + std::chrono::milliseconds { 8 };
        }

        void tick(clock::time_point) override
        {
            if (const std::optional<luil::win32::window_placement> request { requests_.take() }; request.has_value())
            {
                request_ = request;
                ++revision_;
            }
        }

        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
        {
            const float width { metrics_.width > 0.0f ? metrics_.width : 1.0f };
            const float height { metrics_.height > 0.0f ? metrics_.height : 1.0f };
            const float scale { metrics_.scale > 0.0f ? metrics_.scale : 1.0f };
            const auto thickness { static_cast<float>(bar_thickness) };

            auto root { std::make_unique<luil::root_element>() };
            root->arrange({ { 0.0f, 0.0f, width, height }, scale });
            auto bottom { std::make_unique<color_block>(luil::ui_element_id { luil::ui_element_kind::none, u8"bottom" }, bottom_color) };
            bottom->arrange({ { 0.0f, height - thickness, width, thickness }, scale });
            root->add(std::move(bottom));
            auto right { std::make_unique<color_block>(luil::ui_element_id { luil::ui_element_kind::none, u8"right" }, right_color) };
            right->arrange({ { width - thickness, 0.0f, thickness, height - thickness }, scale });
            root->add(std::move(right));

            auto frame { std::make_shared<luil::win32::ui_frame>() };
            frame->tree = std::make_shared<const luil::ui_tree>(std::move(root));
            if (request_.has_value())
            {
                frame->window_placement_revision = revision_;
                frame->window_placement_request = request_;
            }
            return frame;
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { close_intent {} };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed_.load();
        }

    private:
        request_box& requests_;
        metrics_intent metrics_ {};
        std::optional<luil::win32::window_placement> request_ {};
        std::uint64_t revision_ { 0 };
        std::atomic<bool> closed_ { false };
    };

    class check_delegate final : public luil::win32::window_delegate
    {
    public:
        [[nodiscard]] luil::app_message make_window_metrics_message(const float width, const float height, const float scale) override
        {
            return luil::app_message { metrics_intent { width, height, scale } };
        }
    };

    // 화면의 한 사각형을 캡처하는 판이다.
    class screen_grabber
    {
    public:
        explicit screen_grabber(const RECT& area)
            : area_ { area }
            , width_ { area.right - area.left }
            , height_ { area.bottom - area.top }
        {
            screen_ = GetDC(nullptr);
            memory_ = CreateCompatibleDC(screen_);
            BITMAPINFO info {};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width_;
            info.bmiHeader.biHeight = -height_;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* bits { nullptr };
            bitmap_ = CreateDIBSection(memory_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            bits_ = static_cast<const std::uint32_t*>(bits);
            previous_ = SelectObject(memory_, bitmap_);
        }

        screen_grabber(const screen_grabber&) = delete;
        screen_grabber& operator=(const screen_grabber&) = delete;

        ~screen_grabber()
        {
            SelectObject(memory_, previous_);
            DeleteObject(bitmap_);
            DeleteDC(memory_);
            ReleaseDC(nullptr, screen_);
        }

        // 다음 합성을 기다렸다가 찍는다.
        void grab() noexcept
        {
            DwmFlush();
            BitBlt(memory_, 0, 0, width_, height_, screen_, area_.left, area_.top, SRCCOPY | CAPTUREBLT);
        }

        // 마지막으로 찍은 것을 BMP로 남긴다 (창 둘레의 그림자·테두리를 눈으로 비교할 때).
        void save(const wchar_t* const path) const
        {
            const DWORD pixels { static_cast<DWORD>(width_) * static_cast<DWORD>(height_) * 4 };
            BITMAPFILEHEADER file {};
            file.bfType = 0x4D42;
            file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            file.bfSize = file.bfOffBits + pixels;
            BITMAPINFOHEADER header {};
            header.biSize = sizeof(BITMAPINFOHEADER);
            header.biWidth = width_;
            header.biHeight = -height_;
            header.biPlanes = 1;
            header.biBitCount = 32;
            header.biCompression = BI_RGB;
            const HANDLE output { CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
            if (output == INVALID_HANDLE_VALUE)
                return;
            DWORD written { 0 };
            WriteFile(output, &file, sizeof(file), &written, nullptr);
            WriteFile(output, &header, sizeof(header), &written, nullptr);
            WriteFile(output, bits_, pixels, &written, nullptr);
            CloseHandle(output);
        }

        // 화면 좌표의 색이 그 색에 가까운가다.
        [[nodiscard]] bool matches(const int x, const int y, const std::uint32_t color) const noexcept
        {
            const int local_x { x - area_.left };
            const int local_y { y - area_.top };
            if (local_x < 0 || local_y < 0 || local_x >= width_ || local_y >= height_)
                return false;
            const std::uint32_t pixel { bits_[static_cast<std::size_t>(local_y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(local_x)] };
            const auto channel { [](const std::uint32_t value, const int shift) { return static_cast<int>((value >> shift) & 0xFFu); } };
            for (const int shift : { 0, 8, 16 })
                if (std::abs(channel(pixel, shift) - channel(color, shift)) > 24)
                    return false;
            return true;
        }

    private:
        RECT area_ {};
        int width_ { 0 };
        int height_ { 0 };
        HDC screen_ { nullptr };
        HDC memory_ { nullptr };
        HBITMAP bitmap_ { nullptr };
        HGDIOBJ previous_ { nullptr };
        const std::uint32_t* bits_ { nullptr };
    };

    // 고정된 아래·오른쪽 끝에서 두 막대가 제자리에 있는가다.
    [[nodiscard]] bool bars_in_place(const screen_grabber& grabber, const int right, const int bottom)
    {
        const int bottom_row { bottom - bar_thickness / 2 };
        const int right_column { right - bar_thickness / 2 };
        for (const int offset : { 100, 200, 300 })
        {
            if (grabber.matches(right - bar_thickness - offset, bottom_row, bottom_color) == false)
                return false;
            if (grabber.matches(right_column, bottom - bar_thickness - offset, right_color) == false)
                return false;
        }
        // 막대 바로 위·왼쪽은 막대가 아니어야 한다 — 옛 frame이 아래로 밀려 막대가 두 겹으로 보이는 것을 거른다.
        return grabber.matches(right - bar_thickness - 200, bottom - bar_thickness - 12, bottom_color) == false
            && grabber.matches(right - bar_thickness - 12, bottom - bar_thickness - 200, right_color) == false;
    }

    [[nodiscard]] HWND wait_for_window(const std::chrono::milliseconds timeout)
    {
        const auto deadline { clock::now() + timeout };
        while (clock::now() < deadline)
        {
            if (const HWND window { FindWindowW(window_class_name, nullptr) }; window != nullptr && IsWindowVisible(window) != FALSE)
                return window;
            std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
        }
        return nullptr;
    }

    [[nodiscard]] bool wait_for_rect(const HWND window, const RECT& expected, const std::chrono::milliseconds timeout)
    {
        const auto deadline { clock::now() + timeout };
        while (clock::now() < deadline)
        {
            RECT current {};
            GetWindowRect(window, &current);
            if (EqualRect(&current, &expected) != FALSE)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
        }
        return false;
    }

    struct path_result
    {
        int resizes { 0 };
        int frames { 0 };
        int displaced_frames { 0 };
        int resizes_with_displacement { 0 };
        // 끄는 동안 본 창 위 끝의 범위다. 끌기가 창을 실제로 움직였는지 본다 — 움직이지 않은 창은 어긋날 일도 없다.
        long lowest_top { 0 };
        long highest_top { 0 };
        bool completed { false };
    };

    enum class resize_path
    {
        set_window_pos,
        app_request,
        drag,
    };

    // 화면 좌표 → `window_placement`가 쓰는 작업 영역 좌표다.
    [[nodiscard]] luil::win32::window_placement to_placement(const HWND window, const RECT& bounds)
    {
        MONITORINFO information {};
        information.cbSize = sizeof(information);
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &information);
        const int offset_x { information.rcWork.left - information.rcMonitor.left };
        const int offset_y { information.rcWork.top - information.rcMonitor.top };
        return luil::win32::window_placement { bounds.left - offset_x, bounds.top - offset_y, bounds.right - bounds.left, bounds.bottom - bounds.top };
    }

    void send_mouse(const DWORD flags, const int x, const int y)
    {
        const int width { GetSystemMetrics(SM_CXVIRTUALSCREEN) };
        const int height { GetSystemMetrics(SM_CYVIRTUALSCREEN) };
        const int left { GetSystemMetrics(SM_XVIRTUALSCREEN) };
        const int top { GetSystemMetrics(SM_YVIRTUALSCREEN) };
        INPUT input {};
        input.type = INPUT_MOUSE;
        input.mi.dx = static_cast<LONG>((static_cast<long long>(x - left) * 65535) / (width - 1));
        input.mi.dy = static_cast<LONG>((static_cast<long long>(y - top) * 65535) / (height - 1));
        input.mi.dwFlags = flags | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;
        SendInput(1, &input, sizeof(input));
    }

    // 한 길로 위·왼쪽 끝을 옮기며 잰다. 아래·오른쪽 끝은 그대로다.
    [[nodiscard]] path_result measure_path(const HWND window, const resize_path path, request_box& requests, const int rounds)
    {
        path_result result {};
        RECT start {};
        GetWindowRect(window, &start);
        const RECT area { start.left - edge_step - 8, start.top - edge_step - 8, start.right + 8, start.bottom + 8 };
        screen_grabber grabber { area };

        // 막대가 제자리에 보이기까지 기다린다. 보이지 않으면 다른 창이 덮고 있다.
        bool baseline { false };
        for (int attempt = 0; attempt < 120 && baseline == false; ++attempt)
        {
            grabber.grab();
            baseline = bars_in_place(grabber, start.right, start.bottom);
        }
        if (baseline == false)
        {
            std::printf("  기준 화면에서 막대를 찾지 못했다 (다른 창이 덮고 있거나 화면이 꺼져 있다).\n");
            return result;
        }

        struct step
        {
            int left { 0 };
            int top { 0 };
        };
        const step steps[] { { 0, edge_step }, { 0, -edge_step }, { edge_step, 0 }, { -edge_step, 0 } };
        RECT current { start };
        if (path == resize_path::drag)
        {
            // 위 테두리 가운데를 잡고 위아래로 끈다. 크기 조절 루프는 처음 사각형과 마우스 변위로 창을 셈한다.
            const int x { (current.left + current.right) / 2 };
            int y { current.top + 1 };
            send_mouse(0, x, y);
            std::this_thread::sleep_for(std::chrono::milliseconds { 150 });
            send_mouse(MOUSEEVENTF_LEFTDOWN, x, y);
            std::this_thread::sleep_for(std::chrono::milliseconds { 150 });
            result.lowest_top = current.top;
            result.highest_top = current.top;
            for (int round = 0; round < rounds; ++round)
                for (const int direction : { -1, 1 })
                {
                    for (int moved = 0; moved < edge_step; moved += 12)
                    {
                        y += direction * 12;
                        send_mouse(0, x, y);
                        const auto until { clock::now() + std::chrono::milliseconds { 16 } };
                        bool displaced { false };
                        while (clock::now() < until)
                        {
                            grabber.grab();
                            ++result.frames;
                            if (bars_in_place(grabber, start.right, start.bottom) == false)
                            {
                                ++result.displaced_frames;
                                displaced = true;
                            }
                        }
                        ++result.resizes;
                        if (displaced)
                            ++result.resizes_with_displacement;
                        RECT moved_to {};
                        GetWindowRect(window, &moved_to);
                        result.lowest_top = std::max(result.lowest_top, moved_to.top);
                        result.highest_top = std::min(result.highest_top, moved_to.top);
                    }
                }
            send_mouse(MOUSEEVENTF_LEFTUP, x, y);
            std::this_thread::sleep_for(std::chrono::milliseconds { 300 });
            result.completed = result.highest_top < result.lowest_top;
            if (result.completed == false)
                std::printf("  창이 끌기를 따라 움직이지 않았다.\n");
            return result;
        }

        for (int round = 0; round < rounds; ++round)
        {
            for (const step& value : steps)
            {
                const RECT target { current.left - value.left, current.top - value.top, current.right, current.bottom };
                if (path == resize_path::set_window_pos)
                    SetWindowPos(window, nullptr, target.left, target.top, target.right - target.left, target.bottom - target.top, SWP_NOZORDER | SWP_NOACTIVATE);
                else
                    requests.put(to_placement(window, target));

                const auto until { clock::now() + capture_window };
                bool displaced { false };
                while (clock::now() < until)
                {
                    grabber.grab();
                    ++result.frames;
                    if (bars_in_place(grabber, start.right, start.bottom) == false)
                    {
                        ++result.displaced_frames;
                        displaced = true;
                    }
                }
                ++result.resizes;
                if (displaced)
                    ++result.resizes_with_displacement;
                if (wait_for_rect(window, target, std::chrono::milliseconds { 1500 }) == false)
                {
                    std::printf("  창이 요청한 사각형에 닿지 않았다.\n");
                    return result;
                }
                current = target;
            }
        }
        result.completed = true;
        return result;
    }

    // 창을 띄우고 잰 뒤 닫는다 (자식 프로세스 하나가 한 표시 방식을 맡는다).
    int run_child(const bool layered, const bool drag)
    {
        luil::win32::enable_per_monitor_dpi_awareness();
        const luil::win32::com_sta_scope com {};

        request_box requests {};
        check_driver driver { requests };
        check_delegate delegate {};

        luil::win32::window_config config {};
        config.class_name = window_class_name;
        config.caption.title = u8"layered screen check";
        config.renderer = luil::renderer_mode::cpu;
        config.presentation = layered ? luil::win32::window_presentation::layered : luil::win32::window_presentation::standard;
        config.minimum_client_width = 200;
        config.minimum_client_height = 200;
        config.initial_placement = luil::win32::window_placement { 240, 240, 900, 640 };

        std::atomic<int> exit_code { 1 };
        const auto check = [&requests, &exit_code, layered, drag] {
            const HWND window { wait_for_window(std::chrono::seconds { 10 }) };
            if (window == nullptr)
            {
                std::printf("  창이 뜨지 않았다.\n");
                return;
            }
            // 다른 창에 가리지 않게 맨 위로 세운다. 활성화는 하지 않는다.
            SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            std::this_thread::sleep_for(std::chrono::milliseconds { 300 });
            const char* const mode { layered ? "layered" : "standard" };
            // `LUIL_SCREEN_SHOT_DIR`가 있으면 창 둘레를 찍어 둔다. 레이어드 창의 그림자·테두리를 표준 창과 눈으로 견준다.
            if (wchar_t directory[MAX_PATH] {}; GetEnvironmentVariableW(L"LUIL_SCREEN_SHOT_DIR", directory, MAX_PATH) != 0)
            {
                RECT bounds {};
                GetWindowRect(window, &bounds);
                screen_grabber shot { RECT { bounds.left - 40, bounds.top - 40, bounds.right + 40, bounds.bottom + 40 } };
                shot.grab();
                std::wstring path { directory };
                path += layered ? L"\\layered.bmp" : L"\\standard.bmp";
                shot.save(path.c_str());
            }
            bool completed { true };
            int displaced { 0 };
            const auto report = [mode, &completed, &displaced](const char* const name, const path_result& result) {
                std::printf("%s %s: resizes=%d frames=%d displaced_frames=%d resizes_with_displacement=%d%s", mode, name, result.resizes, result.frames, result.displaced_frames,
                    result.resizes_with_displacement, result.completed ? "" : " (incomplete)");
                if (result.lowest_top != result.highest_top)
                    std::printf(" top_range=%ld..%ld", result.highest_top, result.lowest_top);
                std::printf("\n");
                std::fflush(stdout);
                completed = completed && result.completed;
                displaced += result.displaced_frames;
            };
            report("set_window_pos", measure_path(window, resize_path::set_window_pos, requests, 8));
            report("app_request", measure_path(window, resize_path::app_request, requests, 8));
            if (drag)
                report("drag", measure_path(window, resize_path::drag, requests, 3));
            // 레이어드는 한 frame도 어긋나지 않아야 한다. 표준은 어긋나야 한다 — 어긋남을 못 보는 검사는 레이어드의
            // 0도 증명하지 못한다.
            const bool expected { layered ? displaced == 0 : displaced > 0 };
            if (expected == false)
                std::printf("%s: %s\n", mode, layered ? "어긋난 frame이 있다." : "표준 표시에서 어긋남을 보지 못했다 — 검사가 이 현상을 잡지 못한다.");
            exit_code.store(completed && expected ? 0 : 1);
            PostMessageW(window, WM_CLOSE, 0, 0);
        };
        std::thread checker { check };

        luil::win32::window_environment environment {};
        environment.driver = &driver;
        environment.delegate = &delegate;
        const int window_result { luil::win32::run_application_window(config, environment) };
        checker.join();
        return window_result != 0 ? window_result : exit_code.load();
    }

    // 표시 방식 하나를 자식 프로세스로 돌린다.
    //  - 창을 활성화하지 않고 띄운다 (`SW_SHOWNOACTIVATE` — 초기 배치가 있는 창은 STARTUPINFO를 따른다).
    int run_mode(const bool layered, const bool drag)
    {
        wchar_t path[MAX_PATH] {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring command { L"\"" };
        command += path;
        command += L"\" --child=";
        command += layered ? L"layered" : L"standard";
        if (drag)
            command += L" --drag";
        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_SHOWNOACTIVATE;
        PROCESS_INFORMATION process {};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process) == FALSE)
            return 1;
        WaitForSingleObject(process.hProcess, 120000);
        DWORD code { 1 };
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return static_cast<int>(code);
    }
}

int main(const int argc, char** const argv)
{
    bool drag { false };
    const char* child { nullptr };
    for (int index = 1; index < argc; ++index)
        if (std::strcmp(argv[index], "--drag") == 0)
            drag = true;
        else if (std::strncmp(argv[index], "--child=", 8) == 0)
            child = argv[index] + 8;
    if (child != nullptr)
        return run_child(std::strcmp(child, "layered") == 0, drag);

    // 사람의 화면에 창을 띄운다. 명시적으로 켰을 때만 돈다.
    wchar_t enabled[8] {};
    if (GetEnvironmentVariableW(L"LUIL_SCREEN_TESTS", enabled, 8) == 0 || std::wcscmp(enabled, L"1") != 0)
    {
        std::printf("LUIL_SCREEN_TESTS=1이 아니라 건너뛴다 (입력 데스크톱에 창을 띄우는 검사다).\n");
        return 77;
    }
    std::fflush(stdout);
    const int standard { run_mode(false, drag) };
    std::fflush(stdout);
    const int layered { run_mode(true, drag) };
    std::fflush(stdout);
    return standard == 0 && layered == 0 ? 0 : 1;
}
