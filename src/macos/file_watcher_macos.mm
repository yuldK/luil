#include "luil/app/file_watcher.h"

#include "host/file_watch_batch.h"
#include "macos/macos_text.h"

#import <Foundation/Foundation.h>

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// 파일 감시의 macOS 쪽이다 (file-watch-design.md). 감시마다 FSEvents stream 하나를 파일 단위 이벤트로 건다.
//  - FSEvents는 늘 그 아래 전부를 본다. `folder`·`file`은 바로 아래 이름만 남긴다.
//  - stream은 감시자의 직렬 dispatch queue에서 부른다. 콜백은 이벤트마다 지금 그 경로를 `lstat`으로 보고 감시 thread에 넘긴다.
//    묶음 짓기·`deliver`는 감시자가 소유한 thread 하나가 한다 (Android의 `poll` 자리가 조건 변수다).
//  - FSEvents의 표지는 같은 경로에서 쌓인다. 만든 뒤 고쳐도 `ItemCreated`가 남고, 이름을 바꿔 온 파일을 고쳐도
//    `ItemRenamed`가 남는다. 그래서 표지만으로 종류를 정하지 않고, 지금 있는지와 파일 번호(inode)와 이 감시가 본 이름으로 가른다.
namespace luil {
    namespace {
        using clock = file_watch_batch::clock;

        // 첫 이벤트는 곧바로 오고(`NoDefer`) 이어지는 것은 이만큼 모아 온다. 묶음의 `settle`이 진짜 디바운스다.
        constexpr CFTimeInterval stream_latency_seconds { 0.01 };
        constexpr FSEventStreamCreateFlags stream_flags {
            kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer | kFSEventStreamCreateFlagWatchRoot
            | kFSEventStreamCreateFlagUseCFTypes | kFSEventStreamCreateFlagUseExtendedData,
        };
        // 내용·이름의 변경이다. 속성만 바뀐 것(`InodeMetaMod`·`XattrMod`·`FinderInfoMod`·`ChangeOwner`)은 다른 플랫폼처럼 받지 않는다.
        //  - 새 파일의 첫 이벤트가 `XattrMod`만 들고 `ItemCreated`보다 먼저 올 때가 있다(실측). 그것을 고침으로 내지 않게 하는 자리이기도 하다.
        constexpr FSEventStreamEventFlags content_flags {
            kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRemoved | kFSEventStreamEventFlagItemRenamed | kFSEventStreamEventFlagItemModified,
        };
        // 이 감시가 본 이름의 상한이다. 넘으면 잊는다. 잊은 이름의 다음 변경은 쌓인 표지 때문에 modified 대신 created로 올 수 있다.
        constexpr std::size_t seen_limit { 65536 };

        // 콜백이 감시 thread에 넘기는 원 이벤트 하나다.
        struct raw_event
        {
            std::uint64_t ticket { 0 };
            // 절대 경로다. UTF-8로 옮길 수 없었으면 비었고 `unnamed`가 참이다.
            std::string path {};
            bool unnamed { false };
            FSEventStreamEventFlags flags { 0 };
            // 이벤트가 가리킨 파일의 번호다.
            std::uint64_t inode { 0 };
            // 콜백 때 그 경로에 무엇이 있었는가.
            bool exists { false };
            std::uint64_t current_inode { 0 };
            // 콜백 때 그 경로의 것의 상태가 바뀐 때(ctime)다 (`exists`일 때만). 만듦·고침·이름 바꿈·복사가 모두 새로 하고 앱이 되돌릴 수 없다.
            timespec changed {};
        };

        // `earlier`가 `later`보다 분명히 앞인가. 나노초가 0인 시각은 거친 파일 시스템(HFS+의 1초, FAT의 2초)의 것으로 보고 2초를 둔다.
        [[nodiscard]] bool clearly_before(const timespec& earlier, const timespec& later) noexcept
        {
            if (earlier.tv_sec == 0 && earlier.tv_nsec == 0)
                return false;
            if (earlier.tv_nsec == 0)
                return earlier.tv_sec + 2 < later.tv_sec;
            return earlier.tv_sec < later.tv_sec || (earlier.tv_sec == later.tv_sec && earlier.tv_nsec < later.tv_nsec);
        }

