#pragma once

#include "net/dns_message.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

// mDNS 질의를 같은 네트워크에 보내고 답을 모은다 (POSIX 소켓, Android). docs/smb-client-design.md의 "같은 네트워크의
// 서버 찾기"를 보라.
//
// 질의는 임시 포트에서 보낸다. 그러면 응답자는 질의를 보낸 주소와 포트로 유니캐스트로 답한다 (RFC 6762 5.1의 한 번
// 질의, 6.7의 legacy unicast). 멀티캐스트를 받지 않으므로 5353 포트를 묶지 않고, Android의 `WifiManager.MulticastLock`
// (그리고 `CHANGE_WIFI_MULTICAST_STATE`)이 필요 없다.
namespace luil::net {
    // 질의를 보낼 곳 하나다.
    struct mdns_destination
    {
        // 숫자 주소와 포트다 (mDNS 묶음은 "224.0.0.251"·"ff02::fb"와 5353이다).
        std::string address {};
        std::uint16_t port { 5353 };
        // IPv4 멀티캐스트를 내보낼 인터페이스의 주소다. 비면 정하지 않는다.
        std::string interface_address {};
        // IPv6 멀티캐스트를 내보낼 인터페이스 번호다. 0이면 정하지 않는다.
        unsigned interface_index { 0 };
    };

    // 이 기기의 켜진 멀티캐스트 인터페이스(루프백 밖)마다 mDNS 묶음 주소를 낸다 (IPv4와 IPv6).
    [[nodiscard]] std::vector<mdns_destination> mdns_destinations();

    // 질의 하나를 모든 곳으로 보내고 `deadline`까지 답을 모아 `records`에 더한다. 응답(QR 비트)만 읽고, 읽지 못하는 패킷은
    // 버린다. IPv6로 온 답의 레코드에는 받은 인터페이스 이름을 붙인다. `stop`이 참이면 일찍 끝낸다 — 모은 레코드를 보고
    // 끝낼 수도 있다. 소켓을 열지 못했거나 어느 곳으로도 보내지 못했으면 거짓이고 `error`가 이유다.
    [[nodiscard]] bool mdns_exchange(std::span<const std::uint8_t> query, std::span<const mdns_destination> destinations, std::chrono::steady_clock::time_point deadline,
        const std::function<bool(const std::vector<dns_record>&)>& stop, std::vector<dns_record>& records, std::string& error);
}
