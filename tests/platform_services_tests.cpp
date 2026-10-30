#include "luil/app/platform_services.h"

#include "win32/command_line.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
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