        struct add_request
        {
            std::u8string path {};
            file_watch_scope scope { file_watch_scope::folder };
            bool done { false };
            file_watch_ticket ticket {};
            std::u8string error {};
        };

        // 콜백과 감시 thread가 함께 쥐는 몫이다. `stop`이 예산을 넘겨 콜백이 늦게 와도 이것은 산다.
        struct channel
        {
            std::mutex mutex {};
            std::condition_variable wake {};
            std::condition_variable answered {};
            std::vector<raw_event> events {};
            std::deque<add_request*> adds {};
            std::vector<file_watch_ticket> removals {};
            bool stopping { false };
            bool signalled { false };
            std::uint64_t next_ticket { 0 };
        };

        // stream의 `info`다. stream이 없어질 때 FSEvents가 `release`로 지운다.
        struct stream_source
        {
            std::shared_ptr<channel> target {};
            std::uint64_t ticket { 0 };
        };

        void release_source(const void* const info)
        {
            delete static_cast<const stream_source*>(info);
        }

        [[nodiscard]] std::uint32_t cookie_of(const std::uint64_t inode) noexcept
        {
            return static_cast<std::uint32_t>(inode ^ (inode >> 32u));
        }

        void receive_events(ConstFSEventStreamRef, void* const info, const size_t count, void* const paths, const FSEventStreamEventFlags flags[], const FSEventStreamEventId[])
        {
            const auto* const source { static_cast<const stream_source*>(info) };
            std::vector<raw_event> events {};
            @autoreleasepool
            {
                NSArray* const list { (__bridge NSArray*)paths };
                events.reserve(count);
                for (size_t index { 0 }; index < count && index < list.count; ++index)
                {
                    raw_event event {};
                    event.ticket = source->ticket;
                    event.flags = flags[index];
                    NSDictionary* const data { list[index] };
                    if ([data isKindOfClass:[NSDictionary class]])
                    {
                        NSString* const path { data[(__bridge NSString*)kFSEventStreamEventExtendedDataPathKey] };
                        NSNumber* const inode { data[(__bridge NSString*)kFSEventStreamEventExtendedFileIDKey] };
                        const char* const bytes { [path isKindOfClass:[NSString class]] ? path.UTF8String : nullptr };
                        if (bytes != nullptr)
                            event.path = bytes;
                        else
                            event.unnamed = true;
                        if ([inode isKindOfClass:[NSNumber class]])
                            event.inode = inode.unsignedLongLongValue;
                    }
                    else
                        event.unnamed = true;
                    // 지금 있는가를 여기서 본다. 이벤트에 가장 가까운 때이고, 감시 thread가 파일 시스템에 막히지 않는다.
                    struct stat information {};
                    if (event.unnamed == false && ::lstat(event.path.c_str(), &information) == 0)
                    {
                        event.exists = true;
                        event.current_inode = static_cast<std::uint64_t>(information.st_ino);
                        event.changed = information.st_ctimespec;
                    }
                    events.push_back(std::move(event));
                }
            }
            channel& shared { *source->target };
            const std::lock_guard lock { shared.mutex };
            if (shared.stopping)
                return;
            shared.events.insert(shared.events.end(), std::make_move_iterator(events.begin()), std::make_move_iterator(events.end()));
            shared.wake.notify_one();
        }

        // 감시 하나다. 감시 thread만 만진다.
        struct watch
        {
            explicit watch(const file_watch_timing timing) noexcept
                : batch { timing }
            {
            }

