#include "luil/app/file_watcher.h"

#include "host/file_watch_batch.h"
#include "luil/text/utf8_text.h"
#include "macos/macos_text.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

// 파일 감시의 iOS 쪽이다 (file-watch-design.md). kqueue의 `EVFILT_VNODE`와 정지용 `EVFILT_USER`를 `kevent` 하나로 기다린다.
//  - FSEvents(CoreServices)는 iOS SDK에 없다. kqueue는 BSD 그대로라 Mac에서도 돌고, 이 파일을 Mac에서 같은 test로 확인한다.
//  - 폴더의 알림은 "안의 이름이 바뀌었다"뿐이다. 폴더마다 이름 → 파일 번호(inode)·종류 표를 쥐고, 알림이 오면 다시 읽어 견준다.
//    사라진 번호가 같은 감시 안의 다른 이름으로 서 있으면 이름 바꿈이다. 다른 번호가 같은 이름에 섰으면 고침이다.
//  - 안의 파일 내용이 바뀌어도 폴더에는 알림이 없다. 내용을 볼 파일(`folder`는 바로 아래, `subtree`는 그 아래 전부,
//    `file`은 그 파일)은 `O_EVTONLY`로 따로 열어 건다. 연 파일 수는 감시자마다 상한이 있고, 넘친 파일은 크기·고친 때를
//    `poll_interval`마다 견준다. 폴더는 상한 없이 연다. 열지 못하면 `add`는 오류, 이미 선 감시는 `lost`다 (Android의 `ENOSPC`와 같다).
//  - 감시 thread 하나가 기다리고, 견주고, `deliver`를 부른다. Android의 inotify 판과 같은 뼈대다.
namespace luil {
    namespace {
        using clock = file_watch_batch::clock;

        // 폴더 자신의 알림이다. 안의 이름이 바뀌면 `NOTE_WRITE`, 하위 폴더 수가 바뀌면 `NOTE_LINK`가 온다.
        constexpr std::uint32_t folder_notes { NOTE_WRITE | NOTE_EXTEND | NOTE_LINK | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE };
        // 파일의 알림이다. 잘라 내기(`O_TRUNC`)는 `NOTE_ATTRIB`만 오므로 받는다. 크기·고친 때가 그대로면 속성만 바뀐 것으로 버린다.
        constexpr std::uint32_t file_notes { NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE };
        constexpr std::uintptr_t wake_ident { 1 };
        constexpr int event_capacity { 64 };
        // 상한 때문에 열지 못한 파일을 견주는 간격이다. `max_delay`의 기본(1초) 안이다.
        constexpr std::chrono::milliseconds poll_interval { 500 };
        // 내용을 보려고 여는 파일 수의 상한이다. `RLIMIT_NOFILE`의 1/4을 이 사이로 묶는다 (iOS의 기본 상한은 256이다).
        constexpr std::size_t file_budget_floor { 16 };
        constexpr std::size_t file_budget_ceiling { 1024 };

        [[nodiscard]] std::string join(const std::string& folder, const std::string_view name)
        {
            if (folder.empty())
                return std::string { name };
            std::string path { folder };
            path += '/';
            path += name;
            return path;
        }

        [[nodiscard]] bool same_time(const timespec& left, const timespec& right) noexcept
        {
            return left.tv_sec == right.tv_sec && left.tv_nsec == right.tv_nsec;
        }

        [[nodiscard]] void* node_data(const std::uint64_t node) noexcept
        {
            return reinterpret_cast<void*>(static_cast<std::uintptr_t>(node));
        }

        enum class item_kind
        {
            folder,
            regular,
            other,
        };

        [[nodiscard]] item_kind kind_of_mode(const mode_t mode) noexcept
        {
            if (S_ISDIR(mode))
                return item_kind::folder;
            if (S_ISREG(mode))
                return item_kind::regular;
            return item_kind::other;
        }

        // 폴더를 읽은 이름 하나다.
        struct item_info
        {
            std::uint64_t inode { 0 };
            item_kind kind { item_kind::other };
        };

        using listing = std::map<std::string, item_info>;

