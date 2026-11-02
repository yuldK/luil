#include "luil/app/file_watcher.h"

#include "host/file_watch_batch.h"
#include "win32/file_watcher_win32.h"
#include "win32/owned_handle.h"
#include "win32/utf8.h"
#include "win32/win32_error.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

// 파일 감시의 Windows 쪽이다 (file-watch-design.md). 감시마다 폴더 핸들 하나에 겹친 I/O의 `ReadDirectoryChangesW`를 걸고,
// 모든 핸들을 I/O 완료 포트 하나에 묶어 감시 thread가 `GetQueuedCompletionStatusEx`로 기다린다. 핸들 수에 64개 상한이 없다.
namespace luil {
    namespace win32 {
        namespace {
            std::atomic<std::size_t> buffer_bytes_override { 0 };
        }

        void set_file_watch_buffer_bytes_for_tests(const std::size_t bytes) noexcept
        {
            buffer_bytes_override.store(bytes);
        }
    }

    namespace {
        using clock = file_watch_batch::clock;
        using win32::owned_handle;

        // 네트워크 경로는 64 KB를 넘는 버퍼를 거절한다. 모든 경로를 같은 크기로 두어 갈래를 만들지 않는다.
        constexpr std::size_t default_buffer_bytes { 64 * 1024 };
        // 깨우기 신호의 완료 키다. 감시의 완료 키는 그 감시의 주소다.
        constexpr ULONG_PTR wake_key { 0 };
        // 이름(파일·폴더)과 내용(마지막 쓰기·크기)만 받는다. 속성만 바뀐 것은 받지 않는다.
        constexpr DWORD notify_filter {
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
        };
        // Windows의 이름 바꿈 반쪽은 늘 이어 오므로 짝을 cookie 하나로 맞춘다.
        constexpr std::uint32_t rename_cookie { 1 };

        [[nodiscard]] std::u8string error_of(const std::u8string_view message, const DWORD code)
        {
            return win32::make_hresult_error(message, HRESULT_FROM_WIN32(code));
        }

        [[nodiscard]] bool same_name(const std::wstring_view left, const std::wstring_view right) noexcept
        {
            // 파일 이름은 대소문자를 가리지 않는다. NTFS가 쓰는 대문자 표와 같은 서수 비교다.
            return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
        }

        [[nodiscard]] std::wstring final_path_of(const HANDLE handle)
        {
            std::wstring path(MAX_PATH, L'\0');
            for (int attempt { 0 }; attempt < 2; ++attempt)
            {
                const DWORD length { GetFinalPathNameByHandleW(handle, path.data(), static_cast<DWORD>(path.size()), FILE_NAME_NORMALIZED) };
                if (length == 0)
                    return {};
                if (length < path.size())
                {
                    path.resize(length);
                    return path;
                }
                path.resize(length);
            }
            return {};
        }

        [[nodiscard]] std::wstring long_path_of(const std::wstring& path)
        {
            const DWORD length { GetLongPathNameW(path.c_str(), nullptr, 0) };
            if (length == 0)
                return {};
            std::wstring result(length, L'\0');
            const DWORD written { GetLongPathNameW(path.c_str(), result.data(), length) };
            if (written == 0 || written >= length)
                return {};
            result.resize(written);
            return result;
        }

        // 감시 하나다. 감시 thread만 만진다 (`stop`의 마지막 정리는 thread가 끝난 뒤다).
        struct watch
        {
            explicit watch(const file_watch_timing timing) noexcept
                : batch { timing }
            {
            }

            OVERLAPPED overlapped {};
            file_watch_ticket ticket {};
            owned_handle directory {};
            // 감시하는 폴더다 (`file`이면 부모 폴더). 짧은 이름을 늘릴 때 앞에 붙인다.
            std::wstring root {};
            // 짧은 이름을 늘린 결과에서 떼어 낼 앞부분이다.
            std::wstring long_root {};
            // 걸 때의 실제 경로다. 핸들은 옮겨 간 폴더를 따라가므로, 달라졌으면 감시 폴더가 옮겨진 것이다.
            std::wstring final_path {};
            BOOL subtree { FALSE };
            // `FILE_NOTIFY_INFORMATION`은 DWORD 정렬이다.
            std::vector<DWORD> buffer {};
            file_watch_batch batch;
            // OS가 버퍼를 쥐고 있다. 이 동안은 감시를 거두지 않는다.
            bool reading { false };
            // 그만 읽기로 했다 (`CancelIoEx`를 불렀거나 다시 걸지 않는다).
            bool cancelled { false };
        };

