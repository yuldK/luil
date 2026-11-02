#include "luil/app/file_watcher.h"

#if defined(_WIN32)
#include "luil/app/platform_services.h"
#include "win32/file_watcher_win32.h"
#endif

#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

// 파일 감시를 실제 폴더와 OS 알림으로 본다 (file-watch-design.md의 "확인").
// Windows는 luil_tests에서, Android는 luil_core_tests에 감시자 소스를 묶어 기기 셸에서 돈다. 한 파일이 두 플랫폼을 맡으므로
// OS 헤더는 폴더 이름과 test 손잡이에만 쓴다.

namespace {
    using namespace std::chrono_literals;
    using luil::file_change;
    using luil::file_change_batch;
    using luil::file_change_kind;

    [[nodiscard]] std::string narrow(const std::u8string_view text)
    {
        return { text.begin(), text.end() };
    }

    [[nodiscard]] std::u8string number(const std::size_t value)
    {
        const std::string text { std::to_string(value) };
        return { text.begin(), text.end() };
    }

    [[nodiscard]] unsigned long process_id() noexcept
    {
#if defined(_WIN32)
        return GetCurrentProcessId();
#else
        return static_cast<unsigned long>(::getpid());
#endif
    }

    // 기기 셸에는 쓸 수 있는 임시 폴더가 정해져 있지 않다. 실행 파일이 도는 자리(adb_run.cmake의 기기 폴더) 아래에 둔다.
    [[nodiscard]] std::filesystem::path scratch_base()
    {
#if defined(__ANDROID__)
        return std::filesystem::current_path();
#else
        return std::filesystem::temp_directory_path();
#endif
    }

    class scratch_folder
    {
    public:
        scratch_folder()
            : path_ { scratch_base() / ("luil-watch-" + std::to_string(process_id()) + "-" + std::to_string(++counter_)) }
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

        [[nodiscard]] std::filesystem::path file(const std::u8string_view name) const
        {
            return path_ / std::filesystem::path { name };
        }

        [[nodiscard]] std::u8string text() const
        {
            return path_.u8string();
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

    private:
        static inline int counter_ { 0 };
        std::filesystem::path path_ {};
    };

