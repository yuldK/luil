// 배경 효과가 **합성된 화면**에서 실제로 서는지 잰다 (backdrop-design.md의 검증).
//
// 빨강·파랑 반반 판 위에 luil 표면(주 창·보조 창·popup)을 띄우고 배경 효과를 건다. 표면 안의 빈 자리(바탕만 칠한
// 자리)에서 판의 경계가 흐리게 비치는지, 불투명한 표면 칸은 그대로인지, 위쪽에 캡션 높이만큼 다른 색 띠가 서지 않는지를
// 화면 DC 캡처로 본다. 사람의 입력 데스크톱에 창을 띄우므로 `LUIL_SCREEN_TESTS=1`일 때만 돈다.
//  - 시스템 배경 효과는 비활성 창에서 단색 대체 색이다. 초점을 빼앗지 않으려고 `WM_NCACTIVATE`로 활성 모습만 세운다.
//    보조 창은 그 모습을 세운 것(`secondary-active`)과 세우지 않은 것(`secondary-inactive`)을 따로 잰다.
//  - popup에는 `WM_NCACTIVATE`를 보내지 않는다. 활성화되지 않는 표면 그대로를 잰다.
//  - `LUIL_SCREEN_SHOT_DIR`가 있으면 표면 캡처를 BMP로 남긴다.

#include "luil/luil.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <windows.h>

