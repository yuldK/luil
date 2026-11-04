#pragma once

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_inspect.h"
#include "luil/ui/ui_tree.h"

namespace luil {
    // 검사기 overlay를 그린다 (inspector-design.md). 집은 요소의 윤곽, 부모의 점선, 안쪽 여백 띠, 정보 상자와 팔레트 견본이다.
    //  - 그리기만 한다. tree, interaction, config를 바꾸지 않고 요소도 아니라서 hit test, 입력, 초점, 접근성 tree에 닿지 않는다.
    //  - `tree`를 그린 변환 안에서 부른다. 주 tree는 안전 영역 원점으로 옮긴 좌표이고 popup layer는 layer 원점 안이다.
    //  - `config.enabled`가 거짓이거나 tree가 비어 있으면 아무것도 그리지 않는다.
    void draw_inspector(draw_context& context, const ui_tree& tree, const interaction_snapshot& interaction, const inspector_config& config);
}
