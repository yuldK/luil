#include "host/file_watch_batch.h"

#include <algorithm>
#include <utility>

namespace luil {
    file_watch_batch::file_watch_batch(const file_watch_timing timing) noexcept
        : timing_ { timing }
    {
    }

    void file_watch_batch::set_name_filter(std::function<bool(std::u8string_view)> matches)
    {
        name_filter_ = std::move(matches);
    }

    bool file_watch_batch::pending() const noexcept
    {
        return live_ != 0 || move_.has_value() || rescan_ || end_ != file_watch_end::none;
    }

    void file_watch_batch::touch(const clock::time_point now) noexcept
    {
        if (first_.has_value() == false)
            first_ = now;
        last_ = now;
    }

    void file_watch_batch::created(std::u8string path, const clock::time_point now)
    {
        if (ending())
            return;
        touch(now);
        resolve_pending_move();
        apply_created(std::move(path));
        check_limit();
    }

    void file_watch_batch::modified(std::u8string path, const clock::time_point now)
    {
        if (ending())
            return;
        touch(now);
        resolve_pending_move();
        apply_modified(std::move(path));
        check_limit();
    }

    void file_watch_batch::removed(std::u8string path, const clock::time_point now)
    {
        if (ending())
            return;
        touch(now);
        resolve_pending_move();
        apply_removed(std::move(path));
        check_limit();
    }

    void file_watch_batch::renamed(std::u8string from, std::u8string to, const clock::time_point now)
    {
        if (ending())
            return;
        touch(now);
        resolve_pending_move();
        apply_renamed(std::move(from), std::move(to));
        check_limit();
    }

    void file_watch_batch::moved_from(std::u8string path, const std::uint32_t cookie, const clock::time_point now)
    {
        if (ending())
            return;
        touch(now);
        resolve_pending_move();
        move_ = pending_move { std::move(path), cookie };
    }

    void file_watch_batch::moved_to(std::u8string path, const std::uint32_t cookie, const clock::time_point now)
    {
        if (ending())
            return;
        touch(now);
        if (move_.has_value() && move_->cookie == cookie)
        {
            std::u8string from { std::move(move_->path) };
            move_.reset();
            apply_renamed(std::move(from), std::move(path));
        }
        else
        {
            resolve_pending_move();
            apply_created(std::move(path));
        }
        check_limit();
    }

    void file_watch_batch::overflow(const clock::time_point now)
    {
        if (ending())
            return;
        touch(now);
        entries_.clear();
        by_path_.clear();
        by_old_path_.clear();
        live_ = 0;
        move_.reset();
        rescan_ = true;
    }

    void file_watch_batch::finish(const file_watch_end end, std::u8string error, const clock::time_point now)
    {
        if (ending() || end == file_watch_end::none)
            return;
        touch(now);
        end_ = end;
        error_ = std::move(error);
        end_due_ = now;
    }

    std::optional<file_watch_batch::clock::time_point> file_watch_batch::due_at() const noexcept
    {
        if (closed_ || pending() == false)
            return std::nullopt;
        if (end_due_.has_value())
            return end_due_;
        if (first_.has_value() == false || last_.has_value() == false)
            return std::nullopt;
        return std::min(*last_ + timing_.settle, *first_ + timing_.max_delay);
    }

    std::optional<file_change_batch> file_watch_batch::prepare(const file_watch_ticket ticket)
    {
        resolve_pending_move();
        file_change_batch batch {};
        batch.ticket = ticket;
        batch.rescan = rescan_;
        batch.end = end_;
        batch.error = error_;
        batch.changes.reserve(live_);
        for (const std::optional<file_change>& entry : entries_)
        {
            if (entry.has_value() == false)
                continue;
            if (name_filter_ != nullptr
                && name_filter_(entry->path) == false
                && (entry->kind != file_change_kind::renamed || name_filter_(entry->old_path) == false))
                continue;
            batch.changes.push_back(*entry);
        }
        if (batch.changes.empty() && batch.rescan == false && batch.end == file_watch_end::none)
        {
            // 거름이 다 걸러 냈다. 낼 것이 없으니 모은 것만 비운다.
            clear();
            return std::nullopt;
        }
        return batch;
    }

