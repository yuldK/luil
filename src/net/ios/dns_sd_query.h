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
//  - 로컬 네트워크 권한은 첫 질의에서 시스템이 묻는다. 사용자가 답하기 전에는 답이 오지 않고(창 끝까지 기다린다), 거절했으면
//    mDNSResponder가 `kDNSServiceErr_PolicyDenied`로 답한다. 그것을 `denied`로 알린다.
namespace luil::net {
    // DNS-SD 질의 하나의 끝이다.
    enum class dns_sd_outcome
    {
        // 끝까지 물었다 (찾은 것이 없어도 오류가 아니다).
        ok,
        // 로컬 네트워크 권한이 없다 (`kDNSServiceErr_PolicyDenied`).
        denied,
        // 질의를 시작하지 못했거나 시스템 서비스가 끊겼다.
        failed,
    };

    // 같은 네트워크의 SMB 서버(`_smb._tcp.local`)를 deadline까지 찾는다. 인스턴스마다 호스트·포트를 풀고 주소를 묻는다.
    // `ok`가 아니면 error가 영문 진단이고 servers는 비었다.
    [[nodiscard]] dns_sd_outcome browse_smb_servers(std::chrono::steady_clock::time_point deadline, const std::function<bool()>& stop,
        std::vector<smb_server_found>& servers, std::string& error);

    // `.local` 이름의 주소 하나를 deadline까지 묻는다. IPv4가 오면 바로 돌려주고, IPv6만 오면 deadline에 그것을 돌려준다
    // (링크 로컬이면 `%인터페이스`를 붙인다). 찾지 못했으면 `ok`와 빈 address다.
    struct local_address
    {
        dns_sd_outcome outcome { dns_sd_outcome::ok };
        std::string address {};
        std::string error {};
    };
    [[nodiscard]] local_address resolve_local_address(const std::string& host, std::chrono::steady_clock::time_point deadline, const std::function<bool()>& stop);
}
