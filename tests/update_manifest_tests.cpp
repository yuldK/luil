#include "host/update_manifest.h"

#include "update_fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// 자동 업데이트가 OS 없이 다루는 값의 규칙이다 (docs/auto-update-design.md). 서버도 암호 API도 없이 돈다.

namespace {
    using luil::testing::update_fixture_bytes;

    [[nodiscard]] std::string ascii(const std::u8string_view text)
    {
        return std::string { reinterpret_cast<const char*>(text.data()), text.size() };
    }

    [[nodiscard]] std::vector<std::uint8_t> bytes_of(const std::u8string_view text)
    {
        return update_fixture_bytes(text);
    }

    [[nodiscard]] std::optional<luil::update_envelope> envelope_of(const std::u8string_view text)
    {
        std::u8string error {};
        std::optional<luil::update_envelope> envelope { luil::parse_update_envelope(bytes_of(text), error) };
        if (envelope.has_value() == false)
            CHECK(error.empty() == false);
        return envelope;
    }

    [[nodiscard]] std::optional<luil::update_manifest> manifest_of(const std::u8string_view text)
    {
        std::u8string error {};
        std::optional<luil::update_manifest> manifest { luil::parse_update_manifest(bytes_of(text), error) };
        if (manifest.has_value() == false)
            CHECK(error.empty() == false);
        return manifest;
    }

    [[nodiscard]] luil::update_version version_of(const std::u8string_view text)
    {
        const std::optional<luil::update_version> version { luil::parse_update_version(text) };
        REQUIRE(version.has_value());
        return *version;
    }

    [[nodiscard]] std::vector<std::uint8_t> decoded(const std::u8string_view text)
    {
        std::optional<std::vector<std::uint8_t>> bytes { luil::decode_update_base64(text) };
        REQUIRE(bytes.has_value());
        return *bytes;
    }

    [[nodiscard]] luil::update_raw_signature fixture_raw()
    {
        const std::vector<std::uint8_t> bytes { decoded(luil::testing::update_fixture_signature_raw) };
        REQUIRE(bytes.size() == 64);
        luil::update_raw_signature raw {};
        std::copy(bytes.begin(), bytes.end(), raw.begin());
        return raw;
    }

    // 플랫폼 칸 하나를 바꾼 매니페스트다. 서명은 보지 않는 자리라 서명하지 않는다.
    [[nodiscard]] std::u8string manifest_with(const std::u8string_view fields)
    {
        return u8R"({"format":1,"app":"a","channel":"stable","sequence":3,"version":"1.2",)" + std::u8string { fields } + u8"}";
    }

    constexpr std::u8string_view windows_asset { u8R"("windows-x64":{"url":"a.bin","size":10,"sha256":"689ead2b477045595dd62d682932723deebcfd922b9fa00fc55aa3aaa64e7e50"})" };
}

TEST_CASE("update base64 decodes only the strict padded standard form", "[update][manifest]")
{
    CHECK(decoded(u8"").empty());
    CHECK(decoded(u8"Zg==") == bytes_of(u8"f"));
    CHECK(decoded(u8"Zm8=") == bytes_of(u8"fo"));
    CHECK(decoded(u8"Zm9v") == bytes_of(u8"foo"));
    CHECK(decoded(u8"+/+/") == std::vector<std::uint8_t> { 0xfb, 0xff, 0xbf });
    // 패딩 없음, 길이, 공백·줄바꿈, 가운데 패딩, URL 알파벳, 쓰이지 않는 끝 비트를 거절한다.
    for (const std::u8string_view bad : { u8"Zg", u8"Zg=", u8"Zm9", u8" Zm9v", u8"Zm9v\n", u8"Zm 9v", u8"Zg==Zg==", u8"Z===", u8"====", u8"-_8=", u8"Zh==", u8"Zm9=" })
    {
        INFO(ascii(bad));
        CHECK(luil::decode_update_base64(bad).has_value() == false);
    }
}

