#include "luil/app/app_host.h"
#include "luil/ui/image_element.h"
#include "luil/win32/win32_window.h"
#include "win32/icon_from_image.h"
#include "win32/taskbar_button.h"
#include "win32/window_messages.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <shobjidl.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {
    using luil::taskbar_progress_state;
    using luil::taskbar_request;
    using luil::win32::taskbar_button;
    using luil::win32::taskbar_report;
    using luil::win32::taskbar_status;

    // 셸 개체의 대역이다. 받은 호출을 순서대로 적고, 시험이 정한 결과를 돌려준다.
    class fake_taskbar final : public ITaskbarList3
    {
    public:
        enum class kind
        {
            progress_value,
            progress_state,
            overlay,
        };

        struct call
        {
            kind what { kind::progress_state };
            HWND window { nullptr };
            ULONGLONG completed { 0 };
            ULONGLONG total { 0 };
            TBPFLAG flags { TBPF_NOPROGRESS };
            HICON icon { nullptr };
            std::wstring text {};
            // 오버레이를 받은 순간의 그림 크기다. 핸들은 셸이 보는 동안만 산다.
            int icon_width { 0 };
        };

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
        {
            if (id == __uuidof(IUnknown) || id == __uuidof(ITaskbarList) || id == __uuidof(ITaskbarList2) || id == __uuidof(ITaskbarList3))
            {
                *object = static_cast<ITaskbarList3*>(this);
                AddRef();
                return S_OK;
            }
            *object = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++references_;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG remaining { --references_ };
            if (remaining == 0)
                delete this;
            return remaining;
        }

        HRESULT STDMETHODCALLTYPE HrInit() override
        {
            ++initializations;
            return init_result;
        }

        HRESULT STDMETHODCALLTYPE AddTab(HWND) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DeleteTab(HWND) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE ActivateTab(HWND) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE SetActiveAlt(HWND) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE MarkFullscreenWindow(HWND, BOOL) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE SetProgressValue(HWND window, ULONGLONG completed, ULONGLONG total) override
        {
            calls.push_back(call { .what = kind::progress_value, .window = window, .completed = completed, .total = total });
            return value_result;
        }

        HRESULT STDMETHODCALLTYPE SetProgressState(HWND window, TBPFLAG flags) override
        {
            calls.push_back(call { .what = kind::progress_state, .window = window, .flags = flags });
            return state_result;
        }

        HRESULT STDMETHODCALLTYPE RegisterTab(HWND, HWND) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE UnregisterTab(HWND) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE SetTabOrder(HWND, HWND) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE SetTabActive(HWND, HWND, DWORD) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE ThumbBarAddButtons(HWND, UINT, LPTHUMBBUTTON) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE ThumbBarUpdateButtons(HWND, UINT, LPTHUMBBUTTON) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE ThumbBarSetImageList(HWND, HIMAGELIST) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE SetOverlayIcon(HWND window, HICON icon, LPCWSTR description) override
        {
            call overlay { .what = kind::overlay, .window = window, .icon = icon, .text = description != nullptr ? description : L"" };
            ICONINFO information {};
            if (icon != nullptr && GetIconInfo(icon, &information) != FALSE)
            {
                BITMAP bitmap {};
                if (GetObjectW(information.hbmColor, sizeof(bitmap), &bitmap) != 0)
                    overlay.icon_width = bitmap.bmWidth;
                DeleteObject(information.hbmColor);
                DeleteObject(information.hbmMask);
            }
            calls.push_back(std::move(overlay));
            return overlay_result;
        }

        HRESULT STDMETHODCALLTYPE SetThumbnailTooltip(HWND, LPCWSTR) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE SetThumbnailClip(HWND, RECT*) override
        {
            return S_OK;
        }

        [[nodiscard]] std::size_t count(const kind what) const
        {
            std::size_t total { 0 };
            for (const call& item : calls)
            {
                if (item.what == what)
                    ++total;
            }
            return total;
        }

        std::vector<call> calls {};
        int initializations { 0 };
        HRESULT init_result { S_OK };
        HRESULT value_result { S_OK };
        HRESULT state_result { S_OK };
        HRESULT overlay_result { S_OK };

    private:
        ULONG references_ { 0 };
    };

    // 보통 창 하나다. 단추를 걸 대상이고, 시험이 끝나면 파괴한다.
    class test_window
    {
    public:
        test_window()
            : window_ { CreateWindowExW(0, L"STATIC", L"taskbar button", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) }
        {
        }
        test_window(const test_window&) = delete;
        test_window& operator=(const test_window&) = delete;
        ~test_window()
        {
            if (window_ != nullptr)
                DestroyWindow(window_);
        }

        [[nodiscard]] HWND get() const noexcept
        {
            return window_;
        }

    private:
        HWND window_ { nullptr };
    };

    struct fixture
    {
        fixture()
            : shell { new fake_taskbar }
        {
            REQUIRE(button.open(Microsoft::WRL::ComPtr<ITaskbarList3> { shell }));
        }

        Microsoft::WRL::ComPtr<fake_taskbar> shell;
        taskbar_button button {};
        test_window window {};
        const std::u8string main_surface {};
    };

    [[nodiscard]] taskbar_request progress_request(const taskbar_progress_state state, const float value)
    {
        taskbar_request request {};
        request.progress.state = state;
        request.progress.value = value;
        return request;
    }

    // 가로 `width`, 세로 `height`를 한 색으로 채운 이미지다 (straight alpha RGBA).
    [[nodiscard]] luil::ui_image make_solid_image(const int width, const int height, const std::array<std::uint8_t, 4>& color)
    {
        std::vector<std::uint8_t> pixels {};
        pixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        for (int index = 0; index < width * height; ++index)
            pixels.insert(pixels.end(), color.begin(), color.end());
        return luil::make_rgba_image(width, height, pixels);
    }

    // 아이콘은 USER 개체이고 안의 비트맵은 GDI 개체다. 둘 다 새지 않아야 하므로 합쳐서 센다.
    [[nodiscard]] int gdi_object_count()
    {
        return static_cast<int>(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) + GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS));
    }

    // 처음 한 번 GDI가 프로세스에 두는 개체가 있어 첫 아이콘이 개체 수를 올려 놓는다. 센 값이 아이콘 때문에 늘었는지
    // 보려면 같은 길을 한 번 지나 둔 뒤에 센다.
    void warm_up_gdi()
    {
        fixture items {};
        items.button.button_created(items.main_surface, items.window.get());
        taskbar_request request {};
        request.overlay.icon = make_solid_image(4, 4, { 1, 2, 3, 255 });
        static_cast<void>(items.button.apply(items.main_surface, items.window.get(), 96, request));
        items.button.forget_all();
    }

    // 아이콘을 `background` 회색 위에 그려 한 점의 색을 읽는다. 셸이 단추 위에 얹는 것과 같은 알파 합성이다.
    struct drawn_pixel
    {
        std::uint8_t red { 0 };
        std::uint8_t green { 0 };
        std::uint8_t blue { 0 };
    };

    [[nodiscard]] drawn_pixel draw_over_gray(const HICON icon, const int size, const int x, const int y, const std::uint8_t gray)
    {
        BITMAPINFO information {};
        information.bmiHeader.biSize = sizeof(information.bmiHeader);
        information.bmiHeader.biWidth = size;
        information.bmiHeader.biHeight = -size;
        information.bmiHeader.biPlanes = 1;
        information.bmiHeader.biBitCount = 32;
        information.bmiHeader.biCompression = BI_RGB;
        void* bits { nullptr };
        const HDC memory { CreateCompatibleDC(nullptr) };
        const HBITMAP section { CreateDIBSection(memory, &information, DIB_RGB_COLORS, &bits, nullptr, 0) };
        REQUIRE(section != nullptr);
        const HGDIOBJ previous { SelectObject(memory, section) };
        auto* const pixels { static_cast<std::uint8_t*>(bits) };
        for (int index = 0; index < size * size; ++index)
        {
            pixels[index * 4 + 0] = gray;
            pixels[index * 4 + 1] = gray;
            pixels[index * 4 + 2] = gray;
            pixels[index * 4 + 3] = 255;
        }
        REQUIRE(DrawIconEx(memory, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL) != FALSE);
        GdiFlush();
        const std::uint8_t* const at { pixels + (y * size + x) * 4 };
        const drawn_pixel result { .red = at[2], .green = at[1], .blue = at[0] };
        SelectObject(memory, previous);
        DeleteObject(section);
        DeleteDC(memory);
        return result;
    }

    [[nodiscard]] int distance(const int left, const int right)
    {
        return left > right ? left - right : right - left;
    }

    // 셸이 단추를 만들었다는 등록 메시지를 받으면 소유자가 하듯이 단추를 맞추는 창이다.
    struct owner_state
    {
        taskbar_button* button { nullptr };
        taskbar_request request {};
        std::vector<taskbar_report> reports {};
    };

    LRESULT CALLBACK owner_procedure(const HWND window, const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
    {
        auto* const state { reinterpret_cast<owner_state*>(GetWindowLongPtrW(window, GWLP_USERDATA)) };
        if (state != nullptr && luil::win32::shell_event_of(message) == luil::win32::shell_event::taskbar_button_created)
        {
            state->button->button_created(u8"", window);
            if (const auto report { state->button->apply(u8"", window, 96, state->request) }; report.has_value())
                state->reports.push_back(*report);
            return 0;
        }
        return DefWindowProcW(window, message, word_parameter, long_parameter);
    }

    constexpr const wchar_t* owner_class_name { L"Luil.Test.TaskbarOwner" };
}

