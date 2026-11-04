#include "luil/app/platform_services.h"

#include "win32/command_line.h"
#include "win32/instance_message.h"
#include "win32/owned_handle.h"
#include "win32/utf8.h"
#include "win32/win32_error.h"
#include "win32/win32_locale.h"

#include <windows.h>

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
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
            return failure(win32::make_last_error(what));
        }

        [[nodiscard]] std::optional<std::wstring> wide(const std::u8string_view text)
        {
            return win32::utf8_to_utf16(text).value;
        }

        using win32::owned_handle;

        // 창 속성 이름을 가진 최상위 창을 찾는 상태다 (`find_marked_window`).
        struct mark_search
        {
            const wchar_t* property { nullptr };
            HWND found { nullptr };
        };

        BOOL CALLBACK pick_marked_window(const HWND window, const LPARAM parameter)
        {
            auto* const search { reinterpret_cast<mark_search*>(parameter) };
            if (GetPropW(window, search->property) == nullptr)
                return TRUE;
            search->found = window;
            return FALSE;
        }

        // 표식이 선 첫 최상위 창이다. 숨은 창도 훑는다. 없으면 nullptr다.
        [[nodiscard]] HWND find_marked_window(const std::wstring& property) noexcept
        {
            mark_search search { property.c_str(), nullptr };
            EnumWindows(&pick_marked_window, reinterpret_cast<LPARAM>(&search));
            return search.found;
        }

        // 이 프로세스의 작업 폴더다. 읽지 못하면 오류 글을 `error`에 둔다.
        [[nodiscard]] std::optional<std::u8string> current_directory(std::u8string& error)
        {
            // 확장 길이 경로의 상한(32,767자)과 끝 NUL을 한 번에 담는다. 크기를 묻고 읽는 두 호출 사이에 폴더가 바뀌는 틈이 없다.
            std::wstring buffer(32768, L'\0');
            const DWORD length { GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data()) };
            if (length == 0 || length >= buffer.size())
            {
                error = win32::make_last_error(u8"Failed to read the working directory");
                return std::nullopt;
            }
            buffer.resize(length);
            std::optional<std::u8string> directory { win32::utf16_to_utf8(buffer).value };
            if (directory.has_value() == false)
                error = u8"The working directory is not valid UTF-16.";
            return directory;
        }
    }

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
    }

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
        // 받은 이름 그대로다. 첫째의 주 창이 표식을 거는 이름이고 둘째가 첫째를 찾는 이름이다.
        std::u8string name {};
        owned_handle mutex {};
        bool primary { false };
        platform_result status {};
    };

    single_instance::single_instance(const std::u8string_view name)
        : state_ { std::make_unique<state>() }
    {
        state_->name = name;
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

    std::u8string_view single_instance::name() const noexcept
    {
        return state_ != nullptr ? std::u8string_view { state_->name } : std::u8string_view {};
    }

    platform_result single_instance::send_to_primary(const std::span<const std::u8string> arguments, const instance_send_options& options) const
    {
        if (state_ == nullptr)
            return failure(u8"The instance guard was moved from.");
        // 서지 못한 객체(빈 이름이나 뮤텍스 오류)는 보낼 곳도 모른다.
        if (state_->status.ok() == false)
            return state_->status;
        const std::optional<std::wstring> property { win32::instance_property_name(state_->name) };
        if (property.has_value() == false)
            return failure(u8"The instance name holds a NUL character or is longer than 241 UTF-16 code units.");
        std::u8string directory_error {};
        const std::optional<std::u8string> directory { current_directory(directory_error) };
        if (directory.has_value() == false)
            return failure(std::move(directory_error));
        const win32::instance_encode_result message { win32::encode_instance_message(arguments, *directory, options.activate) };
        if (message.ok() == false)
            return failure(std::u8string { win32::instance_message_error_text(message.error) });
        if (state_->primary)
            return { platform_status::not_found, {}, std::nullopt };

        using clock = std::chrono::steady_clock;
        const clock::time_point deadline { clock::now() + std::max(options.timeout, std::chrono::milliseconds::zero()) };
        // 다음 시도까지 쉰다. 시한이 지났으면 거짓이다. 첫 창이 아직 서지 않았을 수 있어 50 ms마다 다시 찾는다.
        const auto pause = [&deadline]() {
            const auto remaining { std::chrono::ceil<std::chrono::milliseconds>(deadline - clock::now()) };
            if (remaining <= std::chrono::milliseconds::zero())
                return false;
            Sleep(static_cast<DWORD>(std::min(remaining, std::chrono::milliseconds { 50 }).count()));
            return true;
        };

        COPYDATASTRUCT data {};
        data.dwData = win32::instance_message_tag;
        data.cbData = static_cast<DWORD>(message.units.size() * sizeof(wchar_t));
        data.lpData = const_cast<wchar_t*>(message.units.data());
        for (;;)
        {
            const HWND window { find_marked_window(*property) };
            if (window == nullptr)
            {
                if (pause() == false)
                    return { platform_status::not_found, {}, std::nullopt };
                continue;
            }
            // 첫째는 앞 창이 아니라 스스로 앞으로 서지 못한다. 사용자의 동작으로 뜬 이 프로세스가 그 권한을 미리 넘긴다.
            // 둘째가 사용자 동작으로 뜬 것이 아니면 OS가 거절하지만 그래도 보내고, 결과에 싣지 않는다.
            if (options.activate)
            {
                DWORD process { 0 };
                GetWindowThreadProcessId(window, &process);
                if (process != 0)
                    static_cast<void>(AllowSetForegroundWindow(process));
            }
            const long long remaining { std::chrono::ceil<std::chrono::milliseconds>(deadline - clock::now()).count() };
            // 0이나 음수는 "기다리지 않음"이 되므로 적어도 1 ms를 준다. 위쪽은 INFINITE(0xFFFFFFFF)를 피한다.
            const UINT wait { static_cast<UINT>(std::clamp<long long>(remaining, 1, std::numeric_limits<LONG>::max())) };
            DWORD_PTR answer { 0 };
            SetLastError(ERROR_SUCCESS);
            // 첫째가 응답하지 않으면 시한까지 기다린다. 그동안 부른 thread는 다른 메시지를 풀지 않는다.
            if (SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG | SMTO_BLOCK, wait, &answer) != 0)
                return answer != 0 ? success() : failure(u8"The first instance did not accept the arguments.");
            const DWORD error { GetLastError() };
            // 그 사이 창이 사라졌다. 새 첫 창이 설 수 있어 다시 찾는다.
            if (error == ERROR_INVALID_WINDOW_HANDLE)
            {
                if (pause() == false)
                    return { platform_status::not_found, {}, std::nullopt };
                continue;
            }
            if (error == ERROR_ACCESS_DENIED)
                return failure(u8"Access is denied. The first instance runs with a higher privilege than this process.");
            if (error == ERROR_TIMEOUT || error == ERROR_SUCCESS)
                return failure(u8"The first instance did not answer in time. It may still receive the arguments later.");
            return failure(win32::make_hresult_error(u8"Failed to send the arguments to the first instance", HRESULT_FROM_WIN32(error)));
        }
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

    std::optional<std::chrono::weekday> user_first_weekday()
    {
        // `LOCALE_RETURN_NUMBER`면 버퍼에 수가 들고 크기는 wchar_t 수로 센다. 사용자가 지역 설정에서 바꾼 값을 읽는다.
        DWORD value { 0 };
        if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK | LOCALE_RETURN_NUMBER, reinterpret_cast<LPWSTR>(&value), sizeof(value) / sizeof(wchar_t)) == 0)
            return std::nullopt;
        return win32::weekday_from_locale_first_day(value);
    }

    std::vector<std::u8string> user_ui_languages()
    {
        // 글꼴이 읽는 `GetUserDefaultLocaleName`은 지역 서식의 이름이라 표시 언어와 다를 수 있어 쓰지 않는다.
        ULONG count { 0 };
        ULONG size { 0 };
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) == FALSE || size == 0u)
            return {};
        std::wstring list(size, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, list.data(), &size) == FALSE)
            return {};
        return win32::language_tags_from_multi_string(list);
    }
}
