#include "luil/app/platform_services.h"

#include "win32/command_line.h"
#include "win32/utf8.h"
#include "win32/win32_error.h"

#include <windows.h>

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <utility>
#include <vector>

// 플랫폼 도구의 Windows 쪽이다 (platform-services-design.md).
namespace luil {
    namespace {
        [[nodiscard]] platform_result success() noexcept
        {
            return { platform_status::ok, {}, std::nullopt };
        }

        [[nodiscard]] platform_result failure(std::u8string message)
        {
            return { platform_status::failed, std::move(message), std::nullopt };
        }

        [[nodiscard]] platform_result last_error(const std::u8string_view what)
        {
            return failure(win32::make_hresult_error(what, HRESULT_FROM_WIN32(GetLastError())));
        }

        [[nodiscard]] std::optional<std::wstring> wide(const std::u8string_view text)
        {
            return win32::utf8_to_utf16(text).value;
        }

        // 핸들 하나를 쥐고 없어질 때 닫는다.
        class owned_handle
        {
        public:
            owned_handle() noexcept = default;
            explicit owned_handle(const HANDLE handle) noexcept
                : handle_ { handle }
            {}
            owned_handle(const owned_handle&) = delete;
            owned_handle& operator=(const owned_handle&) = delete;
            owned_handle(owned_handle&& other) noexcept
                : handle_ { std::exchange(other.handle_, nullptr) }
            {}
            owned_handle& operator=(owned_handle&& other) noexcept
            {
                if (this != &other)
                {
                    close();
                    handle_ = std::exchange(other.handle_, nullptr);
                }
                return *this;
            }
            ~owned_handle()
            {
                close();
            }

            [[nodiscard]] HANDLE get() const noexcept
            {
                return handle_;
            }

            [[nodiscard]] bool valid() const noexcept
            {
                return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
            }

        private:
            void close() noexcept
            {
                if (valid())
                    static_cast<void>(CloseHandle(handle_));
                handle_ = nullptr;
            }

            HANDLE handle_ { nullptr };
        };
    } // namespace

