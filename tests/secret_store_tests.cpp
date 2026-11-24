#include "luil/app/secret_store.h"

#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32)
#include <windows.h>

#include <wincred.h>
#else
#include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// 비밀 저장소를 실제 OS 저장소로 본다 (secret-store-design.md). Windows는 자격 증명 관리자, macOS는 Keychain이다.
// test가 쓴 항목은 그 자리에서 지운다. 프로세스 id와 시각을 써 과거 test의 항목과도 겹치지 않게 한다.

namespace {
    [[nodiscard]] std::u8string test_service()
    {
#if defined(_WIN32)
        const std::string id { std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) };
#else
        const auto now { std::chrono::steady_clock::now().time_since_epoch() };
        const std::string id { std::to_string(::getpid()) + "-" + std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()) };
#endif
        std::u8string service { u8"luil-tests-" };
        service.append(id.begin(), id.end());
        return service;
    }

    [[nodiscard]] std::vector<std::uint8_t> bytes_of(const std::u8string_view text)
    {
        return { text.begin(), text.end() };
    }

    struct scoped_key
    {
        std::u8string_view service {};
        std::u8string_view name {};

        ~scoped_key()
        {
            static_cast<void>(luil::erase_secret(service, name));
        }
    };

#if defined(_WIN32)
    struct scoped_legacy_key
    {
        std::wstring target {};

        ~scoped_legacy_key()
        {
            static_cast<void>(CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0));
        }
    };
#endif

    void check_distinct(const scoped_key& first, const scoped_key& second)
    {
        using luil::secret_store_status;
        const auto first_value { bytes_of(u8"first") };
        const auto second_value { bytes_of(u8"second") };
        REQUIRE(luil::store_secret(first.service, first.name, first_value).status == secret_store_status::ok);
        REQUIRE(luil::store_secret(second.service, second.name, second_value).status == secret_store_status::ok);
        std::vector<std::uint8_t> read {};
        REQUIRE(luil::load_secret(first.service, first.name, read).status == secret_store_status::ok);
        REQUIRE(read == first_value);
        REQUIRE(luil::load_secret(second.service, second.name, read).status == secret_store_status::ok);
        REQUIRE(read == second_value);
        REQUIRE(luil::erase_secret(first.service, first.name).status == secret_store_status::ok);
        REQUIRE(luil::load_secret(second.service, second.name, read).status == secret_store_status::ok);
        REQUIRE(read == second_value);
    }
}

TEST_CASE("A secret is written, read back, overwritten and erased", "[secret]")
{
    const std::u8string service { test_service() };
    const std::u8string name { u8"서버/관리자" };
    std::vector<std::uint8_t> read {};
    REQUIRE(luil::load_secret(service, name, read).status == luil::secret_store_status::not_found);

    REQUIRE(luil::store_secret(service, name, bytes_of(u8"첫 토큰")).status == luil::secret_store_status::ok);
    REQUIRE(luil::load_secret(service, name, read).status == luil::secret_store_status::ok);
    REQUIRE(read == bytes_of(u8"첫 토큰"));

    // 같은 짝에 다시 쓰면 덮는다. 다른 이름은 따로다.
    REQUIRE(luil::store_secret(service, name, bytes_of(u8"둘째 토큰")).status == luil::secret_store_status::ok);
    REQUIRE(luil::load_secret(service, name, read).status == luil::secret_store_status::ok);
    REQUIRE(read == bytes_of(u8"둘째 토큰"));
    REQUIRE(luil::load_secret(service, u8"다른 이름", read).status == luil::secret_store_status::not_found);
    REQUIRE(read.empty());

    REQUIRE(luil::erase_secret(service, name).status == luil::secret_store_status::ok);
    REQUIRE(luil::erase_secret(service, name).status == luil::secret_store_status::not_found);
    REQUIRE(luil::load_secret(service, name, read).status == luil::secret_store_status::not_found);
}

