#pragma once

#include "luil/ui/ui_tree.h"

namespace luil {
    // 플랫폼 안의 텍스트 편집 메뉴다. 동작은 host가 실행하고 텍스트 초점은 옮기지 않는다.
    //  - caret이 링크 안이면(`text_input_snapshot::link`) "링크 열기"(`open-link`)·"주소 복사"(`copy-link`) 줄이 더 선다.
    //    터치에는 Ctrl이 없어 편집 칸의 링크를 여는 길이 이것이다.
    [[nodiscard]] bool text_edit_menu_has_link(const text_input_snapshot& value) noexcept;
    [[nodiscard]] float text_edit_menu_height(bool link = false) noexcept;
    [[nodiscard]] std::shared_ptr<const ui_tree> make_text_edit_menu(const text_input_snapshot& value, bool korean, float width, float height, float scale);
}
