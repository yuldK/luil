#include "luil/ui/ui_platform.h"

#include <mutex>

namespace luil {
    namespace {
        // 시작할 때 한 번 쓰고 여러 thread가 읽는다. 값이 여럿이라 한 번에 읽히도록 잠금으로 묶는다
        // (읽는 쪽은 frame을 지을 때 몇 번뿐이다).
        std::mutex platform_mutex {};
        ui_platform platform_value {};
    } // namespace

    void set_ui_platform(const ui_platform& platform) noexcept
    {
        const std::lock_guard lock { platform_mutex };
        platform_value = platform;
    }

    ui_platform current_ui_platform() noexcept
    {
        const std::lock_guard lock { platform_mutex };
        return platform_value;
    }
} // namespace luil
