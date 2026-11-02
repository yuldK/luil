#include "luil/app/platform_services.h"

#include "win32/command_line.h"
#include "win32/instance_message.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// 플랫폼 도구의 Windows 쪽을 실제 OS 호출로 본다 (platform-services-design.md).
// 탐색기를 여는 갈래는 창이 뜨므로 보지 않는다 — 없는 경로의 답만 본다.

namespace {
    // test 하나의 임시 폴더다. 끝나면 지운다.
    class scratch_folder
    {
    public:
        scratch_folder()
            : path_ { std::filesystem::temp_directory_path() / (L"luil-platform-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter_)) }
        {
            std::filesystem::create_directories(path_);
        }
        scratch_folder(const scratch_folder&) = delete;
        scratch_folder& operator=(const scratch_folder&) = delete;
        ~scratch_folder()
        {
            std::error_code ignored {};
            std::filesystem::remove_all(path_, ignored);
        }

        [[nodiscard]] std::u8string file(const std::wstring_view name) const
        {
            return (path_ / name).u8string();
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

    private:
        static inline int counter_ { 0 };
        std::filesystem::path path_ {};
    };

    [[nodiscard]] std::vector<std::uint8_t> bytes_of(const std::string_view text)
    {
        return { text.begin(), text.end() };
    }

    [[nodiscard]] std::string read_text(const std::u8string& path)
    {
        std::ifstream file { std::filesystem::path { path }, std::ios::binary };
        return { std::istreambuf_iterator<char> { file }, std::istreambuf_iterator<char> {} };
    }

    [[nodiscard]] std::size_t files_in(const std::filesystem::path& folder)
    {
        std::size_t count { 0 };
        for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator { folder })
            ++count;
        return count;
    }
}

TEST_CASE("A file is replaced whole and leaves no temporary file behind", "[platform]")
{
    const scratch_folder folder {};
    const std::u8string path { folder.file(L"설정.json") };
    REQUIRE(luil::replace_file_atomically(path, bytes_of("first")).ok());
    REQUIRE(read_text(path) == "first");
    REQUIRE(luil::replace_file_atomically(path, bytes_of("second, longer contents")).ok());
    REQUIRE(read_text(path) == "second, longer contents");
    // 짧아진 내용에 옛 꼬리가 남지 않는다.
    REQUIRE(luil::replace_file_atomically(path, bytes_of("3")).ok());
    REQUIRE(read_text(path) == "3");
    REQUIRE(files_in(folder.path()) == 1u);

    // 폴더를 만들지 않는다.
    const luil::platform_result missing { luil::replace_file_atomically(folder.file(L"없는 폴더/a.txt"), bytes_of("x")) };
    REQUIRE(missing.status == luil::platform_status::failed);
    REQUIRE(missing.error.find(u8"folder") != std::u8string::npos);
}

TEST_CASE("Only the first instance of a name is primary until it lets go", "[platform]")
{
    const std::u8string name { u8"luil-tests-" + std::u8string { reinterpret_cast<const char8_t*>(std::to_string(GetCurrentProcessId()).c_str()) } };
    std::optional<luil::single_instance> first { std::in_place, name };
    REQUIRE(first->status().ok());
    REQUIRE(first->primary());
    {
        const luil::single_instance second { name };
        REQUIRE(second.primary() == false);
    }
    // 둘째가 놓아도 첫째는 첫째다. 첫째가 놓으면 다음 것이 첫째다.
    REQUIRE(luil::single_instance { name }.primary() == false);
    first.reset();
    REQUIRE(luil::single_instance { name }.primary());
}

TEST_CASE("A file lock is exclusive and released with its owner", "[platform]")
{
    const scratch_folder folder {};
    const std::u8string path { folder.file(L"작업.lock") };
    std::optional<luil::file_lock> first { std::in_place, path };
    REQUIRE(first->locked());
    {
        const luil::file_lock second { path };
        REQUIRE(second.locked() == false);
        // 잠그지 못한 것은 오류가 아니라 답이다.
        REQUIRE(second.status().ok());
    }
    first.reset();
    REQUIRE(luil::file_lock { path }.locked());
}

TEST_CASE("Command line arguments survive spaces, quotes and trailing backslashes", "[platform]")
{
    using luil::win32::quote_argument;
    REQUIRE(quote_argument(L"plain") == L"plain");
    REQUIRE(quote_argument(L"") == L"\"\"");
    REQUIRE(quote_argument(L"with space") == L"\"with space\"");
    REQUIRE(quote_argument(L"say \"hi\"") == L"\"say \\\"hi\\\"\"");
    // 닫는 따옴표 앞의 백슬래시는 두 배다 — 그러지 않으면 따옴표가 글자로 읽힌다.
    REQUIRE(quote_argument(L"C:\\Program Files\\") == L"\"C:\\Program Files\\\\\"");
    REQUIRE(quote_argument(L"a\\\\\"b") == L"\"a\\\\\\\\\\\"b\"");
    // 따옴표가 없는 백슬래시는 그대로다.
    REQUIRE(quote_argument(L"C:\\temp\\file") == L"C:\\temp\\file");

    // 되읽기가 같은 인자를 준다.
    const std::vector<std::wstring> arguments { L"one", L"two words", L"end\\", L"q\"uote", L"" };
    const std::wstring line { luil::win32::make_command_line(L"C:\\Program Files\\app.exe", arguments) };
    int count { 0 };
    LPWSTR* const parsed { CommandLineToArgvW(line.c_str(), &count) };
    REQUIRE(parsed != nullptr);
    REQUIRE(count == 6);
    REQUIRE(std::wstring { parsed[0] } == L"C:\\Program Files\\app.exe");
    for (std::size_t index { 0 }; index < arguments.size(); ++index)
        REQUIRE(std::wstring { parsed[index + 1] } == arguments[index]);
    LocalFree(parsed);
}

TEST_CASE("A launched program reports its exit code without a window", "[platform]")
{
    const std::vector<std::u8string> arguments { u8"/c", u8"exit 3" };
    const luil::platform_result result { luil::launch_program(u8"cmd.exe", arguments, { .show_window = false, .wait = true }) };
    REQUIRE(result.ok());
    REQUIRE(result.exit_code == 3);
    REQUIRE(luil::launch_program(u8"luil-no-such-program.exe", {}, { .show_window = false }).status == luil::platform_status::not_found);
    REQUIRE(luil::reveal_in_file_manager(u8"C:\\luil-no-such-folder\\file.txt").status == luil::platform_status::not_found);
}

TEST_CASE("Only URLs whose scheme is on the allow list may be opened", "[platform]")
{
    const luil::open_url_options defaults {};
    const auto allowed = [&defaults](const std::u8string_view url) { return luil::url_scheme_allowed(url, defaults.schemes); };
    REQUIRE(allowed(u8"https://example.com/path?q=1"));
    REQUIRE(allowed(u8"http://example.com"));
    REQUIRE(allowed(u8"mailto:someone@example.com"));
    // scheme은 대소문자를 가리지 않는다.
    REQUIRE(allowed(u8"HTTPS://EXAMPLE.COM"));
    REQUIRE(allowed(u8"MailTo:someone@example.com"));
    REQUIRE(allowed(u8"https://例え.jp/경로"));

    REQUIRE(allowed(u8"javascript:alert(1)") == false);
    REQUIRE(allowed(u8"file:///C:/Windows/System32/calc.exe") == false);
    REQUIRE(allowed(u8"C:\\Windows\\System32\\calc.exe") == false);
    REQUIRE(allowed(u8"calc.exe") == false);
    REQUIRE(allowed(u8"example.com/a:b") == false);
    REQUIRE(allowed(u8"//example.com") == false);
    REQUIRE(allowed(u8"") == false);
    REQUIRE(allowed(u8":") == false);
    REQUIRE(allowed(u8"https:") == false);
    REQUIRE(allowed(u8"1http://example.com") == false);
    REQUIRE(allowed(u8"ht tp://example.com") == false);
    // OS가 공백·제어 문자를 걸러 읽으면 막은 scheme이 되살아난다.
    REQUIRE(allowed(u8" http://example.com") == false);
    REQUIRE(allowed(u8"http://example.com ") == false);
    REQUIRE(allowed(u8"java\tscript:alert(1)") == false);
    REQUIRE(allowed(u8"https://example.com/\n") == false);
    REQUIRE(allowed(u8"https://example.com/\u0085") == false);

    // 사용자 지정 목록은 기본 목록을 대신한다. 목록 쪽 대소문자도 가리지 않는다.
    const std::vector<std::u8string> custom { u8"VSCODE", u8"git+ssh" };
    REQUIRE(luil::url_scheme_allowed(u8"vscode://file/a.txt", custom));
    REQUIRE(luil::url_scheme_allowed(u8"git+ssh://host/repo.git", custom));
    REQUIRE(luil::url_scheme_allowed(u8"https://example.com", custom) == false);
    REQUIRE(luil::url_scheme_allowed(u8"https://example.com", {}) == false);
}

TEST_CASE("Opening a URL with a blocked scheme opens nothing and fails", "[platform]")
{
    // 막히는 주소만 넘긴다. 허용된 주소는 실제 브라우저를 띄우므로 여기서 열지 않는다.
    for (const std::u8string_view url : { u8"javascript:alert(1)", u8"file:///C:/Windows/System32/calc.exe", u8"calc.exe", u8"C:\\Windows\\System32\\calc.exe", u8" https://example.com" })
    {
        const luil::platform_result result { luil::open_url(url) };
        REQUIRE(result.status == luil::platform_status::failed);
        REQUIRE(result.error.find(u8"scheme") != std::u8string::npos);
    }
    // 허용 목록을 비우면 무엇도 열지 않는다.
    REQUIRE(luil::open_url(u8"https://example.com", { .schemes = {} }).status == luil::platform_status::failed);
}

TEST_CASE("Background priority goes down and back up on the calling thread", "[platform]")
{
    REQUIRE(luil::set_background_priority(true).ok());
    // 같은 모드를 두 번 청해도 실패가 아니다.
    REQUIRE(luil::set_background_priority(true).ok());
    REQUIRE(luil::set_background_priority(false).ok());
    REQUIRE(luil::set_background_priority(false).ok());
}

TEST_CASE("A thread blocked on a synchronous pipe read is cancelled from another thread", "[platform]")
{
    HANDLE read_end { nullptr };
    HANDLE write_end { nullptr };
    REQUIRE(CreatePipe(&read_end, &write_end, nullptr, 0) != FALSE);

    std::optional<luil::blocking_io_canceller> canceller {};
    std::atomic<bool> ready { false };
    std::atomic<DWORD> read_error { 0 };
    const auto block_on_read = [&] {
        canceller.emplace();
        ready.store(true);
        char byte { 0 };
        DWORD read { 0 };
        // 쓰는 쪽이 아무것도 쓰지 않으므로 여기서 막힌다.
        if (ReadFile(read_end, &byte, 1, &read, nullptr) == FALSE)
            read_error.store(GetLastError());
    };
    std::thread reader { block_on_read };
    while (ready.load() == false)
        std::this_thread::yield();

    // thread가 ReadFile에 들어서기 전이면 끊을 것이 없다(`not_found`). 들어설 때까지 다시 부른다.
    bool cancelled { false };
    const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 5 } };
    while (cancelled == false && std::chrono::steady_clock::now() < deadline)
    {
        const luil::platform_result result { canceller->cancel() };
        REQUIRE((result.ok() || result.status == luil::platform_status::not_found));
        cancelled = result.ok();
        if (cancelled == false)
            std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
    }
    reader.join();
    REQUIRE(cancelled);
    REQUIRE(read_error.load() == ERROR_OPERATION_ABORTED);
    // 막힌 것이 없으면 `not_found`다.
    REQUIRE(canceller->cancel().status == luil::platform_status::not_found);
    CloseHandle(read_end);
    CloseHandle(write_end);
}

