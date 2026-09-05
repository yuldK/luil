#pragma once

#include <chrono>
#include <optional>

namespace luil {
    // 값 하나가 시간에 따라 옮겨 가는 것이다 (사이드바 접힘 폭·페이드).
    //
    // **앱 상태다.** 라이브러리는 이 구조체를 들고 있지 않는다 — 앱이 들고,
    // 지금 값을 물어(`transition_value`) element에 넘긴다. 접힘·선택·스크롤 값이
    // 앱 상태인 것과 같은 규칙이고, 그래서 element는 "전환 중"이라는 것을 몰라도 된다.
    //
    // **시각만의 함수다.** 어느 frame에 물어도 같은 값이 나오고 그 사이의 상태가
    // 없다 — spinner의 각도(`spinner_angle`)와 토스트의 불투명도(`opacity_at`)가
    // 이미 선 그 규칙이다. 그래서 frame을 건너뛰어도 어긋나지 않고, test가 시각을
    // 손으로 정해 확인할 수 있다.
    struct transition
    {
        float from { 0.0f };
        float to { 0.0f };
        // 시작 시각이다. `now`가 이보다 이르면 아직 `from`이다.
        std::chrono::steady_clock::time_point started {};
        // 0이면 전환이 없다 — 언제 물어도 `to`다.
        std::chrono::milliseconds duration { 0 };
    };

    // 통상적인 전환 길이다.
    // 더 길면 굼떠 보이고 더 짧으면 튀어 보인다 — 정하는 것은 소비자이고 이 값은
    // 고를 것이 없을 때의 기본이다.
    inline constexpr std::chrono::milliseconds transition_duration { 140 };
    // 전환이 도는 동안 다음 frame을 물어볼 간격이다.
    // 라이브러리는 시계도 frame 루프도 갖지 않으므로 "이만큼 뒤에 다시 물어라"만
    // 답한다 (`next_tick`·`next_update`가 쓰는 그 계약이다).
    inline constexpr std::chrono::milliseconds transition_frame { 16 };

    // 완만하게 시작하고 완만하게 멈춘다. [0, 1] → [0, 1].
    // 범위 밖 입력은 끝값으로 잘린다.
    //  - 2차식이다. 3차 이상이 더 부드럽지만 눈으로 갈리지 않고, 식이 짧아야
    //    test가 손으로 검산할 수 있다.
    [[nodiscard]] constexpr float ease_in_out(const float t) noexcept
    {
        if (t <= 0.0f)
            return 0.0f;
        if (t >= 1.0f)
            return 1.0f;
        if (t < 0.5f)
            return 2.0f * t * t;
        const float back { -2.0f * t + 2.0f };
        return 1.0f - back * back / 2.0f;
    }

    // 전환이 끝났는가.
    // 길이가 0이면 시작한 적도 없이 끝난 것이다.
    [[nodiscard]] constexpr bool transition_finished(const transition& value, const std::chrono::steady_clock::time_point now) noexcept
    {
        return value.duration <= std::chrono::milliseconds { 0 } || now >= value.started + value.duration;
    }

    // `now` 시점의 값이다.
    [[nodiscard]] constexpr float transition_value(const transition& value, const std::chrono::steady_clock::time_point now) noexcept
    {
        if (transition_finished(value, now))
            return value.to;
        if (now <= value.started)
            return value.from;
        const auto elapsed { std::chrono::duration_cast<std::chrono::milliseconds>(now - value.started) };
        const float t { static_cast<float>(elapsed.count()) / static_cast<float>(value.duration.count()) };
        return value.from + (value.to - value.from) * ease_in_out(t);
    }

    // 다음에 다시 물어야 하는 시각이다 (끝났으면 nullopt).
    // 아직 시작 전이면 시작 시각이고, 도는 중이면 한 frame 뒤다.
    //  - 답이 없으면 시간 경로가 잠잔다는 것이 `next_tick`의 계약이라, 끝난 전환이
    //    logic thread를 계속 깨우지 않는다.
    [[nodiscard]] constexpr std::optional<std::chrono::steady_clock::time_point> transition_next_update(const transition& value, const std::chrono::steady_clock::time_point now) noexcept
    {
        if (transition_finished(value, now))
            return std::nullopt;
        if (now < value.started)
            return value.started;
        return now + transition_frame;
    }

    // `now`에서 `target`으로 새로 출발하는 전환이다.
    // 도는 중이면 **지금 값에서** 이어 출발한다 — 끝값으로 튀지 않는다.
    [[nodiscard]] constexpr transition transition_to(
        const transition& value, const float target, const std::chrono::steady_clock::time_point now, const std::chrono::milliseconds duration = transition_duration) noexcept
    {
        return transition { transition_value(value, now), target, now, duration };
    }
} // namespace luil