TEST_CASE("A secret keeps arbitrary bytes up to the store limit", "[secret]")
{
    const std::u8string service { test_service() };
    std::vector<std::uint8_t> value(luil::secret_store_maximum_bytes);
    for (std::size_t index { 0 }; index < value.size(); ++index)
        value[index] = static_cast<std::uint8_t>(index * 37u);
    REQUIRE(luil::store_secret(service, u8"binary", value).status == luil::secret_store_status::ok);
    std::vector<std::uint8_t> read {};
    REQUIRE(luil::load_secret(service, u8"binary", read).status == luil::secret_store_status::ok);
    REQUIRE(read == value);
    REQUIRE(luil::erase_secret(service, u8"binary").status == luil::secret_store_status::ok);

    // 빈 값도 값이다.
    REQUIRE(luil::store_secret(service, u8"empty", {}).status == luil::secret_store_status::ok);
    read.push_back(1u);
    REQUIRE(luil::load_secret(service, u8"empty", read).status == luil::secret_store_status::ok);
    REQUIRE(read.empty());
    REQUIRE(luil::erase_secret(service, u8"empty").status == luil::secret_store_status::ok);
}

TEST_CASE("A secret over the limit or without a name is refused", "[secret]")
{
    const std::u8string service { test_service() };
    const std::vector<std::uint8_t> too_large(luil::secret_store_maximum_bytes + 1u, 7u);
    const luil::secret_store_result large { luil::store_secret(service, u8"large", too_large) };
    REQUIRE(large.status == luil::secret_store_status::failed);
    REQUIRE(large.error.find(u8"2560") != std::u8string::npos);
    std::vector<std::uint8_t> read {};
    REQUIRE(luil::load_secret(service, u8"large", read).status == luil::secret_store_status::not_found);

    REQUIRE(luil::store_secret(u8"", u8"name", bytes_of(u8"x")).status == luil::secret_store_status::failed);
    REQUIRE(luil::store_secret(service, u8"", bytes_of(u8"x")).status == luil::secret_store_status::failed);
    REQUIRE(luil::erase_secret(service, u8"").status == luil::secret_store_status::failed);
}

#if defined(_WIN32)
TEST_CASE("Windows secret keys distinguish slashes, letter case and embedded zero bytes", "[secret]")
#else
TEST_CASE("Secret keys distinguish slashes and letter case and refuse embedded zero bytes", "[secret]")
#endif
{
    const std::u8string service { test_service() };
    check_distinct({ service, u8"a/b" }, { service + u8"/a", u8"b" });
    check_distinct({ service + u8"A", u8"name" }, { service + u8"a", u8"name" });
    check_distinct({ service, u8"Name" }, { service, u8"name" });
#if defined(__APPLE__)
    // Keychain은 속성을 NUL에서 자른다. 남의 항목을 덮지 않게 쓰기 전에 거절한다.
    const std::u8string_view with_zero { u8"name\0tail", 9 };
    const auto refused { luil::store_secret(service, with_zero, bytes_of(u8"x")) };
    REQUIRE(refused.status == luil::secret_store_status::failed);
    REQUIRE(refused.error.find(u8"NUL") != std::u8string::npos);
    std::vector<std::uint8_t> read { 1u };
    REQUIRE(luil::load_secret(with_zero, u8"name", read).status == luil::secret_store_status::failed);
    REQUIRE(read.empty());
    REQUIRE(luil::erase_secret(service, with_zero).status == luil::secret_store_status::failed);
#else
    check_distinct({ service, u8"name" }, { service, std::u8string_view { u8"name\0tail", 9 } });

    std::u8string first_service { service };
    first_service.append(u8"\0one", 4);
    std::u8string second_service { service };
    second_service.append(u8"\0two", 4);
    check_distinct({ first_service, u8"name" }, { second_service, u8"name" });
#endif
}

