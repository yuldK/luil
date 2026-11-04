#include "host/notification_queue.h"

#include "luil/text/utf8_text.h"

#include <limits>
#include <utility>

namespace luil {
    namespace {
        // U+2026 HORIZONTAL ELLIPSIS다.
        constexpr std::u8string_view ellipsis { u8"…" };

        [[nodiscard]] bool id_character(const char8_t character) noexcept
        {
            return (character >= u8'a' && character <= u8'z')
                || (character >= u8'A' && character <= u8'Z')
                || (character >= u8'0' && character <= u8'9')
                || character == u8'.'
                || character == u8'_'
                || character == u8'-';
        }

        [[nodiscard]] std::uint32_t saturating_add(const std::uint32_t value, const std::uint32_t more) noexcept
        {
            if (value > std::numeric_limits<std::uint32_t>::max() - more)
                return std::numeric_limits<std::uint32_t>::max();
            return value + more;
        }
    }

    bool notification_id_valid(const std::u8string_view id) noexcept
    {
        if (id.empty() || id.size() > notification_id_limit)
            return false;
        for (const char8_t character : id)
        {
            if (id_character(character) == false)
                return false;
        }
        return true;
    }

    std::u8string truncate_code_points(const std::u8string_view text, const std::size_t limit)
    {
        if (limit == 0)
            return {};
        // 앞에서부터 코드 포인트를 세며 `limit - 1`번째의 끝 자리를 기억한다. `limit`개를 넘는 것이 보이면 거기서 자른다.
        std::size_t count { 0 };
        std::size_t offset { 0 };
        std::size_t keep { 0 };
        while (offset < text.size())
        {
            const text::utf8_decoded decoded { text::utf8_decode(text, offset) };
            offset += decoded.length;
            ++count;
            if (count == limit - 1)
                keep = offset;
            if (count > limit)
                return std::u8string { text.substr(0, keep) } + std::u8string { ellipsis };
        }
        return std::u8string { text };
    }

    std::optional<notification_text> prepare_notification_text(const notification& value, std::u8string& error)
    {
        if (notification_id_valid(value.id) == false)
        {
            error = u8"The notification id must be 1 to 64 ASCII letters, digits, '.', '_' or '-'.";
            return std::nullopt;
        }
        if (text::utf8_is_valid(value.title) == false || text::utf8_is_valid(value.body) == false)
        {
            error = u8"The notification title or body is not valid UTF-8.";
            return std::nullopt;
        }
        return notification_text { truncate_code_points(value.title, notification_title_limit), truncate_code_points(value.body, notification_body_limit) };
    }

    std::int32_t notification_request_code(const std::u8string_view id) noexcept
    {
        std::uint32_t hash { 2166136261u };
        for (const char8_t character : id)
        {
            hash ^= static_cast<std::uint32_t>(character);
            hash *= 16777619u;
        }
        return static_cast<std::int32_t>(hash);
    }

    void notification_queue::push(notification_event event)
    {
        const std::lock_guard lock { mutex_ };
        events_.push_back(std::move(event));
        trim();
    }

    std::optional<notification_event> notification_queue::take()
    {
        const std::lock_guard lock { mutex_ };
        if (events_.empty())
            return std::nullopt;
        notification_event event { std::move(events_.front()) };
        events_.pop_front();
        return event;
    }

    void notification_queue::put_back(notification_event event)
    {
        const std::lock_guard lock { mutex_ };
        events_.push_front(std::move(event));
        // 되돌리는 사이 다른 thread가 채웠으면 되돌린 것이 가장 오래되어 먼저 버려진다.
        trim();
    }

    void notification_queue::clear()
    {
        const std::lock_guard lock { mutex_ };
        events_.clear();
    }

    bool notification_queue::empty() const
    {
        const std::lock_guard lock { mutex_ };
        return events_.empty();
    }

    std::size_t notification_queue::size() const
    {
        const std::lock_guard lock { mutex_ };
        return events_.size();
    }

    void notification_queue::trim()
    {
        while (events_.size() > capacity)
        {
            const std::uint32_t dropped { saturating_add(events_.front().lost_before, 1) };
            events_.pop_front();
            events_.front().lost_before = saturating_add(events_.front().lost_before, dropped);
        }
    }

    notification_delivery deliver_notification_events(
        notification_queue& queue, const std::function<app_message(const notification_event&)>& make, const std::function<std::uint64_t(app_message)>& post)
    {
        notification_delivery delivery {};
        while (std::optional<notification_event> event { queue.take() })
        {
            app_message message {};
            try
            {
                message = make(*event);
            }
            catch (...)
            {
                ++delivery.failed_calls;
                continue;
            }
            if (message.empty() == false && post(std::move(message)) == 0)
            {
                queue.put_back(std::move(*event));
                delivery.stalled = true;
                break;
            }
            ++delivery.delivered;
            if (event->kind == notification_event_kind::activated)
                delivery.activated = true;
        }
        return delivery;
    }
}