    void file_watch_batch::delivered(const bool accepted, const clock::time_point now)
    {
        if (accepted)
        {
            if (end_ != file_watch_end::none)
                closed_ = true;
            clear();
            return;
        }
        // 쥔 채 다시 센다. 새 변경이 없으면 `settle` 뒤, 이어지면 늦어도 `max_delay` 뒤에 합친 묶음으로 다시 낸다.
        first_ = now;
        last_ = now;
        if (end_due_.has_value())
            end_due_ = now + timing_.settle;
    }

    void file_watch_batch::clear() noexcept
    {
        entries_.clear();
        by_path_.clear();
        by_old_path_.clear();
        live_ = 0;
        move_.reset();
        rescan_ = false;
        first_.reset();
        last_.reset();
        end_due_.reset();
    }

    void file_watch_batch::resolve_pending_move()
    {
        if (move_.has_value() == false)
            return;
        std::u8string path { std::move(move_->path) };
        move_.reset();
        apply_removed(std::move(path));
    }

    std::optional<std::size_t> file_watch_batch::find(const std::u8string& path) const
    {
        if (const auto found { by_path_.find(path) }; found != by_path_.end())
            return found->second;
        return std::nullopt;
    }

    void file_watch_batch::unindex(const std::size_t index)
    {
        const file_change& entry { *entries_[index] };
        by_path_.erase(entry.path);
        if (entry.kind == file_change_kind::renamed)
            by_old_path_.erase(entry.old_path);
    }

    void file_watch_batch::place(const std::size_t index, file_change change)
    {
        if (entries_[index].has_value())
            unindex(index);
        else
            ++live_;
        by_path_[change.path] = index;
        if (change.kind == file_change_kind::renamed)
            by_old_path_[change.old_path] = index;
        else
            change.old_path.clear();
        entries_[index] = std::move(change);
    }

    void file_watch_batch::append(file_change change)
    {
        entries_.emplace_back();
        place(entries_.size() - 1, std::move(change));
    }

    void file_watch_batch::erase(const std::size_t index)
    {
        if (entries_[index].has_value() == false)
            return;
        unindex(index);
        entries_[index].reset();
        --live_;
    }

    void file_watch_batch::check_limit()
    {
        if (live_ <= timing_.max_changes)
            return;
        entries_.clear();
        by_path_.clear();
        by_old_path_.clear();
        live_ = 0;
        move_.reset();
        rescan_ = true;
    }

    void file_watch_batch::release_old_path(const std::u8string& path)
    {
        const auto found { by_old_path_.find(path) };
        if (found == by_old_path_.end())
            return;
        const std::size_t index { found->second };
        // 옛 이름의 원래 파일은 새 이름으로 갔다. 새 이름은 새로 생긴 것이고, 옛 이름은 그 자리를 비웠다가 다시 쓰인다.
        const std::u8string moved_to { entries_[index]->path };
        place(index, file_change { file_change_kind::created, moved_to, {} });
        append(file_change { file_change_kind::removed, path, {} });
    }

    void file_watch_batch::apply_created(std::u8string path)
    {
        if (rescan_)
            return;
        release_old_path(path);
        if (const std::optional<std::size_t> index { find(path) }; index.has_value())
        {
            file_change& entry { *entries_[*index] };
            // 지웠다 다시 만든 것은 내용이 바뀐 것이다. 나머지는 앞의 종류가 그대로 맞다.
            if (entry.kind == file_change_kind::removed)
                entry.kind = file_change_kind::modified;
            return;
        }
        append(file_change { file_change_kind::created, std::move(path), {} });
    }

    void file_watch_batch::apply_modified(std::u8string path)
    {
        if (rescan_)
            return;
        release_old_path(path);
        if (const std::optional<std::size_t> index { find(path) }; index.has_value())
        {
            file_change& entry { *entries_[*index] };
            if (entry.kind == file_change_kind::removed)
                entry.kind = file_change_kind::modified;
            return;
        }
        append(file_change { file_change_kind::modified, std::move(path), {} });
    }

