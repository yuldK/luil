#include "luil/app/file_watcher.h"

#include "android/errno_text.h"
#include "host/file_watch_batch.h"
#include "luil/text/utf8_text.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <dirent.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

// 파일 감시의 Android 쪽이다 (file-watch-design.md). inotify와 정지용 eventfd를 `poll` 하나로 기다린다.
// JNI도 Java도 쓰지 않는다. 감시 thread는 JVM에 붙지 않으므로 이 파일은 core test에 그대로 묶여 기기 셸에서 돈다.
namespace luil {
    namespace {
        using clock = file_watch_batch::clock;

        // 닫힘 알림(`IN_CLOSE_WRITE`)도 받아 `modified` 하나로 합친다. 속성(`IN_ATTRIB`)은 받지 않는다.
        constexpr std::uint32_t watch_mask {
            IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR,
        };
        constexpr std::size_t read_buffer_bytes { 64 * 1024 };

        [[nodiscard]] std::string narrow(const std::u8string_view text)
        {
            return { text.begin(), text.end() };
        }

        [[nodiscard]] std::u8string widen(const std::string_view text)
        {
            return { text.begin(), text.end() };
        }

        [[nodiscard]] std::string join(const std::string& folder, const std::string_view name)
        {
            if (folder.empty())
                return std::string { name };
            std::string path { folder };
            path += '/';
            path += name;
            return path;
        }

        [[nodiscard]] bool below(const std::string& path, const std::string& prefix)
        {
            return path == prefix
                || (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 && path[prefix.size()] == '/')
            ;
        }

        // 감시 하나다. 감시 thread만 만진다.
        struct watch
        {
            explicit watch(const file_watch_timing timing) noexcept
                : batch { timing }
            {
            }

            file_watch_ticket ticket {};
            // 감시하는 폴더다 (`file`이면 부모 폴더).
            std::string root {};
            bool subtree { false };
            int root_wd { -1 };
            // 이 감시가 건 폴더다. inotify는 하위 폴더를 따라가지 않아 `subtree`는 폴더마다 건다 (wd → 뿌리 기준 상대 경로).
            std::unordered_map<int, std::string> folders {};
            // 이름이 바뀌어 나간 폴더다. 같은 읽기 안에 짝이 오면 그 아래 경로를 고치고, 오지 않으면 그 아래 watch를 푼다.
            //  - 한 읽기 안에 여럿이 나갈 수 있어 cookie마다 하나씩 둔다. 하나만 두면 앞의 것을 덮어써 그 아래 watch가 남는다.
            struct moved_folder
            {
                std::uint32_t cookie { 0 };
                std::string path {};
            };
            std::vector<moved_folder> moved {};
            file_watch_batch batch;
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

        ~engine()
        {
            if (inotify >= 0)
                ::close(inotify);
            if (wake_fd >= 0)
                ::close(wake_fd);
        }

        void run() noexcept;
        void loop();
        void wake() noexcept;
        [[nodiscard]] file_watch_ticket open(std::u8string_view path, file_watch_scope scope, std::u8string& error);
        // 폴더 하나에 watch를 건다. `subtree`면 그 아래 폴더에도 건다. `announce`면 이미 있는 항목을 created로 낸다
        // (watch를 걸기 전에 새 폴더 안에 생긴 파일이 이렇게 잡힌다).
        [[nodiscard]] bool watch_folder(watch& target, const std::string& relative, bool announce, clock::time_point now, std::u8string& error);
        void drop(watch& target, int wd);
        void release(watch& target);
        void lose(watch& target, std::u8string error, clock::time_point now);
        void handle(const inotify_event& event, clock::time_point now);
        void read_events(clock::time_point now);
        void fail_pending_adds();
        [[nodiscard]] int wait_milliseconds(clock::time_point now) const;

        file_watcher_config config {};
        file_watch_timing timing {};
        int inotify { -1 };
        int wake_fd { -1 };
        std::thread thread {};
        std::atomic<std::thread::id> thread_id {};

        // 아래 셋은 `mutex`가 지킨다.
        std::mutex mutex {};
        std::condition_variable answered {};
        std::deque<add_request*> adds {};
        std::vector<file_watch_ticket> removals {};
        bool stopping { false };
        std::uint64_t next_ticket { 0 };

        std::mutex stop_mutex {};
        bool stopped { false };