TEST_CASE("Without a shell object a request is reported unavailable and a default one stays quiet", "[win32][taskbar]")
{
    taskbar_button button {};
    const test_window window {};
    REQUIRE(button.available() == false);

    REQUIRE(button.apply(u8"", window.get(), 96, taskbar_request {}).has_value() == false);

    const std::optional<taskbar_report> report { button.apply(u8"", window.get(), 96, progress_request(taskbar_progress_state::normal, 0.5f)) };
    REQUIRE(report.has_value());
    REQUIRE(report->status == taskbar_status::unavailable);
    REQUIRE(report->surface.empty());
    // 같은 상태를 다시 알리지 않는다.
    REQUIRE(button.apply(u8"", window.get(), 96, progress_request(taskbar_progress_state::normal, 0.6f)).has_value() == false);
}

TEST_CASE("A shell object that cannot start is not taken", "[win32][taskbar]")
{
    const Microsoft::WRL::ComPtr<fake_taskbar> shell { new fake_taskbar };
    shell->init_result = E_FAIL;
    taskbar_button button {};
    REQUIRE(button.open(Microsoft::WRL::ComPtr<ITaskbarList3> { shell }) == false);
    REQUIRE(button.available() == false);
    REQUIRE(button.open(Microsoft::WRL::ComPtr<ITaskbarList3> {}) == false);
}

