#include <catch2/catch_test_macros.hpp>

// "asan: " 이름을 단 test가 sanitizer 없는 바이너리로 통과하는 일을 막는
// 관문이다. ASan은 /RTC1과 충돌해 Debug가 아닌 구성에만 켜지므로
// (tests/CMakeLists.txt), Debug에서는 이 target의 test가 DISABLED로 등록된다.
// 그 배선이 어긋나 ASan 없는 실행이 여기까지 오면 이 test가 즉시 실패한다.
TEST_CASE("The asan test binary is instrumented with AddressSanitizer", "[asan]")
{
#if defined(__SANITIZE_ADDRESS__)
    SUCCEED();
#else
    FAIL("이 실행 파일은 /fsanitize=address 없이 빌드되었다");
#endif
}
