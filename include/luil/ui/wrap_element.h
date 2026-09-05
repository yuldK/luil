#pragma once

#include "luil/ui/layout_metrics.h"
#include "luil/ui/ui_element.h"

#include <cstddef>
#include <memory>

namespace luil {
    // 한 줄에 들어가지 않으면 다음 줄로 넘기는 배치의 설정이다.
    //
    // 항목은 **전부 같은 크기**다.
    //  - 크기가 제각각이면 몇 줄이 될지를 담는 쪽이 미리 알 수 없는데, 측정 단계가
    //    없어 물어볼 수도 없다. 같은 크기면 줄 수가 폭에서 나오는 산수라
    //    `wrap_height_for`가 tree를 짓기 **전에** 답할 수 있다 — 이미 있는 정적
    //    사이저(`height_for`)들과 같은 결이다.
    //  - "길이는 담을 때 정한다"는 그대로다. 다만 그 길이가 항목마다가 아니라
    //    컨테이너에 한 번 적혀 있다.
    //
    // 비율(weight)은 두지 않는다 — 남는 자리가 줄마다 달라 "전체에서 몇 분의 몇"
    // 이라는 기준 자체가 사라진다.
    // 여백도 정렬도 두지 않는다 — 여백이 필요하면 감싸는 stack이 준다.
    struct wrap_config
    {
        // 항목 하나의 크기다 (논리 픽셀).
        float item_width { 0.0f };
        float item_height { 0.0f };
        // 한 줄 안 항목 사이 간격이다 (논리 픽셀).
        float spacing { 0.0f };
        // 줄과 줄 사이 간격이다 (논리 픽셀).
        float line_spacing { 0.0f };
    };

    // 한 줄에 들어가는 항목 수다 (논리 픽셀 입력). 적어도 1이다.
    //  - `item_width`가 0보다 크지 않으면 1이다. **나누지 않으므로** 크기를 주지
    //    않은 설정이 정의되지 않은 답을 내지 않는다.
    //  - 항목이 slot보다 넓어도 한 줄에 하나다. 그만큼 넘치고, 넘침은 stack과 같이
    //    이 element가 다루지 않는다.
    [[nodiscard]] constexpr std::size_t wrap_columns_for(const wrap_config& config, const float available_width) noexcept
    {
        if (config.item_width <= 0.0f)
            return 1;
        const float step { config.item_width + config.spacing };
        if (step <= 0.0f)
            return 1;
        // 마지막 항목 뒤에는 간격이 붙지 않으므로 폭에 간격 한 칸을 더해 나눈다.
        const float fits { (available_width + config.spacing) / step };
        return fits < 1.0f ? std::size_t { 1 } : static_cast<std::size_t>(fits);
    }

    // `count`개를 `available_width`에 담았을 때의 높이다 (전부 논리 픽셀).
    // `count`가 0이면 0이다.
    //  - `arrange`도 **같은 함수**로 열 수를 센다. 세는 식이 둘이면 담는 쪽이 잡은
    //    높이와 실제 줄 수가 어긋나 마지막 줄이 조용히 잘린다.
    [[nodiscard]] constexpr float wrap_height_for(const wrap_config& config, const float available_width, const std::size_t count) noexcept
    {
        if (count == 0)
            return 0.0f;
        const std::size_t columns { wrap_columns_for(config, available_width) };
        const std::size_t rows { (count + columns - 1) / columns };
        return static_cast<float>(rows) * config.item_height + static_cast<float>(rows - 1) * config.line_spacing;
    }

    // 자식을 왼쪽에서 오른쪽으로 채우다가 폭이 모자라면 다음 줄로 넘긴다.
    //
    // `stack_element`와 갈린 자리는 하나다: **줄이 여럿이다.** 그래서 간격이 두
    // 벌이고(`spacing`·`line_spacing`) 정렬과 비율이 뜻을 잃는다.
    // 줄이 slot보다 길어지면 stack과 같이 **그냥 넘친다** — 담는 쪽이
    // `wrap_height_for`로 높이를 잡으면 넘치지 않는다.
    //
    // 보이지 않는 자식도 자리를 그대로 차지하는 것은 stack과 같다.
    // 보임은 표시의 일이고 자리는 배치의 일이라 한 값이 둘을 정하지 않는다.
    class wrap_element final : public ui_element
    {
    public:
        wrap_element(ui_element_id id, wrap_config config);

        // 크기는 설정이 정하므로 자식만 담는다.
        void add(std::unique_ptr<ui_element> child);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        // 항목이 전부 같은 크기라 항목별로 기억할 것이 없다 — `children()`이 곧 순서다.
        wrap_config config_ {};
    };
} // namespace luil
