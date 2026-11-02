#pragma once

#include "luil/ui/ui_element.h"

#include <optional>

namespace luil {
    // 요소의 안쪽 여백을 재는 바탕이 되는 자리다. 배치된 보이는 자식들의 bounds 합집합을 요소 자신의 bounds로 자른 것이다.
    //  - 자식이 없거나 모두 숨었거나 배치되지 않았으면 비어 있다. 넓이 없는 자식(일부러 0인 자리)은 합집합을 끌어당기지 않는다.
    //  - 정보 상자의 `inset`과 overlay의 안쪽 여백 띠가 같은 답을 쓰도록 한 곳에 둔다. 공개 형식이 아니다.
    [[nodiscard]] std::optional<rect_f> inspect_content_bounds(const ui_element& element);
}
