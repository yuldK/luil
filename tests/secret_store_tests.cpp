#include "luil/app/secret_store.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

// 비밀 저장소의 Windows 쪽을 실제 자격 증명 관리자로 본다 (secret-store-design.md).
// test가 쓴 항목은 그 자리에서 지운다. service 이름에 프로세스 id를 넣어 동시에 도는 test 실행끼리 겹치지 않는다.

namespace {
    [[nodiscard]] std::u8string test_service()
    {
        const std::string id { std::to_string(GetCurrentProcessId()) };
        std::u8string service { u8"luil-tests-" };
        service.append(id.begin(), id.end());
        return service;
    }

    [[nodiscard]] std::vector<std::uint8_t> bytes_of(const std::u8string_view text)
    {
        return { text.begin(), text.end() };
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