        // 감시 thread의 것이다.
        std::map<std::uint64_t, std::unique_ptr<watch>> watches {};
        // 같은 폴더를 두 감시가 걸면 inotify는 같은 wd를 준다. 마지막 감시가 놓을 때만 푼다.
        std::unordered_map<int, std::vector<watch*>> users {};
        std::vector<char> buffer {};
    };

    void file_watcher::engine::wake() noexcept
    {
        const std::uint64_t one { 1 };
        static_cast<void>(::write(wake_fd, &one, sizeof(one)));
    }

    bool file_watcher::engine::watch_folder(watch& target, const std::string& relative, const bool announce, const clock::time_point now, std::u8string& error)
    {
        const std::string full { relative.empty() ? target.root : target.root + "/" + relative };
        const int wd { inotify_add_watch(inotify, full.c_str(), watch_mask) };
        if (wd < 0)
        {
            const int code { errno };
            // 하위 폴더는 걸기 전에 사라질 수 있다. 사라진 것은 그 지움 알림이 따로 온다.
            if (relative.empty() == false && (code == ENOENT || code == ENOTDIR))
                return true;
            if (code == ENOSPC)
                error = android::make_errno_error(u8"Too many folders to watch (fs.inotify.max_user_watches)");
            else
                error = android::make_errno_error(u8"Failed to watch the folder");
            return false;
        }
        target.folders[wd] = relative;
        if (relative.empty())
            target.root_wd = wd;
        std::vector<watch*>& list { users[wd] };
        if (std::find(list.begin(), list.end(), &target) == list.end())
            list.push_back(&target);
        if (target.subtree == false && announce == false)
            return true;

        DIR* const directory { ::opendir(full.c_str()) };
        if (directory == nullptr)
            return true;
        std::vector<std::string> children {};
        while (const dirent* const entry { ::readdir(directory) })
        {
            const std::string_view name { entry->d_name };
            if (name == "." || name == "..")
                continue;
            const std::string path { join(relative, name) };
            if (announce)
            {
                const std::u8string reported { widen(path) };
                if (text::utf8_is_valid(reported))
                    target.batch.created(reported, now);
                else
                    target.batch.overflow(now);
            }
            bool folder { entry->d_type == DT_DIR };
            if (entry->d_type == DT_UNKNOWN)
            {
                struct stat information {};
                folder = ::lstat((full + "/" + std::string { name }).c_str(), &information) == 0 && S_ISDIR(information.st_mode);
            }
            if (folder && target.subtree)
                children.push_back(path);
        }
        ::closedir(directory);
        for (const std::string& child : children)
        {
            if (watch_folder(target, child, announce, now, error) == false)
                return false;
        }
        return true;
    }

    void file_watcher::engine::drop(watch& target, const int wd)
    {
        target.folders.erase(wd);
        const auto found { users.find(wd) };
        if (found == users.end())
            return;
        std::erase(found->second, &target);
        if (found->second.empty())
        {
            users.erase(found);
            static_cast<void>(inotify_rm_watch(inotify, wd));
        }
    }

    void file_watcher::engine::release(watch& target)
    {
        std::vector<int> wds {};
        wds.reserve(target.folders.size());
        for (const auto& [wd, relative] : target.folders)
        {
            static_cast<void>(relative);
            wds.push_back(wd);
        }
        for (const int wd : wds)
            drop(target, wd);
        target.root_wd = -1;
        target.moved.clear();
    }

    void file_watcher::engine::lose(watch& target, std::u8string error, const clock::time_point now)
    {
        release(target);
        target.batch.finish(file_watch_end::lost, std::move(error), now);
    }

