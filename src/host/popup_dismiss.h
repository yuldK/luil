#pragma once

#include "luil/app/app_host.h"
#include "luil/ui/ui_events.h"

#include <functional>
#include <optional>

namespace luil {
    // 닫힘 계기 하나를 popup 하나에 묻는다.
    // 낼 것이 없으면 nullopt다.
    //  - factory가 없는 popup은 어느 계기에도 반응하지 않는다.
    //  - factory가 **빈 액션**을 내면 앱이 이 계기에는 닫지 않기로 한 것이다.
    //    낸 것이 없으므로 표식도 쓰지 않는다.
    //  - `requested`는 "이 popup이 이미 닫자고 했다"는 표식이다. 한 계기에 한 번만
    //    내며, 새 frame이 popup을 계속 실으면 소유자가 푼다
    //    (Win32 `popup_surface::adopt`, 모바일은 overlay를 다시 세우는 앱 host).
    [[nodiscard]] std::optional<input_action> take_popup_dismiss_action(const std::function<input_action(popup_dismiss_reason)>& dismiss, bool& requested, popup_dismiss_reason reason);
} // namespace luil
