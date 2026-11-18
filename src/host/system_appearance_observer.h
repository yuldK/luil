#pragma once

#include "luil/app/app_delegate.h"

namespace luil {
    // 첫 관찰은 host마다 한 번이다. 프로세스의 최신 값은 훅을 부르기 전에 게시한다.
    class system_appearance_observer
    {
    public:
        [[nodiscard]] app_message observe(system_appearance value, app_delegate* delegate);

    private:
        std::optional<system_appearance> last_ {};
    };
}
