#include "win32/layered_placement.h"

#include <algorithm>

namespace luil::win32 {
    void deferred_placements::push(const screen_bounds& bounds, const std::uint64_t ticket, const clock::time_point now)
    {
        entries_.push_back(entry { bounds, ticket, now });
    }

    void deferred_placements::push_move(const screen_bounds& bounds, const clock::time_point now)
    {
        const std::uint64_t ticket { entries_.empty() ? 0 : entries_.back().ticket };
        entries_.push_back(entry { bounds, ticket, now });
    }

    std::optional<screen_bounds> deferred_placements::take_ready(const std::uint64_t tree_ticket, const clock::time_point now, const clock::duration timeout)
    {
        if (entries_.empty())
            return std::nullopt;

        // 표는 쌓인 순서대로 커지므로 뒤에서부터 찾은 첫 배치가 그 tree가 반영한 가장 새 배치다.
        const auto ready { std::find_if(entries_.rbegin(), entries_.rend(), [tree_ticket](const entry& value) { return value.ticket <= tree_ticket; }) };
        if (ready != entries_.rend())
        {
            const screen_bounds bounds { ready->bounds };
            entries_.erase(entries_.begin(), ready.base());
            return bounds;
        }
        if (now - entries_.front().queued_at >= timeout)
        {
            const screen_bounds bounds { entries_.back().bounds };
            entries_.clear();
            return bounds;
        }
        return std::nullopt;
    }

    std::optional<screen_bounds> deferred_placements::latest() const noexcept
    {
        if (entries_.empty())
            return std::nullopt;
        return entries_.back().bounds;
    }

    std::optional<deferred_placements::clock::time_point> deferred_placements::deadline(const clock::duration timeout) const noexcept
    {
        if (entries_.empty())
            return std::nullopt;
        return entries_.front().queued_at + timeout;
    }
}