TEST_CASE("update base64 round trips through an independent encoder", "[update][manifest]")
{
    std::vector<std::uint8_t> bytes {};
    for (std::size_t length = 0; length < 70; ++length)
    {
        const std::u8string core { luil::encode_update_base64(bytes) };
        CHECK(core == luil::testing::update_fixture_base64(bytes));
        CHECK(decoded(core) == bytes);
        bytes.push_back(static_cast<std::uint8_t>(length * 37u + 11u));
    }
}

TEST_CASE("update versions compare dotted numbers with missing parts as zero", "[update][manifest]")
{
    CHECK(version_of(u8"1.4") == version_of(u8"1.4.0"));
    CHECK(version_of(u8"1.4.0.0") == version_of(u8"1.4"));
    CHECK(version_of(u8"1.4.0") < version_of(u8"1.4.1"));
    CHECK(version_of(u8"1.10") > version_of(u8"1.9"));
    CHECK(version_of(u8"2") > version_of(u8"1.99.99.99"));
    CHECK(version_of(u8"0") == version_of(u8"0.0.0.0"));
    CHECK(version_of(u8"4294967295").parts[0] == 4294967295u);
    for (const std::u8string_view bad : { u8"", u8"01.0", u8"1.02", u8"1.2.3.4.5", u8"4294967296", u8"1..2", u8"1.2.", u8".1", u8"-1", u8"1.4.0-beta.1", u8" 1.0", u8"1.0 ", u8"v1", u8"1,0" })
    {
        INFO(ascii(bad));
        CHECK(luil::parse_update_version(bad).has_value() == false);
    }
}

TEST_CASE("update digests are lowercase hex only", "[update][manifest]")
{
    const std::optional<luil::update_digest> digest { luil::parse_update_digest(luil::testing::update_fixture_asset_sha256) };
    REQUIRE(digest.has_value());
    CHECK(luil::format_update_digest(*digest) == luil::testing::update_fixture_asset_sha256);
    CHECK(luil::parse_update_digest(u8"689EAD2B477045595DD62D682932723DEEBCFD922B9FA00FC55AA3AAA64E7E50").has_value() == false);
    CHECK(luil::parse_update_digest(luil::testing::update_fixture_asset_sha256.substr(1)).has_value() == false);
    CHECK(luil::parse_update_digest(u8"g89ead2b477045595dd62d682932723deebcfd922b9fa00fc55aa3aaa64e7e50").has_value() == false);
}

TEST_CASE("the fixture envelopes parse in both signature forms", "[update][manifest]")
{
    for (const bool der : { true, false })
    {
        const std::optional<luil::update_envelope> envelope { envelope_of(luil::testing::update_fixture_envelope(der)) };
        REQUIRE(envelope.has_value());
        CHECK(envelope->payload == bytes_of(luil::testing::update_fixture_payload));
        REQUIRE(envelope->signatures.size() == 1);
        CHECK(envelope->signatures[0].key == luil::testing::update_fixture_key_id);
        CHECK(envelope->signatures[0].form == (der ? luil::update_signature_form::der : luil::update_signature_form::raw));
        CHECK(envelope->signatures[0].bytes.size() == (der ? 70u : 64u));
    }
}