namespace {
    // 둘째 인스턴스의 보내기를 받는 시험용 최상위 창이다 (single-instance-args-design.md).
    //  - 별도 thread의 숨은 창이다. 같은 thread의 창이면 `SendMessageTimeoutW`가 시한 없이 프로시저를 바로 부르므로
    //    실제로 thread를 건너는 길을 보려면 따로 세워야 한다.
    //  - 첫째의 주 창처럼 이름 표식을 걸 수도, 걸지 않을 수도 있다.
    class receiver_window
    {
    public:
        enum class reply
        {
            accept,
            reject,
            // 받은 것을 적고 한참 있다가 답한다. 응답하지 않는 첫째다.
            stall,
        };

        struct delivery
        {
            luil::instance_arguments arguments {};
            bool activate { false };
        };

        receiver_window(const std::u8string& name, const bool marked, const reply behavior)
            : behavior_ { behavior }
        {
            if (marked)
                property_ = luil::win32::instance_property_name(name).value_or(std::wstring {});
            thread_ = std::thread { [this] { run(); } };
            std::unique_lock lock { mutex_ };
            ready_.wait(lock, [this] { return window_ != nullptr || failed_; });
            if (window_ == nullptr)
            {
                // 생성자가 던지면 소멸자가 돌지 않으므로 thread를 여기서 거둔다.
                lock.unlock();
                thread_.join();
                FAIL("The receiver window could not be created.");
            }
        }