    void write_file(const std::filesystem::path& path, const std::string_view text)
    {
        std::ofstream file { path, std::ios::binary | std::ios::trunc };
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    // 감시 thread가 준 묶음을 모은다. `accept`가 거짓이면 그 묶음을 거절한다.
    class recorder
    {
    public:
        [[nodiscard]] std::function<bool(file_change_batch)> deliver()
        {
            return [this](file_change_batch batch) {
                const std::lock_guard lock { mutex_ };
                ++calls_;
                if (accept_ == false)
                {
                    accept_ = true;
                    rejected_.push_back(std::move(batch));
                    changed_.notify_all();
                    return false;
                }
                batches_.push_back(std::move(batch));
                changed_.notify_all();
                return true;
            };
        }

        // 다음 묶음 하나를 거절한다.
        void reject_next()
        {
            const std::lock_guard lock { mutex_ };
            accept_ = false;
        }

        // 받은 묶음이 조건을 채울 때까지 기다린다.
        [[nodiscard]] bool wait(const std::function<bool(const std::vector<file_change_batch>&)>& done, const std::chrono::milliseconds timeout = 5s)
        {
            std::unique_lock lock { mutex_ };
            return changed_.wait_for(lock, timeout, [&] { return done(batches_); });
        }

        // 어느 묶음에서든 이 항목이 왔을 때까지 기다린다.
        [[nodiscard]] bool wait_change(const file_change& wanted, const std::chrono::milliseconds timeout = 5s)
        {
            const auto has = [&wanted](const file_change_batch& batch) { return std::find(batch.changes.begin(), batch.changes.end(), wanted) != batch.changes.end(); };
            const auto done = [&has](const std::vector<file_change_batch>& batches) { return std::any_of(batches.begin(), batches.end(), has); };
            return wait(done, timeout);
        }

        [[nodiscard]] bool wait_end(const luil::file_watch_end end, const std::chrono::milliseconds timeout = 5s)
        {
            const auto ended = [end](const file_change_batch& batch) { return batch.end == end; };
            const auto done = [&ended](const std::vector<file_change_batch>& batches) { return std::any_of(batches.begin(), batches.end(), ended); };
            return wait(done, timeout);
        }

        [[nodiscard]] std::vector<file_change_batch> batches() const
        {
            const std::lock_guard lock { mutex_ };
            return batches_;
        }

        [[nodiscard]] std::vector<file_change> changes() const
        {
            const std::lock_guard lock { mutex_ };
            std::vector<file_change> all {};
            for (const file_change_batch& batch : batches_)
                all.insert(all.end(), batch.changes.begin(), batch.changes.end());
            return all;
        }

        [[nodiscard]] std::size_t calls() const
        {
            const std::lock_guard lock { mutex_ };
            return calls_;
        }

        [[nodiscard]] std::size_t rejected() const
        {
            const std::lock_guard lock { mutex_ };
            return rejected_.size();
        }

        void clear()
        {
            const std::lock_guard lock { mutex_ };
            batches_.clear();
        }

    private:
        mutable std::mutex mutex_ {};
        std::condition_variable changed_ {};
        std::vector<file_change_batch> batches_ {};
        std::vector<file_change_batch> rejected_ {};
        std::size_t calls_ { 0 };
        bool accept_ { true };
    };

    [[nodiscard]] luil::file_watcher_config test_config(recorder& sink)
    {
        luil::file_watcher_config config {};
        config.deliver = sink.deliver();
        // test가 기다리는 시간을 줄인다. 저장 하나는 이 안에 끝난다.
        config.settle = 50ms;
        config.max_delay = 500ms;
        return config;
    }

    [[nodiscard]] std::unique_ptr<luil::file_watcher> make_watcher(luil::file_watcher_config config)
    {
        std::u8string error {};
        std::unique_ptr<luil::file_watcher> watcher { luil::file_watcher::create(std::move(config), error) };
        INFO(narrow(error));
        REQUIRE(watcher != nullptr);
        return watcher;
    }

    [[nodiscard]] luil::file_watch_ticket add(luil::file_watcher& watcher, const std::u8string& path, const luil::file_watch_scope scope)
    {
        std::u8string error {};
        const luil::file_watch_ticket ticket { watcher.add(path, scope, error) };
        INFO(narrow(error));
        REQUIRE(ticket);
        return ticket;
    }

    [[nodiscard]] file_change change(const file_change_kind kind, std::u8string path, std::u8string old_path = {})
    {
        return { kind, std::move(path), std::move(old_path) };
    }
}

TEST_CASE("A folder watch reports create, modify, rename and remove", "[file_watch][platform]")
{
    const scratch_folder folder {};
    recorder sink {};
    const auto watcher { make_watcher(test_config(sink)) };
    const luil::file_watch_ticket ticket { add(*watcher, folder.text(), luil::file_watch_scope::folder) };

    write_file(folder.file(u8"가.txt"), "one");
    REQUIRE(sink.wait_change(change(file_change_kind::created, u8"가.txt")));
    REQUIRE(sink.batches().front().ticket == ticket);
    sink.clear();

    write_file(folder.file(u8"가.txt"), "two, longer");
    REQUIRE(sink.wait_change(change(file_change_kind::modified, u8"가.txt")));
    sink.clear();

    std::filesystem::rename(folder.file(u8"가.txt"), folder.file(u8"나.txt"));
    REQUIRE(sink.wait_change(change(file_change_kind::renamed, u8"나.txt", u8"가.txt")));
    sink.clear();

    std::filesystem::remove(folder.file(u8"나.txt"));
    REQUIRE(sink.wait_change(change(file_change_kind::removed, u8"나.txt")));
    watcher->stop();
}

TEST_CASE("A missing folder is refused with an error", "[file_watch][platform]")
{
    const scratch_folder folder {};
    recorder sink {};
    const auto watcher { make_watcher(test_config(sink)) };
    std::u8string error {};
    REQUIRE_FALSE(watcher->add((folder.path() / "missing").u8string(), luil::file_watch_scope::folder, error));
    REQUIRE(error.empty() == false);
    error.clear();
    REQUIRE_FALSE(watcher->add((folder.path() / "missing" / "a.json").u8string(), luil::file_watch_scope::file, error));
    REQUIRE(error.empty() == false);
    // 파일 감시는 파일이 아직 없어도 걸린다.
    error.clear();
    REQUIRE(watcher->add(folder.file(u8"later.json").u8string(), luil::file_watch_scope::file, error));
}

TEST_CASE("A subtree watch sees nested files and files in new folders", "[file_watch][platform]")
{
    const scratch_folder folder {};
    std::filesystem::create_directories(folder.file(u8"sub/deep"));
    recorder sink {};
    const auto watcher { make_watcher(test_config(sink)) };
    static_cast<void>(add(*watcher, folder.text(), luil::file_watch_scope::subtree));

    write_file(folder.file(u8"sub/deep/a.txt"), "a");
    REQUIRE(sink.wait_change(change(file_change_kind::created, u8"sub/deep/a.txt")));

    // 새 폴더를 만들고 곧바로 그 안에 쓴다. inotify는 watch를 걸기 전의 파일을 훑어 잡는다.
    std::filesystem::create_directories(folder.file(u8"new/inner"));
    write_file(folder.file(u8"new/inner/b.txt"), "b");
    REQUIRE(sink.wait_change(change(file_change_kind::created, u8"new/inner/b.txt")));

    // 새 폴더 안의 다음 변경도 온다.
    sink.clear();
    write_file(folder.file(u8"new/inner/c.txt"), "c");
    REQUIRE(sink.wait_change(change(file_change_kind::created, u8"new/inner/c.txt")));
}

TEST_CASE("A folder watch does not see nested files", "[file_watch][platform]")
{
    const scratch_folder folder {};
    std::filesystem::create_directories(folder.file(u8"sub"));
    recorder sink {};
    const auto watcher { make_watcher(test_config(sink)) };
    static_cast<void>(add(*watcher, folder.text(), luil::file_watch_scope::folder));

    write_file(folder.file(u8"sub/hidden.txt"), "x");
    write_file(folder.file(u8"top.txt"), "y");
    REQUIRE(sink.wait_change(change(file_change_kind::created, u8"top.txt")));
    for (const file_change& seen : sink.changes())
        REQUIRE(seen.path != u8"sub/hidden.txt");
}

TEST_CASE("A file watch receives a temporary-file save as one change", "[file_watch][platform]")
{
    const scratch_folder folder {};
    recorder sink {};
    const auto watcher { make_watcher(test_config(sink)) };
    // 아직 없는 파일이다.
    static_cast<void>(add(*watcher, folder.file(u8"a.json").u8string(), luil::file_watch_scope::file));

    write_file(folder.file(u8"other.txt"), "noise");
    write_file(folder.file(u8"a.json.tmp"), "{}");
    std::filesystem::rename(folder.file(u8"a.json.tmp"), folder.file(u8"a.json"));
    REQUIRE(sink.wait([](const std::vector<file_change_batch>& batches) { return batches.empty() == false; }));
    std::this_thread::sleep_for(200ms);

    const std::vector<file_change> seen { sink.changes() };
    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front().path == u8"a.json");
    REQUIRE((seen.front().kind == file_change_kind::created || seen.front().kind == file_change_kind::renamed));

