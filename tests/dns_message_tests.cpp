#include "net/dns_message.h"

#include "dns_test_packets.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

// mDNS(DNS-SD)로 SMB 서버를 찾을 때의 DNS 메시지 짓기와 읽기다. 소켓 없이 패킷만 본다.
namespace {
    [[nodiscard]] std::optional<luil::net::dns_message> parse(const std::vector<std::uint8_t>& packet)
    {
        return luil::net::parse_dns_message(packet);
    }
}

TEST_CASE("A DNS query names its questions with class IN", "[net][dns]")
{
    const std::array<luil::net::dns_question, 1> questions { luil::net::dns_question { luil::net::smb_service_name(), luil::net::dns_type_ptr } };
    const std::vector<std::uint8_t> query { luil::net::make_dns_query(0x1234, questions) };
    const std::vector<std::uint8_t> expected {
        0x12,
        0x34,
        0,
        0,
        0,
        1,
        0,
        0,
        0,
        0,
        0,
        0,
        4,
        '_',
        's',
        'm',
        'b',
        4,
        '_',
        't',
        'c',
        'p',
        5,
        'l',
        'o',
        'c',
        'a',
        'l',
        0,
        0,
        12,
        0,
        1,
    };
    CHECK(query == expected);
    // 라벨이 63바이트를 넘으면 짓지 않는다.
    const std::array<luil::net::dns_question, 1> long_label { luil::net::dns_question { { std::string(64, 'a'), "local" }, luil::net::dns_type_a } };
    CHECK(luil::net::make_dns_query(1, long_label).empty());
}

TEST_CASE("A DNS-SD answer reads PTR, SRV, TXT, A and AAAA records", "[net][dns]")
{
    const std::optional<luil::net::dns_message> message { parse(dns_test::nas_response(7)) };
    REQUIRE(message.has_value());
    CHECK(message->id == 7);
    CHECK(message->response());
    REQUIRE(message->records.size() == 5u);
    const luil::net::dns_record& pointer { message->records[0] };
    CHECK(pointer.type == luil::net::dns_type_ptr);
    CHECK(luil::net::same_dns_name(pointer.name, luil::net::smb_service_name()));
    CHECK(luil::net::dns_name_text(pointer.target) == "Living Room NAS._smb._tcp.local");
    const luil::net::dns_record& service { message->records[1] };
    CHECK(service.type == luil::net::dns_type_srv);
    CHECK(service.port == 445);
    CHECK(luil::net::dns_name_text(service.target) == "nas.local");
    CHECK(service.ttl == 120u);
    CHECK(message->records[2].type == luil::net::dns_type_txt);
    CHECK(message->records[2].text == std::vector<std::string> { "" });
    CHECK(luil::net::dns_address_text(message->records[3]) == "192.168.0.10");
    // 범위를 모르는 링크 로컬 IPv6는 쓸 수 없어 빈 글이다. 받은 인터페이스를 알면 붙인다.
    luil::net::dns_record linked { message->records[4] };
    CHECK(luil::net::dns_address_text(linked).empty());
    linked.scope = "wlan0";
    CHECK(luil::net::dns_address_text(linked) == "fe80::211:32ff:fe12:3456%wlan0");
}

TEST_CASE("Compressed names follow pointers that point backwards", "[net][dns]")
{
    dns_test::packet response {};
    response.count_record();
    // 이름: "_smb._tcp.local" 앞에 놓고 PTR의 주인 이름으로 쓴다.
    const std::size_t service_at { response.name("_smb._tcp.local") };
    response.raw({ 0, 12, 0, 1, 0, 0, 0, 120 });
    // 데이터: "NAS" + 앞 이름을 가리키는 포인터 (길이 1 + 3 + 2 = 6).
    response.raw({ 0, 6 });
    response.pointer_name("NAS", service_at);
    const std::optional<luil::net::dns_message> message { parse(response.finish()) };
    REQUIRE(message.has_value());
    REQUIRE(message->records.size() == 1u);
    CHECK(luil::net::dns_name_text(message->records[0].target) == "NAS._smb._tcp.local");
}

