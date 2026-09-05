#include "win32/popup_dismiss.h"

#include <variant>

namespace luil::win32 {
    std::optional<input_action> take_popup_dismiss_action(const std::function<input_action(popup_dismiss_reason)>& dismiss, bool& requested, const popup_dismiss_reason reason)
    {
        if (dismiss == nullptr || requested)
            return std::nullopt;
        input_action action { dismiss(reason) };
        // 빈 액션은 "이 계기에는 닫지 않는다"는 뜻이다.
        // 낸 것이 없으므로 한 번 내기도 쓰지 않는다 — 같은 frame의 다음 계기는
        // 다시 물어야 한다.
        if (std::holds_alternative<std::monostate>(action))
            return std::nullopt;
        requested = true;
        return action;
    }
} // namespace luil::win32
