#include "net/update_crypto.h"

#include "host/update_manifest.h"
#include "update_fixtures.h"
#include "update_test_signing.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// 자동 업데이트의 Win32 암호(CNG)를 실제 호출로 본다 (docs/auto-update-design.md).

namespace {
    using luil::net::update_signature_check;

    [[nodiscard]] std::vector<std::uint8_t> decoded(const std::u8string_view text)
    {
        std::optional<std::vector<std::uint8_t>> bytes { luil::decode_update_base64(text) };
        REQUIRE(bytes.has_value());
        return *bytes;
    }

    [[nodiscard]] std::array<std::uint8_t, 64> fixture_raw()
    {
        const std::vector<std::uint8_t> bytes { decoded(luil::testing::update_fixture_signature_raw) };
        REQUIRE(bytes.size() == 64);
        std::array<std::uint8_t, 64> raw {};
        std::copy(bytes.begin(), bytes.end(), raw.begin());
        return raw;
    }

    [[nodiscard]] std::u8string digest_text(const std::span<const std::uint8_t> bytes)
    {
        luil::net::update_sha256 hasher {};
        REQUIRE(hasher.valid());
        REQUIRE(hasher.update(bytes));
        const std::optional<std::array<std::uint8_t, 32>> digest { hasher.finish() };
        REQUIRE(digest.has_value());
        return luil::format_update_digest(*digest);
    }

    [[nodiscard]] update_signature_check check_of(const std::u8string_view spki, const std::array<std::uint8_t, 64>& signature, const std::span<const std::uint8_t> message)
    {
        return luil::net::verify_update_signature(decoded(spki), signature, message).check;
    }
}

