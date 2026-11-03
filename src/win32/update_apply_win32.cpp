#include "win32/update_apply_win32.h"

#include "luil/app/platform_services.h"
#include "net/update_apply.h"
#include "net/update_crypto.h"
#include "win32/command_line.h"
#include "win32/executable_path.h"
#include "win32/owned_handle.h"
#include "win32/utf8.h"
#include "win32/win32_error.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// 적용의 Win32 쪽이다 (docs/auto-update-apply-design.md의 규칙).
//
// **옛 exe 자리는 늘 옛 것 전체이거나 새 것 전체다.** 바꾸는 것이 같은 폴더 안 이름 바꾸기(`MoveFileExW`) 하나라서다.
// `ReplaceFileW`와 "옛 것을 먼저 비키기"는 쓰지 않는다. 앞의 것은 실패 갈래마다 남는 이름이 달라 되돌리기가 길고, 뒤의 것은
// 두 이름 바꾸기 사이에 꺼지면 exe가 없다.
namespace luil {
    namespace {
        using win32::owned_handle;

        [[nodiscard]] std::filesystem::path path_from(const std::u8string_view text)
        {
            return std::filesystem::path { std::u8string { text } };
        }

        [[nodiscard]] std::filesystem::path file_in(const std::filesystem::path& directory, const std::u8string_view name)
        {
            return directory / path_from(name);
        }

        // 옛 exe 옆에 이름을 붙인 경로다 (`<이름>.old`, `<이름>.<pid>.tmp`).
        [[nodiscard]] std::filesystem::path beside(const std::filesystem::path& executable, const std::wstring& suffix)
        {
            std::wstring name { executable.wstring() };
            name += suffix;
            return std::filesystem::path { name };
        }

        [[nodiscard]] std::chrono::milliseconds clamp_wait(const std::chrono::milliseconds value) noexcept
        {
            return std::clamp(value, std::chrono::milliseconds::zero(), std::chrono::milliseconds { static_cast<long long>(update_helper_maximum_timeout_ms) });
        }

        // 이 빌드의 Machine이다. 새 판은 같은 자리에서 같은 꼴로 떠야 한다.
#if defined(_M_ARM64)
        constexpr WORD running_machine { IMAGE_FILE_MACHINE_ARM64 };
#elif defined(_M_X64)
        constexpr WORD running_machine { IMAGE_FILE_MACHINE_AMD64 };
#else
        constexpr WORD running_machine { IMAGE_FILE_MACHINE_I386 };
#endif

        // 받은 파일의 앞쪽이다. PE 머리 자리(`e_lfanew`)는 대개 첫 몇백 바이트 안이라 64 KiB면 넉넉하다.
        [[nodiscard]] std::optional<std::vector<std::byte>> read_executable_header(const std::filesystem::path& file, std::u8string& error)
        {
            std::ifstream stream { file, std::ios::binary };
            if (stream.is_open() == false)
            {
                error = u8"Failed to open the downloaded executable to read its header.";
                return std::nullopt;
            }
            // 중괄호는 초기값 목록이 되어 크기를 줄 수 없다.
            std::vector<std::byte> bytes(64 * 1024);
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            bytes.resize(static_cast<std::size_t>(stream.gcount()));
            return bytes;
        }

        struct started_process
        {
            owned_handle process {};
            DWORD id { 0 };
        };