        struct add_request
        {
            std::u8string path {};
            file_watch_scope scope { file_watch_scope::folder };
            bool done { false };
            file_watch_ticket ticket {};
            std::u8string error {};
        };
    }

    struct file_watcher::engine
    {
        explicit engine(file_watcher_config configuration) noexcept
            : config { std::move(configuration) }
            , timing { config.settle, config.max_delay, config.max_changes }
        {
        }

        void run() noexcept;
        void loop();
        void wake() noexcept;
        void complete(watch& target, clock::time_point now);
        void parse(watch& target, DWORD transferred, clock::time_point now);
        void lose(watch& target, std::u8string error, clock::time_point now);
        [[nodiscard]] bool read(watch& target) noexcept;
        [[nodiscard]] file_watch_ticket open(std::u8string_view path, file_watch_scope scope, std::u8string& error);
        [[nodiscard]] std::optional<std::u8string> relative_name(const watch& target, std::wstring_view name) const;
        void fail_pending_adds();
        [[nodiscard]] DWORD wait_milliseconds(clock::time_point now) const;

        file_watcher_config config {};
        file_watch_timing timing {};
        owned_handle port {};
        std::thread thread {};
        std::atomic<std::thread::id> thread_id {};

        // 아래 셋은 `mutex`가 지킨다.
        std::mutex mutex {};
        std::condition_variable answered {};
        std::deque<add_request*> adds {};
        std::vector<file_watch_ticket> removals {};
        bool stopping { false };
        std::uint64_t next_ticket { 0 };

        // 둘이 함께 `stop`을 불러도 뒤엣것은 앞엣것이 끝날 때까지 기다린다.
        std::mutex stop_mutex {};
        bool stopped { false };

        // 감시 thread의 것이다. 표 차례로 둔다. `deliver` 안의 `add`가 더해도 다른 자리를 흔들지 않는다.
        std::map<std::uint64_t, std::unique_ptr<watch>> watches {};
    };

    void file_watcher::engine::wake() noexcept
    {
        static_cast<void>(PostQueuedCompletionStatus(port.get(), 0, wake_key, nullptr));
    }

    bool file_watcher::engine::read(watch& target) noexcept
    {
        target.overlapped = OVERLAPPED {};
        const DWORD bytes { static_cast<DWORD>(target.buffer.size() * sizeof(DWORD)) };
        target.reading = ReadDirectoryChangesW(target.directory.get(), target.buffer.data(), bytes, target.subtree, notify_filter, nullptr, &target.overlapped, nullptr) != FALSE;
        return target.reading;
    }

    file_watch_ticket file_watcher::engine::open(const std::u8string_view path, const file_watch_scope scope, std::u8string& error)
    {
        const std::optional<std::wstring> wide { win32::utf8_to_utf16(path).value };
        if (wide.has_value() == false || wide->empty())
        {
            error = u8"The path is empty or not valid UTF-8.";
            return {};
        }
        const DWORD full_length { GetFullPathNameW(wide->c_str(), 0, nullptr, nullptr) };
        if (full_length == 0)
        {
            error = win32::make_last_error(u8"Failed to resolve the path");
            return {};
        }
        std::wstring full(full_length, L'\0');
        const DWORD full_written { GetFullPathNameW(wide->c_str(), full_length, full.data(), nullptr) };
        if (full_written == 0 || full_written >= full_length)
        {
            error = win32::make_last_error(u8"Failed to resolve the path");
            return {};
        }
        full.resize(full_written);
        while (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/'))
            full.pop_back();

        std::wstring folder { full };
        std::wstring name {};
        if (scope == file_watch_scope::file)
        {
            const std::filesystem::path split { full };
            name = split.filename().wstring();
            folder = split.parent_path().wstring();
            if (name.empty() || folder.empty())
            {
                error = u8"The file path has no name.";
                return {};
            }
        }
        const DWORD attributes { GetFileAttributesW(folder.c_str()) };
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            error = scope == file_watch_scope::file ? win32::make_last_error(u8"The folder of the file does not exist") : win32::make_last_error(u8"The folder does not exist");
            return {};
        }
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            error = u8"The path is not a folder.";
            return {};
        }

        auto target { std::make_unique<watch>(timing) };
        target->root = folder;
        target->subtree = scope == file_watch_scope::subtree ? TRUE : FALSE;
        // 공유 삭제가 없으면 감시가 그 폴더의 삭제·이름 바꿈을 막는다.
        target->directory = owned_handle {
            CreateFileW(folder.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr),
        };