    namespace win32 {
        std::wstring quote_argument(const std::wstring_view argument)
        {
            if (argument.empty() == false && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
                return std::wstring { argument };
            std::wstring quoted { L"\"" };
            std::size_t backslashes { 0 };
            for (const wchar_t character : argument)
            {
                if (character == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                // 따옴표 앞의 백슬래시는 두 배로, 따옴표는 백슬래시 하나를 더 붙인다. 그 밖의 백슬래시는 그대로다.
                if (character == L'"')
                    quoted.append(backslashes * 2 + 1, L'\\');
                else
                    quoted.append(backslashes, L'\\');
                backslashes = 0;
                quoted.push_back(character);
            }
            // 닫는 따옴표 앞이라 끝의 백슬래시도 두 배다.
            quoted.append(backslashes * 2, L'\\');
            quoted.push_back(L'"');
            return quoted;
        }

        std::wstring make_command_line(const std::wstring_view program, const std::span<const std::wstring> arguments)
        {
            std::wstring line { quote_argument(program) };
            for (const std::wstring& argument : arguments)
            {
                line.push_back(L' ');
                line += quote_argument(argument);
            }
            return line;
        }
    } // namespace win32

    platform_result replace_file_atomically(const std::u8string_view path, const std::span<const std::uint8_t> contents)
    {
        const std::optional<std::wstring> target { wide(path) };
        if (target.has_value() == false || target->empty())
            return failure(u8"The file path is empty or not valid UTF-8.");
        const std::filesystem::path target_path { *target };
        std::wstring temporary { *target };
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";

        {
            // write-through라 쓰기가 디스크 캐시를 지나 내려간다. 다 쓴 뒤 한 번 더 내려 보낸다.
            const owned_handle file { CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr) };
            if (file.valid() == false)
            {
                if (GetLastError() == ERROR_PATH_NOT_FOUND)
                    return failure(u8"The folder of the file does not exist.");
                return last_error(u8"Failed to create the temporary file");
            }
            std::size_t written_total { 0 };
            while (written_total < contents.size())
            {
                const DWORD chunk { static_cast<DWORD>(std::min<std::size_t>(contents.size() - written_total, 1u << 30u)) };
                DWORD written { 0 };
                if (WriteFile(file.get(), contents.data() + written_total, chunk, &written, nullptr) == FALSE || written == 0)
                {
                    platform_result result { last_error(u8"Failed to write the temporary file") };
                    static_cast<void>(DeleteFileW(temporary.c_str()));
                    return result;
                }
                written_total += written;
            }
            if (FlushFileBuffers(file.get()) == FALSE)
            {
                platform_result result { last_error(u8"Failed to flush the temporary file") };
                static_cast<void>(DeleteFileW(temporary.c_str()));
                return result;
            }
        }
        if (MoveFileExW(temporary.c_str(), target->c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
        {
            platform_result result { last_error(u8"Failed to replace the file") };
            static_cast<void>(DeleteFileW(temporary.c_str()));
            return result;
        }
        return success();
    }

    struct single_instance::state
    {
        owned_handle mutex {};
        bool primary { false };
        platform_result status {};
    };

    single_instance::single_instance(const std::u8string_view name)
        : state_ { std::make_unique<state>() }
    {
        std::u8string full { u8"Local\\luil-instance:" };
        full += name;
        const std::optional<std::wstring> mutex_name { wide(full) };
        if (name.empty() || mutex_name.has_value() == false)
        {
            state_->status = failure(u8"The instance name is empty or not valid UTF-8.");
            return;
        }
        state_->mutex = owned_handle { CreateMutexW(nullptr, FALSE, mutex_name->c_str()) };
        if (state_->mutex.valid() == false)
        {
            state_->status = last_error(u8"Failed to create the instance mutex");
            return;
        }
        // 이미 있던 뮤텍스면 다른 인스턴스가 먼저 섰다. 핸들은 그래도 쥔다 — 놓으면 첫째가 끝난 뒤 다음 둘째가 첫째가 된다.
        state_->primary = GetLastError() != ERROR_ALREADY_EXISTS;
        state_->status = success();
    }

    single_instance::single_instance(single_instance&&) noexcept = default;
    single_instance& single_instance::operator=(single_instance&&) noexcept = default;
    single_instance::~single_instance() = default;

    bool single_instance::primary() const noexcept
    {
        return state_ != nullptr && state_->primary;
    }

    const platform_result& single_instance::status() const noexcept
    {
        // 옮겨 간 뒤의 빈 객체도 답한다.
        static const platform_result moved { platform_status::failed, u8"The instance guard was moved from.", std::nullopt };
        return state_ != nullptr ? state_->status : moved;
    }

    struct file_lock::state
    {
        owned_handle file {};
        bool locked { false };
        platform_result status {};
    };

    file_lock::file_lock(const std::u8string_view path)
        : state_ { std::make_unique<state>() }
    {
        const std::optional<std::wstring> target { wide(path) };
        if (target.has_value() == false || target->empty())
        {
            state_->status = failure(u8"The lock path is empty or not valid UTF-8.");
            return;
        }
        state_->file
            = owned_handle { CreateFileW(target->c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
        if (state_->file.valid() == false)
        {
            state_->status = last_error(u8"Failed to open the lock file");
            return;
        }
        OVERLAPPED whole {};
        if (LockFileEx(state_->file.get(), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &whole) == FALSE)
        {
            // 다른 쪽이 쥐었다. 잠그지 못한 것도 답이다 — 오류가 아니라 `locked()`가 거짓이다.
            if (GetLastError() == ERROR_LOCK_VIOLATION)
                state_->status = success();
            else
                state_->status = last_error(u8"Failed to lock the file");
            return;
        }
        state_->locked = true;
        state_->status = success();
    }

    file_lock::file_lock(file_lock&&) noexcept = default;
    file_lock& file_lock::operator=(file_lock&&) noexcept = default;
    file_lock::~file_lock() = default;

    bool file_lock::locked() const noexcept
    {
        return state_ != nullptr && state_->locked;
    }

    const platform_result& file_lock::status() const noexcept
    {
        static const platform_result moved { platform_status::failed, u8"The file lock was moved from.", std::nullopt };
        return state_ != nullptr ? state_->status : moved;
    }

    platform_result reveal_in_file_manager(const std::u8string_view path)
    {
        const std::optional<std::wstring> target { wide(path) };
        if (target.has_value() == false || target->empty())
            return failure(u8"The path is empty or not valid UTF-8.");
        std::error_code error {};
        const std::filesystem::path absolute { std::filesystem::absolute(std::filesystem::path { *target }, error) };
        if (error || std::filesystem::exists(absolute, error) == false)
            return { platform_status::not_found, {}, std::nullopt };
        // 부르는 thread에서 COM을 연다. 이미 열려 있으면(다른 모델이어도) 그대로 쓴다.
        const HRESULT initialized { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) };
        PIDLIST_ABSOLUTE item { nullptr };
        HRESULT result { SHParseDisplayName(absolute.c_str(), nullptr, &item, 0, nullptr) };
        if (SUCCEEDED(result))
        {
            result = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
            CoTaskMemFree(item);
        }
        if (SUCCEEDED(initialized))
            CoUninitialize();
        if (FAILED(result))
            return failure(win32::make_hresult_error(u8"Failed to show the file in the file manager", result));
        return success();
    }

    platform_result launch_program(const std::u8string_view program, const std::span<const std::u8string> arguments, const launch_options& options)
    {
        const std::optional<std::wstring> executable { wide(program) };
        if (executable.has_value() == false || executable->empty())
            return failure(u8"The program path is empty or not valid UTF-8.");
        std::vector<std::wstring> wide_arguments {};
        for (const std::u8string& argument : arguments)
        {
            std::optional<std::wstring> converted { wide(argument) };
            if (converted.has_value() == false)
                return failure(u8"An argument is not valid UTF-8.");
            wide_arguments.push_back(std::move(*converted));
        }
        std::optional<std::wstring> directory {};
        if (options.working_directory.empty() == false)
        {
            directory = wide(options.working_directory);
            if (directory.has_value() == false)
                return failure(u8"The working directory is not valid UTF-8.");
        }
        // CreateProcessW는 명령 줄 버퍼를 고쳐 쓸 수 있어 쓰기 가능한 사본을 준다.
        std::wstring command_line { win32::make_command_line(*executable, wide_arguments) };
        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        if (options.show_window == false)
        {
            startup.dwFlags = STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_HIDE;
        }
        PROCESS_INFORMATION process {};
        const DWORD flags { options.show_window ? 0u : static_cast<DWORD>(CREATE_NO_WINDOW) };
        // 프로그램 경로를 따로 주지 않고 명령 줄의 첫 낱말로 찾는다 — PATH에서 찾는 것이 그쪽이다.
        if (CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, flags | CREATE_UNICODE_ENVIRONMENT, nullptr, directory.has_value() ? directory->c_str() : nullptr, &startup, &process)
            == FALSE)
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)
                return { platform_status::not_found, {}, std::nullopt };
            return last_error(u8"Failed to start the program");
        }
        const owned_handle thread { process.hThread };
        const owned_handle child { process.hProcess };
        platform_result result { success() };
        if (options.wait)
        {
            DWORD code { 0 };
            if (WaitForSingleObject(child.get(), INFINITE) != WAIT_OBJECT_0 || GetExitCodeProcess(child.get(), &code) == FALSE)
                return last_error(u8"Failed to wait for the program");
            result.exit_code = static_cast<int>(code);
        }
        return result;
    }