    void file_watch_batch::apply_removed(std::u8string path)
    {
        if (rescan_)
            return;
        release_old_path(path);
        const std::optional<std::size_t> index { find(path) };
        if (index.has_value() == false)
        {
            append(file_change { file_change_kind::removed, std::move(path), {} });
            return;
        }
        const file_change entry { *entries_[*index] };
        switch (entry.kind)
        {
        case file_change_kind::created:
            // 묶음 안에서 생겼다 사라졌다. 순 변화가 없다.
            erase(*index);
            break;
        case file_change_kind::modified:
        case file_change_kind::removed:
            place(*index, file_change { file_change_kind::removed, entry.path, {} });
            break;
        case file_change_kind::renamed:
            // 옛 이름에서 옮겨 와 지워졌다. 사라진 것은 옛 이름의 파일이다.
            place(*index, file_change { file_change_kind::removed, entry.old_path, {} });
            break;
        }
    }

    void file_watch_batch::apply_renamed(std::u8string from, std::u8string to)
    {
        if (rescan_)
            return;
        if (from == to)
        {
            apply_modified(std::move(to));
            return;
        }

        // 옮겨 가는 쪽이 무엇이었는지다.
        release_old_path(from);
        file_change moved { file_change_kind::renamed, to, from };
        if (const std::optional<std::size_t> source { find(from) }; source.has_value())
        {
            const file_change entry { *entries_[*source] };
            erase(*source);
            switch (entry.kind)
            {
            case file_change_kind::created:
                moved = file_change { file_change_kind::created, to, {} };
                break;
            case file_change_kind::modified:
            case file_change_kind::removed:
                break;
            case file_change_kind::renamed:
                // 이어진 이름 바꿈은 처음 이름에서 마지막 이름으로 간 것이다. 제자리로 돌아왔으면 내용만 바뀌었을 수 있다.
                if (entry.old_path == to)
                    moved = file_change { file_change_kind::modified, to, {} };
                else
                    moved.old_path = entry.old_path;
                break;
            }
        }

        // 옮겨 오는 자리에 무엇이 있었는지다.
        release_old_path(to);
        const std::optional<std::size_t> target { find(to) };
        if (target.has_value() == false)
        {
            append(std::move(moved));
            return;
        }
        const file_change entry { *entries_[*target] };
        bool existed { true };
        switch (entry.kind)
        {
        case file_change_kind::created:
            existed = false;
            break;
        case file_change_kind::modified:
        case file_change_kind::removed:
            break;
        case file_change_kind::renamed:
            // 덮인 파일은 그 옛 이름에서 온 것이다. 그 옛 이름의 파일이 사라졌다.
            place(*target, file_change { file_change_kind::removed, entry.old_path, {} });
            break;
        }
        // 이미 있던 파일을 새로 생긴 파일로 덮은 것은 그 자리의 내용이 바뀐 것이다.
        if (existed && moved.kind == file_change_kind::created)
            moved.kind = file_change_kind::modified;
        if (const std::optional<std::size_t> placed { find(to) }; placed.has_value())
            place(*placed, std::move(moved));
        else
            append(std::move(moved));
    }

    void offer_file_change_batch(file_watch_batch& batch, const file_watch_ticket ticket, const std::function<bool(file_change_batch)>& deliver, const file_watch_batch::clock::time_point now)
    {
        const std::optional<file_watch_batch::clock::time_point> due { batch.due_at() };
        if (due.has_value() == false || *due > now)
            return;
        std::optional<file_change_batch> prepared { batch.prepare(ticket) };
        if (prepared.has_value() == false)
            return;
        bool accepted { true };
        if (deliver != nullptr)
        {
            try
            {
                accepted = deliver(std::move(*prepared));
            }
            catch (...)
            {
                accepted = false;
            }
        }
        batch.delivered(accepted, now);
    }

    std::u8string to_watch_path(std::u8string path)
    {
        std::replace(path.begin(), path.end(), u8'\\', u8'/');
        return path;
    }
}
