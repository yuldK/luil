#pragma once

#include "luil/ui/spring.h"
#include "luil/ui/transition.h"

#include <chrono>
#include <memory>
#include <optional>
#include <variant>

namespace luil {
    class ui_tree;

    // 화면 전환의 모양이다 (docs/page-transition-design.md).
    //  - 밀기: 두 화면이 축을 따라 나란히 움직인다.
    //  - 옅어짐: 앞 절반에 이전 화면이 사라지고 뒤 절반에 새 화면이 나타난다. 두 화면이 반투명으로 겹치지 않는다.
    //  - 확대: 옅어짐에 배율을 얹는다.
    enum class page_transition_effect
    {
        slide,
        fade,
        zoom
    };

    enum class page_transition_axis
    {
        horizontal,
        vertical
    };

    // 앞으로(깊이 들어감)인가 뒤로(돌아옴)인가다. 밀기의 쪽과 확대의 쪽을 정한다.
    enum class page_transition_direction
    {
        forward,
        backward
    };

    // 화면 전환의 기본 길이다. 값 하나의 전환(`transition_duration`)보다 길다 — 화면 전체가 움직여 눈이 따라갈 시간이 든다.
    inline constexpr std::chrono::milliseconds page_transition_duration { 250 };

    // [0, 1] → [0, 1]인 easing이다 (`ease_in_out`).
    using page_transition_easing = float (*)(float) noexcept;

    // 길이 곡선이다. 길이가 0 이하면 전환이 없다. easing이 비면 `ease_in_out`이다.
    struct page_transition_timing
    {
        std::chrono::milliseconds duration { page_transition_duration };
        page_transition_easing easing { ease_in_out };
    };

    // 스프링 곡선이다. 진행이 0에서 1로 간다.
    //  - velocity는 진행 / 초다 (끌다 놓은 속도 ÷ 요소의 폭).
    //  - 허용치는 0.001을 넘지 않게 쓰고 진행은 [0, 1]로 자른다 (결정 6). 기본 허용치 0.01은 논리 픽셀 값에 맞춘 것이라
    //    0~1 진행에서는 마지막 1%가 한 번에 붙고, 넘치는 스프링은 밀기 가장자리에 바탕 띠를 남긴다.
    struct page_transition_spring
    {
        spring_config config { spring_smooth };
        float velocity { 0.0f };
    };

    using page_transition_curve = std::variant<page_transition_timing, page_transition_spring>;

    struct page_transition_style
    {
        page_transition_effect effect { page_transition_effect::slide };
        page_transition_axis axis { page_transition_axis::horizontal };
        page_transition_direction direction { page_transition_direction::forward };
        page_transition_curve curve { page_transition_timing {} };
    };

    // 도는 화면 전환이다. **앱 상태다** (`transition`·`spring`과 같다). 앱이 들고 전환 요소(`page_transition_element`)에 넘긴다.
    //  - 시각만의 함수다. 어느 frame에 물어도 같은 진행이 나온다.
    //  - 놓는 것은 앱이다. `page_transition_end`의 시각에 `tick`이 와서 끝났으면 prior를 버린다.
    struct page_transition
    {
        // 떠나는 화면이다. 앱이 따로 지어 요소가 설 자리와 같은 크기·같은 배율로 배치한 tree다. 비면 전환이 없다 (언제 물어도 끝).
        //  - 전환 요소·modal·시트·토스트처럼 root에 겹치는 것은 넣지 않는다. 화면 본문만이다.
        //  - `const`라 logic thread가 짓고 UI thread는 읽기만 한다 (tooltip 내용과 같은 규칙).
        std::shared_ptr<const ui_tree> prior {};
        std::chrono::steady_clock::time_point started {};
        page_transition_style style {};
    };

    // 새 전환이다. `reduce_motion`(`window_metrics::reduce_motion`)이 참이면 prior를 버린 값이다.
    //  - 곡선이 움직이지 않는 것(길이 0 이하, 쓸 수 없는 스프링)이어도 prior를 버린다. 언제 물어도 끝인 전환이 입력을 막지 않는다.
    //  - 짓는 비용까지 아끼려는 앱은 먼저 `reduce_motion`을 보고 이전 화면을 짓지 않는다.
    [[nodiscard]] page_transition start_page_transition(
        std::shared_ptr<const ui_tree> prior, const page_transition_style& style, std::chrono::steady_clock::time_point now, bool reduce_motion);

    // [0, 1]이다. 시작 전이면 0, 끝났거나 prior가 없으면 1이다.
    [[nodiscard]] float page_transition_progress(const page_transition& value, std::chrono::steady_clock::time_point now) noexcept;
    // `now`가 끝 시각(`page_transition_end`) 이후인가다. prior가 없으면 참이다.
    [[nodiscard]] bool page_transition_finished(const page_transition& value, std::chrono::steady_clock::time_point now) noexcept;
    // 끝나는 시각이다 (prior가 없으면 nullopt). 길이 곡선은 `started + duration`, 스프링은 `spring_rest_time`이다.
    //  - 앱이 `next_tick`에 싣는다. 스프링이어도 frame마다 깰 필요가 없다 (결정 2).
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> page_transition_end(const page_transition& value) noexcept;
    // `transition_next_update`와 같은 계약이다. 시작 전이면 시작 시각, 도는 중이면 `transition_frame` 뒤, 끝났으면 nullopt다.
    //  - 요소의 `next_update`가 쓴다.
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> page_transition_next_update(
        const page_transition& value, std::chrono::steady_clock::time_point now) noexcept;
}