    // 이미 있는 파일을 다시 같은 길로 저장하면 그 파일의 항목 하나다.
    sink.clear();
    write_file(folder.file(u8"a.json.tmp"), "{\"b\":1}");
    std::filesystem::rename(folder.file(u8"a.json.tmp"), folder.file(u8"a.json"));
    REQUIRE(sink.wait([](const std::vector<file_change_batch>& batches) { return batches.empty() == false; }));
    std::this_thread::sleep_for(200ms);
    const std::vector<file_change> again { sink.changes() };
    REQUIRE(again.size() == 1);
    REQUIRE(again.front().path == u8"a.json");
    REQUIRE(again.front().kind != file_change_kind::removed);
}

#if defined(_WIN32)
TEST_CASE("A file watch receives a replace_file_atomically save as one change", "[file_watch][platform]")
{
    const scratch_folder folder {};
    write_file(folder.file(u8"설정.json"), "old");
    recorder sink {};
    const auto watcher { make_watcher(test_config(sink)) };
    static_cast<void>(add(*watcher, folder.file(u8"설정.json").u8string(), luil::file_watch_scope::file));

    const std::string contents { "new contents" };
    REQUIRE(luil::replace_file_atomically(folder.file(u8"설정.json").u8string(), std::span { reinterpret_cast<const std::uint8_t*>(contents.data()), contents.size() }).ok());
    REQUIRE(sink.wait([](const std::vector<file_change_batch>& batches) { return batches.empty() == false; }));
    std::this_thread::sleep_for(200ms);
    const std::vector<file_change> seen { sink.changes() };
    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front().path == u8"설정.json");
    REQUIRE(seen.front().kind != file_change_kind::removed);
}
#endif