TEST_CASE("an update envelope needs its fields in the right form", "[update][manifest]")
{
    const std::u8string payload { luil::testing::update_fixture_base64(bytes_of(u8"{}")) };
    const std::u8string raw { luil::testing::update_fixture_signature_raw };
    const auto envelope_text {
        [&payload](const std::u8string_view format, const std::u8string_view signatures) {
            return u8"{\"format\":\"" + std::u8string { format } + u8"\",\"payload\":\"" + payload + u8"\",\"signatures\":[" + std::u8string { signatures } + u8"]}";
        },
    };
    const std::u8string good { u8"{\"key\":\"k\",\"raw\":\"" + raw + u8"\"}" };

    CHECK(envelope_of(envelope_text(u8"luil-update-envelope/1", good)).has_value());
    CHECK(envelope_of(envelope_text(u8"luil-update-envelope/2", good)).has_value() == false);
    CHECK(envelope_of(envelope_text(u8"luil-update-envelope/1", u8"")).has_value() == false);
    CHECK(envelope_of(u8"[]").has_value() == false);
    CHECK(envelope_of(u8"{").has_value() == false);
    CHECK(envelope_of(u8"").has_value() == false);
    CHECK(envelope_of(u8"{\"format\":\"luil-update-envelope/1\",\"signatures\":[" + good + u8"]}").has_value() == false);
    CHECK(envelope_of(u8"{\"format\":\"luil-update-envelope/1\",\"payload\":\"\",\"signatures\":[" + good + u8"]}").has_value() == false);
    CHECK(envelope_of(u8"{\"format\":\"luil-update-envelope/1\",\"payload\":\"e30\",\"signatures\":[" + good + u8"]}").has_value() == false);

    // 항목마다 key와 raw·der 가운데 하나다.
    CHECK(envelope_of(envelope_text(u8"luil-update-envelope/1", u8"{\"raw\":\"" + raw + u8"\"}")).has_value() == false);
    CHECK(envelope_of(envelope_text(u8"luil-update-envelope/1", u8"{\"key\":\"\",\"raw\":\"" + raw + u8"\"}")).has_value() == false);
    CHECK(envelope_of(envelope_text(u8"luil-update-envelope/1", u8"{\"key\":\"k\"}")).has_value() == false);
    CHECK(envelope_of(envelope_text(u8"luil-update-envelope/1", u8"{\"key\":\"k\",\"raw\":\"" + raw + u8"\",\"der\":\"" + raw + u8"\"}")).has_value() == false);
    CHECK(envelope_of(envelope_text(u8"luil-update-envelope/1", u8"{\"key\":\"k\",\"raw\":\"AAAA\"}")).has_value() == false);
    CHECK(envelope_of(envelope_text(u8"luil-update-envelope/1", u8"{\"key\":\"k\",\"der\":\"not base64\"}")).has_value() == false);
    CHECK(envelope_of(envelope_text(u8"luil-update-envelope/1", u8"\"k\"")).has_value() == false);

    // 모르는 칸은 넘긴다 (뒤 판 호환).
    const std::optional<luil::update_envelope> extra {
        envelope_of(u8"{\"format\":\"luil-update-envelope/1\",\"future\":[1,2],\"payload\":\"" + payload + u8"\",\"signatures\":[{\"key\":\"k\",\"note\":\"x\",\"raw\":\"" + raw + u8"\"}]}"),
    };
    CHECK(extra.has_value());

    // 너무 깊은 문서는 짓기 전에 거절한다.
    CHECK(envelope_of(std::u8string(40, u8'[') + std::u8string(40, u8']')).has_value() == false);
}

TEST_CASE("an update envelope carries one to eight signatures", "[update][manifest]")
{
    const std::u8string payload { luil::testing::update_fixture_base64(bytes_of(u8"{}")) };
    const std::u8string raw { luil::testing::update_fixture_signature_raw };
    const auto with_count {
        [&](const std::size_t count) {
            std::u8string list {};
            for (std::size_t index = 0; index < count; ++index)
            {
                if (index > 0)
                    list.push_back(u8',');
                list += u8"{\"key\":\"k" + std::u8string(1, static_cast<char8_t>(u8'0' + index)) + u8"\",\"raw\":\"" + raw + u8"\"}";
            }
            return u8"{\"format\":\"luil-update-envelope/1\",\"payload\":\"" + payload + u8"\",\"signatures\":[" + list + u8"]}";
        },
    };
    CHECK(envelope_of(with_count(1)).has_value());
    CHECK(envelope_of(with_count(8)).has_value());
    CHECK(envelope_of(with_count(9)).has_value() == false);
}

