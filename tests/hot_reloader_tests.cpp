#include "luil/app/hot_reload.h"

#include "hot_reload_test_folder.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 도우미를 실제 폴더와 OS 알림으로 본다 (hot-reload-design.md의 "확인").
// Windows는 luil_tests에서, Android는 luil_core_tests에 감시자 소스와 함께 묶어 기기 셸에서 돈다.

namespace {
    using namespace std::chrono_literals;
    using hot_reload_test::save_file;
    using hot_reload_test::scratch_folder;
    using hot_reload_test::write_file;
    using luil::hot_reload_kind;
    using luil::hot_reload_status;
    using luil::hot_reload_update;

    // 바른 스타일의 가장 작은 글을 시험이 짓지 않아도 되게 빈 글·깨진 글과 마크업만 쓴다. 값은 hot_reload_load_tests.cpp가 본다.
    constexpr std::u8string_view markup_one { u8"<label text=\"one\"/>" };
    constexpr std::u8string_view markup_two { u8"<label text=\"two\"/>" };
    constexpr std::u8string_view markup_broken { u8"<stack>" };

    // 감시 thread가 준 결과를 모은다. `reject_next`를 부르면 다음 결과 하나를 거절한다.
    class recorder
    {
    public:
        [[nodiscard]] std::function<bool(hot_reload_update)> deliver()
        {
            return [this](hot_reload_update update) {
                std::function<void()> on_reject {};
                {
                    const std::lock_guard lock { mutex_ };
                    ++calls_;
                    if (accept_)
                    {
                        updates_.push_back(std::move(update));
                        changed_.notify_all();
                        return true;
                    }
                    accept_ = true;
                    rejected_.push_back(std::move(update));
                    on_reject = std::move(on_reject_);
                    changed_.notify_all();
                }
                if (on_reject != nullptr)
                    on_reject();
                return false;
            };
        }

        void reject_next(std::function<void()> on_reject = {})
        {
            const std::lock_guard lock { mutex_ };
            accept_ = false;
            on_reject_ = std::move(on_reject);
        }

        [[nodiscard]] bool wait(const std::function<bool(const std::vector<hot_reload_update>&)>& done, const std::chrono::milliseconds timeout = 10s)
        {
            std::unique_lock lock { mutex_ };
            return changed_.wait_for(lock, timeout, [&] { return done(updates_); });
        }

        // 결과가 `count`개 올 때까지 기다린다.
        [[nodiscard]] bool wait_count(const std::size_t count, const std::chrono::milliseconds timeout = 10s)
        {
            return wait([count](const std::vector<hot_reload_update>& updates) { return updates.size() >= count; }, timeout);
        }

        // 이 상태의 결과가 올 때까지 기다린다.
        [[nodiscard]] bool wait_status(const hot_reload_status status, const std::chrono::milliseconds timeout = 10s)
        {
            const auto has = [status](const hot_reload_update& update) { return update.status == status; };
            return wait([&has](const std::vector<hot_reload_update>& updates) { return std::any_of(updates.begin(), updates.end(), has); }, timeout);
        }

        [[nodiscard]] std::vector<hot_reload_update> updates() const
        {
            const std::lock_guard lock { mutex_ };
            return updates_;
        }

        [[nodiscard]] std::vector<hot_reload_update> rejected() const
        {
            const std::lock_guard lock { mutex_ };
            return rejected_;
        }

        [[nodiscard]] std::size_t calls() const
        {
            const std::lock_guard lock { mutex_ };
            return calls_;
        }

        void clear()
        {
            const std::lock_guard lock { mutex_ };
            updates_.clear();
        }

    private:
        mutable std::mutex mutex_ {};
        std::condition_variable changed_ {};
        std::vector<hot_reload_update> updates_ {};
        std::vector<hot_reload_update> rejected_ {};
        std::function<void()> on_reject_ {};
        std::size_t calls_ { 0 };
        bool accept_ { true };
    };

    [[nodiscard]] luil::hot_reloader_config test_config(recorder& sink)
    {
        luil::hot_reloader_config config {};
        config.deliver = sink.deliver();
        // test가 기다리는 시간을 줄인다. 저장 하나는 이 안에 끝난다.
        config.settle = 50ms;
        config.max_delay = 500ms;
        return config;
    }

