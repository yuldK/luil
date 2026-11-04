#include "luil/app/hot_reload.h"

#include "host/hot_reload_load.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

// 감시자를 쥐고 `deliver`를 잇는 몫이다 (docs/hot-reload-design.md). `file_watcher::create`가 플랫폼 쪽이라 core 밖에 둔다.
namespace luil {
    struct hot_reloader::engine
    {
        struct entry
        {
            hot_reload_file file {};
            // 앱이 `remove`했다. 끝 묶음이 올 때까지 남긴다. 지우면 그 끝 묶음이 "아직 모르는 표"가 되어 끝없이 다시 온다.
            bool removed { false };
        };

        [[nodiscard]] bool receive(const file_change_batch& batch);
        [[nodiscard]] std::optional<hot_reload_file> live_file(file_watch_ticket ticket);
        void forget(file_watch_ticket ticket);

        std::function<bool(hot_reload_update)> deliver {};
        hot_reload_limits limits {};
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
            std::optional<hot_reload_update> update { read_hot_reload_batch(batch, *file, limits) };
            // 읽는 사이에 내렸으면 건네지 않는다.
            if (update.has_value() && live_file(batch.ticket).has_value())
            {
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