TEST_CASE("update signatures from unknown keys are skipped in order", "[update][manifest]")
{
    luil::update_envelope envelope {};
    for (const std::u8string_view key : { u8"old", u8"b", u8"a", u8"b" })
        envelope.signatures.push_back(luil::update_signature_entry { std::u8string { key }, luil::update_signature_form::raw, std::vector<std::uint8_t>(64) });
    const std::vector<luil::update_key> keys { luil::update_key { u8"a", {} }, luil::update_key { u8"b", {} } };
    const std::vector<luil::update_signature_match> matches { luil::match_update_signatures(envelope, keys) };
    REQUIRE(matches.size() == 3);
    CHECK(matches[0].signature == 1);
    CHECK(matches[0].key == 1);
    CHECK(matches[1].signature == 2);
    CHECK(matches[1].key == 0);
    CHECK(matches[2].signature == 3);
    CHECK(luil::match_update_signatures(envelope, std::vector<luil::update_key> { luil::update_key { u8"none", {} } }).empty());
}

TEST_CASE("the fixture payload reads as a manifest", "[update][manifest]")
{
    const std::optional<luil::update_manifest> manifest { manifest_of(luil::testing::update_fixture_payload) };
    REQUIRE(manifest.has_value());
    CHECK(manifest->app == luil::testing::update_fixture_app);
    CHECK(manifest->channel == luil::testing::update_fixture_channel);
    CHECK(manifest->sequence == luil::testing::update_fixture_sequence);
    CHECK(manifest->version_text == luil::testing::update_fixture_version);
    CHECK(manifest->version == version_of(u8"1.0.1"));
    CHECK(manifest->notes == luil::testing::update_fixture_notes);
    CHECK(manifest->notes_url == luil::testing::update_fixture_notes_url);
    REQUIRE(manifest->windows_x64.has_value());
    CHECK(manifest->windows_x64->url == luil::testing::update_fixture_asset_name);
    CHECK(manifest->windows_x64->size == luil::testing::update_fixture_asset_size);
    CHECK(luil::format_update_digest(manifest->windows_x64->sha256) == luil::testing::update_fixture_asset_sha256);
    CHECK(manifest->windows_arm64.has_value());
    REQUIRE(manifest->android.has_value());
    CHECK(manifest->android->store_url == luil::testing::update_fixture_store_url);
}