    [[nodiscard]] std::unique_ptr<luil::hot_reloader> make_reloader(luil::hot_reloader_config config)
    {
        std::u8string error {};
        std::unique_ptr<luil::hot_reloader> reloader { luil::hot_reloader::create(std::move(config), error) };
        REQUIRE(reloader != nullptr);
        REQUIRE(error.empty());
        return reloader;
    }

    [[nodiscard]] hot_reload_update add_markup(luil::hot_reloader& reloader, const std::filesystem::path& path)
    {
        std::u8string error {};
        hot_reload_update first { reloader.add({ path.u8string(), hot_reload_kind::markup }, error) };
        REQUIRE(first.ticket);
        REQUIRE(error.empty());
        return first;
    }
}

TEST_CASE("add returns the first read and does not deliver it", "[hot_reload][platform]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"markup.xml") };
    write_file(path, markup_one);
    recorder sink {};
    const auto reloader { make_reloader(test_config(sink)) };

    const hot_reload_update first { add_markup(*reloader, path) };
    REQUIRE(first.status == hot_reload_status::loaded);
    REQUIRE(first.markup != nullptr);
    REQUIRE(first.text == markup_one);

    // 아직 없는 파일도 걸린다. 첫 결과가 없음을 말한다.
    const hot_reload_update later { add_markup(*reloader, folder.file(u8"later.xml")) };
    REQUIRE(later.status == hot_reload_status::missing);
    REQUIRE(later.ticket != first.ticket);

    std::this_thread::sleep_for(200ms);
    REQUIRE(sink.calls() == 0);
}

TEST_CASE("add reports a missing folder and still carries the read", "[hot_reload][platform]")
{
    const scratch_folder folder {};
    recorder sink {};
    const auto reloader { make_reloader(test_config(sink)) };

    std::u8string error {};
    const hot_reload_update first { reloader->add({ folder.text(u8"no-folder/markup.xml"), hot_reload_kind::markup }, error) };
    REQUIRE_FALSE(first.ticket);
    REQUIRE(error.empty() == false);
    REQUIRE(first.status == hot_reload_status::missing);
    REQUIRE(first.errors.size() == 1);
}

TEST_CASE("A save reaches deliver once and each kind of file follows the table", "[hot_reload][platform]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"markup.xml") };
    write_file(path, markup_one);
    recorder sink {};
    const auto reloader { make_reloader(test_config(sink)) };
    const hot_reload_update first { add_markup(*reloader, path) };

    // 임시 파일 뒤 이름 바꾸기로 새 글을 쓰면 `loaded`가 한 번이다.
    save_file(path, markup_two);
    REQUIRE(sink.wait_count(1));
    std::this_thread::sleep_for(200ms);
    std::vector<hot_reload_update> seen { sink.updates() };
    REQUIRE(seen.size() == 1);
    REQUIRE(seen.front().status == hot_reload_status::loaded);
    REQUIRE(seen.front().ticket == first.ticket);
    REQUIRE(seen.front().path == path.u8string());
    REQUIRE(seen.front().markup != nullptr);
    REQUIRE(seen.front().markup != first.markup);
    REQUIRE(seen.front().text == markup_two);
    sink.clear();

    // 깨진 글은 `rejected`다. 값이 없고 글과 이유가 있다.
    save_file(path, markup_broken);
    REQUIRE(sink.wait_status(hot_reload_status::rejected));
    seen = sink.updates();
    REQUIRE(seen.back().markup == nullptr);
    REQUIRE(seen.back().text == markup_broken);
    REQUIRE(seen.back().errors.empty() == false);
    sink.clear();

    // 지우면 `missing`, 다시 만들면 `loaded`다.
    std::filesystem::remove(path);
    REQUIRE(sink.wait_status(hot_reload_status::missing));
    sink.clear();
    save_file(path, markup_one);
    REQUIRE(sink.wait_status(hot_reload_status::loaded));
    REQUIRE(sink.updates().back().text == markup_one);
}

TEST_CASE("A style save reaches deliver with its value empty when refused", "[hot_reload][platform]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"style.json") };
    recorder sink {};
    const auto reloader { make_reloader(test_config(sink)) };
    std::u8string error {};
    const hot_reload_update first { reloader->add({ path.u8string(), hot_reload_kind::style }, error) };
    REQUIRE(first.ticket);
    REQUIRE(first.kind == hot_reload_kind::style);
    REQUIRE(first.status == hot_reload_status::missing);

    save_file(path, u8"{ \"dark\": ");
    REQUIRE(sink.wait_status(hot_reload_status::rejected));
    const hot_reload_update refused { sink.updates().back() };
    REQUIRE(refused.kind == hot_reload_kind::style);
    REQUIRE(refused.style == nullptr);
    REQUIRE(refused.style_errors.size() == 1);
    REQUIRE(refused.errors.front() == luil::describe(refused.style_errors.front()));
}