TEST_CASE("A request waits for the button and goes out once the creation message arrives", "[win32][taskbar]")
{
    fixture items {};
    const std::optional<taskbar_report> waiting { items.button.apply(items.main_surface, items.window.get(), 96, progress_request(taskbar_progress_state::normal, 0.42f)) };
    REQUIRE(waiting.has_value());
    REQUIRE(waiting->status == taskbar_status::waiting);
    REQUIRE(items.shell->calls.empty());

    items.button.button_created(items.main_surface, items.window.get());
    REQUIRE(items.button.ready(items.main_surface));
    const std::optional<taskbar_report> applied { items.button.apply(items.main_surface, items.window.get(), 96, progress_request(taskbar_progress_state::normal, 0.42f)) };
    REQUIRE(applied.has_value());
    REQUIRE(applied->status == taskbar_status::applied);
    REQUIRE(items.shell->calls.size() == 2);
    REQUIRE(items.shell->calls[0].what == fake_taskbar::kind::progress_value);
    REQUIRE(items.shell->calls[0].window == items.window.get());
    REQUIRE(items.shell->calls[0].completed == 420u);
    REQUIRE(items.shell->calls[0].total == 1000u);
    REQUIRE(items.shell->calls[1].what == fake_taskbar::kind::progress_state);
    REQUIRE(items.shell->calls[1].window == items.window.get());
    REQUIRE(items.shell->calls[1].flags == TBPF_NORMAL);

    // 같은 요청은 호출도 보고도 없다.
    REQUIRE(items.button.apply(items.main_surface, items.window.get(), 96, progress_request(taskbar_progress_state::normal, 0.42f)).has_value() == false);
    REQUIRE(items.shell->calls.size() == 2);
}

