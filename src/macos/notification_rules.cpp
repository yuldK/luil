#include "macos/notification_rules.h"

#include <utility>

namespace luil::macos {
    notification_availability notification_availability_of(const notification_settings& settings) noexcept
    {
        switch (settings.authorization)
        {
        case notification_authorization::not_determined:
            return notification_availability::permission_needed;
        case notification_authorization::denied:
            return notification_availability::blocked;
        case notification_authorization::authorized:
        case notification_authorization::provisional:
            return settings.alerts || settings.center ? notification_availability::available : notification_availability::blocked;
        case notification_authorization::other:
            break;
        }
        return notification_availability::available;
    }

    permission_state notification_permission_of(const notification_authorization authorization) noexcept
    {
        switch (authorization)
        {
        case notification_authorization::authorized:
        case notification_authorization::provisional:
        case notification_authorization::other:
            return permission_state::granted;
        case notification_authorization::not_determined:
        case notification_authorization::denied:
            break;
        }
        return permission_state::denied;
    }

    std::optional<notification_request_content> make_notification_request(const notification& value, std::u8string& error)
    {
        std::optional<notification_text> text { prepare_notification_text(value, error) };
        if (text.has_value() == false)
            return std::nullopt;
        notification_request_content content {};
        content.identifier = value.id;
        content.title = std::move(text->title);
        content.body = std::move(text->body);
        content.sound = value.silent == false;
        return content;
    }

    std::optional<notification_event> notification_event_from_response(const std::u8string_view identifier, const notification_response_action action, const bool launched)
    {
        if (notification_id_valid(identifier) == false)
            return std::nullopt;
        notification_event event {};
        event.id = std::u8string { identifier };
        if (action == notification_response_action::dismiss)
        {
            event.kind = notification_event_kind::dismissed;
            return event;
        }
        event.kind = notification_event_kind::activated;
        event.launched = launched;
        return event;
    }

    void notification_launch_tracker::finish_launching(const bool by_notification) noexcept
    {
        if (finished_)
            return;
        finished_ = true;
        pending_ = by_notification;
    }

    bool notification_launch_tracker::take() noexcept
    {
        if (finished_ == false)
            return true;
        const bool launched { pending_ };
        pending_ = false;
        return launched;
    }

    bool notification_launch_tracker::finished() const noexcept
    {
        return finished_;
    }

    void notification_relay::set_receiver(std::function<void(notification_event)> receiver)
    {
        receiver_ = std::move(receiver);
        flush();
    }

    void notification_relay::deliver(notification_event event)
    {
        pending_.push(std::move(event));
        flush();
    }

    std::size_t notification_relay::pending() const
    {
        return pending_.size();
    }

    void notification_relay::flush()
    {
        // 받는 자리가 안에서 자리를 바꾸거나 비워도 다음 사건부터 그것을 따른다.
        while (receiver_ != nullptr)
        {
            std::optional<notification_event> event { pending_.take() };
            if (event.has_value() == false)
                return;
            const std::function<void(notification_event)> receiver { receiver_ };
            try
            {
                receiver(std::move(*event));
            }
            catch (...)
            {
            }
        }
    }
}
