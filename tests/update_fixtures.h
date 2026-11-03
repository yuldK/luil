#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// 자동 업데이트 test와 데모가 함께 쓰는 고정 서명 자료다 (docs/auto-update-design.md의 확인 절).
//  - openssl로 한 번 지어 커밋했다 (짓는 명령은 update_fixtures.cpp 머리에 있다). test 실행 때 키를 새로 짓지 않는다.
//  - 여기는 공개 키와 미리 서명한 봉투뿐이다. 시험 키의 비밀 값은 update_test_key.h에만 있고 luil_tests의 시험 서명만 들인다.
//  - 데모도 이 파일을 그대로 컴파일한다. 플랫폼 헤더를 쓰지 않는다.
namespace luil::testing {
    inline constexpr std::u8string_view update_fixture_app { u8"luil.update.fixture" };
    inline constexpr std::u8string_view update_fixture_channel { u8"stable" };
    inline constexpr std::u8string_view update_fixture_version { u8"1.0.1" };
    inline constexpr std::uint64_t update_fixture_sequence { 7 };
    inline constexpr std::u8string_view update_fixture_notes { u8"Fixture release for the luil updater tests." };
    inline constexpr std::u8string_view update_fixture_notes_url { u8"https://example.com/luil-update-fixture/1.0.1" };
    inline constexpr std::u8string_view update_fixture_store_url { u8"https://example.com/store/luil.update.fixture" };
    // 매니페스트의 asset `url`은 이 이름 하나다. 매니페스트 주소의 마지막 조각을 바꿔 받는다.
    inline constexpr std::u8string_view update_fixture_asset_name { u8"fixture-1.0.1.bin" };
    inline constexpr std::uint64_t update_fixture_asset_size { 3u * 1024u * 1024u };
    inline constexpr std::u8string_view update_fixture_asset_sha256 { u8"689ead2b477045595dd62d682932723deebcfd922b9fa00fc55aa3aaa64e7e50" };

    // 시험 키 A(봉투를 서명한 키)와 B(서명하지 않은 다른 키)의 SPKI DER base64다.
    inline constexpr std::u8string_view update_fixture_key_id { u8"luil-test-a" };
    inline constexpr std::u8string_view update_fixture_key_spki {
        u8"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEXr0xhgrz/HWwMJEp6cIIKBrpRs78zCQ/Hsj2AeXHoYp1IkUTcqYf+79pyDjUUWaXsAK2x9+rZpro5QmmCy0XvQ==",
    };
    inline constexpr std::u8string_view update_fixture_other_key_id { u8"luil-test-b" };
    inline constexpr std::u8string_view update_fixture_other_key_spki {
        u8"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEP1Q2v2qEwCig3oqWchXsTgdprvQyzAfryCcXnd7ahjWCwR7AHjSCoaSA+6W4b8abcUKIAsVWEeexw/sG0XTlGg==",
    };
    // 키 A의 공개 점 X||Y다. openssl이 낸 SPKI의 머리 뒤 64바이트와 같아야 한다 (SPKI 머리 실측).
    inline constexpr std::array<std::uint8_t, 64> update_fixture_key_point {
        0x5e, 0xbd, 0x31, 0x86, 0x0a, 0xf3, 0xfc, 0x75, 0xb0, 0x30, 0x91, 0x29, 0xe9, 0xc2, 0x08, 0x28, 0x1a, 0xe9, 0x46, 0xce, 0xfc, 0xcc,
        0x24, 0x3f, 0x1e, 0xc8, 0xf6, 0x01, 0xe5, 0xc7, 0xa1, 0x8a, 0x75, 0x22, 0x45, 0x13, 0x72, 0xa6, 0x1f, 0xfb, 0xbf, 0x69, 0xc8, 0x38,
        0xd4, 0x51, 0x66, 0x97, 0xb0, 0x02, 0xb6, 0xc7, 0xdf, 0xab, 0x66, 0x9a, 0xe8, 0xe5, 0x09, 0xa6, 0x0b, 0x2d, 0x17, 0xbd,
    };

    // 키 A로 서명한 매니페스트 바이트다. 봉투의 payload가 이 바이트 그대로다 (한 줄, 끝 줄바꿈 없음).
    inline constexpr std::u8string_view update_fixture_payload {
        u8R"({"format":1,"app":"luil.update.fixture","channel":"stable","sequence":7,"version":"1.0.1","notes":"Fixture release for the luil updater tests.","notes_url":"https://example.com/luil-update-fixture/1.0.1","assets":{"windows-x64":{"url":"fixture-1.0.1.bin","size":3145728,"sha256":"689ead2b477045595dd62d682932723deebcfd922b9fa00fc55aa3aaa64e7e50"},"windows-arm64":{"url":"fixture-1.0.1.bin","size":3145728,"sha256":"689ead2b477045595dd62d682932723deebcfd922b9fa00fc55aa3aaa64e7e50"},"android":{"store_url":"https://example.com/store/luil.update.fixture"}}})",
    };
    // openssl `dgst -sha256 -sign`이 낸 DER 서명과, 같은 서명의 r||s다 (base64).
    inline constexpr std::u8string_view update_fixture_signature_der { u8"MEQCIH9swHg5s3UNulAZ9H/ZX25tLTClyzqa1y83VhFXAc5EAiB00YrSdGD3ApIRZSq/GGrbvdKM+x0gDE60gQrMgF5EoQ==" };
    inline constexpr std::u8string_view update_fixture_signature_raw { u8"f2zAeDmzdQ26UBn0f9lfbm0tMKXLOprXLzdWEVcBzkR00YrSdGD3ApIRZSq/GGrbvdKM+x0gDE60gQrMgF5EoQ==" };

    // 봉투의 서명 항목 하나다. `form`은 `raw`나 `der`, `value`는 base64 그대로다.
    struct update_fixture_signature
    {
        std::u8string key {};
        std::u8string form {};
        std::u8string value {};
    };

    // 글을 바이트로 옮긴다.
    [[nodiscard]] std::vector<std::uint8_t> update_fixture_bytes(std::u8string_view text);
    // core의 것과 따로 쓴 base64 인코더다. test가 core 디코더를 다른 구현과 맞춰 보는 자리이기도 하다.
    [[nodiscard]] std::u8string update_fixture_base64(std::span<const std::uint8_t> bytes);
    // 봉투 글을 짓는다. payload는 바이트를 받아 base64로 싣는다.
    [[nodiscard]] std::u8string make_update_envelope(std::span<const std::uint8_t> payload, std::span<const update_fixture_signature> signatures);
    // 키 A로 미리 서명한 봉투다. `der`이면 DER 칸, 아니면 raw 칸에 싣는다.
    [[nodiscard]] std::u8string update_fixture_envelope(bool der);
    // payload 한 바이트를 바꾼 봉투다 (서명은 그대로라 맞지 않는다).
    [[nodiscard]] std::u8string update_fixture_tampered_envelope();
    // DER 서명의 마지막 바이트를 바꾼 봉투다 (서명이 맞지 않는다).
    [[nodiscard]] std::u8string update_fixture_broken_signature_envelope();
    // 매니페스트가 가리키는 고정 asset이다. 바이트 `i`는 `(i × 2654435761 mod 2^32) >> 24`다.
    [[nodiscard]] std::vector<std::uint8_t> update_fixture_asset(std::uint64_t size = update_fixture_asset_size);
}
