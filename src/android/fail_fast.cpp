#include "host/fail_fast.h"

#include <cstdlib>

namespace luil {
    void platform_fail_fast() noexcept
    {
        // abort는 SIGABRT로 끝나 tombstone과 logcat에 호출 스택이 남는다.
        std::abort();
    }
} // namespace luil
