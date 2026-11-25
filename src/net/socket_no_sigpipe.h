#pragma once

// 끊긴 TCP 연결에 쓸 때 프로세스가 끝나지 않게 한다 (docs/smb-client-design.md의 "끊긴 연결").
//  - Apple 플랫폼은 상대가 끊은 소켓에 쓰면 쓴 thread에 SIGPIPE를 보내고, 기본 처리가 프로세스를 끝낸다. 소켓 옵션
//    `SO_NOSIGPIPE`를 건 소켓은 신호 없이 `EPIPE`로 실패한다. 프로세스의 신호 처리는 건드리지 않는다.
//  - Android는 런타임(ART)이 프로세스에서 SIGPIPE를 이미 무시하고, Windows에는 SIGPIPE가 없다. 둘은 아무것도 하지 않는다.
//  - libsmb2는 소켓을 제 안에서 만들고 `writev`로 쓴다 (`MSG_NOSIGNAL`을 줄 자리가 없다). 그래서 libsmb2에 소켓을 맡기기
//    전마다 이것을 부른다. 같은 소켓에 여러 번 걸어도 된다.
namespace luil::net {
    // 소켓에 쓰다 상대가 끊었으면 SIGPIPE 대신 `EPIPE`로 돌려주게 한다. 음수 소켓이나 옵션을 걸지 못하면 거짓이다.
    // 할 일이 없는 플랫폼은 음수가 아니면 참이다.
    bool suppress_sigpipe(int socket) noexcept;
}
