#pragma once

namespace luil {
    // 배치가 공유하는 값과 순수 함수다.
    //
    // 여기에는 element도 config도 두지 않는다. 어느 한 컨테이너의 것이 아니라
    // **여럿이 같은 식을 써야 하는 것**만 온다 — 식이 두 벌이 되면 그 둘은
    // 반드시 어긋난다.

    // 창 폭의 등급이다 (Material의 창 크기 구간, device-request-design.md의 창 폭 등급).
    // 배치를 기기 방향이 아니라 창 폭으로 고르면 화면 분할·접는 기기·태블릿·데스크톱 창을 한 규칙으로 다룬다.
    enum class window_width_class
    {
        // 휴대폰 세로, 좁게 나눈 화면이다. 한 판씩 보인다.
        compact,
        // 휴대폰 가로, 접는 기기를 편 것, 작은 태블릿이다. 두 판이 들어간다.
        medium,
        // 태블릿 가로, 데스크톱 창이다. 옆 판·옆 시트가 늘 설 자리가 있다.
        expanded,
    };

    // 등급의 경계다 (논리 픽셀, Android의 dp). 이 폭부터 그 등급이다.
    struct window_width_breakpoints
    {
        float medium { 600.0f };
        float expanded { 840.0f };

        [[nodiscard]] bool operator==(const window_width_breakpoints&) const noexcept = default;
    };

    // 논리 폭을 등급으로 옮긴다.
    //  - 폭이 유한하지 않으면 compact다.
    //  - `expanded`가 `medium`보다 작으면 expanded는 서지 않는다 (경계가 뒤집힌 설정을 추측해 고치지 않는다).
    [[nodiscard]] constexpr window_width_class classify_window_width(const float width, const window_width_breakpoints breakpoints = {}) noexcept
    {
        if (width >= breakpoints.expanded && breakpoints.expanded >= breakpoints.medium)
            return window_width_class::expanded;
        if (width >= breakpoints.medium)
            return window_width_class::medium;
        return window_width_class::compact;
    }

    // 네 변의 여백이다 (논리 픽셀).
    struct edge_insets
    {
        float left { 0.0f };
        float top { 0.0f };
        float right { 0.0f };
        float bottom { 0.0f };

        [[nodiscard]] static constexpr edge_insets all(const float value) noexcept
        {
            return { value, value, value, value };
        }

        [[nodiscard]] static constexpr edge_insets symmetric(const float horizontal, const float vertical) noexcept
        {
            return { horizontal, vertical, horizontal, vertical };
        }

        [[nodiscard]] bool operator==(const edge_insets&) const noexcept = default;
    };

    // 칸 안에서 자식을 어디에 둘지다 (한 축). 표 배치(`grid_element`)와 겹침 배치(`layer_element`)가 같은 값을 쓴다.
    //  - `stretch`는 칸을 다 채운다. 나머지는 자식의 원하는 크기(`ui_element::preferred_size`)로 서고, 답이 없으면
    //    칸을 다 채운다.
    //  - stack의 교차축 정렬(`stack_alignment`)과 뜻이 같지만 그쪽은 그대로 둔다 (이미 쓰는 이름이다).
    enum class layout_alignment
    {
        start,
        center,
        end,
        stretch,
    };

    // 칸(`start`부터 `available`만큼) 안에 `length`를 정렬한 시작점이다. 같은 단위면 무엇이든 된다.
    //  - `stretch`는 시작점이 칸의 처음이다 (길이는 부르는 쪽이 칸 전체로 잡는다).
    //  - 길이가 칸보다 길면 가운데·끝은 칸 앞으로 넘친다. 자르지 않는다 (stack의 교차축과 같다).
    [[nodiscard]] constexpr float align_within(const float start, const float available, const float length, const layout_alignment alignment) noexcept
    {
        switch (alignment)
        {
        case layout_alignment::center:
            return start + (available - length) / 2.0f;
        case layout_alignment::end:
            return start + available - length;
        case layout_alignment::start:
        case layout_alignment::stretch:
            break;
        }
        return start;
    }

