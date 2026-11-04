#include "luil/app/hot_reload.h"

#include "host/hot_reload_load.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

// 감시자를 쥐고 `deliver`를 잇는 몫이다 (docs/hot-reload-design.md). `file_watcher::create`가 플랫폼 쪽이라 core 밖에 둔다.
namespace luil {
    namespace {
        // 읽지 못한 결과를 앱에 건네기 전에 `settle`마다 다시 읽어 보는 시간이다.
        constexpr std::chrono::seconds unreadable_retry { 2 };
        // `retry_gap`의 아래 한계다. 아주 짧은 `settle`·`max_delay`에서 thread가 조금 늦은 것을 묶음이 사라진 것으로 세지 않는다.
        constexpr std::chrono::milliseconds retry_gap_floor { 500 };
    }

    struct hot_reloader::engine
    {
        struct entry
        {
            hot_reload_file file {};
            // 앱이 `remove`했다. 끝 묶음이 올 때까지 남긴다. 지우면 그 끝 묶음이 "아직 모르는 표"가 되어 끝없이 다시 온다.
            bool removed { false };
            // 앱에 건네지 않고 다시 읽는 중이면 처음 읽지 못한 때다.
            std::optional<std::chrono::steady_clock::time_point> unreadable_since {};
            // 마지막으로 건네지 않은 때다. 거절한 묶음이 감시자에서 사라지면 `unreadable_since`를 풀 길이 없어 다음 실패가 이 간격으로 안다.
            std::chrono::steady_clock::time_point last_held {};
        };

        [[nodiscard]] bool receive(const file_change_batch& batch);
        [[nodiscard]] std::optional<hot_reload_file> live_file(file_watch_ticket ticket);
        // 이번 읽기 결과를 건네지 않고 다시 읽을지다. 상한이 지났거나 읽었으면 거짓이고 다음 실패는 상한을 처음부터 센다.
        // 앞서 거절한 묶음이 `retry_gap` 안에 다시 오지 않았으면 그 묶음은 사라졌으니 이 실패도 상한을 처음부터 센다.
        [[nodiscard]] bool hold_unreadable(file_watch_ticket ticket, bool unreadable, std::chrono::steady_clock::time_point read_started);
        void forget(file_watch_ticket ticket);

        std::function<bool(hot_reload_update)> deliver {};
        hot_reload_limits limits {};
        // 거절한 묶음이 다시 오기까지 걸릴 수 있는 시간이다.
        std::chrono::milliseconds retry_gap { retry_gap_floor };
        std::mutex mutex {};
        std::unordered_map<std::uint64_t, entry> entries {};
        // 마지막 칸이라 소멸이 가장 먼저 멈춘다. 감시 thread가 위의 칸을 쓴다.
        std::unique_ptr<file_watcher> watcher {};
    };

    std::optional<hot_reload_file> hot_reloader::engine::live_file(const file_watch_ticket ticket)
    {
        const std::lock_guard lock { mutex };
        const auto found { entries.find(ticket.value) };
        if (found == entries.end() || found->second.removed)
            return std::nullopt;
        return found->second.file;
    }

    bool hot_reloader::engine::hold_unreadable(const file_watch_ticket ticket, const bool unreadable, const std::chrono::steady_clock::time_point read_started)
    {
        const std::lock_guard lock { mutex };
        const auto found { entries.find(ticket.value) };
        if (found == entries.end())
            return false;
        std::optional<std::chrono::steady_clock::time_point>& since { found->second.unreadable_since };
        if (unreadable == false)
        {
            since.reset();
            return false;
        }
        const auto now { std::chrono::steady_clock::now() };
        // 읽는 데 걸린 시간은 간격에 넣지 않는다. 넣으면 열기가 오래 걸려 실패하는 파일을 매번 새 실패로 세어 상한에 닿지 못한다.
        if (since.has_value() == false || read_started - found->second.last_held > retry_gap)
            since = now;
        if (now - *since < unreadable_retry)
        {
            found->second.last_held = now;
            return true;
        }
        since.reset();
        return false;
    }

    void hot_reloader::engine::forget(const file_watch_ticket ticket)
    {
        const std::lock_guard lock { mutex };
        entries.erase(ticket.value);
    }