        // 프로세스 하나를 띄운다. 경로는 찾지 않고 준 그대로 쓴다 (PATH나 작업 폴더의 같은 이름을 띄우지 않는다).
        //  - `inherit`가 있으면 손잡이 목록 속성으로 그 하나만 물린다. 다른 상속 가능 손잡이가 새지 않는다.
        //  - 작업 폴더와 환경은 이 프로세스의 것이다.
        [[nodiscard]] std::optional<started_process> start_process(const std::filesystem::path& program, const std::span<const std::u8string> arguments, HANDLE inherit, std::u8string& error)
        {
            std::vector<std::wstring> wide_arguments {};
            wide_arguments.reserve(arguments.size());
            for (const std::u8string& argument : arguments)
            {
                // NUL은 명령 줄을 거기서 끊는다.
                std::optional<std::wstring> converted { argument.find(u8'\0') == std::u8string::npos ? win32::utf8_to_utf16(argument).value : std::nullopt };
                if (converted.has_value() == false)
                {
                    error = u8"An argument is not valid UTF-8 or holds a NUL character.";
                    return std::nullopt;
                }
                wide_arguments.push_back(std::move(*converted));
            }
            const std::wstring executable { program.wstring() };
            // CreateProcessW는 명령 줄 버퍼를 고쳐 쓸 수 있어 쓰기 가능한 사본을 준다.
            std::wstring command_line { win32::make_command_line(executable, wide_arguments) };

            STARTUPINFOEXW startup {};
            startup.StartupInfo.cb = sizeof(startup);
            DWORD flags { CREATE_UNICODE_ENVIRONMENT };
            BOOL inherit_handles { FALSE };
            std::vector<std::byte> attribute_storage {};
            HANDLE inherited { inherit };
            if (inherit != nullptr)
            {
                SIZE_T size { 0 };
                // 크기를 묻는 첫 호출은 버퍼가 모자라다고 거짓을 준다.
                static_cast<void>(InitializeProcThreadAttributeList(nullptr, 1, 0, &size));
                attribute_storage.resize(size);
                auto* const list { reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data()) };
                if (size == 0 || InitializeProcThreadAttributeList(list, 1, 0, &size) == FALSE)
                {
                    error = win32::make_last_error(u8"Failed to prepare the inherited handle list");
                    return std::nullopt;
                }
                if (UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inherited, sizeof(inherited), nullptr, nullptr) == FALSE)
                {
                    error = win32::make_last_error(u8"Failed to set the inherited handle list");
                    DeleteProcThreadAttributeList(list);
                    return std::nullopt;
                }
                startup.lpAttributeList = list;
                flags |= EXTENDED_STARTUPINFO_PRESENT;
                inherit_handles = TRUE;
            }
            PROCESS_INFORMATION information {};
            // PE가 아닌 파일을 띄우면 OS가 hard error 대화 상자("지원되지 않는 16비트 응용 프로그램")를 띄우고 이 호출이 그 상자가
            // 닫힐 때까지 돌아오지 않는다. updater thread가 멈춰 launch_failed를 보고하지 못하고 앱 종료의 join도 끝나지 않는다.
            // 받은 파일은 `launch_update_helper`가 머리를 먼저 거르지만, 머리 검사가 보지 않는 꼴이 남을 수 있어 여기서도 막는다.
            // 이 thread의 오류 방식만 바꿔 상자 없이 실패로 돌아오게 한다. 프로세스 전체 방식은 다른 thread와 자식이 물려받아 건드리지 않는다.
            DWORD previous_mode { 0 };
            const bool mode_set { SetThreadErrorMode(SEM_FAILCRITICALERRORS, &previous_mode) != FALSE };
            const BOOL created {
                CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, inherit_handles, flags, nullptr, nullptr, &startup.StartupInfo, &information),
            };
            const DWORD created_error { GetLastError() };
            if (mode_set)
                static_cast<void>(SetThreadErrorMode(previous_mode, nullptr));
            if (startup.lpAttributeList != nullptr)
                DeleteProcThreadAttributeList(startup.lpAttributeList);
            if (created == FALSE)
            {
                SetLastError(created_error);
                error = win32::make_last_error(u8"Failed to start the process");
                return std::nullopt;
            }
            const owned_handle thread { information.hThread };
            return started_process { owned_handle { information.hProcess }, information.dwProcessId };
        }

        [[nodiscard]] std::vector<std::uint8_t> read_small_file(const std::filesystem::path& path)
        {
            std::ifstream file { path, std::ios::binary };
            if (file.is_open() == false)
                return {};
            std::vector<std::uint8_t> bytes { std::istreambuf_iterator<char> { file }, std::istreambuf_iterator<char> {} };
            // 상태·결과·확인 파일은 작다. 큰 파일은 꼴이 틀린 것으로 본다.
            if (bytes.size() > 64u * 1024u)
                return {};
            return bytes;
        }

        // 파일 전체의 SHA-256이 `expected`와 같은가.
        [[nodiscard]] bool file_matches(const std::filesystem::path& path, const update_digest& expected, std::u8string& error)
        {
            net::update_sha256 hasher {};
            if (hasher.valid() == false)
            {
                error = hasher.error();
                return false;
            }
            std::ifstream file { path, std::ios::binary };
            if (file.is_open() == false)
            {
                error = u8"Failed to open the copied image for hashing.";
                return false;
            }
            std::vector<char> buffer(1024u * 1024u);
            while (file)
            {
                file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize count { file.gcount() };
                if (count > 0 && hasher.update(std::span<const std::uint8_t> { reinterpret_cast<const std::uint8_t*>(buffer.data()), static_cast<std::size_t>(count) }) == false)
                {
                    error = hasher.error();
                    return false;
                }
            }
            if (file.bad())
            {
                error = u8"Failed to read the copied image for hashing.";
                return false;
            }
            const std::optional<update_digest> digest { hasher.finish() };
            if (digest.has_value() == false || *digest != expected)
            {
                error = u8"The helper image does not match the sha256 it was given.";
                return false;
            }
            return true;
        }

        // 공유 위반과 접근 거부는 잠깐 쥔 쪽(백신 검사, 막 끝난 프로세스)일 수 있어 시한까지 다시 한다.
        [[nodiscard]] bool move_replacing(const std::filesystem::path& from, const std::filesystem::path& to, std::u8string& error)
        {
            using clock = std::chrono::steady_clock;
            const clock::time_point deadline { clock::now() + win32::update_replace_retry_limit };
            while (true)
            {
                if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE)
                    return true;
                const DWORD code { GetLastError() };
                const bool transient { code == ERROR_SHARING_VIOLATION || code == ERROR_ACCESS_DENIED || code == ERROR_LOCK_VIOLATION };
                if (transient == false || clock::now() >= deadline)
                {
                    SetLastError(code);
                    error = win32::make_last_error(u8"Failed to move the executable into place");
                    return false;
                }
                Sleep(static_cast<DWORD>(win32::update_retry_interval.count()));
            }
        }

        // 받기 폴더의 `update.lock`을 시한까지 다시 잡아 본다. 못 잡으면 비었다.
        [[nodiscard]] std::optional<file_lock> take_lock(const std::filesystem::path& directory)
        {
            using clock = std::chrono::steady_clock;
            const clock::time_point deadline { clock::now() + win32::update_lock_retry_limit };
            while (true)
            {
                file_lock lock { file_in(directory, update_lock_file).u8string() };
                if (lock.locked())
                    return lock;
                if (clock::now() >= deadline)
                    return std::nullopt;
                Sleep(static_cast<DWORD>(win32::update_retry_interval.count()));
            }
        }

        [[nodiscard]] std::u8string exit_code_text(const DWORD code)
        {
            const std::string digits { std::to_string(code) };
            return std::u8string { digits.begin(), digits.end() };
        }

        // 도우미 한 번이다. 차례는 docs/auto-update-apply-design.md의 도우미 1~8이다.
        class helper_run
        {
        public:
            explicit helper_run(update_helper_arguments arguments)
                : arguments_ { std::move(arguments) }
                , old_executable_ { path_from(arguments_.old_executable) }
                , directory_ { path_from(arguments_.download_directory) }
                , temporary_ { beside(old_executable_, L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp") }
                , backup_ { beside(old_executable_, L".old") }
            {
            }

            [[nodiscard]] int run()
            {
                // 2. 부모를 손잡이로 기다린다. PID가 다시 쓰여도 엉뚱한 프로세스를 기다리지 않는다.
                const owned_handle parent { reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(arguments_.parent_handle)) };
                const DWORD waited { WaitForSingleObject(parent.get(), static_cast<DWORD>(arguments_.parent_timeout_ms)) };
                if (waited != WAIT_OBJECT_0)
                {
                    // 옛 앱이 살아 있어 띄우면 둘째가 된다. 바꾸지도 띄우지도 않는다.
                    write_result(update_apply_outcome::parent_timeout,
                        waited == WAIT_TIMEOUT ? std::u8string { u8"The app did not end within parent_timeout." } : win32::make_last_error(u8"Failed to wait for the app")
                    );
                    return 1;
                }

                // 3. 같은 받기 폴더를 쓰는 다른 updater와 엇갈리지 않게 잠근다.
                std::optional<file_lock> lock { take_lock(directory_) };
                std::u8string error {};
                bool replaced { false };
                if (lock.has_value() == false)
                    error = u8"Another updater holds update.lock in the download folder.";
                else
                    replaced = replace(error);

                // 7. **다시 띄우기 전에** 결과를 쓰고 잠금을 놓는다. 다시 띄운 앱의 `create`가 늘 결과를 읽고, 받기·적용이 `busy`가 되지 않는다.
                write_result(replaced ? update_apply_outcome::replaced : update_apply_outcome::replace_failed, error);
                lock.reset();

                // 8. 바꾸지 못했어도 옛 exe를 띄워 사용자가 앱을 잃지 않는다.
                if (arguments_.restart == false)
                    return replaced ? 0 : 1;
                std::u8string launch_error {};
                std::optional<started_process> child { start_process(old_executable_, arguments_.restart_arguments, nullptr, launch_error) };
                if (child.has_value() == false)
                {
                    write_result(replaced ? update_apply_outcome::replaced : update_apply_outcome::replace_failed, join(error, u8"Restarting failed: " + launch_error));
                    return 1;
                }
                static_cast<void>(AllowSetForegroundWindow(child->id));
                if (replaced == false)
                    return 1;
                if (arguments_.confirm_timeout_ms == 0)
                    return 0;
                return watch(std::move(*child));
            }

        private:
            [[nodiscard]] static std::u8string join(const std::u8string& first, const std::u8string& second)
            {
                if (first.empty())
                    return second;
                return first + u8" " + second;
            }

            void write_result(const update_apply_outcome outcome, std::u8string error) const
            {
                update_apply_record record {};
                record.result.outcome = outcome;
                record.result.from_version = arguments_.from_version;
                record.result.to_version = arguments_.to_version;
                record.result.error = std::move(error);
                record.sha256 = arguments_.sha256;
                // 쓰지 못해도 할 수 있는 일이 없다. 다음 시작의 `last_apply`가 비어 있을 뿐이다.
                static_cast<void>(replace_file_atomically(file_in(directory_, update_apply_result_file).u8string(), serialize_update_apply_record(record)));
            }

            // 4~6이다. 바꾸었으면 참이다. 실패하면 남긴 `.tmp`·`.old`를 지우고, 옛 exe는 그대로다.
            [[nodiscard]] bool replace(std::u8string& error) const
            {
                const std::wstring self { win32::current_executable_path() };
                if (self.empty())
                {
                    error = win32::make_last_error(u8"Failed to read the helper image path");
                    return false;
                }
                // 4. 자기 이미지(곧 받아 둔 새 exe)를 옛 exe 폴더에 복사하고 디스크까지 내린 뒤 해시를 맞춘다.
                //    같은 폴더라 6의 이름 바꾸기가 볼륨을 건너지 않는다.
                static_cast<void>(DeleteFileW(temporary_.c_str()));
                if (CopyFileExW(self.c_str(), temporary_.c_str(), nullptr, nullptr, nullptr, COPY_FILE_FAIL_IF_EXISTS) == FALSE)
                {
                    error = win32::make_last_error(u8"Failed to copy the new executable beside the old one");
                    static_cast<void>(DeleteFileW(temporary_.c_str()));
                    return false;
                }
                if (net::flush_update_file(temporary_, error) == false || file_matches(temporary_, arguments_.sha256, error) == false)
                {
                    static_cast<void>(DeleteFileW(temporary_.c_str()));
                    return false;
                }
                // 5. 옛 exe를 `.old`로 복사한다. 옛 exe는 제자리에 그대로다. 되돌리기가 이것을 쓴다.
                if (CopyFileExW(old_executable_.c_str(), backup_.c_str(), nullptr, nullptr, nullptr, 0) == FALSE || net::flush_update_file(backup_, error) == false)
                {
                    if (error.empty())
                        error = win32::make_last_error(u8"Failed to copy the old executable to .old");
                    static_cast<void>(DeleteFileW(temporary_.c_str()));
                    static_cast<void>(DeleteFileW(backup_.c_str()));
                    return false;
                }
                // 6. 한 번에 바꾼다.
                if (move_replacing(temporary_, old_executable_, error) == false)
                {
                    static_cast<void>(DeleteFileW(temporary_.c_str()));
                    static_cast<void>(DeleteFileW(backup_.c_str()));
                    return false;
                }
                return true;
            }

            // 다시 띄운 새 판의 `create`가 남긴 확인이 이 적용의 것인가.
            [[nodiscard]] bool confirmed() const
            {
                const std::vector<std::uint8_t> bytes { read_small_file(file_in(directory_, update_apply_confirm_file)) };
                const std::u8string text { bytes.begin(), bytes.end() };
                return text == format_update_digest(arguments_.sha256);
            }

            // 결정 9다. 확인이 서거나, 0으로 끝나거나, 시간이 지나면 성공이다. 확인 전에 0이 아닌 코드로 끝나면 되돌린다.
            [[nodiscard]] int watch(started_process child) const
            {
                using clock = std::chrono::steady_clock;
                const clock::time_point deadline { clock::now() + std::chrono::milliseconds { static_cast<long long>(arguments_.confirm_timeout_ms) } };
                while (true)
                {
                    if (confirmed())
                        return 0;
                    const auto remaining { std::chrono::ceil<std::chrono::milliseconds>(deadline - clock::now()) };
                    if (remaining <= std::chrono::milliseconds::zero())
                        return 0;
                    const DWORD waited { WaitForSingleObject(child.process.get(), static_cast<DWORD>(std::min(remaining, std::chrono::milliseconds { 100 }).count())) };
                    if (waited == WAIT_TIMEOUT)
                        continue;
                    // 지켜볼 수 없으면 바꾼 판을 그대로 둔다.
                    DWORD code { 0 };
                    if (waited != WAIT_OBJECT_0 || GetExitCodeProcess(child.process.get(), &code) == FALSE)
                        return 0;
                    // 끝나기 직전에 확인을 남겼을 수 있다.
                    if (code == 0 || confirmed())
                        return 0;
                    child = started_process {};
                    roll_back(code);
                    return 1;
                }
            }

            void roll_back(const DWORD code) const
            {
                const std::u8string reason { u8"The new version exited with code " + exit_code_text(code) + u8" before confirming." };
                std::u8string error {};
                if (move_replacing(backup_, old_executable_, error) == false)
                {
                    // 새 판이 그 자리에 남았다. 다시 띄우면 또 쓰러지므로 띄우지 않는다.
                    write_result(update_apply_outcome::replaced, reason + u8" Restoring the old executable failed: " + error);
                    return;
                }
                std::optional<file_lock> lock { take_lock(directory_) };
                // 그 판은 더 새 판이 나올 때까지 내주지 않는다. 읽은 상태를 고쳐 써서 `sequence`를 지우지 않는다.
                const std::filesystem::path state_path { file_in(directory_, update_state_file) };
                update_state state { parse_update_state(read_small_file(state_path)).value_or(update_state {}) };
                state.skipped_version = arguments_.to_version;
                std::u8string written_error {};
                if (const platform_result written { replace_file_atomically(state_path.u8string(), serialize_update_state(state)) }; written.ok() == false)
                    written_error = u8" Recording the skipped version failed: " + written.error;
                // 옛 exe를 다시 띄우기 **전에** 덮어 쓴다. 그 앱의 `create`가 `rolled_back`을 읽는다.
                write_result(update_apply_outcome::rolled_back, reason + written_error);
                lock.reset();
                std::u8string launch_error {};
                if (std::optional<started_process> child { start_process(old_executable_, arguments_.restart_arguments, nullptr, launch_error) }; child.has_value())
                    static_cast<void>(AllowSetForegroundWindow(child->id));
                else
                    write_result(update_apply_outcome::rolled_back, reason + written_error + u8" Restarting failed: " + launch_error);
            }

            update_helper_arguments arguments_ {};
            std::filesystem::path old_executable_ {};
            std::filesystem::path directory_ {};
            std::filesystem::path temporary_ {};
            std::filesystem::path backup_ {};
        };
    }

    update_apply_mode platform_update_apply_mode() noexcept
    {
        return update_apply_mode::replace_executable;
    }

    std::optional<int> run_update_helper(const std::span<const std::u8string> arguments)
    {
        const update_helper_parse_result parsed { parse_update_helper_arguments(arguments) };
        if (parsed.kind == update_helper_parse::not_helper)
            return std::nullopt;
        // 표식은 있는데 나머지가 틀렸다. 무엇을 바꿀지 모르니 아무것도 하지 않는다.
        if (parsed.kind == update_helper_parse::malformed)
            return 2;
        try
        {
            return helper_run { parsed.arguments }.run();
        }
        catch (...)
        {
            // 경로 변환이나 할당이 던졌다. 이름 바꾸기 하나가 바꾸는 전부라 옛 exe 자리는 옛 것이거나 새 것이다.
            return 1;
        }
    }

    namespace win32 {
        bool runnable_executable_header(const std::span<const std::byte> header, std::u8string& error)
        {
            IMAGE_DOS_HEADER dos {};
            if (header.size() < sizeof(dos))
            {
                error = u8"The downloaded file is not a Windows executable: it is shorter than an MZ header.";
                return false;
            }
            std::memcpy(&dos, header.data(), sizeof(dos));
            if (dos.e_magic != IMAGE_DOS_SIGNATURE)
            {
                error = u8"The downloaded file is not a Windows executable: it does not start with MZ.";
                return false;
            }
            // 음수나 바이트 열 밖의 자리는 PE 머리가 없는 것과 같다.
            const std::uint64_t nt_offset {
                dos.e_lfanew < 0 ? std::numeric_limits<std::uint64_t>::max() : static_cast<std::uint64_t>(dos.e_lfanew)
            };
            constexpr std::uint64_t nt_size { sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) };
            if (nt_offset > header.size()
                || header.size() - nt_offset < nt_size)
            {
                error = u8"The downloaded file is not a Windows executable: its PE header is missing.";
                return false;
            }
            DWORD signature { 0 };
            IMAGE_FILE_HEADER file {};
            const std::byte* const nt { header.data() + static_cast<std::size_t>(nt_offset) };
            std::memcpy(&signature, nt, sizeof(signature));
            std::memcpy(&file, nt + sizeof(signature), sizeof(file));
            if (signature != IMAGE_NT_SIGNATURE)
            {
                error = u8"The downloaded file is not a Windows executable: its PE signature is missing.";
                return false;
            }
            if (file.Machine != running_machine)
            {
                error = u8"The downloaded executable is built for another machine than this app.";
                return false;
            }
            if ((file.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) == 0
                || (file.Characteristics & IMAGE_FILE_DLL) != 0)
            {
                error = u8"The downloaded file is not an executable image.";
                return false;
            }
            return true;
        }

        bool launch_update_helper(const update_helper_launch& launch, std::u8string& error)
        {
            const std::chrono::milliseconds parent_timeout { clamp_wait(launch.options.parent_timeout) };
            const std::chrono::milliseconds confirm_timeout { clamp_wait(launch.options.confirm_timeout) };
            if (launch.options.parent_timeout <= std::chrono::milliseconds::zero() || launch.options.confirm_timeout < std::chrono::milliseconds::zero()
                || parent_timeout != launch.options.parent_timeout || confirm_timeout != launch.options.confirm_timeout)
            {
                error = u8"parent_timeout must be positive, confirm_timeout must not be negative, and both must fit a Win32 wait.";
                return false;
            }
            // OS에 넘기기 전에 머리를 본다. PE가 아닌 파일은 OS가 사용자 화면에 상자를 띄운다.
            const std::optional<std::vector<std::byte>> header { read_executable_header(launch.helper_executable, error) };
            if (header.has_value() == false
                || runnable_executable_header(*header, error) == false)
                return false;
            HANDLE duplicated { nullptr };
            if (DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &duplicated, SYNCHRONIZE, TRUE, 0) == FALSE)
            {
                error = make_last_error(u8"Failed to duplicate the app process handle");
                return false;
            }
            // 이 쪽의 사본은 띄운 뒤 닫는다. 자식은 상속받은 같은 값의 손잡이를 쥔다.
            const owned_handle parent { duplicated };
            update_helper_arguments arguments {};
            arguments.old_executable = launch.old_executable.u8string();
            arguments.parent_handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(duplicated));
            arguments.download_directory = launch.download_directory.u8string();
            arguments.sha256 = launch.sha256;
            arguments.from_version = launch.from_version;
            arguments.to_version = launch.to_version;
            arguments.parent_timeout_ms = static_cast<std::uint64_t>(parent_timeout.count());
            arguments.confirm_timeout_ms = static_cast<std::uint64_t>(confirm_timeout.count());
            arguments.restart = launch.options.restart;
            arguments.restart_arguments = launch.options.restart_arguments;
            const std::vector<std::u8string> list { make_update_helper_arguments(arguments) };
            std::optional<started_process> helper { start_process(launch.helper_executable, list, duplicated, error) };
            if (helper.has_value() == false)
                return false;
            // 도우미가 다시 띄운 앱이 앞에 설 수 있게 이 앱의 앞 세우기 권리를 넘긴다.
            static_cast<void>(AllowSetForegroundWindow(helper->id));
            return true;
        }

        bool folder_writable(const std::filesystem::path& folder, std::u8string& error)
        {
            static std::atomic<unsigned> counter { 0 };
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                const std::wstring name { L".luil-write-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter) };
                const owned_handle file {
                    CreateFileW((folder / name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr),
                };
                if (file.valid())
                    return true;
                // 지난 시도가 남긴 같은 이름이면 다음 이름으로 한다. 닫으면 지워지므로 대개 없다.
                if (GetLastError() != ERROR_FILE_EXISTS)
                {
                    error = make_last_error(u8"The executable folder is not writable");
                    return false;
                }
            }
            error = u8"The executable folder is not writable: every write-test name already exists.";
            return false;
        }

        bool executable_folder_writable(std::u8string& error)
        {
            const std::wstring executable { current_executable_path() };
            if (executable.empty())
            {
                error = make_last_error(u8"Failed to read the executable path");
                return false;
            }
            return folder_writable(std::filesystem::path { executable }.parent_path(), error);
        }
    }

    namespace net {
        bool hand_off_update(const update_handoff& handoff, std::u8string& error)
        {
            const std::filesystem::path target { update_target_path() };
            if (target.empty())
            {
                error = win32::make_last_error(u8"Failed to read the executable path");
                return false;
            }
            win32::update_helper_launch launch {};
            launch.helper_executable = handoff.ready_file;
            launch.old_executable = target;
            launch.download_directory = handoff.download_directory;
            launch.sha256 = handoff.sha256;
            launch.from_version = handoff.from_version;
            launch.to_version = handoff.to_version;
            launch.options = handoff.options;
            return win32::launch_update_helper(launch, error);
        }

        bool update_target_writable(std::u8string& error)
        {
            return win32::executable_folder_writable(error);
        }

        std::filesystem::path update_target_path()
        {
            const std::wstring executable { win32::current_executable_path() };
            return executable.empty() ? std::filesystem::path {} : std::filesystem::path { executable };
        }
    }
}
