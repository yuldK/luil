#include "host/focus_request_tracker.h"

namespace luil {
    bool focus_target_ready(const ui_tree& tree, const ui_element_id& target)
    {
        const ui_element* const element { tree.find(target) };
        if (element == nullptr || element->focusable() == false || tree.root() == nullptr)
            return false;
        // 숨긴 조상 아래의 칸은 그 자신이 보여도 그려지지 않는다.
        const std::optional<rect_f> shown { tree.visible_bounds(*element) };
        if (shown.has_value() == false || shown->width <= 0.0f || shown->height <= 0.0f)
            return false;
        const rect_f surface { tree.root()->bounds() };
        return shown->x < surface.x + surface.width && shown->x + shown->width > surface.x && shown->y < surface.y + surface.height && shown->y + shown->height > surface.y;
    }

    focus_request_tracker::step focus_request_tracker::advance(
        const std::optional<focus_request>& published, const std::chrono::steady_clock::time_point now, const std::function<bool(const focus_request&)>& ready)
    {
        step result {};
        if (published.has_value() && published->id != 0 && published->id != last_id_)
        {
            last_id_ = published->id;
            if (pending_.has_value())
                result.reports.push_back(report { pending_->id, focus_request_outcome::superseded });
            pending_ = published;
            deadline_ = now + published->timeout;
        }
        if (pending_.has_value() == false)
            return result;

        if (ready != nullptr && ready(*pending_))
        {
            result.reports.push_back(report { pending_->id, focus_request_outcome::fulfilled });
            result.fulfil = std::move(pending_);
            pending_.reset();
            return result;
        }
        if (now >= deadline_)
        {
            result.reports.push_back(report { pending_->id, focus_request_outcome::expired });
            pending_.reset();
        }
        return result;
    }

    std::optional<std::chrono::steady_clock::time_point> focus_request_tracker::deadline() const noexcept
    {
        if (pending_.has_value() == false)
            return std::nullopt;
        return deadline_;
    }
} // namespace luil