TEST_CASE("Removing the watched folder ends the watch as lost", "[file_watch][platform]")
{
    const scratch_folder parent {};
    const std::filesystem::path watched { parent.file(u8"watched") };
    std::filesystem::create_directories(watched);
    recorder sink {};
    const auto watcher { make_watcher(test_config(sink)) };
    const luil::file_watch_ticket ticket { add(*watcher, watched.u8string(), luil::file_watch_scope::folder) };

    std::filesystem::remove_all(watched);
    REQUIRE(sink.wait_end(luil::file_watch_end::lost));
    const std::vector<file_change_batch> batches { sink.batches() };
    const auto ends { std::count_if(batches.begin(), batches.end(), [](const file_change_batch& batch) { return batch.end != luil::file_watch_end::none; }) };
    REQUIRE(ends == 1);
    REQUIRE(batches.back().ticket == ticket);
    REQUIRE(batches.back().error.empty() == false);

    // 끝난 표의 remove는 아무 일도 하지 않는다.
    watcher->remove(ticket);
    std::this_thread::sleep_for(150ms);
    REQUIRE(sink.batches().size() == batches.size());
}

TEST_CASE("Removing a watch sends one end batch with what was left", "[file_watch][platform]")
{
    const scratch_folder folder {};
    recorder sink {};
    luil::file_watcher_config config { test_config(sink) };
    // 남은 변경이 끝 묶음에 실리게 settle을 길게 둔다.
    config.settle = 2s;
    config.max_delay = 4s;
    const auto watcher { make_watcher(std::move(config)) };
    const luil::file_watch_ticket ticket { add(*watcher, folder.text(), luil::file_watch_scope::folder) };

    write_file(folder.file(u8"left.txt"), "x");
    std::this_thread::sleep_for(150ms);
    watcher->remove(ticket);
    REQUIRE(sink.wait_end(luil::file_watch_end::removed, 1500ms));
    const std::vector<file_change_batch> batches { sink.batches() };
    REQUIRE(batches.size() == 1);
    REQUIRE(batches.front().ticket == ticket);
    REQUIRE(std::find(batches.front().changes.begin(), batches.front().changes.end(), change(file_change_kind::created, u8"left.txt")) != batches.front().changes.end());

    // 그 뒤로 그 표에는 묶음이 없다. 두 번째 remove도 아무 일도 하지 않는다.
    watcher->remove(ticket);
    write_file(folder.file(u8"after.txt"), "y");
    std::this_thread::sleep_for(300ms);
    REQUIRE(sink.batches().size() == 1);
}

TEST_CASE("Two watchers watch at the same time", "[file_watch][platform]")
{
    const scratch_folder folder {};
    recorder first_sink {};
    recorder second_sink {};
    const auto first { make_watcher(test_config(first_sink)) };
    const auto second { make_watcher(test_config(second_sink)) };
    static_cast<void>(add(*first, folder.text(), luil::file_watch_scope::folder));
    static_cast<void>(add(*second, folder.text(), luil::file_watch_scope::folder));
    // 한 감시자 안에서 같은 폴더를 두 번 걸어도 따로 온다.
    const luil::file_watch_ticket again { add(*second, folder.file(u8"both.txt").u8string(), luil::file_watch_scope::file) };

    write_file(folder.file(u8"both.txt"), "x");
    REQUIRE(first_sink.wait_change(change(file_change_kind::created, u8"both.txt")));
    const auto file_watch = [again](const file_change_batch& batch) { return batch.ticket == again && batch.changes.empty() == false; };
    const auto folder_watch = [again](const file_change_batch& batch) { return batch.ticket != again && batch.changes.empty() == false; };
    const auto both = [&](const std::vector<file_change_batch>& batches) {
        return std::any_of(batches.begin(), batches.end(), file_watch)
            && std::any_of(batches.begin(), batches.end(), folder_watch)
        ;
    };
    REQUIRE(second_sink.wait(both));

    // 하나를 내려도 다른 감시는 이어진다.
    second->remove(again);
    REQUIRE(second_sink.wait_end(luil::file_watch_end::removed));
    second_sink.clear();
    write_file(folder.file(u8"next.txt"), "y");
    REQUIRE(second_sink.wait_change(change(file_change_kind::created, u8"next.txt")));
}

TEST_CASE("A rejected batch comes again", "[file_watch][platform]")
{
    const scratch_folder folder {};
    recorder sink {};
    const auto watcher { make_watcher(test_config(sink)) };
    static_cast<void>(add(*watcher, folder.text(), luil::file_watch_scope::folder));

    sink.reject_next();
    write_file(folder.file(u8"retry.txt"), "x");
    // 새 변경 없이 settle 뒤에 다시 온다.
    REQUIRE(sink.wait_change(change(file_change_kind::created, u8"retry.txt")));
    REQUIRE(sink.rejected() == 1);
}