TEST_CASE("an update manifest needs its fields in the right form", "[update][manifest]")
{
    const std::u8string assets { u8"\"assets\":{" + std::u8string { windows_asset } + u8"}" };
    CHECK(manifest_of(manifest_with(assets)).has_value());
    // 모르는 칸과 모르는 플랫폼 칸은 넘긴다.
    CHECK(manifest_of(manifest_with(u8"\"later\":{\"x\":1},\"assets\":{\"linux-x64\":{\"anything\":true}," + std::u8string { windows_asset } + u8"}")).has_value());
    CHECK(manifest_of(manifest_with(u8"\"assets\":{}")).has_value());
    // 설명 칸은 없어도 되고, 있으면 꼴이 맞아야 한다.
    CHECK(manifest_of(manifest_with(u8"\"notes\":\"n\",\"notes_url\":\"https://example.com/n\"," + assets)).has_value());
    CHECK(manifest_of(manifest_with(u8"\"notes\":3," + assets)).has_value() == false);
    CHECK(manifest_of(manifest_with(u8"\"notes_url\":\"http://example.com/n\"," + assets)).has_value() == false);
    CHECK(manifest_of(manifest_with(u8"\"notes_url\":\"http://127.0.0.1/n\"," + assets)).has_value() == false);
    CHECK(manifest_of(manifest_with(u8"\"notes_url\":\"https://user@example.com/n\"," + assets)).has_value() == false);

    CHECK(manifest_of(u8"{\"format\":2,\"app\":\"a\",\"channel\":\"s\",\"sequence\":1,\"version\":\"1\"," + assets + u8"}").has_value() == false);
    CHECK(manifest_of(u8"{\"format\":\"1\",\"app\":\"a\",\"channel\":\"s\",\"sequence\":1,\"version\":\"1\"," + assets + u8"}").has_value() == false);
    CHECK(manifest_of(u8"{\"format\":1,\"channel\":\"s\",\"sequence\":1,\"version\":\"1\"," + assets + u8"}").has_value() == false);
    CHECK(manifest_of(u8"{\"format\":1,\"app\":\"a\",\"sequence\":1,\"version\":\"1\"," + assets + u8"}").has_value() == false);
    CHECK(manifest_of(u8"{\"format\":1,\"app\":\"a\",\"channel\":\"s\",\"sequence\":-1,\"version\":\"1\"," + assets + u8"}").has_value() == false);
    CHECK(manifest_of(u8"{\"format\":1,\"app\":\"a\",\"channel\":\"s\",\"sequence\":1.5,\"version\":\"1\"," + assets + u8"}").has_value() == false);
    CHECK(manifest_of(u8"{\"format\":1,\"app\":\"a\",\"channel\":\"s\",\"sequence\":1,\"version\":\"1.0-rc\"," + assets + u8"}").has_value() == false);
    CHECK(manifest_of(u8"{\"format\":1,\"app\":\"a\",\"channel\":\"s\",\"sequence\":1,\"version\":\"1\"}").has_value() == false);

    // 아는 플랫폼 칸의 꼴이 틀리면 거절한다.
    CHECK(manifest_of(manifest_with(u8"\"assets\":{\"windows-x64\":{\"url\":\"a.bin\",\"size\":0,\"sha256\":\"689ead2b477045595dd62d682932723deebcfd922b9fa00fc55aa3aaa64e7e50\"}}")).has_value() == false);
    CHECK(manifest_of(manifest_with(u8"\"assets\":{\"windows-x64\":{\"url\":\"a.bin\",\"size\":10,\"sha256\":\"689EAD2B477045595DD62D682932723DEEBCFD922B9FA00FC55AA3AAA64E7E50\"}}")).has_value() == false);
    CHECK(manifest_of(manifest_with(u8"\"assets\":{\"windows-x64\":{\"size\":10,\"sha256\":\"689ead2b477045595dd62d682932723deebcfd922b9fa00fc55aa3aaa64e7e50\"}}")).has_value() == false);
    CHECK(manifest_of(manifest_with(u8"\"assets\":{\"android\":{}}")).has_value() == false);
    CHECK(manifest_of(manifest_with(u8"\"assets\":{\"android\":\"https://example.com\"}")).has_value() == false);
}

TEST_CASE("an update manifest is judged against this computer", "[update][manifest]")
{
    const std::optional<luil::update_manifest> manifest { manifest_of(luil::testing::update_fixture_payload) };
    REQUIRE(manifest.has_value());
    luil::update_judgement base {};
    base.app = luil::testing::update_fixture_app;
    base.channel = luil::testing::update_fixture_channel;
    base.seen_sequence = 0;
    base.current = version_of(u8"1.0.0");
    base.platform = luil::update_platform::windows_x64;
    base.max_asset_bytes = 512u * 1024u * 1024u;
    CHECK(luil::judge_update_manifest(*manifest, base) == luil::update_verdict::available);

    luil::update_judgement other { base };
    other.app = u8"other.app";
    CHECK(luil::judge_update_manifest(*manifest, other) == luil::update_verdict::mismatch);
    other = base;
    other.channel = u8"beta";
    CHECK(luil::judge_update_manifest(*manifest, other) == luil::update_verdict::mismatch);
    // 같은 sequence는 같은 매니페스트를 다시 받은 것이다. 작은 것만 재생이다.
    other = base;
    other.seen_sequence = luil::testing::update_fixture_sequence;
    CHECK(luil::judge_update_manifest(*manifest, other) == luil::update_verdict::available);
    other.seen_sequence = luil::testing::update_fixture_sequence + 1;
    CHECK(luil::judge_update_manifest(*manifest, other) == luil::update_verdict::replayed);
    // 같거나 낮은 판은 최신이다.
    other = base;
    other.current = version_of(u8"1.0.1.0");
    CHECK(luil::judge_update_manifest(*manifest, other) == luil::update_verdict::up_to_date);
    other.current = version_of(u8"2");
    CHECK(luil::judge_update_manifest(*manifest, other) == luil::update_verdict::up_to_date);
    // 크기 상한은 Win32 칸에만 걸린다.
    other = base;
    other.max_asset_bytes = luil::testing::update_fixture_asset_size - 1;
    CHECK(luil::judge_update_manifest(*manifest, other) == luil::update_verdict::too_large);
    other.platform = luil::update_platform::android;
    CHECK(luil::judge_update_manifest(*manifest, other) == luil::update_verdict::available);

    std::optional<luil::update_manifest> bare { manifest_of(manifest_with(u8"\"assets\":{}")) };
    REQUIRE(bare.has_value());
    luil::update_judgement plain { base };
    plain.app = u8"a";
    plain.current = version_of(u8"1.0");
    CHECK(luil::judge_update_manifest(*bare, plain) == luil::update_verdict::no_asset);
    plain.current = version_of(u8"1.2");
    CHECK(luil::judge_update_manifest(*bare, plain) == luil::update_verdict::up_to_date);
}