        // 폴더를 그 descriptor로 읽는다. 이름이 바뀌었거나 감시 밖으로 나간 폴더도 경로를 거치지 않아 바로 그 폴더다.
        [[nodiscard]] std::optional<listing> read_folder(const int folder)
        {
            const int handle { ::openat(folder, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC) };
            if (handle < 0)
                return std::nullopt;
            DIR* const directory { ::fdopendir(handle) };
            if (directory == nullptr)
            {
                ::close(handle);
                return std::nullopt;
            }
            listing items {};
            while (const dirent* const item { ::readdir(directory) })
            {
                const std::string_view name { item->d_name };
                if (name == "." || name == "..")
                    continue;
                item_info info { static_cast<std::uint64_t>(item->d_ino), item_kind::other };
                if (item->d_type == DT_DIR)
                    info.kind = item_kind::folder;
                else if (item->d_type == DT_REG)
                    info.kind = item_kind::regular;
                else if (item->d_type == DT_UNKNOWN)
                {
                    struct stat information {};
                    if (::fstatat(::dirfd(directory), item->d_name, &information, AT_SYMLINK_NOFOLLOW) != 0)
                        continue;
                    info.inode = static_cast<std::uint64_t>(information.st_ino);
                    info.kind = kind_of_mode(information.st_mode);
                }
                items.emplace(std::string { name }, info);
            }
            ::closedir(directory);
            return items;
        }

        // 폴더 안의 이름 하나다.
        struct entry
        {
            std::uint64_t inode { 0 };
            item_kind kind { item_kind::other };
            // 건 표다. 내용을 보는 파일이면 파일 표, `subtree`의 하위 폴더면 폴더 표다. 0이면 걸지 않았다.
            std::uint64_t node { 0 };
            // 내용을 보려고 연 파일이다.
            int fd { -1 };
            // 내용을 볼 파일인데 상한 때문에 열지 못해 크기·고친 때로 견준다.
            bool polled { false };
            std::int64_t size { 0 };
            timespec modified {};
        };

        // 건 폴더 하나다. 이름 바꿈은 `parent`·`name`만 고친다. descriptor는 옮겨 간 폴더를 따라간다.
        struct folder_node
        {
            int fd { -1 };
            // 뿌리는 0이다.
            std::uint64_t parent { 0 };
            std::string name {};
            std::map<std::string, entry> children {};
        };

        // 연 파일이 어느 폴더의 어느 이름인가.
        struct file_node
        {
            std::uint64_t folder { 0 };
            std::string name {};
        };

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
            // `file` 감시의 이름이다. 그 이름만 내용을 본다.
            std::optional<std::string> only {};
            std::uint64_t root_id { 0 };
            std::unordered_map<std::uint64_t, folder_node> folders {};
            std::unordered_map<std::uint64_t, file_node> files {};
            // 견주는 파일 수다.
            std::size_t polled { 0 };
            // 이번 차례에 다시 읽을 폴더와 알림이 온 파일이다. 모두 받은 뒤에 한꺼번에 견준다 (폴더를 건너간 이름 바꿈의 짝).
            std::vector<std::uint64_t> dirty {};
            std::vector<std::pair<std::uint64_t, std::uint32_t>> touched {};
            std::optional<std::u8string> lost {};
            file_watch_batch batch;
        };

        [[nodiscard]] std::string path_of(const watch& target, std::uint64_t folder)
        {
            std::vector<const std::string*> names {};
            while (folder != target.root_id)
            {
                const auto found { target.folders.find(folder) };
                if (found == target.folders.end())
                    break;
                names.push_back(&found->second.name);
                folder = found->second.parent;
            }
            std::string path {};
            for (auto at { names.rbegin() }; at != names.rend(); ++at)
                path = join(path, **at);
            return path;
        }

        [[nodiscard]] bool wants_content(const watch& target, const std::uint64_t folder, const std::string& name)
        {
            return target.only.has_value() == false || (folder == target.root_id && name == *target.only);
        }

        // UTF-8로 옮길 수 없는 이름은 항목으로 내지 않고 다시 훑게 한다.
        void report(watch& target, const file_change_kind kind, const std::string& path, const clock::time_point now)
        {
            std::u8string text { macos::widen(path) };
            if (text::utf8_is_valid(text) == false)
            {
                target.batch.overflow(now);
                return;
            }
            switch (kind)
            {
            case file_change_kind::created:
                target.batch.created(std::move(text), now);
                break;
            case file_change_kind::removed:
                target.batch.removed(std::move(text), now);
                break;
            case file_change_kind::modified:
                target.batch.modified(std::move(text), now);
                break;
            case file_change_kind::renamed:
                break;
            }
        }

