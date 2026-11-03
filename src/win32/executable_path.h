#pragma once

#include <string>

// 이 프로세스 실행 파일의 경로다. 점프 목록, 알림 활성화기의 명령 줄, 자동 업데이트의 적용이 같은 길로 읽는다.
namespace luil::win32 {
    // 이 프로세스 실행 파일의 절대 경로다. 읽지 못하면 비었다.
    //  - `MAX_PATH`보다 긴 경로도 버퍼를 늘려 끝까지 읽는다.
    [[nodiscard]] std::wstring current_executable_path();
}
