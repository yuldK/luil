#pragma once

#include "luil/theme/ui_theme.h"
#include "luil/ui/image_element.h"
#include "luil/ui/ui_element.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace luil {
    // 잉크의 점 하나다 (ink-design.md). 좌표는 내용 논리 픽셀이고 압력은 0~1이다.
    struct ink_point
    {
        float x { 0.0f };
        float y { 0.0f };
        float pressure { 1.0f };

        [[nodiscard]] bool operator==(const ink_point&) const noexcept = default;
    };

    // 획의 모양이다.
    //  - color가 비면 그리는 시점 팔레트의 `primary_foreground`다 (결정 6). 테마·고대비 전환을 따른다. 색을 가진 획은
    //    문서 내용이라 그 색 그대로다.
    //  - width는 가장 센 압력의 폭이다 (내용 논리 픽셀). 폭 = width × (minimum_ratio + (1 − minimum_ratio) × pressure)이다.
    struct ink_style
    {
        std::optional<ui_color> color {};
        float width { 2.0f };
        float minimum_ratio { 0.25f };

        [[nodiscard]] bool operator==(const ink_style&) const noexcept = default;
    };

    // 압력 하나의 폭이다 (`ink_style`의 식). 압력은 0~1로 접고, 폭과 비율이 유한하지 않거나 음수면 0이다.
    [[nodiscard]] float ink_width_at(const ink_style& style, float pressure) noexcept;

    struct ink_stroke_backing;
    struct ink_layer_backing;
    struct ink_access;

    // 굳은 획 하나다. 만들 때 그릴 path와 경계를 지어 든다.
    //  - 값이 바뀌지 않아 thread 사이로 나눠 그린다. 복사는 같은 실체를 나눈다.
    //  - 기본값은 id 0의 빈 획이다 (점이 없다). 진행 중 획(`ink_canvas_config::wet`)의 기본값이다.
    class ink_stroke
    {
    public:
        ink_stroke() = default;

        [[nodiscard]] std::uint64_t id() const noexcept;
        [[nodiscard]] std::span<const ink_point> points() const noexcept;
        [[nodiscard]] const ink_style& style() const noexcept;
        // 그리는 모양의 경계다 (내용 논리 픽셀, 폭 포함). 빈 획은 빈 사각형이다.
        [[nodiscard]] rect_f bounds() const noexcept;
        [[nodiscard]] bool empty() const noexcept;

    private:
        friend ink_stroke make_ink_stroke(std::uint64_t id, std::vector<ink_point> points, ink_style style);
        friend struct ink_access;

        std::shared_ptr<const ink_stroke_backing> backing_ {};
    };

    // 획 하나를 짓는다. 유한하지 않은 점은 버리고 압력은 0~1로 접는다. 점이 없으면 빈 획이다. 점 하나는 둥근 점이다.
    //  - 그리는 모양은 점마다 반지름이 폭의 절반인 원과 이웃 원을 잇는 사다리꼴을 한 path에 모아 nonzero로 채운 것이다.
    //    겹친 곳이 두 번 칠해지지 않아 반투명 색도 고르다. 선분 사이는 Catmull-Rom으로 보간해 그린다 (저장한 점은 그대로다).
    [[nodiscard]] ink_stroke make_ink_stroke(std::uint64_t id, std::vector<ink_point> points, ink_style style);

    // 확정 때 점을 줄인다 (Ramer-Douglas-Peucker). 자리 오차(`tolerance`, 내용 논리 픽셀)와 압력 오차(`pressure_tolerance`)를
    // 함께 본다. 어느 쪽이든 오차를 넘는 점은 남는다. 처음과 끝은 언제나 남는다.
    //  - `tolerance`가 0 이하이거나 유한하지 않으면 그대로다. `pressure_tolerance`가 그렇다면 압력은 보지 않는다.
    [[nodiscard]] std::vector<ink_point> simplify_ink_points(std::span<const ink_point> points, float tolerance, float pressure_tolerance);

    // 굳은 획 묶음이다. 불변 값이고 그림 한 장(SkPicture)을 memo로 든다 (결정 7).
    //  - 그림은 처음 그릴 때 기록하고 기본색이 같으면 다시 쓴다. 복사한 묶음은 그 memo를 함께 나눈다. 앱이 frame마다
    //    같은 묶음을 config에 실어도 다시 기록하지 않는다.
    //  - 더하기·빼기는 새 묶음을 돌려준다. 새 묶음은 처음 그릴 때 한 번 다시 기록한다.
    class ink_layer
    {
    public:
        ink_layer() = default;

        [[nodiscard]] std::span<const ink_stroke> strokes() const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] bool empty() const noexcept;
        // 획 하나를 끝에 더한 묶음이다. 빈 획은 더하지 않는다.
        [[nodiscard]] ink_layer with(ink_stroke stroke) const;
        // 그 id의 획을 뺀 묶음이다. 없는 id는 무시한다 (지우개가 앱이 지우기 전에 같은 id를 다시 낼 수 있다).
        [[nodiscard]] ink_layer without(std::span<const std::uint64_t> ids) const;

    private:
        friend struct ink_access;

        std::shared_ptr<const ink_layer_backing> backing_ {};
    };

    // 지우개가 `from`에서 `to`까지 반지름 `radius`로 쓸 때 닿는 획 id다 (묶음의 순서). 경계 상자로 먼저 거른다.
    //  - 닿음은 지우개 선분과 획의 선분(점 하나면 그 점) 사이의 거리가 `radius` + 그 자리 폭의 절반 이하인 것이다.
    //  - 압력은 보지 않는다. `radius`가 음수이거나 유한하지 않으면 0이다.
    [[nodiscard]] std::vector<std::uint64_t> ink_strokes_hit(const ink_layer& layer, ink_point from, ink_point to, float radius);

    // 묶음을 그림 하나로 옮긴다 (내보내기). `size`는 내용 논리 픽셀이고 그림은 `size × scale` 픽셀이다.
    //  - 바탕은 투명하다. 기본색 획은 `palette.primary_foreground`로 칠한다. PNG는 앱이 `encode_png`로 쓴다.
    //  - 크기·배율이 양수가 아니거나 유한하지 않으면 빈 그림이다.
    [[nodiscard]] ui_image render_ink_image(const ink_layer& layer, size_f size, float scale, const ui_color_palette& palette);
}