#if defined(_WIN32)
TEST_CASE("A save under another case of the name still reaches deliver", "[hot_reload][platform]")
{
    // Windows의 이름은 대소문자를 가리지 않는다. OS는 디스크의 표기로 알리므로 앱이 준 표기와 다를 수 있다.
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"markup.xml") };
    write_file(path, markup_one);
    recorder sink {};
    const auto reloader { make_reloader(test_config(sink)) };
    static_cast<void>(add_markup(*reloader, path));

    std::filesystem::remove(path);
    REQUIRE(sink.wait_status(hot_reload_status::missing));
    sink.clear();
    save_file(folder.file(u8"MARKUP.XML"), markup_two);
    REQUIRE(sink.wait_status(hot_reload_status::loaded));
    REQUIRE(sink.updates().back().text == markup_two);
}
#endif

TEST_CASE("A refused result comes again and reads the file again", "[hot_reload][platform]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"markup.xml") };
    write_file(path, markup_one);
    recorder sink {};
    const auto reloader { make_reloader(test_config(sink)) };
    static_cast<void>(add_markup(*reloader, path));

    // 거절한 사이에 파일이 또 바뀌어도 다음 결과는 그때 다시 읽은 글이다.
    sink.reject_next([&path] { save_file(path, markup_one); });
    save_file(path, markup_two);
    REQUIRE(sink.wait_count(1));
    REQUIRE(sink.rejected().size() == 1);
    REQUIRE(sink.rejected().front().text == markup_two);
    REQUIRE(sink.updates().front().status == hot_reload_status::loaded);
    REQUIRE(sink.updates().front().text == markup_one);

    // 새 변경이 없어도 다시 온다.
    sink.clear();
    sink.reject_next();
    save_file(path, markup_two);
    REQUIRE(sink.wait_count(1));
    REQUIRE(sink.updates().front().text == markup_two);
    REQUIRE(sink.rejected().size() == 2);
}

TEST_CASE("No result comes for a ticket after remove", "[hot_reload][platform]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"markup.xml") };
    write_file(path, markup_one);
    recorder sink {};
    const auto reloader { make_reloader(test_config(sink)) };
    const hot_reload_update first { add_markup(*reloader, path) };

    // 남은 묶음이 있는 채로 내린다. 끝 묶음을 거둬야 stop이 끝 묶음을 기다리지 않는다.
    save_file(path, markup_two);
    reloader->remove(first.ticket);
    // 두 번째와 모르는 표는 아무 일도 하지 않는다.
    reloader->remove(first.ticket);
    reloader->remove(luil::file_watch_ticket { 9999 });
    reloader->remove({});
    save_file(path, markup_one);
    std::this_thread::sleep_for(300ms);
    const std::size_t calls { sink.calls() };
    save_file(path, markup_two);
    std::this_thread::sleep_for(300ms);
    REQUIRE(sink.calls() == calls);

    // 같은 파일을 다시 걸면 새 표로 이어진다.
    sink.clear();
    const hot_reload_update again { add_markup(*reloader, path) };
    REQUIRE(again.ticket != first.ticket);
    save_file(path, markup_one);
    REQUIRE(sink.wait_count(1));
    REQUIRE(sink.updates().back().ticket == again.ticket);
}

TEST_CASE("Removing the watched folder gives lost once", "[hot_reload][platform]")
{
    const scratch_folder parent {};
    const std::filesystem::path watched { parent.file(u8"watched") };
    std::filesystem::create_directories(watched);
    recorder sink {};
    const auto reloader { make_reloader(test_config(sink)) };
    const hot_reload_update first { add_markup(*reloader, watched / "markup.xml") };

    std::filesystem::remove_all(watched);
    REQUIRE(sink.wait_status(hot_reload_status::lost));
    std::this_thread::sleep_for(200ms);
    const std::vector<hot_reload_update> seen { sink.updates() };
    const auto losses { std::count_if(seen.begin(), seen.end(), [](const hot_reload_update& update) { return update.status == hot_reload_status::lost; }) };
    REQUIRE(losses == 1);
    REQUIRE(seen.back().status == hot_reload_status::lost);
    REQUIRE(seen.back().ticket == first.ticket);
    REQUIRE(seen.back().errors.size() == 1);
    REQUIRE(seen.back().errors.front().empty() == false);

    // 끝난 표의 remove는 아무 일도 하지 않는다.
    reloader->remove(first.ticket);
    std::this_thread::sleep_for(100ms);
    REQUIRE(sink.updates().size() == seen.size());
}

