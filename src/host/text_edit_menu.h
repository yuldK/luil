#pragma once

#include "luil/ui/ui_tree.h"

namespace luil {
    // 플랫폼 안의 텍스트 편집 메뉴다. 동작은 host가 실행하고 텍스트 초점은 옮기지 않는다.
    [[nodiscard]] float text_edit_menu_height() noexcept;
    [[nodiscard]] std::shared_ptr<const ui_tree> make_text_edit_menu(const text_input_snapshot& value, bool korean, float width, float height, float scale);
} // namespace luil
