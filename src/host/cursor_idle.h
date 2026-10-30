#pragma once

#include <chrono>
#include <optional>

namespace luil {
    // 멈춘 포인터의 커서를 감추는 장부다 (`ui_element::set_cursor_idle_hide`).
    // 플랫폼 표면이 포인터 메시지마다 부르고, 기한에 다시 묻는다. 플랫폼을 모른다.
    //  - 시간을 상호작용 발행본에 싣지 않는 이유: 포인터가 움직일 때마다 발행본이 바뀌면 모든 창이
    //    다시 그려진다. 감춤은 그림이 아니라 커서 모양이라 표면 혼자 안다.
    class cursor_idle_tracker
    {
    public:
        using clock = std::chrono::steady_clock;

        // 포인터가 움직였거나 눌렸다. 감춰져 있었으면 참이다 — 부르는 쪽이 커서를 되돌린다.
        bool activity(clock::time_point now) noexcept
        {
            last_activity_ = now;
            return reveal();
        }

        // 그 자리에 정책이 새로 섰다 (전체 화면 보기로 들어갔다). 감춤 시계를 지금부터 잰다.
        //  - 그 전의 마지막 움직임에서 재면, 오래 멈춰 있던 포인터 아래 정책이 서는 순간 커서가 사라진다.
        void restart(const clock::time_point now) noexcept
        {
            last_activity_ = now;
        }

        // 포인터가 창을 떠났거나, 창이 초점을 잃었거나, 그 자리의 정책이 빠졌다.
        // 감춰져 있었으면 참이다.
        bool reveal() noexcept
        {
            const bool was_hidden { hidden_ };
            hidden_ = false;
            return was_hidden;
        }

        // 지금 감출 때인가. `delay`가 0이면 감추지 않는다. 감출 때면 감춤 상태로 들어가고 참이다.
        //  - 이미 감췄으면 거짓이다 (한 번만 감춘다).
        bool hide_if_due(const clock::time_point now, const std::chrono::milliseconds delay) noexcept
        {
            if (hidden_ || delay <= std::chrono::milliseconds { 0 } || last_activity_.has_value() == false || now - *last_activity_ < delay)
                return false;
            hidden_ = true;
            return true;
        }

        // 감출 시각이다. 감췄거나 정책이 없으면 없다.
        [[nodiscard]] std::optional<clock::time_point> deadline(const std::chrono::milliseconds delay) const noexcept
        {
            if (hidden_ || delay <= std::chrono::milliseconds { 0 } || last_activity_.has_value() == false)
                return std::nullopt;
            return *last_activity_ + delay;
        }

        [[nodiscard]] bool hidden() const noexcept
        {
            return hidden_;
        }

    private:
        std::optional<clock::time_point> last_activity_ {};
        bool hidden_ { false };
    };
}