TEST_CASE("add and remove work inside deliver", "[hot_reload][platform]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"markup.xml") };
    const std::filesystem::path second { folder.file(u8"second.xml") };
    write_file(path, markup_one);
    write_file(second, markup_one);

    std::mutex mutex {};
    std::condition_variable changed {};
    std::vector<hot_reload_update> seen {};
    std::atomic<luil::hot_reloader*> live { nullptr };
    luil::hot_reloader_config config {};
    config.settle = 50ms;
    config.max_delay = 500ms;
    config.deliver = [&](hot_reload_update update) {
        // 첫 결과를 받은 자리에서 그 표를 내리고 다른 파일을 건다.
        luil::hot_reloader* const reloader { live.load() };
        if (update.path == path.u8string() && reloader != nullptr)
        {
            reloader->remove(update.ticket);
            std::u8string error {};
            static_cast<void>(reloader->add({ second.u8string(), hot_reload_kind::markup }, error));
        }
        const std::lock_guard lock { mutex };
        seen.push_back(std::move(update));
        changed.notify_all();
        return true;
    };
    auto reloader { make_reloader(std::move(config)) };
    live.store(reloader.get());
    static_cast<void>(add_markup(*reloader, path));

    save_file(path, markup_two);
    {
        std::unique_lock lock { mutex };
        REQUIRE(changed.wait_for(lock, 10s, [&seen] { return seen.empty() == false; }));
    }
    save_file(second, markup_two);
    {
        std::unique_lock lock { mutex };
        REQUIRE(changed.wait_for(lock, 10s, [&seen, &second] {
            return std::any_of(seen.begin(), seen.end(), [&second](const hot_reload_update& update) { return update.path == second.u8string(); });
        }));
    }
    reloader->stop();
}

TEST_CASE("stop ends within the budget during results and nothing comes after it", "[hot_reload][platform]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"markup.xml") };
    write_file(path, markup_one);
    recorder sink {};
    luil::hot_reloader_config config { test_config(sink) };
    config.stop_budget = 1000ms;
    const auto reloader { make_reloader(std::move(config)) };
    static_cast<void>(add_markup(*reloader, path));

    std::atomic<bool> writing { true };
    const auto burst = [&path, &writing] {
        for (bool flip { false }; writing.load(); flip = flip == false)
        {
            try
            {
                save_file(path, flip ? markup_one : markup_two);
            }
            catch (const std::filesystem::filesystem_error&)
            {
                // Windows에서 감시 thread가 읽는 동안에는 이름을 바꾸지 못할 수 있다. 다음 차례에 다시 한다.
            }
        }
    };
    std::thread writer { burst };
    REQUIRE(sink.wait_count(1));
    const auto begin { std::chrono::steady_clock::now() };
    reloader->stop();
    const auto took { std::chrono::steady_clock::now() - begin };
    const std::size_t calls { sink.calls() };
    std::this_thread::sleep_for(300ms);
    writing.store(false);
    writer.join();
    REQUIRE(took < 1500ms);
    REQUIRE(sink.calls() == calls);

    // 멈춘 뒤의 add는 표가 비고 이유를 적지만 읽은 결과는 담는다. 두 번째 stop은 아무 일도 하지 않는다.
    std::u8string error {};
    const hot_reload_update after { reloader->add({ path.u8string(), hot_reload_kind::markup }, error) };
    REQUIRE_FALSE(after.ticket);
    REQUIRE(error.empty() == false);
    REQUIRE(after.status == hot_reload_status::loaded);
    reloader->stop();
}

TEST_CASE("An empty deliver reads nothing and does not refuse", "[hot_reload][platform]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"markup.xml") };
    write_file(path, markup_one);
    luil::hot_reloader_config config {};
    config.settle = 50ms;
    const auto reloader { make_reloader(std::move(config)) };
    static_cast<void>(add_markup(*reloader, path));
    save_file(path, markup_two);
    std::this_thread::sleep_for(200ms);
    reloader->stop();
}