    // 흘리는 창의 위·아래 가장자리 표시다.
    //
    // 흘린 내용이 위로 지나갔으면 위에, 아래에 더 있으면 아래에 그림자를 드리워
    // 잘린 줄이 경계 밖으로 이어진다는 것을 말한다. 구분선은 머리글이나 단추와
    // 맞닿는 쪽에 긋는다 — 그것이 없으면 흘러가는 행이 그 위의 머리글과 섞인다.
    //  - 여기 있는 이유는 두 컨테이너(`scroll_area_element`·`list_element`)가 같은
    //    설정을 받고 같은 함수(`draw_scroll_edges`)로 그려야 해서다. 판정이 두 벌이면
    //    한쪽만 고친 변경이 남는다.
    //  - 기본값은 전부 거짓이다. 지금까지의 창이 이 설정의 특수 경우다.
    struct scroll_edges
    {
        // 흘린 만큼 위에, 더 흘릴 만큼 아래에 그림자를 드리운다.
        bool shadows { false };
        // 위·아래 가장자리에 늘 1px 구분선을 긋는다.
        bool top_rule { false };
        bool bottom_rule { false };
    };

    // 스크롤 값을 [0, content - viewport]로 다듬는다 (전부 논리 픽셀).
    // 흘리는 컨테이너의 `arrange`가 같은 식으로 다듬으므로, tree를 만들기
    // **전에** 앱 상태를 이 함수로 다듬으면 창·스크롤 막대·내용 가상화가 전부
    // 같은 값을 본다 — 앱이 식을 다시 쓰면 둘이 어긋난다.
    //  - 이름에 축을 넣지 않는다. 식이 축과 무관해서다 — 세로 창과 가로 띠가
    //    같은 함수를 쓰고, 축마다 함수를 두면 같은 식이 다시 둘이 된다.
    [[nodiscard]] constexpr float clamp_scroll(const float content_length, const float viewport_length, const float offset) noexcept
    {
        const float maximum { content_length > viewport_length ? content_length - viewport_length : 0.0f };
        if (offset < 0.0f)
            return 0.0f;
        return offset > maximum ? maximum : offset;
    }

    // 대상을 창 안으로 들이는 데 필요한 스크롤 **변화량**이다 (전부 논리 픽셀).
    // 이미 안이면 0이라, 부르는 쪽이 "움직일 것이 없다"를 따로 세지 않는다.
    //  - 이름에 축을 넣지 않는다. `clamp_scroll`과 같은 이유다 — 세로 창과 가로
    //    띠가 같은 식을 쓴다.
    //  - 부호는 휠과 같다: 양수 = offset 증가 = 내용이 위(왼쪽)로. 뒤집으면
    //    목록이 초점에서 **달아나고**, `clamp_scroll`이 0에 붙여 화면에는
    //    "아무 일도 안 일어남"으로 보인다.
    //  - **대상이 창보다 길면 앞쪽 끝을 맞춘다.** 뒤를 맞추면 첫 줄이 잘린 채
    //    서서 어디에 초점이 있는지 보이지 않는다.
    //  - 변화량인 것이 요점이다. 절대 offset을 답하면 앱이 `scroll_ = value`로
    //    받게 되어, `scroll_ += delta`인 휠·막대·목록과 길이 두 갈래가 된다
    //    (focus-reveal-design.md).
    [[nodiscard]] constexpr float scroll_delta_to_reveal(const float target_begin, const float target_length, const float viewport_begin, const float viewport_length) noexcept
    {
        if (target_begin < viewport_begin || target_length > viewport_length)
            return target_begin - viewport_begin;
        const float target_end { target_begin + target_length };
        const float viewport_end { viewport_begin + viewport_length };
        return target_end > viewport_end ? target_end - viewport_end : 0.0f;
    }
} // namespace luil
