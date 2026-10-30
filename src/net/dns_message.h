#pragma once

#include "luil/net/smb_client.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// DNS 메시지의 짓기와 읽기다 — mDNS(DNS-SD)로 같은 네트워크의 SMB 서버를 찾을 때 쓴다 (docs/smb-client-design.md의
// "같은 네트워크의 서버 찾기"). 소켓을 모르는 순수 부분이라 Windows의 core test에서도 돈다.
namespace luil::net {
    // 이름은 라벨의 목록이다. 라벨 안의 점(".")을 잃지 않으려고 이어 붙이지 않는다. 견주기는 ASCII 대소문자를 접는다.
    using dns_name = std::vector<std::string>;

    inline constexpr std::uint16_t dns_type_a { 1 };
    inline constexpr std::uint16_t dns_type_ptr { 12 };
    inline constexpr std::uint16_t dns_type_txt { 16 };
    inline constexpr std::uint16_t dns_type_aaaa { 28 };
    inline constexpr std::uint16_t dns_type_srv { 33 };

    // "a.b.local"을 라벨로 나눈다. 빈 라벨은 버린다.
    [[nodiscard]] dns_name make_dns_name(std::string_view dotted);
    [[nodiscard]] bool same_dns_name(const dns_name& left, const dns_name& right) noexcept;
    // 사람이 읽는 꼴이다 (라벨을 점으로 잇는다).
    [[nodiscard]] std::string dns_name_text(const dns_name& name);

    struct dns_question
    {
        dns_name name {};
        std::uint16_t type { dns_type_ptr };
    };

    // 레코드 하나다. 읽는 레코드는 A·AAAA·PTR·SRV·TXT이고 그 밖은 건너뛴다.
    struct dns_record
    {
        dns_name name {};
        std::uint16_t type { 0 };
        std::uint32_t ttl { 0 };
        // PTR·SRV의 가리키는 이름이다.
        dns_name target {};
        // SRV다.
        std::uint16_t priority { 0 };
        std::uint16_t weight { 0 };
        std::uint16_t port { 0 };
        // TXT의 글들이다.
        std::vector<std::string> text {};
        // A(4바이트)·AAAA(16바이트)의 주소다.
        std::array<std::uint8_t, 16> address {};
        std::size_t address_size { 0 };
        // 이 레코드를 받은 인터페이스 이름이다 (소켓 쪽이 채운다). 링크 로컬 IPv6 주소에 붙인다.
        std::string scope {};
    };

    struct dns_message
    {
        std::uint16_t id { 0 };
        std::uint16_t flags { 0 };
        // 답·권한·추가 칸의 레코드를 차례로 모은다 (mDNS 응답은 주소를 추가 칸에 싣는다).
        std::vector<dns_record> records {};

        [[nodiscard]] bool response() const noexcept
        {
            return (flags & 0x8000u) != 0;
        }
    };

    // 질의를 짓는다. 질문 칸에 차례로 싣고 클래스는 IN이다. 라벨이 63바이트를 넘거나 이름이 255바이트를 넘으면 빈 값이다.
    [[nodiscard]] std::vector<std::uint8_t> make_dns_query(std::uint16_t id, std::span<const dns_question> questions);

    // 받은 패킷을 읽는다. 선언한 수만큼의 칸이 패킷 안에 다 있지 않거나(잘림), 길이·이름이 규칙을 어기거나, 압축
    // 포인터가 앞쪽이 아닌 자리를 가리키면(고리) nullopt다.
    [[nodiscard]] std::optional<dns_message> parse_dns_message(std::span<const std::uint8_t> packet);

    // 모은 레코드에서 SMB 서버(`_smb._tcp.local`의 인스턴스)를 짓는다. PTR이 인스턴스를, 그 SRV가 호스트와 포트를,
    // 호스트의 A·AAAA가 주소를 준다. 주소는 IPv4가 먼저이고, 링크 로컬 IPv6는 레코드의 `scope`를 붙이며 그것을 모르면
    // 뺀다. SRV가 없는 인스턴스는 뺀다. 이름 차례다.
    [[nodiscard]] std::vector<smb_server_found> collect_smb_servers(std::span<const dns_record> records);

    // 아직 SRV를 모르는 인스턴스와 주소를 모르는 호스트에 물을 질문이다 (찾기의 둘째 걸음).
    [[nodiscard]] std::vector<dns_question> missing_smb_questions(std::span<const dns_record> records);

    // 주소 하나의 글 꼴이다 ("192.168.0.10", "fe80::1%wlan0"). 범위를 모르는 링크 로컬 IPv6는 빈 글이다.
    [[nodiscard]] std::string dns_address_text(const dns_record& record);

    // `_smb._tcp.local`이다.
    [[nodiscard]] const dns_name& smb_service_name();
}