    file_watch_ticket file_watcher::engine::open(const std::u8string_view path, const file_watch_scope scope, std::u8string& error)
    {
        std::string full { narrow(path) };
        if (full.empty())
        {
            error = u8"The path is empty.";
            return {};
        }
        while (full.size() > 1 && full.back() == '/')
            full.pop_back();
        std::string folder { full };
        std::string name {};
        if (scope == file_watch_scope::file)
        {
            const std::size_t slash { full.rfind('/') };
            if (slash == std::string::npos)
            {
                folder = ".";
                name = full;
            }
            else
            {
                folder = slash == 0 ? std::string { "/" } : full.substr(0, slash);
                name = full.substr(slash + 1);
            }
            if (name.empty())
            {
                error = u8"The file path has no name.";
                return {};
            }
        }
        struct stat information {};
        if (::stat(folder.c_str(), &information) != 0)
        {
            error = android::make_errno_error(scope == file_watch_scope::file ? u8"The folder of the file does not exist" : u8"The folder does not exist");
            return {};
        }
        if (S_ISDIR(information.st_mode) == false)
        {
            error = u8"The path is not a folder.";
            return {};
        }

        auto target { std::make_unique<watch>(timing) };
        target->root = folder;
        target->subtree = scope == file_watch_scope::subtree;
        if (scope == file_watch_scope::file)
        {
            target->batch.set_name_filter([wanted = widen(name)](const std::u8string_view candidate) { return candidate == wanted; });
        }
        if (watch_folder(*target, {}, false, clock::now(), error) == false)
        {
            release(*target);
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

    void file_watcher::engine::handle(const inotify_event& event, const clock::time_point now)
    {
        // 큐가 넘쳤다. 어느 감시의 것이 빠졌는지 모르므로 모두 다시 훑게 한다.
        if ((event.mask & IN_Q_OVERFLOW) != 0)
        {
            for (auto& [ticket, target] : watches)
            {
                static_cast<void>(ticket);
                target->batch.overflow(now);
            }
            return;
        }
        const auto found { users.find(event.wd) };
        if (found == users.end())
            return;
        // OS가 watch를 거뒀다. 같은 wd를 쓰던 감시 모두에게서 지운다. 뿌리였으면 그 감시는 끝났다.
        if ((event.mask & IN_IGNORED) != 0)
        {
            const std::vector<watch*> list { found->second };
            users.erase(found);
            for (watch* const target : list)
            {
                target->folders.erase(event.wd);
                if (event.wd == target->root_wd && target->batch.ending() == false)
                    lose(*target, u8"The system stopped watching the folder.", now);
            }
            return;
        }

        const std::vector<watch*> list { found->second };
        for (watch* const target : list)
        {
            if (target->batch.ending())
                continue;
            const auto folder { target->folders.find(event.wd) };
            if (folder == target->folders.end())
                continue;
            if ((event.mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT)) != 0)
            {
                // 하위 폴더 자신의 알림은 부모 폴더의 지움·이름 바꿈으로 이미 왔다.
                if (event.wd == target->root_wd)
                    lose(*target, (event.mask & IN_UNMOUNT) != 0 ? u8"The watched folder was unmounted." : u8"The watched folder was removed or moved.", now);
                continue;
            }
            if (event.len == 0)
                continue;
            const std::string relative { folder->second };
            const std::string path { join(relative, std::string_view { event.name, ::strnlen(event.name, event.len) }) };
            const bool is_folder { (event.mask & IN_ISDIR) != 0 };
            const std::u8string reported { widen(path) };
            const bool valid { text::utf8_is_valid(reported) };

            if (valid == false)
                target->batch.overflow(now);
            else if ((event.mask & IN_CREATE) != 0)
                target->batch.created(reported, now);
            else if ((event.mask & IN_DELETE) != 0)
                target->batch.removed(reported, now);
            else if ((event.mask & (IN_MODIFY | IN_CLOSE_WRITE)) != 0)
                target->batch.modified(reported, now);
            else if ((event.mask & IN_MOVED_FROM) != 0)
                target->batch.moved_from(reported, event.cookie, now);
            else if ((event.mask & IN_MOVED_TO) != 0)
                target->batch.moved_to(reported, event.cookie, now);

            if (target->subtree == false || is_folder == false)
                continue;
            std::u8string error {};
            if ((event.mask & IN_CREATE) != 0)
            {
                // watch를 먼저 걸고 그 안을 훑는다. 겹친 것은 created+created로 합쳐진다.
                if (watch_folder(*target, path, true, now, error) == false)
                    lose(*target, std::move(error), now);
            }
            else if ((event.mask & IN_MOVED_FROM) != 0)
                target->moved.push_back(watch::moved_folder { event.cookie, path });
            else if ((event.mask & IN_MOVED_TO) != 0)
            {
                const auto pair { std::find_if(target->moved.begin(), target->moved.end(), [&event](const watch::moved_folder& each) { return each.cookie == event.cookie; }) };
                if (pair != target->moved.end())
                {
                    // 안에서 옮긴 폴더다. watch는 그대로 따라가므로 표의 경로만 고친다.
                    //  - 아직 짝을 기다리는 그 아래의 나간 폴더도 같이 고친다. 읽기 끝에서 새 경로로 풀어야 한다.
                    const std::string from { pair->path };
                    target->moved.erase(pair);
                    for (auto& [wd, folder_path] : target->folders)
                    {
                        static_cast<void>(wd);
                        if (below(folder_path, from))
                            folder_path = path + folder_path.substr(from.size());
                    }
                    for (watch::moved_folder& waiting : target->moved)
                    {
                        if (below(waiting.path, from))
                            waiting.path = path + waiting.path.substr(from.size());
                    }
                }
                else if (watch_folder(*target, path, true, now, error) == false)
                    lose(*target, std::move(error), now);
            }
        }
    }

    void file_watcher::engine::read_events(const clock::time_point now)
    {
        for (;;)
        {
            const ssize_t length { ::read(inotify, buffer.data(), buffer.size()) };
            if (length < 0 && errno == EINTR)
                continue;
            if (length <= 0)
                break;
            std::size_t offset { 0 };
            while (offset + sizeof(inotify_event) <= static_cast<std::size_t>(length))
            {
                // 커널은 이름 길이를 머리의 정렬까지 채워 주고, 버퍼는 `new`가 잡아 정렬돼 있다. 이름은 머리 바로 뒤에 붙는다.
                const auto* const event { reinterpret_cast<const inotify_event*>(buffer.data() + offset) };
                if (offset + sizeof(inotify_event) + event->len > static_cast<std::size_t>(length))
                    break;
                handle(*event, now);
                offset += sizeof(inotify_event) + event->len;
            }
        }
        // 같은 읽기 안에 짝이 오지 않은 폴더는 감시 밖으로 나갔다.
        for (auto& [ticket, target] : watches)
        {
            static_cast<void>(ticket);
            if (target->moved.empty())
                continue;
            const std::vector<watch::moved_folder> gone { std::exchange(target->moved, {}) };
            std::vector<int> wds {};
            for (const auto& [wd, folder_path] : target->folders)
            {
                const auto under = [&folder_path](const watch::moved_folder& each) { return below(folder_path, each.path); };
                if (std::any_of(gone.begin(), gone.end(), under))
                    wds.push_back(wd);
            }
            for (const int wd : wds)
                drop(*target, wd);
        }
    }

    int file_watcher::engine::wait_milliseconds(const clock::time_point now) const
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
            return -1;
        if (*nearest <= now)
            return 0;
        const auto remaining { std::chrono::ceil<std::chrono::milliseconds>(*nearest - now).count() };
        return static_cast<int>(std::min<long long>(remaining, 24 * 60 * 60 * 1000));
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
        buffer.resize(read_buffer_bytes);
        for (;;)
        {
            pollfd descriptors[2] {
                pollfd { inotify, POLLIN, 0 },
                pollfd { wake_fd, POLLIN, 0 },
            };
            const int timeout { wait_milliseconds(clock::now()) };
            if (::poll(descriptors, 2, timeout) < 0)
            {
                descriptors[0].revents = 0;
                descriptors[1].revents = 0;
            }
            const clock::time_point now { clock::now() };
            if ((descriptors[1].revents & POLLIN) != 0)
            {
                std::uint64_t count { 0 };
                static_cast<void>(::read(wake_fd, &count, sizeof(count)));
            }
            if ((descriptors[0].revents & POLLIN) != 0)
                read_events(now);

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
                release(*found->second);
                found->second->batch.finish(file_watch_end::removed, {}, now);
            }

            const clock::time_point offered { clock::now() };
            for (auto& [ticket, target] : watches)
            {
                static_cast<void>(ticket);
                offer_file_change_batch(target->batch, target->ticket, config.deliver, offered);
            }
            for (auto at { watches.begin() }; at != watches.end();)
            {
                if (at->second->batch.closed())
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
        owned->inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (owned->inotify < 0)
        {
            error = android::make_errno_error(u8"Failed to start inotify");
            return nullptr;
        }
        owned->wake_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (owned->wake_fd < 0)
        {
            error = android::make_errno_error(u8"Failed to create the wake event");
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
        // inotify에는 OS가 쥔 우리 버퍼가 없다. 닫으면 watch가 모두 풀린다.
        state.watches.clear();
        state.users.clear();
        if (state.inotify >= 0)
            ::close(state.inotify);
        state.inotify = -1;
        state.stopped = true;
    }
}