TEST_CASE("Malformed DNS packets are refused", "[net][dns]")
{
    const std::vector<std::uint8_t> whole { dns_test::nas_response() };
    // 머리보다 짧다.
    CHECK(parse({ whole.begin(), whole.begin() + 11 }).has_value() == false);
    // 선언한 레코드가 다 오지 않았다 (잘림).
    for (const std::size_t cut : { std::size_t { 13 }, whole.size() / 2, whole.size() - 1 })
        CHECK(parse({ whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(cut) }).has_value() == false);

    // 압축 포인터가 제자리나 뒤쪽을 가리킨다 (고리).
    dns_test::packet looping {};
    looping.count_record();
    const std::size_t at { looping.size() };
    looping.raw({ static_cast<std::uint8_t>(0xC0u | (at >> 8)), static_cast<std::uint8_t>(at & 0xFFu), 0, 1, 0, 1, 0, 0, 0, 0, 0, 4, 1, 2, 3, 4 });
    CHECK(parse(looping.finish()).has_value() == false);

    // 라벨 길이의 윗 두 비트가 01·10이다.
    dns_test::packet reserved {};
    reserved.count_record();
    reserved.raw({ 0x40, 'a', 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4, 1, 2, 3, 4 });
    CHECK(parse(reserved.finish()).has_value() == false);

    // A 레코드의 데이터 길이가 4가 아니다.
    dns_test::packet short_address {};
    short_address.count_record();
    short_address.name("nas.local");
    short_address.raw({ 0, 1, 0, 1, 0, 0, 0, 0, 0, 3, 1, 2, 3 });
    CHECK(parse(short_address.finish()).has_value() == false);

    // 데이터 길이가 패킷 끝을 넘는다.
    dns_test::packet overrun {};
    overrun.count_record();
    overrun.name("nas.local");
    overrun.raw({ 0, 16, 0, 1, 0, 0, 0, 0, 0, 200, 'x' });
    CHECK(parse(overrun.finish()).has_value() == false);

    // 이름이 255바이트를 넘는다.
    dns_test::packet long_name {};
    long_name.count_record();
    std::string dotted {};
    for (int index { 0 }; index < 5; ++index)
        dotted += std::string(60, 'a') + ".";
    long_name.name(dotted + "local");
    long_name.raw({ 0, 1, 0, 1, 0, 0, 0, 0, 0, 4, 1, 2, 3, 4 });
    CHECK(parse(long_name.finish()).has_value() == false);
}

TEST_CASE("Unknown record types are skipped by their declared length", "[net][dns]")
{
    dns_test::packet response {};
    response.count_record();
    response.name("nas.local");
    // HINFO(13) 하나 — 데이터 3바이트를 건너뛴다.
    response.raw({ 0, 13, 0, 1, 0, 0, 0, 0, 0, 3, 1, 'x', 0 });
    response.a("nas.local", { 10, 0, 0, 2 });
    const std::optional<luil::net::dns_message> message { parse(response.finish()) };
    REQUIRE(message.has_value());
    REQUIRE(message->records.size() == 1u);
    CHECK(luil::net::dns_address_text(message->records[0]) == "10.0.0.2");
}

TEST_CASE("SMB servers are assembled from instances, services and addresses", "[net][dns]")
{
    std::vector<luil::net::dns_record> records { parse(dns_test::nas_response())->records };
    for (luil::net::dns_record& record : records)
        record.scope = "wlan0";
    // SRV가 없는 인스턴스는 빠진다.
    dns_test::packet other {};
    other.ptr("_smb._tcp.local", "Attic._smb._tcp.local");
    const std::vector<luil::net::dns_record> more { parse(other.finish())->records };
    records.insert(records.end(), more.begin(), more.end());

    const std::vector<luil::net::smb_server_found> servers { luil::net::collect_smb_servers(records) };
    REQUIRE(servers.size() == 1u);
    CHECK(servers[0].name == u8"Living Room NAS");
    CHECK(servers[0].host == u8"nas.local");
    CHECK(servers[0].port == 445);
    // IPv4가 먼저다.
    REQUIRE(servers[0].addresses.size() == 2u);
    CHECK(servers[0].addresses[0] == u8"192.168.0.10");
    CHECK(servers[0].addresses[1] == u8"fe80::211:32ff:fe12:3456%wlan0");
    CHECK(servers[0].server() == u8"192.168.0.10");

    // 빠진 SRV는 둘째 걸음에서 묻는다.
    const std::vector<luil::net::dns_question> missing { luil::net::missing_smb_questions(records) };
    REQUIRE(missing.size() == 1u);
    CHECK(missing[0].type == luil::net::dns_type_srv);
    CHECK(luil::net::dns_name_text(missing[0].name) == "Attic._smb._tcp.local");
}

TEST_CASE("A server without addresses asks for them and uses its host name meanwhile", "[net][dns]")
{
    dns_test::packet response {};
    response.ptr("_smb._tcp.local", "Backup._smb._tcp.local");
    response.srv("Backup._smb._tcp.local", 4445, "backup.local");
    const std::vector<luil::net::dns_record> records { parse(response.finish())->records };
    const std::vector<luil::net::dns_question> missing { luil::net::missing_smb_questions(records) };
    REQUIRE(missing.size() == 2u);
    CHECK(missing[0].type == luil::net::dns_type_a);
    CHECK(missing[1].type == luil::net::dns_type_aaaa);
    const std::vector<luil::net::smb_server_found> servers { luil::net::collect_smb_servers(records) };
    REQUIRE(servers.size() == 1u);
    CHECK(servers[0].addresses.empty());
    CHECK(servers[0].server() == u8"backup.local:4445");
}

TEST_CASE("DNS names compare without ASCII case and keep dots inside labels", "[net][dns]")
{
    CHECK(luil::net::same_dns_name(luil::net::make_dns_name("NAS.Local"), luil::net::make_dns_name("nas.local")));
    CHECK_FALSE(luil::net::same_dns_name(luil::net::make_dns_name("nas.local"), luil::net::make_dns_name("nas2.local")));
    const luil::net::dns_name dotted { "My.NAS", "_smb", "_tcp", "local" };
    CHECK_FALSE(luil::net::same_dns_name(dotted, luil::net::make_dns_name("My.NAS._smb._tcp.local")));
}
