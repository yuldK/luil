#pragma once

// 판번 매크로(LUIL_VERSION_MAJOR 등)다.
// 근원은 CMake의 project VERSION 하나이고 configure가 생성한다.
// 소비자는 매크로로 `#if` 분기를 하고, 런타임 표시는 아래 함수를 쓴다.
#include "luil/generated/version.h"

namespace luil {
    // 라이브러리 판번 문자열이다 (LUIL_VERSION_STRING과 같다).
    [[nodiscard]] const char* library_version() noexcept;
} // namespace luil