TEST_CASE("each update platform has its manifest key", "[update][manifest]")
{
    CHECK(luil::update_platform_key(luil::update_platform::windows_x64) == u8"windows-x64");
    CHECK(luil::update_platform_key(luil::update_platform::windows_arm64) == u8"windows-arm64");
    CHECK(luil::update_platform_key(luil::update_platform::android) == u8"android");
#if defined(__ANDROID__)
    CHECK(luil::current_update_platform() == luil::update_platform::android);
#elif defined(_M_ARM64)
    CHECK(luil::current_update_platform() == luil::update_platform::windows_arm64);
#else
    CHECK(luil::current_update_platform() == luil::update_platform::windows_x64);
#endif
}

TEST_CASE("update addresses are https or loopback http", "[update][manifest]")
{
    for (const std::u8string_view good : { u8"https://example.com/m.json", u8"HTTPS://example.com", u8"http://127.0.0.1:8080/m", u8"http://[::1]:9/m", u8"http://LocalHost/m" })
    {
        INFO(ascii(good));
        CHECK(luil::update_url_allowed(good));
    }
    for (const std::u8string_view bad : { u8"http://example.com/m", u8"http://127.0.0.1@example.com/m", u8"https://user@example.com/m", u8"ftp://example.com/m", u8"https:///m", u8"https://", u8"example.com/m", u8"http://127.0.0.2/m", u8"file:///c:/m" })
    {
        INFO(ascii(bad));
        CHECK(luil::update_url_allowed(bad) == false);
    }
}

TEST_CASE("an update asset name replaces the last path segment of the manifest address", "[update][manifest]")
{
    const std::u8string_view manifest { u8"http://127.0.0.1:8080/update/stable/manifest.json?x=1#top" };
    CHECK(luil::resolve_update_asset_url(u8"editor-1.4.0.exe", manifest) == std::u8string { u8"http://127.0.0.1:8080/update/stable/editor-1.4.0.exe" });
    CHECK(luil::resolve_update_asset_url(u8"a_b~c.bin", u8"https://cdn.example.com") == std::u8string { u8"https://cdn.example.com/a_b~c.bin" });
    CHECK(luil::resolve_update_asset_url(u8"a.bin", u8"https://cdn.example.com/") == std::u8string { u8"https://cdn.example.com/a.bin" });
    CHECK(luil::resolve_update_asset_url(u8"https://other.example.com/x.exe", manifest) == std::u8string { u8"https://other.example.com/x.exe" });
    // 이름 하나 밖의 꼴과, 규칙 밖의 절대 주소를 거절한다.
    for (const std::u8string_view bad : { u8"", u8"a/b.exe", u8"../a.exe", u8"..", u8".", u8"a:b", u8"a?b", u8"a#b", u8"a b", u8"a%2Fb", u8"a\\b", u8"http://example.com/x.exe" })
    {
        INFO(ascii(bad));
        CHECK(luil::resolve_update_asset_url(bad, manifest).has_value() == false);
    }
    CHECK(luil::resolve_update_asset_url(u8"a.exe", u8"http://example.com/update/m.json").has_value() == false);
}

