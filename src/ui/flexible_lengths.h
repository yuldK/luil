#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace luil {
    // 유연 항목 하나의 비율과 하한·상한이다 (물리 픽셀). 0 이하는 그 제한이 없다는 뜻이다.
    struct flexible_share
    {
        float weight { 1.0f };
        float minimum { 0.0f };
        float maximum { 0.0f };
    };

    // 남은 자리(`pool`)를 유연 항목들에게 비율대로 나누되 하한·상한을 지킨다 (stack·grid가 같이 쓴다).
    //  - 한 걸음마다 남은 항목의 몫을 견주어, 하한·상한을 어긴 항목을 그 값에 세운다. 세운 항목이 쓴 자리를 뺀 나머지를
    //    아직 서지 않은 항목이 다시 나눈다. 어긴 양의 합이 늘리는 쪽(하한)이면 하한 쪽만, 줄이는 쪽(상한)이면 상한 쪽만
    //    세운다 — 한쪽을 세우면 다른 쪽의 몫이 바뀌기 때문이다 (CSS flexbox의 "유연 길이 풀기"와 같은 절차다).
    //  - 그래서 한 항목이 하한에 걸리면 그 부족분은 다른 유연 항목의 몫에서 나온다. 하한들의 합이 자리를 넘을 때만
    //    전체가 넘친다.
    //  - 하한이 상한보다 크면 하한이 이긴다.
    //  - 모두 상한에 섰으면 합이 `pool`보다 작다. 남는 자리는 담는 쪽(주축 정렬)이 둔다.
    [[nodiscard]] inline std::vector<float> distribute_flexible(const std::span<const flexible_share> items, const float pool)
    {
        std::vector<float> lengths(items.size(), 0.0f);
        std::vector<bool> frozen(items.size(), false);
        // 한 걸음에 적어도 하나를 세우므로 항목 수만큼이면 끝난다.
        for (std::size_t step { 0 }; step <= items.size(); ++step)
        {
            float used { 0.0f };
            float open { 0.0f };
            for (std::size_t index { 0 }; index < items.size(); ++index)
            {
                if (frozen[index])
                    used += lengths[index];
                else
                    open += items[index].weight;
            }
            if (open <= 0.0f)
                break;
            const float free { pool - used > 0.0f ? pool - used : 0.0f };
            float violation { 0.0f };
            std::vector<float> excess(items.size(), 0.0f);
            for (std::size_t index { 0 }; index < items.size(); ++index)
            {
                if (frozen[index])
                    continue;
                const flexible_share& item { items[index] };
                const float share { free * (item.weight / open) };
                float clamped { share };
                if (item.maximum > 0.0f && clamped > item.maximum)
                    clamped = item.maximum;
                if (item.minimum > 0.0f && clamped < item.minimum)
                    clamped = item.minimum;
                lengths[index] = clamped;
                excess[index] = clamped - share;
                violation += excess[index];
            }
            bool froze { false };
            for (std::size_t index { 0 }; index < items.size(); ++index)
            {
                if (frozen[index] || excess[index] == 0.0f)
                    continue;
                // 합이 0이면(늘림과 줄임이 맞먹으면) 어긴 것을 모두 세운다.
                if (violation == 0.0f || (violation > 0.0f) == (excess[index] > 0.0f))
                {
                    frozen[index] = true;
                    froze = true;
                }
            }
            if (froze == false)
                break;
        }
        return lengths;
    }
}