        receiver_window(const receiver_window&) = delete;
        receiver_window& operator=(const receiver_window&) = delete;

        ~receiver_window()
        {
            if (window_ != nullptr)
                PostMessageW(window_, WM_CLOSE, 0, 0);
            thread_.join();
        }

        [[nodiscard]] std::vector<delivery> deliveries() const
        {
            const std::lock_guard lock { mutex_ };
            return deliveries_;
        }

        [[nodiscard]] int refused() const
        {
            const std::lock_guard lock { mutex_ };
            return refused_;
        }

    private:
        static constexpr const wchar_t* class_name { L"Luil.Test.InstanceReceiver" };
        static constexpr DWORD stall_milliseconds { 1500 };

        static LRESULT CALLBACK procedure(const HWND window, const UINT message, const WPARAM word_parameter, const LPARAM long_parameter)
        {
            if (message == WM_NCCREATE)
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<const CREATESTRUCTW*>(long_parameter)->lpCreateParams));
            auto* const self { reinterpret_cast<receiver_window*>(GetWindowLongPtrW(window, GWLP_USERDATA)) };
            if (message == WM_COPYDATA && self != nullptr)
                return self->receive(*reinterpret_cast<const COPYDATASTRUCT*>(long_parameter));
            if (message == WM_CLOSE)
            {
                if (self != nullptr && self->property_.empty() == false)
                    RemovePropW(window, self->property_.c_str());
                DestroyWindow(window);
                return 0;
            }
            if (message == WM_DESTROY)
            {
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcW(window, message, word_parameter, long_parameter);
        }

