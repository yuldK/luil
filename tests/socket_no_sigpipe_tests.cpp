#include "net/socket_no_sigpipe.h"

#include <catch2/catch_test_macros.hpp>

#ifdef __APPLE__
#include <cerrno>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

TEST_CASE("A negative socket is refused", "[net][smb]")
{
    REQUIRE_FALSE(luil::net::suppress_sigpipe(-1));
}

#ifdef __APPLE__
// 상대가 닫은 소켓에 쓴다. 옵션이 없으면 SIGPIPE가 이 test 실행 파일을 끝낸다 (libsmb2처럼 `writev`로 쓴다).
TEST_CASE("Writing to a closed peer fails with EPIPE instead of a signal", "[net][smb]")
{
    int pair[2] { -1, -1 };
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    REQUIRE(luil::net::suppress_sigpipe(pair[0]));
    // 두 번 걸어도 된다.
    REQUIRE(luil::net::suppress_sigpipe(pair[0]));
    REQUIRE(::close(pair[1]) == 0);

    char byte { 'x' };
    iovec part { &byte, 1 };
    errno = 0;
    const ssize_t written { ::writev(pair[0], &part, 1) };
    const int error { errno };
    static_cast<void>(::close(pair[0]));
    REQUIRE(written == -1);
    REQUIRE(error == EPIPE);
}
#endif
