#pragma once

#include <cstddef>

namespace luil::win32 {
    // 이 뒤에 거는 감시의 버퍼 크기를 바꾼다. 0이면 기본(64 KB)이다.
    //  - test 손잡이다. 작은 버퍼로 OS 넘침(빈 결과)을 일으켜 rescan을 본다. 앱이 부를 자리가 아니다.
    void set_file_watch_buffer_bytes_for_tests(std::size_t bytes) noexcept;
}