            file_watch_ticket ticket {};
            // 감시하는 폴더의 정식 경로다 (`file`이면 부모 폴더). `/private/var`처럼 FSEvents가 주는 꼴이다.
            std::string root {};
            bool subtree { false };
            FSEventStreamRef stream { nullptr };
            // 이 감시가 있다고 본 이름과 그 파일 번호다. 쌓인 `ItemCreated`·`ItemRenamed`를 걸러 고침을 고침으로 낸다.
            std::unordered_map<std::string, std::uint64_t> seen {};
            // 바로 앞 이벤트가 이름 바꿈의 옛 이름이었으면 그 파일 번호다. 다음 이벤트가 같은 번호로 있으면 새 이름이다.
            std::optional<std::uint64_t> moved_from {};
            // stream을 세우기 직전의 벽시계다. 이보다 먼저 생긴 것의 만듦은 감시 전의 일이다.
            timespec started {};
            file_watch_batch batch;
        };

        // 감시 경로 기준 상대 경로다. 뿌리 자신이나 뿌리 밖이면 빈 값이다.
        [[nodiscard]] std::optional<std::string> relative_to(const std::string& root, const std::string& path)
        {
            const std::size_t prefix { root == "/" ? std::size_t { 1 } : root.size() + 1 };
            if (path.size() <= prefix || path.compare(0, root.size(), root) != 0 || path[prefix - 1] != '/')
                return std::nullopt;
            return path.substr(prefix);
        }

        void forget_below(watch& target, const std::string& folder)
        {
            const std::string prefix { folder + "/" };
            std::erase_if(target.seen, [&prefix](const auto& entry) { return entry.first.compare(0, prefix.size(), prefix) == 0; });
        }
    }

    struct file_watcher::engine
    {
        explicit engine(file_watcher_config configuration)
            : config { std::move(configuration) }
            , timing { config.settle, config.max_delay, config.max_changes }
            , shared { std::make_shared<channel>() }
        {
        }

        void run() noexcept;
        void loop();
        [[nodiscard]] file_watch_ticket open(std::u8string_view path, file_watch_scope scope, std::u8string& error);
        void handle(watch& target, const raw_event& event, clock::time_point now);
        void close_stream(watch& target);
        // FSEvents가 아직 건네지 않은 이벤트를 받아 처리한다. 끝 묶음에 그때까지의 변경이 실리게 한다.
        void flush(watch& target, clock::time_point now);
        void handle_pending(clock::time_point now);
        void lose(watch& target, std::u8string error, clock::time_point now);
        void fail_pending_adds();
        [[nodiscard]] std::optional<clock::time_point> nearest_due() const;

        file_watcher_config config {};
        file_watch_timing timing {};
        std::shared_ptr<channel> shared {};
        dispatch_queue_t queue { nil };
        std::thread thread {};
        std::atomic<std::thread::id> thread_id {};

        std::mutex stop_mutex {};
        bool stopped { false };

        // 감시 thread의 것이다. `stop`은 join한 뒤에 만진다.
        std::map<std::uint64_t, std::unique_ptr<watch>> watches {};
    };

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
        // FSEvents는 링크를 푼 정식 경로(`/var` → `/private/var`, 디스크의 대소문자)로 알린다. 같은 꼴로 견주려고 커널에 묻는다.
        const int descriptor { ::open(folder.c_str(), O_EVTONLY | O_DIRECTORY | O_CLOEXEC) };
        if (descriptor < 0)
        {
            error = macos::errno_error(u8"Failed to open the folder");
            return {};
        }
        char canonical[PATH_MAX] {};
        const bool resolved { ::fcntl(descriptor, F_GETPATH, canonical) == 0 };
        ::close(descriptor);
        if (resolved == false)
        {
            error = macos::errno_error(u8"Failed to resolve the folder path");
            return {};
        }

        auto target { std::make_unique<watch>(timing) };
        target->root = canonical;
        target->subtree = scope == file_watch_scope::subtree;
        if (scope == file_watch_scope::file)
        {
            target->batch.set_name_filter([wanted = macos::widen(name)](const std::u8string_view candidate) { return candidate == wanted; });
        }
        std::uint64_t ticket_value { 0 };
        {
            const std::lock_guard lock { shared->mutex };
            ticket_value = ++shared->next_ticket;
        }
        target->ticket = file_watch_ticket { ticket_value };
        static_cast<void>(::clock_gettime(CLOCK_REALTIME, &target->started));