TEST_CASE("No batch is delivered after stop returns", "[file_watch][platform]")
{
    const scratch_folder folder {};
    recorder sink {};
    const auto watcher { make_watcher(test_config(sink)) };
    static_cast<void>(add(*watcher, folder.text(), luil::file_watch_scope::folder));
    write_file(folder.file(u8"a.txt"), "x");
    watcher->stop();
    const std::size_t calls { sink.calls() };
    write_file(folder.file(u8"b.txt"), "y");
    std::this_thread::sleep_for(300ms);
    REQUIRE(sink.calls() == calls);

    // 멈춘 뒤의 add는 거절된다. 두 번째 stop은 아무 일도 하지 않는다.
    std::u8string error {};
    REQUIRE_FALSE(watcher->add(folder.text(), luil::file_watch_scope::folder, error));
    REQUIRE(error.empty() == false);
    watcher->stop();
}

TEST_CASE("Stop ends within the budget when idle and during a burst", "[file_watch][platform]")
{
    const scratch_folder folder {};
    recorder sink {};
    luil::file_watcher_config config { test_config(sink) };
    config.stop_budget = 1000ms;

    {
        const auto watcher { make_watcher(config) };
        static_cast<void>(add(*watcher, folder.text(), luil::file_watch_scope::subtree));
        const auto begin { std::chrono::steady_clock::now() };
        watcher->stop();
        REQUIRE(std::chrono::steady_clock::now() - begin < 1500ms);
    }

    const auto watcher { make_watcher(config) };
    static_cast<void>(add(*watcher, folder.text(), luil::file_watch_scope::subtree));
    std::atomic<bool> writing { true };
    const auto burst = [&folder, &writing] {
        for (std::size_t index { 0 }; writing.load(); ++index)
            write_file(folder.file(u8"burst-" + number(index % 50) + u8".txt"), "burst");
    };
    std::thread writer { burst };
    std::this_thread::sleep_for(200ms);
    const auto begin { std::chrono::steady_clock::now() };
    watcher->stop();
    const auto took { std::chrono::steady_clock::now() - begin };
    writing.store(false);
    writer.join();
    REQUIRE(took < 1500ms);
}

TEST_CASE("An OS overflow becomes a rescan", "[file_watch][platform]")
{
    const scratch_folder folder {};
    recorder sink {};
    luil::file_watcher_config config { test_config(sink) };
#if defined(_WIN32)
    // 작은 버퍼로 OS 넘침을 일으킨다. 이름 몇 개면 넘친다.
    luil::win32::set_file_watch_buffer_bytes_for_tests(64);
    const auto watcher { make_watcher(std::move(config)) };
    static_cast<void>(add(*watcher, folder.text(), luil::file_watch_scope::folder));
    luil::win32::set_file_watch_buffer_bytes_for_tests(0);
    for (std::size_t index { 0 }; index < 50; ++index)
        write_file(folder.file(u8"overflow-with-a-long-name-" + number(index) + u8".txt"), "x");
#else
    // inotify의 큐를 넘긴다. 감시 thread를 `deliver` 안에 묶어 두는 동안 큐 상한보다 많은 파일을 만든다.
    std::size_t queue_limit { 16384 };
    if (std::ifstream limit { "/proc/sys/fs/inotify/max_queued_events" }; limit)
        limit >> queue_limit;
    std::mutex gate_mutex {};
    std::condition_variable gate {};
    bool open { false };
    bool held { false };
    const std::function<bool(file_change_batch)> inner { config.deliver };
    config.deliver = [&](file_change_batch batch) {
        {
            std::unique_lock lock { gate_mutex };
            held = true;
            gate.notify_all();
            gate.wait(lock, [&open] { return open; });
        }
        return inner(std::move(batch));
    };
    const auto watcher { make_watcher(std::move(config)) };
    static_cast<void>(add(*watcher, folder.text(), luil::file_watch_scope::folder));
    write_file(folder.file(u8"first.txt"), "x");
    {
        std::unique_lock lock { gate_mutex };
        REQUIRE(gate.wait_for(lock, 5s, [&held] { return held; }));
    }
    // 파일 하나가 만들기·쓰기 닫기 두 이벤트다.
    for (std::size_t index { 0 }; index < queue_limit / 2 + 64; ++index)
        write_file(folder.file(u8"f" + number(index)), "");
    {
        const std::lock_guard lock { gate_mutex };
        open = true;
    }
    gate.notify_all();
#endif
    const auto rescan = [](const std::vector<file_change_batch>& batches) {
        return std::any_of(batches.begin(), batches.end(), [](const file_change_batch& batch) { return batch.rescan; });
    };
    REQUIRE(sink.wait(rescan, 10s));
    watcher->stop();
}