#if defined(_WIN32)
TEST_CASE("Windows rejects malformed UTF-8 and oversized secret keys", "[secret]")
{
    using luil::secret_store_status;
    const std::u8string service { test_service() };
    const std::u8string oversized(CRED_MAX_GENERIC_TARGET_NAME_LENGTH, u8'x');
    const auto result { luil::store_secret(service, oversized, {}) };
    REQUIRE(result.status == secret_store_status::failed);
    REQUIRE(result.error.find(u8"too long") != std::u8string::npos);
#else
TEST_CASE("Malformed UTF-8 secret keys are refused", "[secret]")
{
    using luil::secret_store_status;
    const std::u8string service { test_service() };
#endif

    constexpr char8_t invalid_byte { 0xffu };
    const std::u8string_view malformed { &invalid_byte, 1 };
    std::vector<std::uint8_t> read { 1u };
    REQUIRE(luil::load_secret(service, malformed, read).status == secret_store_status::failed);
    REQUIRE(read.empty());
    REQUIRE(luil::erase_secret(malformed, service).status == secret_store_status::failed);
}

#if defined(_WIN32)
TEST_CASE("Windows copies legacy credentials only on request and keeps both versions safe", "[secret]")
{
    using luil::secret_store_status;
    const std::u8string service { test_service() };
    const std::u8string other_service { service + u8"/old" };
    const scoped_key owned { service, u8"old/server" };
    const scoped_key other { other_service, u8"server" };
    scoped_legacy_key legacy { L"luil:" };
    legacy.target.append(service.begin(), service.end());
    legacy.target += L"/old/server";
    auto old_value { bytes_of(u8"legacy token") };
    CREDENTIALW credential {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = legacy.target.data();
    credential.CredentialBlobSize = static_cast<DWORD>(old_value.size());
    credential.CredentialBlob = old_value.data();
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    REQUIRE(CredWriteW(&credential, 0) != FALSE);

    std::vector<std::uint8_t> read { 1u };
    REQUIRE(luil::load_secret(owned.service, owned.name, read).status == secret_store_status::not_found);
    REQUIRE(read.empty());
    REQUIRE(luil::load_secret(other.service, other.name, read).status == secret_store_status::not_found);
    REQUIRE(luil::erase_secret(owned.service, owned.name).status == secret_store_status::not_found);
    REQUIRE(luil::erase_secret(other.service, other.name).status == secret_store_status::not_found);
    REQUIRE(luil::migrate_legacy_win32_secret(service, u8"missing").status == secret_store_status::not_found);

    const auto new_value { bytes_of(u8"new token") };
    REQUIRE(luil::store_secret(other.service, other.name, new_value).status == secret_store_status::ok);
    REQUIRE(luil::migrate_legacy_win32_secret(other.service, other.name).status == secret_store_status::already_exists);
    REQUIRE(luil::load_secret(other.service, other.name, read).status == secret_store_status::ok);
    REQUIRE(read == new_value);

    REQUIRE(luil::migrate_legacy_win32_secret(owned.service, owned.name).status == secret_store_status::ok);
    REQUIRE(luil::load_secret(owned.service, owned.name, read).status == secret_store_status::ok);
    REQUIRE(read == old_value);
    REQUIRE(luil::migrate_legacy_win32_secret(owned.service, owned.name).status == secret_store_status::already_exists);
    REQUIRE(luil::erase_secret(owned.service, owned.name).status == secret_store_status::ok);
    REQUIRE(luil::migrate_legacy_win32_secret(owned.service, owned.name).status == secret_store_status::ok);
    REQUIRE(luil::load_secret(owned.service, owned.name, read).status == secret_store_status::ok);
    REQUIRE(read == old_value);
}
#else
TEST_CASE("There is nothing to migrate outside Windows", "[secret]")
{
    const std::u8string service { test_service() };
    REQUIRE(luil::migrate_legacy_win32_secret(service, u8"server").status == luil::secret_store_status::unsupported);
    std::vector<std::uint8_t> read { 1u };
    REQUIRE(luil::load_secret(service, u8"server", read).status == luil::secret_store_status::not_found);
    REQUIRE(read.empty());
}
#endif