    platform_result open_url(const std::u8string_view url, const open_url_options& options)
    {
        if (url_scheme_allowed(url, options.schemes) == false)
            return failure(u8"The URL has no scheme or its scheme is not allowed.");
        const std::optional<std::wstring> target { wide(url) };
        if (target.has_value() == false)
            return failure(u8"The URL is not valid UTF-8.");
        // scheme 처리기가 COM으로 서는 shell 확장일 수 있어, ShellExecuteEx 문서대로 부르는 thread에서 apartment를 연다.
        // 이미 열려 있으면(다른 모델이어도) 그대로 쓴다.
        const HRESULT initialized { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) };
        SHELLEXECUTEINFOW execute {};
        execute.cbSize = sizeof(execute);
        // NOASYNC: 메시지 루프가 없거나 곧 끝날 thread에서도 넘기기가 끝난 뒤 돌아온다.
        // FLAG_NO_UI: 처리기가 없을 때 shell이 대화 상자를 띄우지 않고 오류로 답하게 한다.
        execute.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        execute.lpVerb = L"open";
        execute.lpFile = target->c_str();
        execute.nShow = SW_SHOWNORMAL;
        const bool opened { ShellExecuteExW(&execute) != FALSE };
        const DWORD error { opened ? ERROR_SUCCESS : GetLastError() };
        if (SUCCEEDED(initialized))
            CoUninitialize();
        if (opened)
            return success();
        if (error == ERROR_NO_ASSOCIATION)
            return { platform_status::not_found, u8"No app is registered for the URL scheme.", std::nullopt };
        return failure(win32::make_hresult_error(u8"Failed to open the URL", HRESULT_FROM_WIN32(error)));
    }

