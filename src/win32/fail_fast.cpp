#include "host/fail_fast.h"

#include <intrin.h>

namespace luil {
    void platform_fail_fast() noexcept
    {
        // winnt.h의 FAST_FAIL_FATAL_APP_EXIT다. windows.h 없이 값만 쓴다.
        constexpr unsigned int fast_fail_fatal_app_exit { 7 };
        __fastfail(fast_fail_fatal_app_exit);
    }
} // namespace luil