    bool hot_reloader::engine::receive(const file_change_batch& batch)
    {
        std::optional<hot_reload_file> file {};
        {
            const std::lock_guard lock { mutex };
            const auto found { entries.find(batch.ticket.value) };
            // 감시자가 `add`의 표를 돌려주기 전에 첫 묶음이 올 수 있다. 거절해 표를 적은 뒤에 다시 받는다.
            if (found == entries.end())
                return false;
            if (found->second.removed == false)
                file = found->second.file;
        }
        if (file.has_value() && deliver != nullptr)
        {
            const auto read_started { std::chrono::steady_clock::now() };
            std::optional<hot_reload_update> update { read_hot_reload_batch(batch, *file, limits) };
            // 읽는 사이에 내렸으면 건네지 않는다.
            if (update.has_value() && live_file(batch.ticket).has_value())
            {
                // 쓰는 쪽이 잠근 채 디스크를 비우면 알림이 잠긴 동안 오고 닫을 때는 오지 않는다. 받았다고 하면 마지막 저장을 잃는다.
                // 거절하면 감시자가 `settle` 뒤에 같은 묶음을 다시 내고 그때 다시 읽는다. 끝 묶음은 읽지 않아 여기에 들지 않는다.
                if (hold_unreadable(batch.ticket, update->status == hot_reload_status::unreadable, read_started))
                    return false;
                // 잠금 밖에서 부른다. 그래야 `deliver` 안의 `add`·`remove`와, 감시 thread를 기다리는 `file_watcher::add`가 서로 막지 않는다.
                if (deliver(std::move(*update)) == false)
                    return false;
            }
        }
        if (batch.end != file_watch_end::none)
            forget(batch.ticket);
        return true;
    }

    hot_reloader::hot_reloader(std::unique_ptr<engine> owned) noexcept
        : engine_ { std::move(owned) }
    {
    }

    hot_reloader::~hot_reloader()
    {
        stop();
    }

    std::unique_ptr<hot_reloader> hot_reloader::create(hot_reloader_config configuration, std::u8string& error)
    {
        auto owned { std::make_unique<engine>() };
        owned->deliver = std::move(configuration.deliver);
        owned->limits = configuration.limits;
        // 거절한 묶음은 변경이 이어져도 `max_delay` 안에 다시 온다. 그보다 짧게 잡으면 이어 쓰는 파일이 상한에 닿지 못한다. `settle`만큼은 thread의 늦음으로 둔다.
        owned->retry_gap = std::max(retry_gap_floor, configuration.max_delay + configuration.settle);

        file_watcher_config watch {};
        engine* const raw { owned.get() };
        watch.deliver = [raw](const file_change_batch batch) { return raw->receive(batch); };
        watch.settle = configuration.settle;
        watch.max_delay = configuration.max_delay;
        watch.stop_budget = configuration.stop_budget;
        owned->watcher = file_watcher::create(std::move(watch), error);
        if (owned->watcher == nullptr)
            return nullptr;
        return std::unique_ptr<hot_reloader> { new hot_reloader { std::move(owned) } };
    }

    hot_reload_update hot_reloader::add(const hot_reload_file& file, std::u8string& error)
    {
        engine& state { *engine_ };
        // 잠금 없이 부른다. 감시 thread를 기다리는데 그 thread가 `receive`에서 이 잠금을 기다릴 수 있다.
        const file_watch_ticket ticket { state.watcher->add(file.path, file_watch_scope::file, error) };
        if (ticket)
        {
            const std::lock_guard lock { state.mutex };
            state.entries.insert_or_assign(ticket.value, engine::entry { file, false });
        }
        // 감시를 건 뒤에 읽는다 (`file_watcher.h`). 그 사이의 저장은 다음 묶음이 다시 읽는다.
        hot_reload_update first { load_hot_reload_file(file, state.limits) };
        first.ticket = ticket;
        return first;
    }

    void hot_reloader::remove(const file_watch_ticket ticket) noexcept
    {
        engine& state { *engine_ };
        {
            const std::lock_guard lock { state.mutex };
            const auto found { state.entries.find(ticket.value) };
            if (found == state.entries.end() || found->second.removed)
                return;
            found->second.removed = true;
        }
        state.watcher->remove(ticket);
    }

    void hot_reloader::stop() noexcept
    {
        if (engine_ != nullptr)
            engine_->watcher->stop();
    }
}