TEST_CASE("Each progress state maps to its shell flag", "[win32][taskbar]")
{
    fixture items {};
    items.button.button_created(items.main_surface, items.window.get());

    struct expectation
    {
        taskbar_progress_state state;
        TBPFLAG flag;
        bool carries_value;
    };
    const std::array<expectation, 5> table {
        expectation { taskbar_progress_state::indeterminate, TBPF_INDETERMINATE, false },
        expectation { taskbar_progress_state::normal, TBPF_NORMAL, true },
        expectation { taskbar_progress_state::error, TBPF_ERROR, true },
        expectation { taskbar_progress_state::paused, TBPF_PAUSED, true },
        expectation { taskbar_progress_state::none, TBPF_NOPROGRESS, false },
    };
    for (const expectation& item : table)
    {
        items.shell->calls.clear();
        static_cast<void>(items.button.apply(items.main_surface, items.window.get(), 96, progress_request(item.state, 0.5f)));
        REQUIRE(items.shell->calls.size() == (item.carries_value ? 2u : 1u));
        REQUIRE(items.shell->calls.back().what == fake_taskbar::kind::progress_state);
        REQUIRE(items.shell->calls.back().flags == item.flag);
    }
}

TEST_CASE("A failed call is reported once and not retried with the same request", "[win32][taskbar]")
{
    fixture items {};
    items.button.button_created(items.main_surface, items.window.get());
    items.shell->state_result = E_FAIL;

    const taskbar_request request { progress_request(taskbar_progress_state::paused, 0.3f) };
    const std::optional<taskbar_report> failed { items.button.apply(items.main_surface, items.window.get(), 96, request) };
    REQUIRE(failed.has_value());
    REQUIRE(failed->status == taskbar_status::failed);
    REQUIRE(failed->error.find(u8"SetProgressState") != std::u8string::npos);
    REQUIRE(failed->error.find(u8"0x80004005") != std::u8string::npos);

    const std::size_t calls { items.shell->calls.size() };
    REQUIRE(items.button.apply(items.main_surface, items.window.get(), 96, request).has_value() == false);
    REQUIRE(items.shell->calls.size() == calls);

    // 다음 변화에서 다시 하고, 이번에는 성공했으니 `applied`로 돌아온다.
    items.shell->state_result = S_OK;
    const std::optional<taskbar_report> recovered { items.button.apply(items.main_surface, items.window.get(), 96, progress_request(taskbar_progress_state::paused, 0.9f)) };
    REQUIRE(recovered.has_value());
    REQUIRE(recovered->status == taskbar_status::applied);
    REQUIRE(recovered->error.empty());
}