        LRESULT receive(const COPYDATASTRUCT& data)
        {
            const std::span<const std::byte> bytes { static_cast<const std::byte*>(data.lpData), data.cbData };
            luil::win32::instance_decode_result message { luil::win32::decode_instance_message(data.dwData, bytes) };
            {
                const std::lock_guard lock { mutex_ };
                if (message.ok())
                    deliveries_.push_back(delivery { std::move(message.value), message.activate });
                else
                    ++refused_;
            }
            if (message.ok() == false)
                return FALSE;
            if (behavior_ == reply::stall)
                Sleep(stall_milliseconds);
            return behavior_ == reply::reject ? FALSE : TRUE;
        }

        void run()
        {
            WNDCLASSEXW window_class {};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = &procedure;
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpszClassName = class_name;
            if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                publish(nullptr);
                return;
            }
            const HWND window { CreateWindowExW(0, class_name, L"instance receiver", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), this) };
            if (window != nullptr && property_.empty() == false)
                SetPropW(window, property_.c_str(), reinterpret_cast<HANDLE>(1));
            publish(window);
            if (window == nullptr)
                return;
            MSG message {};
            while (GetMessageW(&message, nullptr, 0, 0) > 0)
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        void publish(const HWND window)
        {
            {
                const std::lock_guard lock { mutex_ };
                window_ = window;
                failed_ = window == nullptr;
            }
            ready_.notify_all();
        }

        reply behavior_ { reply::accept };
        std::wstring property_ {};
        mutable std::mutex mutex_ {};
        std::condition_variable ready_ {};
        HWND window_ { nullptr };
        bool failed_ { false };
        std::vector<delivery> deliveries_ {};
        int refused_ { 0 };
        std::thread thread_ {};
    };

    // test마다 다른 이름이다. 한 프로세스 안에서 앞 test의 객체와 겹치지 않게 한다.
    [[nodiscard]] std::u8string unique_instance_name(const std::string_view purpose)
    {
        static int counter { 0 };
        const std::string text { "luil-tests-send-" + std::string { purpose } + "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++counter) };
        return std::u8string { text.begin(), text.end() };
    }

    // 보내는 test가 쓰는 설정이다. 시험 창은 앞으로 서지 않는다.
    [[nodiscard]] luil::instance_send_options send_options(const int timeout_milliseconds, const bool activate = false)
    {
        luil::instance_send_options options {};
        options.timeout = std::chrono::milliseconds { timeout_milliseconds };
        options.activate = activate;
        return options;
    }
}