TEST_CASE("SHA-256 matches the known values", "[update][crypto]")
{
    CHECK(digest_text({}) == u8"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(digest_text(luil::testing::update_fixture_bytes(u8"abc")) == u8"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::vector<std::uint8_t> million(1000000, static_cast<std::uint8_t>('a'));
    CHECK(digest_text(million) == u8"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    CHECK(digest_text(luil::testing::update_fixture_asset()) == luil::testing::update_fixture_asset_sha256);
}

TEST_CASE("SHA-256 fed in chunks equals one pass", "[update][crypto]")
{
    const std::vector<std::uint8_t> bytes { luil::testing::update_fixture_asset(200001) };
    const std::u8string whole { digest_text(bytes) };
    for (const std::size_t step : { std::size_t { 1 }, std::size_t { 63 }, std::size_t { 64 }, std::size_t { 65537 } })
    {
        luil::net::update_sha256 hasher {};
        std::span<const std::uint8_t> rest { bytes };
        while (rest.empty() == false)
        {
            const std::size_t count { rest.size() < step ? rest.size() : step };
            REQUIRE(hasher.update(rest.first(count)));
            rest = rest.subspan(count);
        }
        const std::optional<std::array<std::uint8_t, 32>> digest { hasher.finish() };
        REQUIRE(digest.has_value());
        CHECK(luil::format_update_digest(*digest) == whole);
        // 끝낸 뒤로는 먹이지도 다시 끝내지도 못한다.
        CHECK(hasher.update(bytes) == false);
        CHECK(hasher.finish().has_value() == false);
    }
}

TEST_CASE("the fixed key verifies the openssl signature in both forms", "[update][crypto]")
{
    const std::vector<std::uint8_t> payload { luil::testing::update_fixture_bytes(luil::testing::update_fixture_payload) };
    CHECK(check_of(luil::testing::update_fixture_key_spki, fixture_raw(), payload) == update_signature_check::valid);
    const std::optional<luil::update_raw_signature> from_der { luil::update_signature_from_der(decoded(luil::testing::update_fixture_signature_der)) };
    REQUIRE(from_der.has_value());
    CHECK(check_of(luil::testing::update_fixture_key_spki, *from_der, payload) == update_signature_check::valid);
}

TEST_CASE("a changed byte or another key does not verify", "[update][crypto]")
{
    std::vector<std::uint8_t> payload { luil::testing::update_fixture_bytes(luil::testing::update_fixture_payload) };
    payload[10] ^= 0x01;
    CHECK(check_of(luil::testing::update_fixture_key_spki, fixture_raw(), payload) == update_signature_check::mismatch);
    payload[10] ^= 0x01;

    std::array<std::uint8_t, 64> signature { fixture_raw() };
    signature[63] ^= 0x01;
    CHECK(check_of(luil::testing::update_fixture_key_spki, signature, payload) == update_signature_check::mismatch);
    signature = fixture_raw();
    signature[31] ^= 0x01;
    CHECK(check_of(luil::testing::update_fixture_key_spki, signature, payload) == update_signature_check::mismatch);

    CHECK(check_of(luil::testing::update_fixture_other_key_spki, fixture_raw(), payload) == update_signature_check::mismatch);
}

TEST_CASE("a zero r or s never reaches the system", "[update][crypto]")
{
    const std::vector<std::uint8_t> payload { luil::testing::update_fixture_bytes(luil::testing::update_fixture_payload) };
    std::array<std::uint8_t, 64> signature { fixture_raw() };
    std::fill(signature.begin(), signature.begin() + 32, std::uint8_t { 0 });
    CHECK(check_of(luil::testing::update_fixture_key_spki, signature, payload) == update_signature_check::mismatch);
    signature = fixture_raw();
    std::fill(signature.begin() + 32, signature.end(), std::uint8_t { 0 });
    CHECK(check_of(luil::testing::update_fixture_key_spki, signature, payload) == update_signature_check::mismatch);
}

TEST_CASE("a key that is not P-256 SPKI fails instead of mismatching", "[update][crypto]")
{
    const std::vector<std::uint8_t> payload { luil::testing::update_fixture_bytes(luil::testing::update_fixture_payload) };
    const std::vector<std::uint8_t> nonsense { 0x30, 0x03, 0x02, 0x01, 0x01 };
    const luil::net::update_signature_result result { luil::net::verify_update_signature(nonsense, fixture_raw(), payload) };
    CHECK(result.check == update_signature_check::failed);
    CHECK(result.error.empty() == false);

    // 곡선 위에 없는 점은 CNG가 들이지 못한다.
    std::vector<std::uint8_t> off_curve { decoded(luil::testing::update_fixture_key_spki) };
    off_curve.back() ^= 0x01;
    CHECK(luil::net::verify_update_signature(off_curve, fixture_raw(), payload).check != update_signature_check::valid);
}

TEST_CASE("the test signer produces signatures the verifier accepts", "[update][crypto]")
{
    const std::vector<std::uint8_t> payload { luil::testing::update_fixture_bytes(u8"{\"any\":\"bytes\"}") };
    const std::optional<std::array<std::uint8_t, 64>> signature { luil::testing::sign_update_payload(payload) };
    REQUIRE(signature.has_value());
    CHECK(check_of(luil::testing::update_fixture_key_spki, *signature, payload) == update_signature_check::valid);
    CHECK(check_of(luil::testing::update_fixture_other_key_spki, *signature, payload) == update_signature_check::mismatch);
}

TEST_CASE("a finished download flushes to disk", "[update][crypto]")
{
    const std::filesystem::path folder { std::filesystem::temp_directory_path() / (L"luil-update-" + std::to_wstring(GetCurrentProcessId()) + L"-flush") };
    std::filesystem::create_directories(folder);
    const std::filesystem::path file { folder / L"한글 파일.part" };
    {
        std::ofstream stream { file, std::ios::binary };
        stream << "bytes";
    }
    std::u8string error {};
    CHECK(luil::net::flush_update_file(file, error));
    CHECK(error.empty());
    CHECK(luil::net::flush_update_file(folder / L"missing.part", error) == false);
    CHECK(error.empty() == false);
    std::error_code ignored {};
    std::filesystem::remove_all(folder, ignored);
}