TEST_CASE("an openssl DER signature converts to r and s and back", "[update][manifest]")
{
    const std::vector<std::uint8_t> der { decoded(luil::testing::update_fixture_signature_der) };
    const std::optional<luil::update_raw_signature> raw { luil::update_signature_from_der(der) };
    REQUIRE(raw.has_value());
    CHECK(*raw == fixture_raw());
    CHECK(luil::update_signature_to_der(*raw) == der);
    CHECK(luil::update_signature_scalars_nonzero(*raw));
}

TEST_CASE("raw signatures wrap into minimal DER", "[update][manifest]")
{
    luil::update_raw_signature raw {};
    // 높은 비트가 선 r은 0을 하나 앞붙이고, 작은 s는 앞의 0을 뗀다.
    raw[0] = 0x80;
    raw[31] = 0x01;
    raw[63] = 0x05;
    const std::vector<std::uint8_t> der { luil::update_signature_to_der(raw) };
    CHECK(der.size() == 2 + 2 + 33 + 2 + 1);
    CHECK(der[0] == 0x30);
    CHECK(der[2] == 0x02);
    CHECK(der[3] == 33);
    CHECK(der[4] == 0x00);
    CHECK(der[5] == 0x80);
    CHECK(der[der.size() - 3] == 0x02);
    CHECK(der[der.size() - 2] == 1);
    CHECK(der.back() == 0x05);
    CHECK(luil::update_signature_from_der(der) == raw);
}

TEST_CASE("strict DER rejects every loose signature form", "[update][manifest]")
{
    const auto rejects { [](const std::vector<std::uint8_t>& der) { return luil::update_signature_from_der(der).has_value() == false; } };
    CHECK(luil::update_signature_from_der(std::vector<std::uint8_t> { 0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01 }).has_value());
    // 최소 꼴이 아닌 앞의 0, 음수, 0, 33바이트 넘는 값이다.
    CHECK(rejects({ 0x30, 0x07, 0x02, 0x02, 0x00, 0x01, 0x02, 0x01, 0x01 }));
    CHECK(rejects({ 0x30, 0x06, 0x02, 0x01, 0x81, 0x02, 0x01, 0x01 }));
    CHECK(rejects({ 0x30, 0x06, 0x02, 0x01, 0x00, 0x02, 0x01, 0x01 }));
    CHECK(rejects({ 0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x00 }));
    CHECK(rejects({ 0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x00 }));
    std::vector<std::uint8_t> wide { 0x30, 0x26, 0x02, 0x21, 0x01 };
    wide.insert(wide.end(), 32, 0x01);
    wide.insert(wide.end(), { 0x02, 0x01, 0x01 });
    CHECK(rejects(wide));
    // 뒤 바이트, 틀린 길이, 긴 길이 꼴, 다른 꼬리표다.
    CHECK(rejects({ 0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01, 0x00 }));
    CHECK(rejects({ 0x30, 0x07, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01 }));
    CHECK(rejects({ 0x30, 0x81, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01 }));
    CHECK(rejects({ 0x31, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01 }));
    CHECK(rejects({ 0x30, 0x06, 0x03, 0x01, 0x01, 0x02, 0x01, 0x01 }));
    CHECK(rejects({ 0x30, 0x03, 0x02, 0x01, 0x01 }));
    CHECK(rejects({}));
}