        void report_renamed(watch& target, const std::string& from, const std::string& to, const clock::time_point now)
        {
            std::u8string old_text { macos::widen(from) };
            std::u8string new_text { macos::widen(to) };
            if (text::utf8_is_valid(old_text) == false || text::utf8_is_valid(new_text) == false)
            {
                target.batch.overflow(now);
                return;
            }
            target.batch.renamed(std::move(old_text), std::move(new_text), now);
        }

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
            for (auto& [ticket, target] : watches)
            {
                static_cast<void>(ticket);
                release(*target);
            }
            if (queue >= 0)
                ::close(queue);
        }

        void run() noexcept;
        void loop();
        void wake() noexcept;
        [[nodiscard]] bool arm(int fd, std::uint32_t notes, std::uint64_t node) noexcept;
        // 폴더 하나를 열어 건다. 실패하면 0이고 `code`에 errno를 둔다.
        [[nodiscard]] std::uint64_t open_folder(watch& target, int base, const std::string& name, bool follow, std::uint64_t parent, std::u8string& error, int& code);
        // 건 폴더를 읽어 표를 채운다. `subtree`면 하위 폴더도 건다. `announce`면 읽은 것을 created로 낸다
        // (걸기 전에 새 폴더 안에 생긴 것이 이렇게 잡힌다).
        [[nodiscard]] bool populate(watch& target, std::uint64_t folder, bool announce, clock::time_point now, std::u8string& error);
        // 이름 하나의 내용을 볼지 정하고 그대로 맞춘다. 열 자리가 없으면 견주기로 돌린다.
        void watch_content(watch& target, std::uint64_t folder, const std::string& name, entry& item);
        void close_content(watch& target, entry& item) noexcept;
        void close_entry(watch& target, entry& item) noexcept;
        void drop_folder(watch& target, std::uint64_t folder) noexcept;
        void collect_below(const watch& target, std::uint64_t folder, std::unordered_set<std::uint64_t>& below) const;
        void release(watch& target) noexcept;
        void lose(watch& target, std::u8string error, clock::time_point now);
        void collect(const struct kevent& event);
        void scan(watch& target, clock::time_point now);
        void touch_files(watch& target, clock::time_point now);
        void poll(watch& target, clock::time_point now);
        [[nodiscard]] file_watch_ticket open(std::u8string_view path, file_watch_scope scope, std::u8string& error);
        void fail_pending_adds();
        [[nodiscard]] std::optional<clock::time_point> nearest() const;

        file_watcher_config config {};
        file_watch_timing timing {};
        int queue { -1 };
        std::size_t file_budget { file_budget_floor };
        std::thread thread {};
        std::atomic<std::thread::id> thread_id {};

        // 아래 다섯은 `mutex`가 지킨다.
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
        // `kevent`의 `udata`는 표 번호다. 번호 → 감시 표다. 번호는 다시 쓰지 않아, 닫힌 표의 늦은 알림은 찾지 못하고 버린다.
        std::unordered_map<std::uint64_t, std::uint64_t> owners {};
        std::uint64_t next_node { 0 };
        std::size_t files_open { 0 };
        std::optional<clock::time_point> next_poll {};
    };

    void file_watcher::engine::wake() noexcept
    {
        struct kevent change {};
        EV_SET(&change, wake_ident, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
        static_cast<void>(::kevent(queue, &change, 1, nullptr, 0, nullptr));
    }

    bool file_watcher::engine::arm(const int fd, const std::uint32_t notes, const std::uint64_t node) noexcept
    {
        struct kevent change {};
        EV_SET(&change, static_cast<std::uintptr_t>(fd), EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR, notes, 0, node_data(node));
        return ::kevent(queue, &change, 1, nullptr, 0, nullptr) == 0;
    }

    std::uint64_t file_watcher::engine::open_folder(watch& target, const int base, const std::string& name, const bool follow, const std::uint64_t parent, std::u8string& error, int& code)
    {
        const int fd { ::openat(base, name.c_str(), O_EVTONLY | O_DIRECTORY | O_CLOEXEC | (follow ? 0 : O_NOFOLLOW)) };
        if (fd < 0)
        {
            code = errno;
            if (code == EMFILE || code == ENFILE)
                error = macos::errno_error(u8"Too many open files to watch the folder");
            else
                error = macos::errno_error(u8"Failed to watch the folder");
            return 0;
        }
        const std::uint64_t node { ++next_node };
        if (arm(fd, folder_notes, node) == false)
        {
            code = errno;
            error = macos::errno_error(u8"The system refused to watch the folder");
            ::close(fd);
            return 0;
        }
        folder_node& folder { target.folders[node] };
        folder.fd = fd;
        folder.parent = parent;
        folder.name = follow ? std::string {} : name;
        owners[node] = target.ticket.value;
        return node;
    }

    bool file_watcher::engine::populate(watch& target, const std::uint64_t folder, const bool announce, const clock::time_point now, std::u8string& error)
    {
        const auto found { target.folders.find(folder) };
        if (found == target.folders.end())
            return true;
        // 이름 바꿈의 짝은 다른 폴더 표를 더하는 사이에도 그대로다 (unordered_map의 원소는 자리를 옮기지 않는다).
        folder_node& node { found->second };
        // 걸기 전에 사라졌다. 사라진 것은 부모 폴더의 알림으로 따로 온다.
        const std::optional<listing> items { read_folder(node.fd) };
        if (items.has_value() == false)
            return true;
        const std::string base { path_of(target, folder) };
        for (const auto& [name, info] : *items)
        {
            entry& item { node.children[name] };
            item.inode = info.inode;
            item.kind = info.kind;
            if (announce)
                report(target, file_change_kind::created, join(base, name), now);
            watch_content(target, folder, name, item);
            if (info.kind != item_kind::folder || target.subtree == false)
                continue;
            int code { 0 };
            const std::uint64_t child { open_folder(target, node.fd, name, false, folder, error, code) };
            if (child == 0)
            {
                // 걸기 전에 사라졌거나 다른 것으로 바뀌었다. 부모 폴더의 알림이 따로 온다.
                if (code == ENOENT || code == ENOTDIR || code == ELOOP)
                {
                    error.clear();
                    continue;
                }
                return false;
            }
            item.node = child;
            if (populate(target, child, announce, now, error) == false)
                return false;
        }
        return true;
    }

    void file_watcher::engine::watch_content(watch& target, const std::uint64_t folder, const std::string& name, entry& item)
    {
        if (item.kind != item_kind::regular || wants_content(target, folder, name) == false)
        {
            close_content(target, item);
            return;
        }
        if (item.fd >= 0)
            return;
        const auto found { target.folders.find(folder) };
        if (found == target.folders.end())
            return;
        const int base { found->second.fd };
        // 상한을 넘겼거나 열지 못하면(지워짐, 권한, 프로세스 상한) 견주기로 둔다. 사라졌으면 부모 폴더의 알림이 거둔다.
        if (files_open < file_budget)
        {
            const int fd { ::openat(base, name.c_str(), O_EVTONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) };
            if (fd >= 0)
            {
                const std::uint64_t node { ++next_node };
                struct stat information {};
                if (arm(fd, file_notes, node) && ::fstat(fd, &information) == 0)
                {
                    item.fd = fd;
                    item.node = node;
                    item.size = static_cast<std::int64_t>(information.st_size);
                    item.modified = information.st_mtimespec;
                    if (item.polled)
                    {
                        item.polled = false;
                        --target.polled;
                    }
                    target.files[node] = file_node { folder, name };
                    owners[node] = target.ticket.value;
                    ++files_open;
                    return;
                }
                ::close(fd);
            }
        }
        if (item.polled)
            return;
        struct stat information {};
        if (::fstatat(base, name.c_str(), &information, AT_SYMLINK_NOFOLLOW) == 0)
        {
            item.size = static_cast<std::int64_t>(information.st_size);
            item.modified = information.st_mtimespec;
        }
        item.polled = true;
        ++target.polled;
    }

    void file_watcher::engine::close_content(watch& target, entry& item) noexcept
    {
        if (item.fd >= 0)
        {
            // 닫으면 그 알림도 kqueue에서 내려간다.
            ::close(item.fd);
            target.files.erase(item.node);
            owners.erase(item.node);
            --files_open;
            item.fd = -1;
            item.node = 0;
        }
        if (item.polled)
        {
            item.polled = false;
            --target.polled;
        }
    }

    void file_watcher::engine::close_entry(watch& target, entry& item) noexcept
    {
        if (item.kind == item_kind::folder && item.node != 0)
        {
            drop_folder(target, item.node);
            item.node = 0;
            return;
        }
        close_content(target, item);
    }

    void file_watcher::engine::drop_folder(watch& target, const std::uint64_t folder) noexcept
    {
        const auto found { target.folders.find(folder) };
        if (found == target.folders.end())
            return;
        for (auto& [name, item] : found->second.children)
        {
            static_cast<void>(name);
            close_entry(target, item);
        }
        ::close(found->second.fd);
        owners.erase(folder);
        target.folders.erase(found);
    }

    void file_watcher::engine::collect_below(const watch& target, const std::uint64_t folder, std::unordered_set<std::uint64_t>& below) const
    {
        const auto found { target.folders.find(folder) };
        if (found == target.folders.end() || below.insert(folder).second == false)
            return;
        for (const auto& [name, item] : found->second.children)
        {
            static_cast<void>(name);
            if (item.kind == item_kind::folder && item.node != 0)
                collect_below(target, item.node, below);
        }
    }

    void file_watcher::engine::release(watch& target) noexcept
    {
        if (target.root_id != 0)
            drop_folder(target, target.root_id);
        target.root_id = 0;
        target.dirty.clear();
        target.touched.clear();
        target.lost.reset();
    }

    void file_watcher::engine::lose(watch& target, std::u8string error, const clock::time_point now)
    {
        release(target);
        target.batch.finish(file_watch_end::lost, std::move(error), now);
    }

    file_watch_ticket file_watcher::engine::open(const std::u8string_view path, const file_watch_scope scope, std::u8string& error)
    {
        std::string full { macos::narrow(path) };
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
            error = macos::errno_error(scope == file_watch_scope::file ? u8"The folder of the file does not exist" : u8"The folder does not exist");
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
            target->only = name;
            target->batch.set_name_filter([wanted = macos::widen(name)](const std::u8string_view candidate) { return candidate == wanted; });
        }
        // 표 번호가 먼저 있어야 건 표마다 주인을 적는다.
        {
            const std::lock_guard lock { mutex };
            target->ticket = file_watch_ticket { ++next_ticket };
        }
        // 걸고 나서 읽는다. 그 사이의 변경은 표에 들거나 알림으로 온다. `add`가 돌아온 뒤의 것은 놓치지 않는다.
        int code { 0 };
        target->root_id = open_folder(*target, AT_FDCWD, folder, true, 0, error, code);
        if (target->root_id == 0)
            return {};
        if (populate(*target, target->root_id, false, clock::now(), error) == false)
        {
            release(*target);
            return {};
        }
        const file_watch_ticket ticket { target->ticket };
        watches.emplace(ticket.value, std::move(target));
        return ticket;
    }

    void file_watcher::engine::collect(const struct kevent& event)
    {
        if (event.filter != EVFILT_VNODE)
            return;
        const std::uint64_t node { static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(event.udata)) };
        const auto owner { owners.find(node) };
        if (owner == owners.end())
            return;
        const auto found { watches.find(owner->second) };
        if (found == watches.end() || found->second->batch.ending())
            return;
        watch& target { *found->second };
        const std::uint32_t notes { event.fflags };
        if (target.folders.contains(node))
        {
            // 하위 폴더 자신의 지움·이름 바꿈은 부모 폴더의 `NOTE_WRITE`로 함께 온다. 뿌리의 것만 감시의 끝이다.
            if (node == target.root_id && (notes & (NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE)) != 0)
            {
                if (target.lost.has_value() == false)
                    target.lost = (notes & NOTE_REVOKE) != 0 ? u8"The volume of the watched folder was unmounted." : u8"The watched folder was removed or moved.";
                return;
            }
            if ((notes & (NOTE_WRITE | NOTE_EXTEND | NOTE_LINK)) != 0)
                target.dirty.push_back(node);
            return;
        }
        const auto file { target.files.find(node) };
        if (file == target.files.end())
            return;
        // 파일이 지워졌거나 옮겨졌다. 부모 폴더를 다시 읽으면 무엇이 되었는지 안다.
        if ((notes & (NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE)) != 0)
            target.dirty.push_back(file->second.folder);
        if ((notes & (NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB)) != 0)
            target.touched.emplace_back(node, notes);
    }

    void file_watcher::engine::scan(watch& target, const clock::time_point now)
    {
        std::vector<std::uint64_t> dirty { std::exchange(target.dirty, {}) };
        std::sort(dirty.begin(), dirty.end());
        dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());

        // 사라진 이름은 옛 경로를 지금 적는다. 새 이름의 경로는 폴더를 옮긴 뒤에 적는다. 묶음은 그 사이의 순 변화다.
        struct gone_item
        {
            std::uint64_t folder { 0 };
            std::string name {};
            entry item {};
            std::string path {};
            bool paired { false };
            bool skip { false };
        };
        struct new_item
        {
            std::uint64_t folder { 0 };
            std::string name {};
            item_info info {};
            std::optional<std::size_t> pair {};
            bool skip { false };
        };
        std::vector<gone_item> gone {};
        std::vector<new_item> appeared {};
        for (const std::uint64_t folder : dirty)
        {
            const auto found { target.folders.find(folder) };
            if (found == target.folders.end())
                continue;
            const std::optional<listing> items { read_folder(found->second.fd) };
            if (items.has_value() == false)
                continue;
            const std::string base { path_of(target, folder) };
            for (const auto& [name, item] : found->second.children)
            {
                const auto now_item { items->find(name) };
                if (now_item == items->end() || now_item->second.inode != item.inode || now_item->second.kind != item.kind)
                    gone.push_back(gone_item { folder, name, item, join(base, name) });
            }
            for (const auto& [name, info] : *items)
            {
                const auto before { found->second.children.find(name) };
                if (before == found->second.children.end() || before->second.inode != info.inode || before->second.kind != info.kind)
                    appeared.push_back(new_item { folder, name, info });
            }
        }
        if (gone.empty() && appeared.empty())
            return;

        // 같은 번호·같은 종류면 이름 바꿈이다. 짝 없이 사라진 하위 폴더는 감시 밖으로 나갔다. 그 아래에서 읽은 것은 감시 밖의
        // 일이라 거르고, 거른 만큼 짝이 달라질 수 있어 다시 짓는다 (거를 것이 줄기만 하므로 끝난다).
        for (;;)
        {
            std::unordered_multimap<std::uint64_t, std::size_t> by_inode {};
            for (std::size_t index { 0 }; index < gone.size(); ++index)
            {
                gone[index].paired = false;
                if (gone[index].skip == false)
                    by_inode.emplace(gone[index].item.inode, index);
            }
            for (new_item& added : appeared)
            {
                added.pair.reset();
                if (added.skip)
                    continue;
                const auto [first, last] { by_inode.equal_range(added.info.inode) };
                for (auto at { first }; at != last; ++at)
                {
                    gone_item& candidate { gone[at->second] };
                    if (candidate.paired || candidate.item.kind != added.info.kind)
                        continue;
                    candidate.paired = true;
                    added.pair = at->second;
                    break;
                }
            }
            std::unordered_set<std::uint64_t> leaving {};
            for (const gone_item& removed : gone)
            {
                if (removed.skip == false && removed.paired == false && removed.item.kind == item_kind::folder && removed.item.node != 0)
                    collect_below(target, removed.item.node, leaving);
            }
            bool filtered { false };
            for (gone_item& removed : gone)
            {
                if (removed.skip == false && leaving.contains(removed.folder))
                {
                    removed.skip = true;
                    filtered = true;
                }
            }
            for (new_item& added : appeared)
            {
                if (added.skip == false && leaving.contains(added.folder))
                {
                    added.skip = true;
                    filtered = true;
                }
            }
            if (filtered == false)
                break;
        }

        // 짝 없이 사라지고 짝 없이 선 자리는 고침이다 (지우고 다시 만듦).
        std::set<std::pair<std::uint64_t, std::string>> vacated {};
        std::set<std::pair<std::uint64_t, std::string>> filled {};
        for (const gone_item& removed : gone)
        {
            if (removed.skip == false && removed.paired == false)
                vacated.emplace(removed.folder, removed.name);
        }
        for (const new_item& added : appeared)
        {
            if (added.skip == false && added.pair.has_value() == false)
                filled.emplace(added.folder, added.name);
        }

        // 옛 자리를 비운다. 걸러진 것은 그 폴더와 함께 내려간다.
        for (const gone_item& removed : gone)
        {
            if (removed.skip)
                continue;
            if (const auto found { target.folders.find(removed.folder) }; found != target.folders.end())
                found->second.children.erase(removed.name);
        }
        // 지움을 이름 바꿈보다 먼저 낸다. 덮인 이름(`a.tmp` → `a`)의 removed는 뒤의 이름 바꿈이 합친다.
        for (gone_item& removed : gone)
        {
            if (removed.skip || removed.paired)
                continue;
            if (filled.contains({ removed.folder, removed.name }) == false)
                report(target, file_change_kind::removed, removed.path, now);
            close_entry(target, removed.item);
        }
        // 이름 바꿈: 옛 항목을 새 자리로 옮긴다. 연 descriptor는 그 파일·폴더를 따라가므로 그대로 쓴다.
        for (new_item& added : appeared)
        {
            if (added.skip || added.pair.has_value() == false)
                continue;
            gone_item& source { gone[*added.pair] };
            const auto found { target.folders.find(added.folder) };
            if (found == target.folders.end())
            {
                close_entry(target, source.item);
                continue;
            }
            entry& item { found->second.children[added.name] };
            item = std::exchange(source.item, entry {});
            if (item.kind == item_kind::folder && item.node != 0)
            {
                if (const auto moved { target.folders.find(item.node) }; moved != target.folders.end())
                {
                    moved->second.parent = added.folder;
                    moved->second.name = added.name;
                }
            }
            if (item.fd >= 0)
                target.files[item.node] = file_node { added.folder, added.name };
            // `file` 감시는 그 이름이 되었는지에 따라 내용을 볼지가 바뀐다.
            watch_content(target, added.folder, added.name, item);
        }
        for (const new_item& added : appeared)
        {
            if (added.skip || added.pair.has_value() == false)
                continue;
            report_renamed(target, gone[*added.pair].path, join(path_of(target, added.folder), added.name), now);
        }
        // 새것이다. `subtree`의 새 폴더는 걸고 나서 그 안을 읽어 created로 낸다. 겹친 것은 created+created로 합쳐진다.
        for (const new_item& added : appeared)
        {
            if (added.skip || added.pair.has_value())
                continue;
            const auto found { target.folders.find(added.folder) };
            if (found == target.folders.end())
                continue;
            const int base { found->second.fd };
            entry& item { found->second.children[added.name] };
            item = entry {};
            item.inode = added.info.inode;
            item.kind = added.info.kind;
            const bool replaced { vacated.contains({ added.folder, added.name }) };
            report(target, replaced ? file_change_kind::modified : file_change_kind::created, join(path_of(target, added.folder), added.name), now);
            watch_content(target, added.folder, added.name, item);
            if (item.kind != item_kind::folder || target.subtree == false)
                continue;
            std::u8string error {};
            int code { 0 };
            const std::uint64_t child { open_folder(target, base, added.name, false, added.folder, error, code) };
            if (child == 0)
            {
                if (code == ENOENT || code == ENOTDIR || code == ELOOP)
                    continue;
                // 일부만 보는 감시를 조용히 두지 않는다.
                lose(target, std::move(error), now);
                return;
            }
            item.node = child;
            if (populate(target, child, true, now, error) == false)
            {
                lose(target, std::move(error), now);
                return;
            }
        }
    }

    void file_watcher::engine::touch_files(watch& target, const clock::time_point now)
    {
        const std::vector<std::pair<std::uint64_t, std::uint32_t>> touched { std::exchange(target.touched, {}) };
        for (const auto& [node, notes] : touched)
        {
            // 이번 차례의 견줌이 닫은 파일은 그 자리의 고침·지움이 이미 났다.
            const auto file { target.files.find(node) };
            if (file == target.files.end())
                continue;
            const auto folder { target.folders.find(file->second.folder) };
            if (folder == target.folders.end())
                continue;
            const auto found { folder->second.children.find(file->second.name) };
            if (found == folder->second.children.end() || found->second.node != node)
                continue;
            entry& item { found->second };
            bool changed { (notes & (NOTE_WRITE | NOTE_EXTEND)) != 0 };
            struct stat information {};
            if (::fstat(item.fd, &information) == 0)
            {
                const auto size { static_cast<std::int64_t>(information.st_size) };
                if (size != item.size || same_time(information.st_mtimespec, item.modified) == false)
                    changed = true;
                item.size = size;
                item.modified = information.st_mtimespec;
            }
            if (changed)
                report(target, file_change_kind::modified, join(path_of(target, file->second.folder), file->second.name), now);
        }
    }

    void file_watcher::engine::poll(watch& target, const clock::time_point now)
    {
        for (auto& [id, folder] : target.folders)
        {
            for (auto& [name, item] : folder.children)
            {
                if (item.polled == false)
                    continue;
                struct stat information {};
                // 없어졌거나 다른 것이 섰다. 폴더를 다시 읽어 가린다.
                if (::fstatat(folder.fd, name.c_str(), &information, AT_SYMLINK_NOFOLLOW) != 0 || static_cast<std::uint64_t>(information.st_ino) != item.inode)
                {
                    target.dirty.push_back(id);
                    continue;
                }
                const auto size { static_cast<std::int64_t>(information.st_size) };
                if (size != item.size || same_time(information.st_mtimespec, item.modified) == false)
                {
                    item.size = size;
                    item.modified = information.st_mtimespec;
                    report(target, file_change_kind::modified, join(path_of(target, id), name), now);
                }
                // 다른 파일이 닫혀 자리가 났으면 열어 알림으로 돌린다.
                if (files_open < file_budget)
                    watch_content(target, id, name, item);
            }
        }
        if (target.dirty.empty() == false)
            scan(target, now);
    }

    std::optional<clock::time_point> file_watcher::engine::nearest() const
    {
        std::optional<clock::time_point> nearest { next_poll };
        for (const auto& [ticket, target] : watches)
        {
            static_cast<void>(ticket);
            const std::optional<clock::time_point> due { target->batch.due_at() };
            if (due.has_value() && (nearest.has_value() == false || *due < *nearest))
                nearest = due;
        }
        return nearest;
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
        std::vector<struct kevent> events(static_cast<std::size_t>(event_capacity));
        for (;;)
        {
            timespec wait {};
            const timespec* timeout { nullptr };
            if (const std::optional<clock::time_point> due { nearest() }; due.has_value())
            {
                const clock::time_point now { clock::now() };
                const auto remaining { *due <= now ? std::chrono::nanoseconds::zero() : std::chrono::ceil<std::chrono::nanoseconds>(*due - now) };
                const auto bounded { std::min<std::chrono::nanoseconds>(remaining, std::chrono::hours { 24 }) };
                const auto seconds { std::chrono::duration_cast<std::chrono::seconds>(bounded) };
                wait.tv_sec = static_cast<time_t>(seconds.count());
                wait.tv_nsec = static_cast<long>((bounded - seconds).count());
                timeout = &wait;
            }
            // 가득 차 돌아오면 남은 것도 마저 받는다. 폴더를 건너간 이름 바꿈의 두 폴더를 한 차례에 견주려는 것이다.
            int count { ::kevent(queue, nullptr, 0, events.data(), event_capacity, timeout) };
            while (count > 0)
            {
                for (int index { 0 }; index < count; ++index)
                    collect(events[static_cast<std::size_t>(index)]);
                if (count < event_capacity)
                    break;
                const timespec zero {};
                count = ::kevent(queue, nullptr, 0, events.data(), event_capacity, &zero);
            }
            const clock::time_point now { clock::now() };
            for (auto& [ticket, target] : watches)
            {
                static_cast<void>(ticket);
                if (target->batch.ending())
                    continue;
                if (target->lost.has_value())
                {
                    std::u8string reason { std::move(*target->lost) };
                    lose(*target, std::move(reason), now);
                    continue;
                }
                if (target->dirty.empty() == false)
                    scan(*target, now);
                if (target->batch.ending() == false && target->touched.empty() == false)
                    touch_files(*target, now);
            }
            const bool polling { std::any_of(watches.begin(), watches.end(), [](const auto& each) { return each.second->polled > 0 && each.second->batch.ending() == false; }) };
            if (polling == false)
                next_poll.reset();
            else if (next_poll.has_value() == false)
                next_poll = now + poll_interval;
            else if (*next_poll <= now)
            {
                for (auto& [ticket, target] : watches)
                {
                    static_cast<void>(ticket);
                    if (target->polled > 0 && target->batch.ending() == false)
                        poll(*target, now);
                }
                next_poll = now + poll_interval;
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
                {
                    release(*at->second);
                    at = watches.erase(at);
                }
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
        std::unique_ptr<engine> owned {};
        try
        {
            owned = std::make_unique<engine>(std::move(configuration));
        }
        catch (...)
        {
            error = u8"Failed to prepare the file watcher.";
            return nullptr;
        }
        owned->queue = ::kqueue();
        if (owned->queue < 0)
        {
            error = macos::errno_error(u8"Failed to create the kqueue");
            return nullptr;
        }
        static_cast<void>(::fcntl(owned->queue, F_SETFD, FD_CLOEXEC));
        struct kevent change {};
        EV_SET(&change, wake_ident, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        if (::kevent(owned->queue, &change, 1, nullptr, 0, nullptr) != 0)
        {
            error = macos::errno_error(u8"Failed to create the wake event");
            return nullptr;
        }
        owned->file_budget = file_budget_ceiling;
        if (rlimit limit {}; ::getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY)
        {
            const rlim_t quarter { std::min<rlim_t>(limit.rlim_cur / 4, file_budget_ceiling) };
            owned->file_budget = std::max(static_cast<std::size_t>(quarter), file_budget_floor);
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
        // kqueue에는 OS가 쥔 우리 버퍼가 없다. descriptor를 닫으면 알림이 모두 내려가므로 `stop_budget`을 기다릴 것이 없다.
        for (auto& [ticket, target] : state.watches)
        {
            static_cast<void>(ticket);
            state.release(*target);
        }
        state.watches.clear();
        state.owners.clear();
        if (state.queue >= 0)
            ::close(state.queue);
        state.queue = -1;
        state.stopped = true;
    }
}
