#include "host/surface_invalidate.h"

#include <algorithm>

namespace luil {
    namespace {
        // 예고한 시각을 확실히 지나 있도록 더하는 여유다.
        // timer는 밀리초 단위로 잘려 오므로, 여유 없이 깨우면 element가
        // "아직 아니다"라며 같은 시각을 다시 예고해 한 번 더 돈다.
        constexpr std::chrono::milliseconds update_timer_slack { 15 };
    } // namespace

    std::vector<std::u8string> surfaces_to_repaint(const std::span<const surface_content> surfaces, const bool every_surface)
    {
        std::vector<std::u8string> repaint {};
        repaint.reserve(surfaces.size());
        for (const surface_content& content : surfaces)
            if (every_surface || content.current != content.posted)
                repaint.push_back(content.surface);
        return repaint;
    }

    update_timer_plan plan_update_timer(const std::span<const surface_update_deadline> deadlines, const std::chrono::steady_clock::time_point now, const std::chrono::milliseconds repaint_interval)
    {
        std::optional<std::chrono::steady_clock::time_point> earliest {};
        for (const surface_update_deadline& deadline : deadlines)
            if (deadline.next.has_value() && (earliest.has_value() == false || *deadline.next < *earliest))
                earliest = deadline.next;

        update_timer_plan plan {};
        // 아무도 예고하지 않았으면 걸 timer가 없다.
        if (earliest.has_value() == false)
            return plan;

        plan.armed = true;
        // "지금 이하"는 연속 애니메이션이라 짧은 주기로 잇는다.
        std::chrono::milliseconds delay { repaint_interval };
        if (*earliest > now)
            delay = std::chrono::duration_cast<std::chrono::milliseconds>(*earliest - now) + update_timer_slack;
        constexpr std::chrono::milliseconds::rep maximum { maximum_update_timer_delay };
        const std::chrono::milliseconds capped { std::min(delay.count(), maximum) };
        plan.delay_milliseconds = static_cast<unsigned int>(capped.count());

        // 그 tick까지 시각이 되는 표면이 함께 깨어난다.
        // 상한이 지연을 줄인 경우에도 가장 이른 예고를 답한 표면은 반드시 든다 —
        // 그 표면을 빠뜨리면 예고를 낸 쪽이 영영 깨어나지 않는다.
        const std::chrono::steady_clock::time_point fires_at { now + capped };
        for (const surface_update_deadline& deadline : deadlines)
            if (deadline.next.has_value() && (*deadline.next <= fires_at || *deadline.next == *earliest))
                plan.wake.push_back(deadline.surface);
        return plan;
    }
} // namespace luil