TEST_CASE("The creation message sends the held request again from the start", "[win32][taskbar]")
{
    fixture items {};
    items.button.button_created(items.main_surface, items.window.get());
    const taskbar_request request { progress_request(taskbar_progress_state::error, 0.8f) };
    static_cast<void>(items.button.apply(items.main_surface, items.window.get(), 96, request));
    items.shell->calls.clear();

    // 탐색기가 다시 뜬 뒤의 새 단추다.
    items.button.button_created(items.main_surface, items.window.get());
    REQUIRE(items.button.apply(items.main_surface, items.window.get(), 96, request).has_value() == false);
    REQUIRE(items.shell->calls.size() == 2);
    REQUIRE(items.shell->calls[0].completed == 800u);
    REQUIRE(items.shell->calls[1].flags == TBPF_ERROR);
}

TEST_CASE("A hidden window keeps the request and sends it when the button stands again", "[win32][taskbar]")
{
    fixture items {};
    items.button.button_created(items.main_surface, items.window.get());
    static_cast<void>(items.button.apply(items.main_surface, items.window.get(), 96, progress_request(taskbar_progress_state::normal, 0.2f)));
    items.shell->calls.clear();

    items.button.button_lost(items.main_surface);
    REQUIRE(items.button.ready(items.main_surface) == false);
    const std::optional<taskbar_report> waiting { items.button.apply(items.main_surface, items.window.get(), 96, progress_request(taskbar_progress_state::normal, 0.7f)) };
    REQUIRE(waiting.has_value());
    REQUIRE(waiting->status == taskbar_status::waiting);
    REQUIRE(items.shell->calls.empty());

    items.button.button_created(items.main_surface, items.window.get());
    static_cast<void>(items.button.apply(items.main_surface, items.window.get(), 96, progress_request(taskbar_progress_state::normal, 0.7f)));
    REQUIRE(items.shell->calls.size() == 2);
    REQUIRE(items.shell->calls[0].completed == 700u);
}

