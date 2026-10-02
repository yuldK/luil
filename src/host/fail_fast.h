#pragma once

namespace luil {
    // 회복할 수 없는 상태에서 프로세스를 곧바로 끝낸다.
    //
    // 예외·소멸자·atexit 처리를 거치지 않는다. 그 길을 타면 멈추지 않는 thread를 기다리는
    // join 같은, 이미 망가진 상태를 다시 밟기 때문이다. 오류 보고 도구가 원인을 볼 수
    // 있도록 OS의 즉시 종료 경로를 쓴다.
    // **플랫폼 계층이 하나를 정의한다** (Win32는 src/win32/fail_fast.cpp의 `__fastfail`).
    // core에 컴파일러 내장 함수나 OS 헤더를 들이지 않으려고 링크로 묶는다.
    [[noreturn]] void platform_fail_fast() noexcept;
} // namespace luil