    platform_result set_background_priority(const bool background)
    {
        if (SetThreadPriority(GetCurrentThread(), background ? THREAD_MODE_BACKGROUND_BEGIN : THREAD_MODE_BACKGROUND_END) != FALSE)
            return success();
        // 이미 그 모드면 할 일이 없다 — 되돌리기를 두 번 불러도 실패가 아니다.
        if (GetLastError() == ERROR_THREAD_MODE_ALREADY_BACKGROUND || GetLastError() == ERROR_THREAD_MODE_NOT_BACKGROUND)
            return success();
        return last_error(u8"Failed to change the thread priority");
    }

    struct blocking_io_canceller::state
    {
        owned_handle thread {};
        platform_result status {};
    };

    blocking_io_canceller::blocking_io_canceller()
        : state_ { std::make_unique<state>() }
    {
        // `GetCurrentThread`는 "부르는 thread"를 뜻하는 가짜 핸들이라, 다른 thread에서 쓸 진짜 핸들로 옮긴다.
        HANDLE thread { nullptr };
        if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &thread, THREAD_TERMINATE, FALSE, 0) == FALSE)
        {
            state_->status = last_error(u8"Failed to hold the thread for cancellation");
            return;
        }
        state_->thread = owned_handle { thread };
        state_->status = success();
    }

    blocking_io_canceller::blocking_io_canceller(blocking_io_canceller&&) noexcept = default;
    blocking_io_canceller& blocking_io_canceller::operator=(blocking_io_canceller&&) noexcept = default;
    blocking_io_canceller::~blocking_io_canceller() = default;

    platform_result blocking_io_canceller::cancel() const
    {
        if (state_ == nullptr || state_->thread.valid() == false)
            return state_ != nullptr ? state_->status : failure(u8"The canceller holds no thread.");
        if (CancelSynchronousIo(state_->thread.get()) != FALSE)
            return success();
        if (GetLastError() == ERROR_NOT_FOUND)
            return { platform_status::not_found, {}, std::nullopt };
        return last_error(u8"Failed to cancel the blocked I/O");
    }
} // namespace luil
