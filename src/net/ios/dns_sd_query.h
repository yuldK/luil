#pragma once

#include "luil/net/smb_client.h"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

// Apple 플랫폼의 DNS-SD(Bonjour) 질의다. 시스템의 mDNSResponder에 묻는다 (`dns_sd.h`).
//  - iOS는 앱이 멀티캐스트 패킷을 직접 보내는 것을 멀티캐스트 권한(`com.apple.developer.networking.multicast`)이 있을 때만
//    허락한다. `mdns_query.cpp`의 직접 질의는 그래서 기기에서 막힌다. 시스템 서비스로 물으면 그 권한이 필요 없고, 앱의 Info.plist가
//    `NSBonjourServices`에 찾을 서비스(`_smb._tcp`)를 적고 로컬 네트워크 권한을 받으면 된다.
//  - 둘 다 끝날 때까지 막힌다. 100 ms마다 stop을 보고, 참이면 그때까지 모은 것으로 끝낸다.
namespace luil::net {
    // 같은 네트워크의 SMB 서버(`_smb._tcp.local`)를 deadline까지 찾는다. 인스턴스마다 호스트·포트를 풀고 주소를 묻는다.
    // 찾기를 시작하지 못하면(로컬 네트워크 권한을 거절했다 등) 거짓과 error다. 찾은 것이 없는 것은 오류가 아니다.
    [[nodiscard]] bool browse_smb_servers(std::chrono::steady_clock::time_point deadline, const std::function<bool()>& stop, std::vector<smb_server_found>& servers,
        std::string& error);

    // `.local` 이름의 주소 하나를 deadline까지 묻는다. IPv4가 오면 바로 돌려주고, IPv6만 오면 deadline에 그것을 돌려준다
    // (링크 로컬이면 `%인터페이스`를 붙인다). 찾지 못하면 빈 글이다.
    [[nodiscard]] std::string resolve_local_address(const std::string& host, std::chrono::steady_clock::time_point deadline, const std::function<bool()>& stop);
}