        @autoreleasepool
        {
            NSString* const root_text { [NSString stringWithUTF8String:canonical] };
            if (root_text == nil)
            {
                error = u8"The folder path is not valid UTF-8.";
                return {};
            }
            FSEventStreamContext context {};
            context.info = new stream_source { shared, ticket_value };
            context.release = release_source;
            target->stream = FSEventStreamCreate(nullptr, receive_events, &context, (__bridge CFArrayRef) @[ root_text ], kFSEventStreamEventIdSinceNow, stream_latency_seconds, stream_flags);
            if (target->stream == nullptr)
            {
                release_source(context.info);
                error = u8"The system refused to watch the folder.";
                return {};
            }
        }
        FSEventStreamSetDispatchQueue(target->stream, queue);
        // 돌아오면 걸렸다. 이 뒤의 변경은 콜백으로 온다.
        if (FSEventStreamStart(target->stream) == false)
        {
            FSEventStreamInvalidate(target->stream);
            FSEventStreamRelease(target->stream);
            error = u8"The system refused to start watching the folder.";
            return {};
        }
        // `SinceNow`여도 fseventsd가 아직 건네지 않은 `add` 앞의 변경이 뒤늦게 온다. 바쁠 때 실측했다.
        // 지금까지 쌓인 것을 받아 버린다. fseventsd의 밀린 몫까지 다 비우지는 못해, 뒤늦은 만듦은 `handle`이 생긴 때로 한 번 더 거른다.
        FSEventStreamFlushSync(target->stream);
        {
            const std::lock_guard lock { shared->mutex };
            std::erase_if(shared->events, [ticket_value](const raw_event& event) { return event.ticket == ticket_value; });
        }
        const file_watch_ticket ticket { target->ticket };
        watches.emplace(ticket.value, std::move(target));
        return ticket;
    }

    void file_watcher::engine::close_stream(watch& target)
    {
        FSEventStreamRef const stream { std::exchange(target.stream, nullptr) };
        if (stream == nullptr)
            return;
        // 콜백과 같은 queue에서 푼다. 도는 콜백과 엇갈리지 않고, 푼 뒤로는 콜백이 없다.
        dispatch_async(queue, ^{
            FSEventStreamStop(stream);
            FSEventStreamInvalidate(stream);
            FSEventStreamRelease(stream);
        });
    }

    void file_watcher::engine::flush(watch& target, const clock::time_point now)
    {
        if (target.stream == nullptr)
            return;
        // fseventsd를 거쳐 오는 이벤트는 바쁠 때 수백 ms 늦는다. 그동안 쌓인 것을 콜백으로 받을 때까지 막힌다.
        FSEventStreamFlushSync(target.stream);
        handle_pending(now);
    }

    void file_watcher::engine::handle_pending(const clock::time_point now)
    {
        std::vector<raw_event> events {};
        {
            const std::lock_guard lock { shared->mutex };
            events.swap(shared->events);
        }
        for (const raw_event& event : events)
        {
            const auto found { watches.find(event.ticket) };
            if (found != watches.end())
                handle(*found->second, event, now);
        }
    }

    void file_watcher::engine::lose(watch& target, std::u8string error, const clock::time_point now)
    {
        close_stream(target);
        target.batch.finish(file_watch_end::lost, std::move(error), now);
    }

    void file_watcher::engine::handle(watch& target, const raw_event& event, const clock::time_point now)
    {
        if (target.batch.ending())
            return;
        const FSEventStreamEventFlags flags { event.flags };
        if ((flags & kFSEventStreamEventFlagRootChanged) != 0)
        {
            lose(target, u8"The watched folder was removed or moved.", now);
            return;
        }
        if ((flags & kFSEventStreamEventFlagUnmount) != 0 && event.path == target.root)
        {
            lose(target, u8"The volume of the watched folder was unmounted.", now);
            return;
        }
        // OS가 이벤트를 버렸거나 한 폴더의 것을 합쳐 버렸다. 다시 훑게 한다.
        if ((flags & (kFSEventStreamEventFlagUserDropped | kFSEventStreamEventFlagKernelDropped | kFSEventStreamEventFlagMustScanSubDirs)) != 0)
        {
            target.batch.overflow(now);
            target.seen.clear();
            target.moved_from.reset();
            return;
        }
        if ((flags & (kFSEventStreamEventFlagMount | kFSEventStreamEventFlagHistoryDone)) != 0)
            return;
        if (event.unnamed)
        {
            target.batch.overflow(now);
            target.moved_from.reset();
            return;
        }
        // 뿌리 자신의 알림(속성·만든 때)과 뿌리 밖은 감시의 항목이 아니다.
        const std::optional<std::string> relative { relative_to(target.root, event.path) };
        if (relative.has_value() == false)
            return;
        if (target.subtree == false && relative->find('/') != std::string::npos)
            return;
        if ((flags & content_flags) == 0)
            return;

        // `add` 앞의 일이 뒤늦게 온 것은 버린다. 이름 바꿈의 짝은 건드리지 않는다.
        //  - 지금 없는 이름의 만듦뿐인 이벤트: 그 뒤의 지움·이름 바꿈은 제 이벤트로 온다.
        //  - 처음 보는 이름인데 상태가 감시 전부터 그대로다: 그 뒤로 아무것도 바뀌지 않았다. Finder 복사처럼 만든 때·고친 때를
        //    옮겨 와도 ctime은 새로 서므로 감시 뒤의 만듦을 버리지 않는다.
        const bool pairs { event.exists && target.moved_from.has_value() && *target.moved_from == event.inode };
        if (event.exists == false && (flags & content_flags) == kFSEventStreamEventFlagItemCreated)
            return;
        if (event.exists && pairs == false && target.seen.contains(*relative) == false && clearly_before(event.changed, target.started))
        {
            if (target.seen.size() < seen_limit)
                target.seen[*relative] = event.current_inode;
            return;
        }

        const std::u8string reported { macos::widen(*relative) };
        target.moved_from.reset();
        if (event.exists)
        {
            if (pairs)
                target.batch.moved_to(reported, cookie_of(event.inode), now);
            // 본 이름이면 고침이다. 파일 번호가 바뀌었어도(지우고 다시 만듦, 덮어쓴 이름 바꿈) 순 변화는 고침이다.
            else if (target.seen.contains(*relative))
                target.batch.modified(reported, now);
            else if ((flags & (kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRenamed)) != 0)
                target.batch.created(reported, now);
            else
                target.batch.modified(reported, now);
            if (target.seen.size() >= seen_limit && target.seen.contains(*relative) == false)
                target.seen.clear();
            target.seen[*relative] = event.current_inode;
            return;
        }
        target.seen.erase(*relative);
        if ((flags & kFSEventStreamEventFlagItemIsDir) != 0)
            forget_below(target, *relative);
        // 지금 없는 이름이다. 이름 바꿈의 표지가 있으면 옛 이름이고, 다음 이벤트가 그 새 이름일 수 있다.
        if ((flags & kFSEventStreamEventFlagItemRenamed) != 0)
        {
            target.batch.moved_from(reported, cookie_of(event.inode), now);
            target.moved_from = event.inode;
        }
        else
            target.batch.removed(reported, now);
    }

    std::optional<clock::time_point> file_watcher::engine::nearest_due() const
    {
        std::optional<clock::time_point> nearest {};
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
        const std::lock_guard lock { shared->mutex };
        shared->stopping = true;
        for (add_request* const request : shared->adds)
        {
            request->error = u8"The file watcher stopped.";
            request->done = true;
        }
        shared->adds.clear();
        shared->answered.notify_all();
    }

    void file_watcher::engine::loop()
    {
        channel& state { *shared };
        for (;;)
        {
            const std::optional<clock::time_point> due { nearest_due() };
            std::vector<raw_event> events {};
            std::deque<add_request*> new_adds {};
            std::vector<file_watch_ticket> new_removals {};
            {
                std::unique_lock lock { state.mutex };
                const auto ready = [&state] { return state.stopping || state.signalled || state.events.empty() == false || state.adds.empty() == false || state.removals.empty() == false; };
                if (due.has_value())
                    state.wake.wait_until(lock, *due, ready);
                else
                    state.wake.wait(lock, ready);
                if (state.stopping)
                    return;
                state.signalled = false;
                events.swap(state.events);
                new_adds.swap(state.adds);
                new_removals.swap(state.removals);
            }
            const clock::time_point now { clock::now() };
            for (const raw_event& event : events)
            {
                const auto found { watches.find(event.ticket) };
                if (found != watches.end())
                    handle(*found->second, event, now);
            }
            events.clear();
            for (add_request* const request : new_adds)
            {
                std::u8string error {};
                const file_watch_ticket ticket { open(request->path, request->scope, error) };
                const std::lock_guard lock { state.mutex };
                request->ticket = ticket;
                request->error = std::move(error);
                request->done = true;
            }
            if (new_adds.empty() == false)
                state.answered.notify_all();
            for (const file_watch_ticket ticket : new_removals)
            {
                const auto found { watches.find(ticket.value) };
                if (found == watches.end() || found->second->batch.ending())
                    continue;
                flush(*found->second, now);
                if (found->second->batch.ending())
                    continue;
                close_stream(*found->second);
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
        owned->queue = dispatch_queue_create("luil.file_watcher", DISPATCH_QUEUE_SERIAL);
        if (owned->queue == nil)
        {
            error = u8"Failed to create the file watcher queue.";
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
        channel& shared { *state.shared };
        if (std::this_thread::get_id() == state.thread_id.load())
        {
            // `deliver` 안이다. thread가 스스로 건다.
            {
                const std::lock_guard lock { shared.mutex };
                if (shared.stopping)
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
        std::unique_lock lock { shared.mutex };
        if (shared.stopping)
        {
            error = u8"The file watcher stopped.";
            return {};
        }
        shared.adds.push_back(&request);
        shared.signalled = true;
        shared.wake.notify_one();
        shared.answered.wait(lock, [&request] { return request.done; });
        if (request.ticket == file_watch_ticket {})
            error = std::move(request.error);
        return request.ticket;
    }

    void file_watcher::remove(const file_watch_ticket ticket) noexcept
    {
        if (ticket == file_watch_ticket {})
            return;
        channel& shared { *engine_->shared };
        try
        {
            const std::lock_guard lock { shared.mutex };
            if (shared.stopping)
                return;
            shared.removals.push_back(ticket);
            shared.signalled = true;
        }
        catch (...)
        {
            return;
        }
        shared.wake.notify_one();
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
            const std::lock_guard lock { state.shared->mutex };
            state.shared->stopping = true;
        }
        state.shared->wake.notify_all();
        if (state.thread.joinable())
            state.thread.join();
        // stream을 모두 풀고 queue가 그 일을 마치기를 예산 안에서 기다린다. 넘기면 기다리지 않는다.
        //  - 늦은 콜백이 닿는 `channel`은 stream의 `info`가 함께 쥐어 남는다. 일부러 남기는 버퍼다.
        for (auto& [ticket, target] : state.watches)
        {
            static_cast<void>(ticket);
            state.close_stream(*target);
        }
        state.watches.clear();
        dispatch_semaphore_t const drained { dispatch_semaphore_create(0) };
        dispatch_async(state.queue, ^{
            dispatch_semaphore_signal(drained);
        });
        const auto budget { std::chrono::duration_cast<std::chrono::nanoseconds>(std::max(state.config.stop_budget, std::chrono::milliseconds::zero())) };
        static_cast<void>(dispatch_semaphore_wait(drained, dispatch_time(DISPATCH_TIME_NOW, budget.count())));
        state.stopped = true;
    }
}