TEST_CASE("zero signature scalars are refused", "[update][manifest]")
{
    luil::update_raw_signature raw { fixture_raw() };
    std::fill(raw.begin(), raw.begin() + 32, std::uint8_t { 0 });
    CHECK(luil::update_signature_scalars_nonzero(raw) == false);
    raw = fixture_raw();
    std::fill(raw.begin() + 32, raw.end(), std::uint8_t { 0 });
    CHECK(luil::update_signature_scalars_nonzero(raw) == false);
}

TEST_CASE("the P-256 SPKI header matches what openssl emits", "[update][manifest]")
{
    // 고정 머리 27바이트는 실측이다. 시험 키 A를 openssl이 낸 그대로 푼다.
    const std::vector<std::uint8_t> spki { decoded(luil::testing::update_fixture_key_spki) };
    REQUIRE(spki.size() == 91);
    const std::optional<luil::update_public_point> point { luil::update_spki_point(spki) };
    REQUIRE(point.has_value());
    CHECK(*point == luil::testing::update_fixture_key_point);
    CHECK(luil::update_spki_point(decoded(luil::testing::update_fixture_other_key_spki)).has_value());

    // 압축 점, 다른 곡선 OID, 길이가 틀린 것을 거절한다.
    std::vector<std::uint8_t> compressed { spki };
    compressed[26] = 0x02;
    CHECK(luil::update_spki_point(compressed).has_value() == false);
    std::vector<std::uint8_t> curve { spki };
    curve[22] = 0x08;
    CHECK(luil::update_spki_point(curve).has_value() == false);
    std::vector<std::uint8_t> shorter { spki.begin(), spki.end() - 1 };
    CHECK(luil::update_spki_point(shorter).has_value() == false);
    std::vector<std::uint8_t> longer { spki };
    longer.push_back(0x00);
    CHECK(luil::update_spki_point(longer).has_value() == false);
}

TEST_CASE("content ranges parse only the exact byte form", "[update][manifest]")
{
    const std::optional<luil::update_content_range> range { luil::parse_update_content_range(u8"bytes 65536-131071/3145728") };
    REQUIRE(range.has_value());
    CHECK(range->first == 65536);
    CHECK(range->last == 131071);
    CHECK(range->total == 3145728);
    CHECK(luil::parse_update_content_range(u8"BYTES 0-9/10").has_value());
    for (const std::u8string_view bad : { u8"bytes 10-9/20", u8"bytes 0-10/10", u8"bytes */10", u8"bytes 0-9/*", u8"bytes 00-9/10", u8"bytes  0-9/10", u8"bytes 0-9", u8"items 0-9/10", u8"" })
    {
        INFO(ascii(bad));
        CHECK(luil::parse_update_content_range(bad).has_value() == false);
    }
    CHECK(luil::make_update_range(0, 65535) == u8"bytes=0-65535");
}

TEST_CASE("the update state file round trips", "[update][manifest]")
{
    const std::vector<std::uint8_t> bytes { luil::serialize_update_state(luil::update_state { 42 }) };
    const std::optional<luil::update_state> state { luil::parse_update_state(bytes) };
    REQUIRE(state.has_value());
    CHECK(state->sequence == 42);
    CHECK(luil::parse_update_state(bytes_of(u8"{\"format\":1,\"sequence\":9,\"later\":\"x\"}"))->sequence == 9);
    CHECK(luil::parse_update_state(bytes_of(u8"{\"format\":1}")).has_value() == false);
    CHECK(luil::parse_update_state(bytes_of(u8"{\"sequence\":-1}")).has_value() == false);
    CHECK(luil::parse_update_state(bytes_of(u8"not json")).has_value() == false);
    CHECK(luil::parse_update_state({}).has_value() == false);
}

TEST_CASE("the fixture asset follows its formula", "[update][manifest]")
{
    const std::vector<std::uint8_t> asset { luil::testing::update_fixture_asset(16) };
    CHECK(asset == std::vector<std::uint8_t> { 0x00, 0x9e, 0x3c, 0xda, 0x78, 0x17, 0xb5, 0x53, 0xf1, 0x8f, 0x2e, 0xcc, 0x6a, 0x08, 0xa7, 0x45 });
}