TEST_CASE("A registered creation message to a hidden window sends the held request", "[win32][taskbar]")
{
    // 소유자가 하는 일을 그대로 하는 창이다. 등록 메시지는 번호가 실행마다 달라 진짜 메시지를 보내 본다.
    WNDCLASSEXW window_class {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &owner_procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = owner_class_name;
    REQUIRE((RegisterClassExW(&window_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS));
    const HWND window { CreateWindowExW(0, owner_class_name, L"owner", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) };
    REQUIRE(window != nullptr);
    luil::win32::allow_shell_events(window);

    const Microsoft::WRL::ComPtr<fake_taskbar> shell { new fake_taskbar };
    taskbar_button button {};
    REQUIRE(button.open(Microsoft::WRL::ComPtr<ITaskbarList3> { shell }));
    owner_state state {};
    state.button = &button;
    state.request = progress_request(taskbar_progress_state::normal, 0.5f);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    // 단추가 서기 전에는 쥐기만 한다.
    const std::optional<taskbar_report> waiting { button.apply(u8"", window, 96, state.request) };
    REQUIRE(waiting.has_value());
    REQUIRE(waiting->status == taskbar_status::waiting);
    REQUIRE(shell->calls.empty());

    SendMessageW(window, luil::win32::shell_event_message(luil::win32::shell_event::taskbar_button_created), 0, 0);
    REQUIRE(shell->calls.size() == 2);
    REQUIRE(shell->calls[0].window == window);
    REQUIRE(shell->calls[0].completed == 500u);
    REQUIRE(state.reports.size() == 1);
    REQUIRE(state.reports[0].status == taskbar_status::applied);

    // 방송되는 `TaskbarCreated`는 이 길이 쓰지 않는다.
    SendMessageW(window, luil::win32::shell_event_message(luil::win32::shell_event::taskbar_created), 0, 0);
    REQUIRE(shell->calls.size() == 2);

    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    DestroyWindow(window);
}

TEST_CASE("The overlay is set replaced described and cleared", "[win32][taskbar]")
{
    fixture items {};
    items.button.button_created(items.main_surface, items.window.get());

    taskbar_request request {};
    request.overlay.icon = make_solid_image(8, 8, { 255, 0, 0, 255 });
    request.overlay.description = u8"새 소식";
    const std::optional<taskbar_report> first { items.button.apply(items.main_surface, items.window.get(), 96, request) };
    REQUIRE(first.has_value());
    REQUIRE(first->status == taskbar_status::applied);
    REQUIRE(items.shell->calls.size() == 1);
    const fake_taskbar::call set { items.shell->calls[0] };
    REQUIRE(set.what == fake_taskbar::kind::overlay);
    REQUIRE(set.window == items.window.get());
    REQUIRE(set.icon != nullptr);
    REQUIRE(set.text == L"새 소식");
    REQUIRE(set.icon_width == GetSystemMetricsForDpi(SM_CXSMICON, 96));

    // 같은 이미지의 복사본은 호출이 없다.
    request.overlay.icon = luil::ui_image { request.overlay.icon };
    REQUIRE(items.button.apply(items.main_surface, items.window.get(), 96, request).has_value() == false);
    REQUIRE(items.shell->calls.size() == 1);

    // 설명만 바뀌면 같은 핸들로 다시 건다.
    request.overlay.description = u8"새 소식 둘";
    static_cast<void>(items.button.apply(items.main_surface, items.window.get(), 96, request));
    REQUIRE(items.shell->calls.size() == 2);
    REQUIRE(items.shell->calls[1].icon == set.icon);
    REQUIRE(items.shell->calls[1].text == L"새 소식 둘");

    // 다른 이미지는 새 핸들로 바꾼다.
    request.overlay.icon = make_solid_image(8, 8, { 0, 255, 0, 255 });
    static_cast<void>(items.button.apply(items.main_surface, items.window.get(), 96, request));
    REQUIRE(items.shell->calls.size() == 3);
    REQUIRE(items.shell->calls[2].icon != nullptr);
    REQUIRE(items.shell->calls[2].icon != set.icon);

    // 빈 이미지는 해제한다.
    request.overlay.icon = {};
    const std::optional<taskbar_report> cleared { items.button.apply(items.main_surface, items.window.get(), 96, request) };
    REQUIRE(items.shell->calls.size() == 4);
    REQUIRE(items.shell->calls[3].icon == nullptr);
    REQUIRE(items.shell->calls[3].text.empty());
    REQUIRE(cleared.has_value());
    REQUIRE(cleared->status == taskbar_status::off);
}

TEST_CASE("A new scale sends the overlay again at the new size", "[win32][taskbar]")
{
    fixture items {};
    items.button.button_created(items.main_surface, items.window.get());
    taskbar_request request { progress_request(taskbar_progress_state::normal, 0.5f) };
    request.overlay.icon = make_solid_image(8, 8, { 0, 0, 255, 255 });
    static_cast<void>(items.button.apply(items.main_surface, items.window.get(), 96, request));
    items.shell->calls.clear();

    static_cast<void>(items.button.apply(items.main_surface, items.window.get(), 192, request));
    REQUIRE(items.shell->calls.size() == 1);
    REQUIRE(items.shell->calls[0].what == fake_taskbar::kind::overlay);
    REQUIRE(items.shell->calls[0].icon_width == GetSystemMetricsForDpi(SM_CXSMICON, 192));
    // 진행률은 그대로다.
    REQUIRE(items.shell->count(fake_taskbar::kind::progress_value) == 0);
}

TEST_CASE("An overlay description that is not UTF-8 goes out empty with the reason", "[win32][taskbar]")
{
    fixture items {};
    items.button.button_created(items.main_surface, items.window.get());
    taskbar_request request {};
    request.overlay.icon = make_solid_image(4, 4, { 255, 255, 255, 255 });
    request.overlay.description = std::u8string { char8_t { 0xC3 } };
    const std::optional<taskbar_report> report { items.button.apply(items.main_surface, items.window.get(), 96, request) };
    REQUIRE(items.shell->calls.size() == 1);
    REQUIRE(items.shell->calls[0].icon != nullptr);
    REQUIRE(items.shell->calls[0].text.empty());
    REQUIRE(report.has_value());
    REQUIRE(report->status == taskbar_status::failed);
    REQUIRE(report->error.find(u8"UTF-8") != std::u8string::npos);
}

TEST_CASE("A refused overlay leaves the previous one and reports a failure", "[win32][taskbar]")
{
    fixture items {};
    items.button.button_created(items.main_surface, items.window.get());
    taskbar_request request {};
    request.overlay.icon = make_solid_image(4, 4, { 255, 255, 255, 255 });
    static_cast<void>(items.button.apply(items.main_surface, items.window.get(), 96, request));
    REQUIRE(items.shell->calls.size() == 1);

    // 셸이 새 그림을 거절하면 이전 오버레이를 두고 실패로 알린다.
    items.shell->overlay_result = E_FAIL;
    request.overlay.icon = make_solid_image(4, 4, { 0, 0, 0, 255 });
    const std::optional<taskbar_report> report { items.button.apply(items.main_surface, items.window.get(), 96, request) };
    REQUIRE(report.has_value());
    REQUIRE(report->status == taskbar_status::failed);
    REQUIRE(items.shell->calls.size() == 2);
}

TEST_CASE("Windows keep their own records and forgetting one lets go of its icon", "[win32][taskbar]")
{
    warm_up_gdi();
    fixture items {};
    const test_window other {};
    const std::u8string tool { u8"tool" };
    items.button.button_created(items.main_surface, items.window.get());
    items.button.button_created(tool, other.get());

    taskbar_request request { progress_request(taskbar_progress_state::normal, 0.5f) };
    request.overlay.icon = make_solid_image(8, 8, { 255, 0, 0, 255 });
    const int before { gdi_object_count() };
    static_cast<void>(items.button.apply(items.main_surface, items.window.get(), 96, request));
    static_cast<void>(items.button.apply(tool, other.get(), 96, progress_request(taskbar_progress_state::error, 0.1f)));
    REQUIRE(gdi_object_count() > before);

    // 두 창의 호출은 각자의 창에 간다.
    std::size_t on_main { 0 };
    std::size_t on_other { 0 };
    for (const fake_taskbar::call& call : items.shell->calls)
    {
        on_main += call.window == items.window.get() ? 1 : 0;
        on_other += call.window == other.get() ? 1 : 0;
    }
    REQUIRE(on_main == 3);
    REQUIRE(on_other == 2);

    items.button.forget(items.main_surface);
    REQUIRE(gdi_object_count() == before);
    REQUIRE(items.button.ready(items.main_surface) == false);
    REQUIRE(items.button.ready(tool));

    // 목록에 없는 표면의 기록은 놓는다.
    items.button.retain({ items.main_surface });
    REQUIRE(items.button.ready(tool) == false);
}

TEST_CASE("The icon handles are let go when the records go", "[win32][taskbar]")
{
    warm_up_gdi();
    const int before { gdi_object_count() };
    {
        fixture items {};
        items.button.button_created(items.main_surface, items.window.get());
        taskbar_request request {};
        for (int round = 0; round < 8; ++round)
        {
            request.overlay.icon = make_solid_image(16, 16, { static_cast<std::uint8_t>(round * 30), 0, 0, 255 });
            static_cast<void>(items.button.apply(items.main_surface, items.window.get(), 96, request));
        }
        items.button.forget_all();
    }
    REQUIRE(gdi_object_count() == before);
}

TEST_CASE("An icon is made at the asked size from an opaque transparent and translucent pixel", "[win32][taskbar]")
{
    std::u8string error {};
    {
        luil::win32::owned_icon icon { luil::win32::make_icon(make_solid_image(4, 4, { 255, 0, 0, 255 }), 16, error) };
        REQUIRE(icon.valid());
        REQUIRE(error.empty());
        ICONINFO information {};
        REQUIRE(GetIconInfo(icon.get(), &information) != FALSE);
        BITMAP bitmap {};
        REQUIRE(GetObjectW(information.hbmColor, sizeof(bitmap), &bitmap) != 0);
        REQUIRE(bitmap.bmWidth == 16);
        REQUIRE(bitmap.bmHeight == 16);
        REQUIRE(bitmap.bmBitsPixel == 32);
        DeleteObject(information.hbmColor);
        DeleteObject(information.hbmMask);

        const drawn_pixel opaque { draw_over_gray(icon.get(), 16, 8, 8, 128) };
        REQUIRE(opaque.red == 255);
        REQUIRE(opaque.green == 0);
        REQUIRE(opaque.blue == 0);
    }
    {
        luil::win32::owned_icon icon { luil::win32::make_icon(make_solid_image(4, 4, { 255, 255, 255, 0 }), 16, error) };
        REQUIRE(icon.valid());
        const drawn_pixel clear { draw_over_gray(icon.get(), 16, 8, 8, 128) };
        REQUIRE(clear.red == 128);
        REQUIRE(clear.green == 128);
        REQUIRE(clear.blue == 128);
    }
}

TEST_CASE("A translucent edge does not darken", "[win32][taskbar]")
{
    // 알파가 절반인 흰색을 회색 128 위에 얹으면 191 언저리다. 알파를 곱한 값을 알파 없이 다시 곱하면 128로 어두워진다.
    std::u8string error {};
    const luil::win32::owned_icon icon { luil::win32::make_icon(make_solid_image(4, 4, { 255, 255, 255, 128 }), 16, error) };
    REQUIRE(icon.valid());
    const drawn_pixel pixel { draw_over_gray(icon.get(), 16, 8, 8, 128) };
    INFO("red=" << static_cast<int>(pixel.red) << " green=" << static_cast<int>(pixel.green) << " blue=" << static_cast<int>(pixel.blue));
    REQUIRE(distance(pixel.red, 191) <= 6);
    REQUIRE(distance(pixel.green, 191) <= 6);
    REQUIRE(distance(pixel.blue, 191) <= 6);
}

TEST_CASE("A wide image is fitted inside the square and the rest stays transparent", "[win32][taskbar]")
{
    std::u8string error {};
    const luil::win32::owned_icon icon { luil::win32::make_icon(make_solid_image(8, 4, { 255, 0, 0, 255 }), 16, error) };
    REQUIRE(icon.valid());
    // 2:1은 세로 8칸을 차지한다. 위·아래 4줄은 비어 있다.
    const drawn_pixel above { draw_over_gray(icon.get(), 16, 8, 1, 100) };
    const drawn_pixel middle { draw_over_gray(icon.get(), 16, 8, 8, 100) };
    const drawn_pixel below { draw_over_gray(icon.get(), 16, 8, 14, 100) };
    REQUIRE(above.red == 100);
    REQUIRE(middle.red == 255);
    REQUIRE(middle.green == 0);
    REQUIRE(below.red == 100);
}

TEST_CASE("An image that is not there or a size that is not positive is refused", "[win32][taskbar]")
{
    std::u8string error {};
    REQUIRE(luil::win32::make_icon(luil::ui_image {}, 16, error).valid() == false);
    REQUIRE(error.empty() == false);

    error.clear();
    REQUIRE(luil::win32::make_icon(make_solid_image(2, 2, { 1, 2, 3, 255 }), 0, error).valid() == false);
    REQUIRE(error.empty() == false);
}

TEST_CASE("Owned icons move and are destroyed once", "[win32][taskbar]")
{
    warm_up_gdi();
    const int before { gdi_object_count() };
    std::u8string error {};
    luil::win32::owned_icon first { luil::win32::make_icon(make_solid_image(4, 4, { 9, 9, 9, 255 }), 16, error) };
    REQUIRE(first.valid());
    luil::win32::owned_icon second { std::move(first) };
    REQUIRE(first.valid() == false);
    REQUIRE(second.valid());
    second.reset();
    REQUIRE(second.valid() == false);
    REQUIRE(gdi_object_count() == before);
}