TEST_CASE("A single instance remembers the name it was given", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("name") };
    luil::single_instance first { name };
    REQUIRE(first.name() == name);

    // 옮겨 간 뒤의 빈 객체는 빈 이름이고 보낼 수 없다.
    const luil::single_instance moved { std::move(first) };
    REQUIRE(moved.name() == name);
    REQUIRE(first.name().empty());
    REQUIRE(first.send_to_primary({}, send_options(50)).status == luil::platform_status::failed);
}

TEST_CASE("The first instance has no one to send to", "[platform][instance]")
{
    const luil::single_instance first { unique_instance_name("first") };
    REQUIRE(first.primary());
    REQUIRE(first.send_to_primary(std::vector<std::u8string> { u8"a.txt" }, send_options(50)).status == luil::platform_status::not_found);
}

TEST_CASE("A second instance gives up after the time limit when no window carries the mark", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("none") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    REQUIRE(second.primary() == false);

    const auto before { std::chrono::steady_clock::now() };
    const luil::platform_result result { second.send_to_primary(std::vector<std::u8string> { u8"a.txt" }, send_options(50)) };
    const auto elapsed { std::chrono::steady_clock::now() - before };
    REQUIRE(result.status == luil::platform_status::not_found);
    // 한 번은 찾아 보고, 시한을 넘겨 기다리지도 않는다.
    REQUIRE(elapsed < std::chrono::seconds { 2 });
}

TEST_CASE("Arguments, the working directory and the flag reach the marked window unchanged", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("deliver") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    receiver_window window { name, true, receiver_window::reply::accept };

    const std::vector<std::u8string> arguments { u8"a.txt", u8"b c", u8"\"q\" \\", u8"한글 \U0001F600", u8"" };
    const luil::platform_result result { second.send_to_primary(arguments, send_options(2000)) };
    REQUIRE(result.ok());
    REQUIRE(result.error.empty());

    // 표지를 켠 보내기는 시험 창을 앞으로 세우지 않는다 (시험 창이 활성화하지 않는다). 보내는 쪽이 권한을 넘길 뿐이다.
    REQUIRE(second.send_to_primary({}, send_options(2000, true)).ok());

    const std::vector<receiver_window::delivery> received { window.deliveries() };
    REQUIRE(received.size() == 2);
    REQUIRE(received[0].arguments.arguments == arguments);
    REQUIRE(received[0].arguments.working_directory == std::filesystem::current_path().u8string());
    REQUIRE(received[0].activate == false);
    REQUIRE(received[1].arguments.arguments.empty());
    REQUIRE(received[1].activate);
    REQUIRE(window.refused() == 0);
}

