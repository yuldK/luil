#pragma once

#include "luil/ui/ui_element.h"

#include <string_view>

namespace luil {
    // 순서를 바꾸는 항목(목록 행·탭)이 drop을 받는 규칙 하나다.
    //
    // 받는 것은 **같은 컨테이너의 같은 종류**뿐이고, 자기 자리는 이동이 아니라
    // 받지 않는다. 키만 보면 목록 행이 탭 막대 위에, 한 목록의 행이 다른 목록에
    // 놓여 그쪽 `reorder`가 소속되지 않은 키를 받는다 — 앱은 모르는 키로 모델을
    // 뒤진다. 규칙이 목록과 탭에 한 벌씩 살면 한쪽만 고쳐지므로 여기 하나다.
    //  - 밖에서 온 끌기(파일)는 `source`가 비어 있어 종류에서 걸러진다.
    [[nodiscard]] inline bool accepts_reorder_drop(const drag_payload& payload, const ui_element_kind item_kind, const ui_element_id& container, const std::u8string_view key) noexcept
    {
        return payload.source.kind == item_kind && payload.container == container && payload.dragged_owner.empty() == false && payload.dragged_owner != key;
    }
} // namespace luil