        if (target->directory.valid() == false)
        {
            error = win32::make_last_error(u8"Failed to open the folder");
            return {};
        }
        if (CreateIoCompletionPort(target->directory.get(), port.get(), reinterpret_cast<ULONG_PTR>(target.get()), 0) == nullptr)
        {
            error = win32::make_last_error(u8"Failed to bind the folder to the completion port");
            return {};
        }
        target->final_path = final_path_of(target->directory.get());
        target->long_root = long_path_of(folder);
        if (target->long_root.empty())
            target->long_root = folder;
        const std::size_t override_bytes { win32::buffer_bytes_override.load() };
        const std::size_t bytes { override_bytes != 0 ? override_bytes : default_buffer_bytes };
        target->buffer.resize((bytes + sizeof(DWORD) - 1) / sizeof(DWORD));
        if (scope == file_watch_scope::file)
        {
            const auto matches = [name](const std::u8string_view candidate) {
                const std::optional<std::wstring> wide_candidate { win32::utf8_to_utf16(candidate).value };
                return wide_candidate.has_value() && same_name(*wide_candidate, name);
            };
            target->batch.set_name_filter(matches);
        }
        // SMB 공유처럼 변경 알림을 주지 않는 경로는 여기서 거절된다.
        if (read(*target) == false)
        {
            error = win32::make_last_error(u8"Failed to watch the folder");
            return {};
        }
        {
            const std::lock_guard lock { mutex };
            target->ticket = file_watch_ticket { ++next_ticket };
        }
        const file_watch_ticket ticket { target->ticket };
        watches.emplace(ticket.value, std::move(target));
        return ticket;
    }

    std::optional<std::u8string> file_watcher::engine::relative_name(const watch& target, const std::wstring_view name) const
    {
        std::wstring relative { name };
        // 짧은 이름(8.3)으로 왔으면 늘린다. 지워진 파일은 늘릴 수 없어 그대로 둔다.
        if (relative.find(L'~') != std::wstring::npos)
        {
            const std::wstring expanded { long_path_of(target.root + L"\\" + relative) };
            if (expanded.size() > target.long_root.size() + 1
                && same_name(std::wstring_view { expanded }.substr(0, target.long_root.size()), target.long_root)
                && expanded[target.long_root.size()] == L'\\')
                relative = expanded.substr(target.long_root.size() + 1);
        }
        // 짝 없는 surrogate는 UTF-8로 옮길 수 없다. 부르는 쪽이 rescan을 단다.
        std::optional<std::u8string> converted { win32::utf16_to_utf8(relative).value };
        if (converted.has_value() == false)
            return std::nullopt;
        return to_watch_path(std::move(*converted));
    }

    void file_watcher::engine::parse(watch& target, const DWORD transferred, const clock::time_point now)
    {
        const auto* const base { reinterpret_cast<const std::byte*>(target.buffer.data()) };
        std::size_t offset { 0 };
        while (offset + offsetof(FILE_NOTIFY_INFORMATION, FileName) <= transferred)
        {
            const auto* const information { reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(base + offset) };
            const std::size_t name_bytes { information->FileNameLength };
            if (offset + offsetof(FILE_NOTIFY_INFORMATION, FileName) + name_bytes > transferred)
                break;
            const std::wstring_view name { information->FileName, name_bytes / sizeof(wchar_t) };
            std::optional<std::u8string> path { relative_name(target, name) };
            if (path.has_value() == false)
                target.batch.overflow(now);
            else
            {
                switch (information->Action)
                {
                case FILE_ACTION_ADDED:
                    target.batch.created(std::move(*path), now);
                    break;
                case FILE_ACTION_REMOVED:
                    target.batch.removed(std::move(*path), now);
                    break;
                case FILE_ACTION_MODIFIED:
                    target.batch.modified(std::move(*path), now);
                    break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    target.batch.moved_from(std::move(*path), rename_cookie, now);
                    break;
                case FILE_ACTION_RENAMED_NEW_NAME:
                    target.batch.moved_to(std::move(*path), rename_cookie, now);
                    break;
                default:
                    break;
                }
            }
            if (information->NextEntryOffset == 0)
                break;
            offset += information->NextEntryOffset;
        }
    }

    void file_watcher::engine::lose(watch& target, std::u8string error, const clock::time_point now)
    {
        if (target.reading && target.cancelled == false)
            static_cast<void>(CancelIoEx(target.directory.get(), &target.overlapped));
        target.cancelled = true;
        target.batch.finish(file_watch_end::lost, std::move(error), now);
    }

    void file_watcher::engine::complete(watch& target, const clock::time_point now)
    {
        target.reading = false;
        if (target.cancelled)
            return;
        DWORD transferred { 0 };
        if (GetOverlappedResult(target.directory.get(), &target.overlapped, &transferred, FALSE) == FALSE)
        {
            const DWORD code { GetLastError() };
            // 버퍼보다 많이 바뀌었다는 뜻이다. 감시는 그대로 다시 건다.
            if (code == ERROR_NOTIFY_ENUM_DIR)
                target.batch.overflow(now);
            else
            {
                // 감시 폴더가 지워지면 읽기가 오류로 끝난다 (`ERROR_ACCESS_DENIED`가 흔하다).
                lose(target, error_of(u8"The watched folder is gone or the system stopped watching it", code), now);
                return;
            }
        }
        else if (transferred == 0)
            target.batch.overflow(now);
        else
            parse(target, transferred, now);
        if (read(target) == false)
            lose(target, win32::make_last_error(u8"The watched folder is gone or the system stopped watching it"), now);
    }

    DWORD file_watcher::engine::wait_milliseconds(const clock::time_point now) const
    {
        std::optional<clock::time_point> nearest {};
        for (const auto& [ticket, target] : watches)
        {
            static_cast<void>(ticket);
            const std::optional<clock::time_point> due { target->batch.due_at() };
            if (due.has_value() && (nearest.has_value() == false || *due < *nearest))
                nearest = due;
        }
        if (nearest.has_value() == false)
            return INFINITE;
        if (*nearest <= now)
            return 0;
        // 올림이다. 내림이면 시한 직전에 깨어 한 번 더 돈다.
        const auto remaining { std::chrono::ceil<std::chrono::milliseconds>(*nearest - now).count() };
        return static_cast<DWORD>(std::min<long long>(remaining, INFINITE - 1));
    }

    void file_watcher::engine::fail_pending_adds()
    {
        const std::lock_guard lock { mutex };
        stopping = true;
        for (add_request* const request : adds)
        {
            request->error = u8"The file watcher stopped.";
            request->done = true;
        }
        adds.clear();
        answered.notify_all();
    }

    void file_watcher::engine::loop()
    {
        std::array<OVERLAPPED_ENTRY, 32> entries {};
        for (;;)
        {
            ULONG count { 0 };
            const DWORD timeout { wait_milliseconds(clock::now()) };
            if (GetQueuedCompletionStatusEx(port.get(), entries.data(), static_cast<ULONG>(entries.size()), &count, timeout, FALSE) == FALSE)
                count = 0;
            const clock::time_point now { clock::now() };
            for (ULONG index { 0 }; index < count; ++index)
            {
                if (entries[index].lpCompletionKey == wake_key || entries[index].lpOverlapped == nullptr)
                    continue;
                complete(*reinterpret_cast<watch*>(entries[index].lpCompletionKey), now);
            }

            std::deque<add_request*> new_adds {};
            std::vector<file_watch_ticket> new_removals {};
            {
                const std::lock_guard lock { mutex };
                if (stopping)
                    return;
                new_adds.swap(adds);
                new_removals.swap(removals);
            }
            for (add_request* const request : new_adds)
            {
                std::u8string error {};
                const file_watch_ticket ticket { open(request->path, request->scope, error) };
                const std::lock_guard lock { mutex };
                request->ticket = ticket;
                request->error = std::move(error);
                request->done = true;
            }
            if (new_adds.empty() == false)
                answered.notify_all();
            for (const file_watch_ticket ticket : new_removals)
            {
                const auto found { watches.find(ticket.value) };
                if (found == watches.end() || found->second->batch.ending())
                    continue;
                watch& target { *found->second };
                if (target.reading)
                    static_cast<void>(CancelIoEx(target.directory.get(), &target.overlapped));
                target.cancelled = true;
                target.batch.finish(file_watch_end::removed, {}, now);
            }

            const clock::time_point offered { clock::now() };
            for (auto& [ticket, target] : watches)
            {
                const std::optional<clock::time_point> due { target->batch.due_at() };
                if (due.has_value() == false || *due > offered)
                    continue;
                // 핸들은 옮겨 간 폴더를 따라간다. 안의 변경이 없으면 늦게 안다.
                if (target->batch.ending() == false && target->final_path.empty() == false && final_path_of(target->directory.get()) != target->final_path)
                    lose(*target, u8"The watched folder was moved.", offered);
                offer_file_change_batch(target->batch, target->ticket, config.deliver, offered);
            }
            for (auto at { watches.begin() }; at != watches.end();)
            {
                if (at->second->batch.closed() && at->second->reading == false)
                    at = watches.erase(at);
                else
                    ++at;
            }
        }
    }

    void file_watcher::engine::run() noexcept
    {
        thread_id.store(std::this_thread::get_id());
        try
        {
            loop();
        }
        catch (...)
        {
        }
        fail_pending_adds();
    }

    file_watcher::file_watcher(std::unique_ptr<engine> owned) noexcept
        : engine_ { std::move(owned) }
    {
    }

    file_watcher::~file_watcher()
    {
        stop();
    }

    std::unique_ptr<file_watcher> file_watcher::create(file_watcher_config configuration, std::u8string& error)
    {
        auto owned { std::make_unique<engine>(std::move(configuration)) };
        owned->port = owned_handle { CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1) };
        if (owned->port.valid() == false)
        {
            error = win32::make_last_error(u8"Failed to create the completion port");
            return nullptr;
        }
        engine* const raw { owned.get() };
        try
        {
            owned->thread = std::thread { [raw] { raw->run(); } };
        }
        catch (...)
        {
            error = u8"Failed to start the file watcher thread.";
            return nullptr;
        }
        return std::unique_ptr<file_watcher> { new file_watcher { std::move(owned) } };
    }

    file_watch_ticket file_watcher::add(const std::u8string_view path, const file_watch_scope scope, std::u8string& error)
    {
        engine& state { *engine_ };
        if (std::this_thread::get_id() == state.thread_id.load())
        {
            // `deliver` 안이다. thread가 스스로 건다.
            {
                const std::lock_guard lock { state.mutex };
                if (state.stopping)
                {
                    error = u8"The file watcher stopped.";
                    return {};
                }
            }
            return state.open(path, scope, error);
        }

        add_request request {};
        request.path = std::u8string { path };
        request.scope = scope;
        std::unique_lock lock { state.mutex };
        if (state.stopping)
        {
            error = u8"The file watcher stopped.";
            return {};
        }
        state.adds.push_back(&request);
        state.wake();
        state.answered.wait(lock, [&request] { return request.done; });
        if (request.ticket == file_watch_ticket {})
            error = std::move(request.error);
        return request.ticket;
    }

    void file_watcher::remove(const file_watch_ticket ticket) noexcept
    {
        if (ticket == file_watch_ticket {})
            return;
        engine& state { *engine_ };
        try
        {
            const std::lock_guard lock { state.mutex };
            if (state.stopping)
                return;
            state.removals.push_back(ticket);
        }
        catch (...)
        {
            return;
        }
        state.wake();
    }

    void file_watcher::stop() noexcept
    {
        if (engine_ == nullptr)
            return;
        engine& state { *engine_ };
        assert(std::this_thread::get_id() != state.thread_id.load() && "file_watcher::stop must not be called from deliver");
        const std::lock_guard stop_lock { state.stop_mutex };
        if (state.stopped)
            return;
        {
            const std::lock_guard lock { state.mutex };
            state.stopping = true;
        }
        state.wake();
        if (state.thread.joinable())
            state.thread.join();

        // thread가 끝났으니 감시는 이제 이쪽 것이다. OS가 버퍼를 놓기를 예산 안에서 기다린다.
        for (auto& [ticket, target] : state.watches)
        {
            static_cast<void>(ticket);
            if (target->reading)
                static_cast<void>(CancelIoEx(target->directory.get(), &target->overlapped));
        }
        const clock::time_point deadline { clock::now() + state.config.stop_budget };
        std::array<OVERLAPPED_ENTRY, 32> entries {};
        for (;;)
        {
            bool reading { false };
            for (const auto& [ticket, target] : state.watches)
            {
                static_cast<void>(ticket);
                reading = reading || target->reading;
            }
            const clock::time_point now { clock::now() };
            if (reading == false || now >= deadline)
                break;
            const auto remaining { std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count() };
            ULONG count { 0 };
            if (GetQueuedCompletionStatusEx(state.port.get(), entries.data(), static_cast<ULONG>(entries.size()), &count, static_cast<DWORD>(remaining), FALSE) == FALSE)
                continue;
            for (ULONG index { 0 }; index < count; ++index)
            {
                if (entries[index].lpCompletionKey != wake_key && entries[index].lpOverlapped != nullptr)
                    reinterpret_cast<watch*>(entries[index].lpCompletionKey)->reading = false;
            }
        }
        for (auto& [ticket, target] : state.watches)
        {
            static_cast<void>(ticket);
            // 예산을 넘겼다. OS가 아직 쓸지 모르는 버퍼를 푸는 것보다 새는 편이 낫다 (`http_client`와 같다).
            if (target->reading)
                static_cast<void>(target.release());
        }
        state.watches.clear();
        state.stopped = true;
    }
}
