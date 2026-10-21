#pragma once

#include "luil/ui/virtual_list_element.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 여러 항목을 고르는 목록의 선택 상태다 (keyboard-selection-design.md).
    //
    // **값은 앱이 든다.** 라이브러리는 입력의 뜻(`ui_action_context`, `virtual_list_move_context`)을 메시지로
    // 보내고, 앱은 받은 메시지에서 이 파일의 순수 함수로 다음 상태를 계산해 다음 frame에 목록에 싣는다.
    // 규칙을 앱마다 다시 짜면 반드시 한 앱이 기준점을 틀린다 — Qt의 ExtendedSelection, WPF DataGrid의
    // Extended 모드, Windows 탐색기가 같은 규칙을 쓴다.
    //
    // 세 값이 갈린다.
    //  - `anchor`(기준점)는 Shift 범위가 시작하는 자리다. 그냥 고르거나 Ctrl로 하나를 뒤집을 때 그 자리로 옮긴다.
    //  - `cursor`는 키보드 초점 테가 선 자리다 (`virtual_list_config::cursor`).
    //  - `keys`는 고른 항목이다. **모델 순서**로 둔다 — 같은 선택이 언제나 같은 값이라 비교와 test가 쉽다.
    struct item_selection
    {
        std::u8string anchor {};
        std::u8string cursor {};
        std::vector<std::u8string> keys {};

        [[nodiscard]] bool operator==(const item_selection&) const = default;
    };

    // 그 키가 골라져 있는가.
    [[nodiscard]] bool selection_contains(const item_selection& selection, std::u8string_view key) noexcept;

    // 행을 눌렀다 (`virtual_list_config::pick`이 받은 문맥).
    //  - 그냥 누름: 그것 하나만 고르고 기준점·커서를 그리로 옮긴다.
    //  - Ctrl: 그것을 뒤집고 기준점·커서를 그리로 옮긴다.
    //  - Shift: 기준점부터 그것까지만 고른다. 기준점은 그대로다.
    //  - Ctrl+Shift: 기준점부터 그것까지를 지금 고른 것에 더한다.
    //  - 비활성 항목이나 모델에 없는 키는 아무것도 바꾸지 않는다. 범위 안의 비활성 항목은 고르지 않는다.
    [[nodiscard]] item_selection selection_pick(item_selection selection, std::span<const virtual_list_item> items, std::u8string_view key, const ui_action_context& context);

    // 키보드가 커서를 옮겼다 (`virtual_list_config::move_with`가 받은 문맥).
    //  - 수식 키 없음: 그것 하나만 고르고 기준점·커서를 그리로 옮긴다.
    //  - `extend`(Shift): 기준점부터 그것까지만 고른다.
    //  - `keep_selection`(Ctrl): 커서만 옮긴다.
    //  - 둘 다: 기준점부터 그것까지를 지금 고른 것에 더한다.
    //  - 기준점이 모델에 없으면 지금 커서를(그것도 없으면 옮겨 간 자리를) 기준점으로 삼는다.
    [[nodiscard]] item_selection selection_move(item_selection selection, std::span<const virtual_list_item> items, std::u8string_view key, const virtual_list_move_context& context);

    // 그 항목을 고른 것에 더하거나 뺀다 (Ctrl+Space, `virtual_list_config::toggle_at_cursor`). 기준점을 그리로 옮긴다.
    [[nodiscard]] item_selection selection_toggle(item_selection selection, std::span<const virtual_list_item> items, std::u8string_view key);

    // 활성 항목을 전부 고른다 (Ctrl+A, `virtual_list_config::select_all`). 기준점과 커서는 그대로다.
    [[nodiscard]] item_selection selection_select_all(item_selection selection, std::span<const virtual_list_item> items);

    // 모델이 바뀐 뒤(필터·정렬·삭제) 선택을 새 모델에 맞춘다.
    //  - 새 모델에 없는 키와 비활성이 된 키는 고른 것에서 빠진다. 남은 것은 새 모델 순서로 다시 선다.
    //  - 커서가 사라졌으면 **옛 모델에서 커서 뒤쪽의 가장 가까운 남은 항목**으로, 뒤에 없으면 앞쪽으로 옮긴다.
    //    옛 모델을 모르면(빈 span) 새 모델의 첫 활성 항목이다. 새 모델이 비면 비운다.
    //  - 기준점이 사라졌으면 같은 규칙으로 옮기되, 찾지 못하면 커서로 둔다.
    [[nodiscard]] item_selection selection_reconcile(item_selection selection, std::span<const virtual_list_item> previous, std::span<const virtual_list_item> items);
} // namespace luil