#include <dwmapi.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace {
    using clock = std::chrono::steady_clock;

    constexpr wchar_t window_class_name[] { L"Luil.BackdropScreenCheck" };
    constexpr wchar_t secondary_class_name[] { L"Luil.BackdropScreenCheck.Secondary" };
    constexpr wchar_t popup_class_name[] { L"Luil.BackdropScreenCheck.Popup" };
    constexpr wchar_t rounded_popup_class_name[] { L"Luil.BackdropScreenCheck.PopupRounded" };
    constexpr wchar_t board_class_name[] { L"Luil.BackdropScreenCheck.Board" };
    // 판과 잴 표면의 자리다 (물리 픽셀). 표면은 판의 빨강·파랑 경계에 걸친다.
    constexpr int board_x { 160 };
    constexpr int board_y { 160 };
    constexpr int board_width { 1100 };
    constexpr int board_height { 760 };
    constexpr int window_width { 700 };
    constexpr int window_height { 520 };
    constexpr int window_x { board_x + board_width / 2 - window_width / 2 };
    constexpr int window_y { board_y + 120 };
    // 보조 창·popup을 잴 때 주 창은 판 밖 왼쪽 위에 비켜 둔다 (화면 좌표).
    constexpr int aside_x { 20 };
    constexpr int aside_y { 20 };
    constexpr int aside_width { 220 };
    constexpr int aside_height { 120 };
    // 표면 밖 둘레도 캡처한다. 둥근 모서리와 그림자를 본다.
    constexpr int capture_margin { 16 };
    // 표면 아래쪽에 둔 불투명 표면 칸의 높이다.
    constexpr int surface_height { 140 };
    constexpr std::uint32_t surface_color { 0xFF2D2D30u };

    // 효과를 걸어 잴 표면이다.
    enum class surface_kind
    {
        main,
        secondary,
        popup,
    };

    struct metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 1.0f };
    };

    struct secondary_metrics_intent
    {
        metrics_intent metrics {};
    };

    struct close_intent
    {
    };

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

    // 위쪽은 비워 바탕만 두고, 아래쪽 칸은 불투명한 표면이라 효과가 비치면 안 된다. 크기는 물리 픽셀이다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_board_tree(const float width, const float height, const float scale)
    {
        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, width, height }, scale });
        auto surface { std::make_unique<color_block>(luil::ui_element_id { luil::ui_element_kind::none, u8"surface" }, surface_color) };
        surface->arrange({ { 0.0f, height - static_cast<float>(surface_height), width, static_cast<float>(surface_height) }, scale });
        root->add(std::move(surface));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    // 배치는 작업 영역 좌표다. 화면 좌표의 자리를 그 모니터의 작업 영역 기준으로 옮긴다 (작업 표시줄이 위·왼쪽이면 다르다).
    [[nodiscard]] luil::win32::window_placement work_placement(const int x, const int y, const int width, const int height)
    {
        MONITORINFO monitor {};
        monitor.cbSize = sizeof(monitor);
        const POINT corner { x, y };
        GetMonitorInfoW(MonitorFromPoint(corner, MONITOR_DEFAULTTOPRIMARY), &monitor);
        const int work_x { monitor.rcWork.left - monitor.rcMonitor.left };
        const int work_y { monitor.rcWork.top - monitor.rcMonitor.top };
        return luil::win32::window_placement { x - work_x, y - work_y, width, height };
    }

    class check_driver final : public luil::win32::logic_driver
    {
    public:
        check_driver(const surface_kind kind, const luil::window_backdrop backdrop, const luil::theme_preference theme)
            : kind_ { kind }
            , backdrop_ { backdrop }
            , theme_ { theme }
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
            if (const auto* const secondary { message.get<secondary_metrics_intent>() }; secondary != nullptr)
                secondary_metrics_ = secondary->metrics;
        }

        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
        {
            const float width { metrics_.width > 0.0f ? metrics_.width : 1.0f };
            const float height { metrics_.height > 0.0f ? metrics_.height : 1.0f };
            const float scale { metrics_.scale > 0.0f ? metrics_.scale : 1.0f };
            auto frame { std::make_shared<luil::win32::ui_frame>() };
            frame->tree = make_board_tree(width, height, scale);
            frame->appearance.theme = theme_;
            // popup은 상태를 알리지 않는다. 주 창에 acrylic을 걸어 그 보고로 OS 사정(투명 효과·고대비·원격)을 안다.
            if (kind_ == surface_kind::main)
                frame->backdrop = backdrop_;
            else if (kind_ == surface_kind::popup)
                frame->backdrop = luil::window_backdrop::acrylic;

            if (kind_ == surface_kind::secondary)
            {
                luil::ui_window window {};
                window.id = u8"tool";
                window.width = static_cast<float>(window_width) / scale;
                window.height = static_cast<float>(window_height) / scale;
                window.minimum_width = 200.0f;
                window.minimum_height = 200.0f;
                window.initial_placement = work_placement(window_x, window_y, window_width, window_height);
                window.metrics = [](const float w, const float h, const float s) { return luil::app_message { secondary_metrics_intent { { w, h, s } } }; };
                const float tree_width { secondary_metrics_.width > 0.0f ? secondary_metrics_.width : static_cast<float>(window_width) };
                const float tree_height { secondary_metrics_.height > 0.0f ? secondary_metrics_.height : static_cast<float>(window_height) };
                window.tree = make_board_tree(tree_width, tree_height, secondary_metrics_.scale > 0.0f ? secondary_metrics_.scale : scale);
                window.backdrop = backdrop_;
                frame->windows.push_back(std::move(window));
            }
            else if (kind_ == surface_kind::popup && metrics_.width > 0.0f)
            {
                // 주 창은 캡션을 떼어 client가 창 사각형과 같다. 그 원점에서 잴 자리까지가 popup의 자리다.
                luil::ui_popup popup {};
                popup.id = u8"panel";
                popup.x = static_cast<float>(window_x - aside_x) / scale;
                popup.y = static_cast<float>(window_y - aside_y) / scale;
                popup.width = static_cast<float>(window_width) / scale;
                popup.height = static_cast<float>(window_height) / scale;
                popup.tree = make_board_tree(static_cast<float>(window_width), static_cast<float>(window_height), scale);
                popup.backdrop = backdrop_;
                frame->popups.push_back(std::move(popup));
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
        surface_kind kind_ { surface_kind::main };
        luil::window_backdrop backdrop_ { luil::window_backdrop::none };
        luil::theme_preference theme_ { luil::theme_preference::dark };
        metrics_intent metrics_ {};
        metrics_intent secondary_metrics_ {};
        std::atomic<bool> closed_ { false };
    };

    class check_delegate final : public luil::win32::window_delegate
    {
    public:
        [[nodiscard]] luil::app_message make_window_metrics_message(const float width, const float height, const float scale) override
        {
            return luil::app_message { metrics_intent { width, height, scale } };
        }

        [[nodiscard]] luil::app_message make_backdrop_message(const luil::win32::backdrop_report& report) override
        {
            if (report.surface.empty())
                main_status.store(static_cast<int>(report.status));
            else
                window_status.store(static_cast<int>(report.status));
            return {};
        }

        std::atomic<int> main_status { -1 };
        std::atomic<int> window_status { -1 };
    };

    LRESULT CALLBACK board_procedure(const HWND window, const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint {};
            const HDC context { BeginPaint(window, &paint) };
            const RECT left { 0, 0, board_width / 2, board_height };
            const RECT right { board_width / 2, 0, board_width, board_height };
            const HBRUSH red { CreateSolidBrush(RGB(230, 30, 30)) };
            const HBRUSH blue { CreateSolidBrush(RGB(30, 30, 230)) };
            FillRect(context, &left, red);
            FillRect(context, &right, blue);
            DeleteObject(red);
            DeleteObject(blue);
            EndPaint(window, &paint);
            return 0;
        }
        return DefWindowProcW(window, message, word_parameter, long_parameter);
    }

    struct color
    {
        int red { 0 };
        int green { 0 };
        int blue { 0 };
    };

    // 표면 자리와 그 둘레를 캡처해 두고 표면 좌표로 색을 읽는다 (둘레는 음수·표면 크기 이상의 좌표다).
    class window_capture
    {
    public:
        window_capture()
        {
            const HDC screen { GetDC(nullptr) };
            memory_ = CreateCompatibleDC(screen);
            BITMAPINFO info {};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = capture_width;
            info.bmiHeader.biHeight = -capture_height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* bits { nullptr };
            bitmap_ = CreateDIBSection(memory_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            bits_ = static_cast<const std::uint32_t*>(bits);
            previous_ = SelectObject(memory_, bitmap_);
            DwmFlush();
            BitBlt(memory_, 0, 0, capture_width, capture_height, screen, window_x - capture_margin, window_y - capture_margin, SRCCOPY | CAPTUREBLT);
            ReleaseDC(nullptr, screen);
        }

        window_capture(const window_capture&) = delete;
        window_capture& operator=(const window_capture&) = delete;

        ~window_capture()
        {
            SelectObject(memory_, previous_);
            DeleteObject(bitmap_);
            DeleteDC(memory_);
        }

        [[nodiscard]] color at(const int x, const int y) const noexcept
        {
            const std::size_t row { static_cast<std::size_t>(y + capture_margin) };
            const std::uint32_t pixel { bits_[row * capture_width + static_cast<std::size_t>(x + capture_margin)] };
            return color { static_cast<int>((pixel >> 16) & 0xFFu), static_cast<int>((pixel >> 8) & 0xFFu), static_cast<int>(pixel & 0xFFu) };
        }

        // 한 줄에서 빨강에서 파랑으로 넘어가는 사이 값(10%~90%)의 픽셀 수다. 선명하면 0~2, 흐리면 수십이다.
        [[nodiscard]] int transition(const int y) const noexcept
        {
            const color left { at(60, y) };
            const color right { at(window_width - 60, y) };
            const double left_balance { static_cast<double>(left.red - left.blue) };
            const double right_balance { static_cast<double>(right.red - right.blue) };
            const double span { left_balance - right_balance };
            if (span < 30.0)
                return 0;
            int count { 0 };
            for (int x = window_width / 2 - 150; x < window_width / 2 + 150; ++x)
            {
                const color sample { at(x, y) };
                const double t { (static_cast<double>(sample.red - sample.blue) - right_balance) / span };
                if (t > 0.1 && t < 0.9)
                    ++count;
            }
            return count;
        }

        void save(const std::wstring& path) const
        {
            const DWORD pixels { static_cast<DWORD>(capture_width) * capture_height * 4 };
            BITMAPFILEHEADER file {};
            file.bfType = 0x4D42;
            file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            file.bfSize = file.bfOffBits + pixels;
            BITMAPINFOHEADER header {};
            header.biSize = sizeof(BITMAPINFOHEADER);
            header.biWidth = capture_width;
            header.biHeight = -capture_height;
            header.biPlanes = 1;
            header.biBitCount = 32;
            header.biCompression = BI_RGB;
            const HANDLE output { CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
            if (output == INVALID_HANDLE_VALUE)
                return;
            DWORD written { 0 };
            WriteFile(output, &file, sizeof(file), &written, nullptr);
            WriteFile(output, &header, sizeof(header), &written, nullptr);
            WriteFile(output, bits_, pixels, &written, nullptr);
            CloseHandle(output);
        }

    private:
        static constexpr int capture_width { window_width + capture_margin * 2 };
        static constexpr int capture_height { window_height + capture_margin * 2 };

        HDC memory_ { nullptr };
        HBITMAP bitmap_ { nullptr };
        HGDIOBJ previous_ { nullptr };
        const std::uint32_t* bits_ { nullptr };
    };

    [[nodiscard]] bool near_color(const color& value, const std::uint32_t expected) noexcept
    {
        return std::abs(value.red - static_cast<int>((expected >> 16) & 0xFFu)) <= 12 && std::abs(value.green - static_cast<int>((expected >> 8) & 0xFFu)) <= 12
            && std::abs(value.blue - static_cast<int>(expected & 0xFFu)) <= 12;
    }

    [[nodiscard]] bool reddish(const color& value) noexcept
    {
        return value.red > value.blue + 40;
    }

    [[nodiscard]] bool bluish(const color& value) noexcept
    {
        return value.blue > value.red + 40;
    }

    void pump_for(const std::chrono::milliseconds duration)
    {
        const auto until { clock::now() + duration };
        while (clock::now() < until)
        {
            MSG message {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
        }
    }

    [[nodiscard]] const char* backdrop_name(const luil::window_backdrop backdrop) noexcept
    {
        switch (backdrop)
        {
        case luil::window_backdrop::mica:
            return "mica";
        case luil::window_backdrop::mica_alt:
            return "mica_alt";
        case luil::window_backdrop::acrylic:
            return "acrylic";
        default:
            return "none";
        }
    }

    // 잴 표면의 창이다. 보이는 창이 뜰 때까지 기다린다.
    [[nodiscard]] HWND wait_for_surface(const surface_kind kind)
    {
        for (int attempt = 0; attempt < 500; ++attempt)
        {
            HWND window { nullptr };
            if (kind == surface_kind::main)
                window = FindWindowW(window_class_name, nullptr);
            else if (kind == surface_kind::secondary)
                window = FindWindowW(secondary_class_name, nullptr);
            else
            {
                window = FindWindowW(rounded_popup_class_name, nullptr);
                if (window == nullptr)
                    window = FindWindowW(popup_class_name, nullptr);
            }
            if (window != nullptr && IsWindowVisible(window) != FALSE)
                return window;
            pump_for(std::chrono::milliseconds { 20 });
        }
        return nullptr;
    }

    // 표면 하나를 띄우고 잰 뒤 닫는다. 자식 프로세스 하나가 한 조합을 맡는다.
    //  - `presentation`: 주 창은 `standard-cpu`·`standard-gpu`·`layered`, 그 밖은 `secondary-active`·
    //    `secondary-inactive`·`popup`이다. 보조 창·popup을 잴 때 주 창은 표준 CPU로 판 밖에 비켜 둔다.
    int run_child(const char* const presentation, const luil::window_backdrop backdrop, const bool light)
    {
        luil::win32::enable_per_monitor_dpi_awareness();
        const luil::win32::com_sta_scope com {};
        surface_kind kind { surface_kind::main };
        if (std::strncmp(presentation, "secondary", 9) == 0)
            kind = surface_kind::secondary;
        else if (std::strcmp(presentation, "popup") == 0)
            kind = surface_kind::popup;
        const bool activate { kind == surface_kind::main || std::strcmp(presentation, "secondary-active") == 0 };
        check_driver driver { kind, backdrop, light ? luil::theme_preference::light : luil::theme_preference::dark };
        check_delegate delegate {};

        luil::win32::window_config config {};
        config.class_name = window_class_name;
        config.caption.title = u8"backdrop screen check";
        const bool layered { std::strcmp(presentation, "layered") == 0 };
        config.presentation = layered ? luil::win32::window_presentation::layered : luil::win32::window_presentation::standard;
        config.renderer = std::strcmp(presentation, "standard-gpu") == 0 ? luil::renderer_mode::automatic : luil::renderer_mode::cpu;
        config.minimum_client_width = 100;
        config.minimum_client_height = 80;
        config.initial_placement = kind == surface_kind::main ? work_placement(window_x, window_y, window_width, window_height) : work_placement(aside_x, aside_y, aside_width, aside_height);

        std::atomic<int> exit_code { 1 };
        const auto check = [&exit_code, &delegate, presentation, backdrop, light, kind, activate] {
            WNDCLASSEXW board_class {};
            board_class.cbSize = sizeof(board_class);
            board_class.lpfnWndProc = &board_procedure;
            board_class.hInstance = GetModuleHandleW(nullptr);
            board_class.lpszClassName = board_class_name;
            RegisterClassExW(&board_class);
            const DWORD board_style { WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW };
            const HWND board { CreateWindowExW(board_style, board_class_name, L"", WS_POPUP, board_x, board_y, board_width, board_height, nullptr, nullptr, board_class.hInstance, nullptr) };
            ShowWindow(board, SW_SHOWNOACTIVATE);
            pump_for(std::chrono::milliseconds { 200 });

            const HWND window { wait_for_surface(kind) };
            const HWND main_window { FindWindowW(window_class_name, nullptr) };
            if (window == nullptr)
            {
                std::printf("  표면이 뜨지 않았다.\n");
                if (main_window != nullptr)
                    PostMessageW(main_window, WM_CLOSE, 0, 0);
                DestroyWindow(board);
                return;
            }
            // 둘 다 맨 위 띠에 두고 판을 표면 **바로 아래**로 끼운다. 표면만 맨 위로 올리면 판이 그 위에 남는 것을 실측에서 봤다.
            const POINT center { window_x + window_width / 2, window_y + window_height / 2 };
            for (int attempt = 0; attempt < 10 && WindowFromPoint(center) != window; ++attempt)
            {
                SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetWindowPos(board, window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                pump_for(std::chrono::milliseconds { 100 });
            }
            // 활성 모습만 세운다. 시스템 배경 효과는 비활성 창에서 단색 대체 색이다.
            if (activate)
                SendMessageW(window, WM_NCACTIVATE, TRUE, 0);
            pump_for(std::chrono::milliseconds { 1500 });
            // 표면이 제자리에서 맨 위에 있는지 먼저 본다. 아니면 캡처는 다른 창을 잰다.
            RECT bounds {};
            GetWindowRect(window, &bounds);
            const HWND top { WindowFromPoint(center) };
            if (top != window || bounds.left != window_x || bounds.top != window_y)
            {
                wchar_t top_class[64] {};
                GetClassNameW(top, top_class, 64);
                std::printf("  표면이 제자리 맨 위에 없다: rect=%ld,%ld %ldx%ld, 가운데의 창=%ls\n", bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, top_class);
            }

            const window_capture capture {};
            if (wchar_t directory[MAX_PATH] {}; GetEnvironmentVariableW(L"LUIL_SCREEN_SHOT_DIR", directory, MAX_PATH) != 0)
            {
                std::wstring path { directory };
                path += L"\\backdrop-";
                for (const char* character { presentation }; *character != '\0'; ++character)
                    path += static_cast<wchar_t>(*character);
                path += L"-";
                for (const char* character { backdrop_name(backdrop) }; *character != '\0'; ++character)
                    path += static_cast<wchar_t>(*character);
                path += light ? L"-light.bmp" : L".bmp";
                capture.save(path);
            }

            const int middle { (window_height - surface_height) / 2 };
            const color top_left { capture.at(60, 12) };
            const color middle_left { capture.at(60, middle) };
            const color middle_right { capture.at(window_width - 60, middle) };
            const color surface { capture.at(window_width / 4, window_height - surface_height / 2) };
            // 왼쪽 위 모서리 픽셀(둥글면 판 색)과 아래 6px 밖(그림자면 판보다 어둡다)이다.
            const color corner { capture.at(0, 0) };
            const color below { capture.at(window_width / 4, window_height + 6) };
            const int blur { capture.transition(middle) };
            // popup은 알리지 않는다. 주 창이 같은 OS 사정에서 acrylic을 세웠는가로 갈음한다.
            const int status { kind == surface_kind::secondary ? delegate.window_status.load() : delegate.main_status.load() };
            std::printf("%s %s%s: status=%d middle=(%d,%d,%d)/(%d,%d,%d) transition=%d top_left=(%d,%d,%d) surface=(%d,%d,%d) corner=(%d,%d,%d) below=(%d,%d,%d)\n", presentation,
                backdrop_name(backdrop), light ? " light" : "", status, middle_left.red, middle_left.green, middle_left.blue, middle_right.red, middle_right.green, middle_right.blue, blur,
                top_left.red, top_left.green, top_left.blue, surface.red, surface.green, surface.blue, corner.red, corner.green, corner.blue, below.red, below.green, below.blue);

            // 표면 칸은 어느 효과에서도 불투명하다.
            bool passed { near_color(surface, surface_color) };
            if (passed == false)
                std::printf("  불투명한 표면 칸에 뒤가 비친다.\n");
            const bool applied { status == static_cast<int>(luil::win32::backdrop_status::applied) };
            const bool opaque { reddish(middle_left) == false && bluish(middle_right) == false };
            // popup은 acrylic만 선다. mica 계열은 서지 않아 불투명해야 한다.
            const bool expects_blur { backdrop == luil::window_backdrop::acrylic && applied && (kind != surface_kind::secondary || activate) };
            if (expects_blur)
            {
                // acrylic은 판의 경계를 흐리게 비춘다. 위쪽(캡션 자리)도 같은 효과여야 한다 — 다른 색 띠가 서면 안 된다.
                const bool see_through { reddish(middle_left) && bluish(middle_right) && blur > 6 };
                const bool no_band { reddish(top_left) };
                if (see_through == false)
                    std::printf("  바탕에 흐린 판이 비치지 않는다.\n");
                if (no_band == false)
                    std::printf("  위쪽에 효과와 다른 띠가 있다.\n");
                passed = passed && see_through && no_band;
            }
            else
            {
                // mica·효과 없음·서지 않은 효과·비활성 창의 대체 색은 뒤 창을 비추지 않는다. 판의 빨강·파랑이 보이면
                // 효과가 아니라 투명한 창이다.
                if (opaque == false)
                    std::printf("  효과가 비치지 않아야 할 표면에 뒤가 비친다.\n");
                passed = passed && opaque;
            }
            // 둥근 popup은 효과를 걸어도 모서리와 DWM 그림자가 그대로다.
            if (kind == surface_kind::popup && reddish(corner))
            {
                const bool shadow { below.red < 222 };
                if (shadow == false)
                    std::printf("  둥근 popup 아래에 그림자가 없다.\n");
                passed = passed && shadow;
            }
            exit_code.store(passed ? 0 : 1);
            if (main_window != nullptr)
                PostMessageW(main_window, WM_CLOSE, 0, 0);
            DestroyWindow(board);
        };
        std::thread checker { check };

        luil::win32::window_environment environment {};
        environment.driver = &driver;
        environment.delegate = &delegate;
        const int window_result { luil::win32::run_application_window(config, environment) };
        checker.join();
        return window_result != 0 ? window_result : exit_code.load();
    }

    int run_case(const char* const presentation, const char* const backdrop, const bool light)
    {
        wchar_t path[MAX_PATH] {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring command { L"\"" };
        command += path;
        command += L"\" --child ";
        for (const char* character { presentation }; *character != '\0'; ++character)
            command += static_cast<wchar_t>(*character);
        command += L" ";
        for (const char* character { backdrop }; *character != '\0'; ++character)
            command += static_cast<wchar_t>(*character);
        command += light ? L" light" : L" dark";
        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_SHOWNOACTIVATE;
        PROCESS_INFORMATION process {};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process) == FALSE)
            return 1;
        WaitForSingleObject(process.hProcess, 60000);
        DWORD code { 1 };
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return static_cast<int>(code);
    }

    [[nodiscard]] luil::window_backdrop parse_backdrop(const char* const text) noexcept
    {
        if (std::strcmp(text, "mica") == 0)
            return luil::window_backdrop::mica;
        if (std::strcmp(text, "mica_alt") == 0)
            return luil::window_backdrop::mica_alt;
        if (std::strcmp(text, "acrylic") == 0)
            return luil::window_backdrop::acrylic;
        return luil::window_backdrop::none;
    }
}

int main(const int argc, char** const argv)
{
    if (argc == 5 && std::strcmp(argv[1], "--child") == 0)
        return run_child(argv[2], parse_backdrop(argv[3]), std::strcmp(argv[4], "light") == 0);

    wchar_t enabled[8] {};
    if (GetEnvironmentVariableW(L"LUIL_SCREEN_TESTS", enabled, 8) == 0 || std::wcscmp(enabled, L"1") != 0)
    {
        std::printf("LUIL_SCREEN_TESTS=1이 아니라 건너뛴다 (입력 데스크톱에 창을 띄우는 검사다).\n");
        return 77;
    }
    int failures { 0 };
    const auto run = [&failures](const char* const presentation, const char* const backdrop, const bool light) {
        std::fflush(stdout);
        if (run_case(presentation, backdrop, light) != 0)
            ++failures;
    };
    for (const char* const presentation : { "standard-cpu", "standard-gpu", "layered", "secondary-active", "secondary-inactive", "popup" })
        for (const char* const backdrop : { "none", "mica", "acrylic" })
            run(presentation, backdrop, false);
    // 밝은 테마는 색 막과 옅은 바탕이 다르다. 효과가 비치는 acrylic만 다시 본다.
    for (const char* const presentation : { "standard-gpu", "layered", "secondary-active", "popup" })
        run(presentation, "acrylic", true);
    std::fflush(stdout);
    return failures == 0 ? 0 : 1;
}