TEST_CASE("A window that answers false makes the send fail", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("reject") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    receiver_window window { name, true, receiver_window::reply::reject };

    const luil::platform_result result { second.send_to_primary(std::vector<std::u8string> { u8"a.txt" }, send_options(2000)) };
    REQUIRE(result.status == luil::platform_status::failed);
    REQUIRE(result.error.empty() == false);
    // 받기는 했으나 앱이 받지 않았다고 답했다.
    REQUIRE(window.deliveries().size() == 1);
}

TEST_CASE("A window that does not answer fails at the time limit", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("stall") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    receiver_window window { name, true, receiver_window::reply::stall };

    const auto before { std::chrono::steady_clock::now() };
    const luil::platform_result result { second.send_to_primary(std::vector<std::u8string> { u8"a.txt" }, send_options(200)) };
    const auto elapsed { std::chrono::steady_clock::now() - before };
    REQUIRE(result.status == luil::platform_status::failed);
    REQUIRE(result.error.empty() == false);
    // 응답을 기다린 시간은 시한 안이다. 시한이 지났다고 첫째가 받지 않았다는 뜻은 아니다.
    REQUIRE(elapsed >= std::chrono::milliseconds { 150 });
    REQUIRE(elapsed < std::chrono::milliseconds { 1400 });
}

TEST_CASE("A window without the mark gets nothing", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("unmarked") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    receiver_window window { name, false, receiver_window::reply::accept };

    REQUIRE(second.send_to_primary(std::vector<std::u8string> { u8"a.txt" }, send_options(100)).status == luil::platform_status::not_found);
    REQUIRE(window.deliveries().empty());
}

TEST_CASE("A mark of another name is not the first instance of this one", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("mine") };
    const std::u8string other { unique_instance_name("other") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    receiver_window window { other, true, receiver_window::reply::accept };

    REQUIRE(second.send_to_primary({}, send_options(100)).status == luil::platform_status::not_found);
    REQUIRE(window.deliveries().empty());
}

TEST_CASE("Input the format cannot carry fails before anything is sent", "[platform][instance]")
{
    const std::u8string name { unique_instance_name("invalid") };
    const luil::single_instance first { name };
    const luil::single_instance second { name };
    receiver_window window { name, true, receiver_window::reply::accept };

    const std::u8string with_nul { u8'a', u8'\0', u8'b' };
    const std::u8string broken { static_cast<char8_t>(0xC3), u8'(' };
    const std::vector<std::u8string> too_many(luil::win32::instance_message_max_arguments + 1, u8"a");
    const std::vector<std::u8string> too_large { std::u8string(luil::win32::instance_message_max_bytes, u8'a') };
    for (const std::vector<std::u8string>& arguments : { std::vector<std::u8string> { with_nul }, std::vector<std::u8string> { broken }, too_many, too_large })
        REQUIRE(second.send_to_primary(arguments, send_options(2000)).status == luil::platform_status::failed);
    REQUIRE(window.deliveries().empty());
    REQUIRE(window.refused() == 0);
}

TEST_CASE("An instance name longer than the window mark allows fails even for the first instance", "[platform][instance]")
{
    constexpr std::size_t limit { luil::win32::instance_name_max_code_units };
    const std::u8string prefix { unique_instance_name("long") };
    // 이름 끝을 채워 241 코드 단위 정확히와 하나 넘는 이름을 만든다.
    REQUIRE(prefix.size() < limit);
    const std::u8string longest { prefix + std::u8string(limit - prefix.size(), u8'n') };
    const std::u8string too_long { longest + u8"n" };

    // 뮤텍스에는 이 한계가 없어 생성자는 그대로 선다. 보내기가 첫째 여부를 보기 전에 이름을 거른다.
    const luil::single_instance exact { longest };
    REQUIRE(exact.send_to_primary({}, send_options(50)).status == luil::platform_status::not_found);
    const luil::single_instance over { too_long };
    REQUIRE(over.status().ok());
    REQUIRE(over.send_to_primary({}, send_options(50)).status == luil::platform_status::failed);
}

TEST_CASE("An instance that could not be created cannot send", "[platform][instance]")
{
    const luil::single_instance unnamed { u8"" };
    REQUIRE(unnamed.status().ok() == false);
    REQUIRE(unnamed.send_to_primary({}, send_options(50)).status == luil::platform_status::failed);
}
