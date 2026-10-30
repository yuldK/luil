#include "net/smb_pool.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

// SMB client가 연결을 다시 쓰는 장부다. 손잡이는 아무 주소나 되고(장부는 열고 닫지 않는다), 시각은 손으로 넣는다.
namespace {
    using namespace std::chrono_literals;
    using luil::net::smb_connection_key;
    using luil::net::smb_connection_pool;
    using luil::net::smb_lease;
    using time_point = smb_connection_pool::time_point;

    const time_point start { std::chrono::seconds { 1000 } };

    [[nodiscard]] smb_connection_key key_for(const std::u8string server = u8"192.168.0.10", const std::u8string user = u8"reader")
    {
        luil::net::smb_location location {};
        location.server = server;
        location.share = u8"photos";
        location.user = user;
        location.password = u8"secret";
        return luil::net::make_smb_connection_key(location, location.share);
    }

    // 빌려서 손잡이를 걸고 돌려준다 (쉬는 연결 하나를 만든다).
    [[nodiscard]] smb_lease rest(smb_connection_pool& pool, const smb_connection_key& key, void* const handle, const time_point at)
    {
        std::vector<void*> evicted {};
        const std::optional<smb_lease> lease { pool.acquire(key, at, false, evicted) };
        REQUIRE(lease.has_value());
        pool.attach(lease->id, handle);
        std::vector<void*> closed {};
        pool.release(lease->id, true, at, closed);
        REQUIRE(closed.empty());
        return *lease;
    }

    int first {};
    int second {};
}

TEST_CASE("A server key ignores case and the default port", "[net][smb][pool]")
{
    using luil::net::smb_server_key;
    CHECK(smb_server_key(u8"NAS.example:445") == u8"nas.example");
    CHECK(smb_server_key(u8"nas.example") == u8"nas.example");
    CHECK(smb_server_key(u8"nas.example:4445") == u8"nas.example:4445");
    CHECK(smb_server_key(u8"[FE80::1]:445") == u8"[fe80::1]");
    CHECK(smb_server_key(u8"[fe80::1]:4445") == u8"[fe80::1]:4445");
    // 괄호 없는 IPv6 주소의 끝 조각은 포트가 아니다.
    CHECK(smb_server_key(u8"fe80::1:445") == u8"fe80::1:445");
}

TEST_CASE("A connection key differs when any part differs", "[net][smb][pool]")
{
    CHECK(key_for() == key_for(u8"192.168.0.10:445"));
    CHECK_FALSE(key_for() == key_for(u8"192.168.0.11"));
    CHECK_FALSE(key_for() == key_for(u8"192.168.0.10", u8"writer"));
    smb_connection_key other_password { key_for() };
    other_password.password = u8"other";
    CHECK_FALSE(key_for() == other_password);
    smb_connection_key sealed { key_for() };
    sealed.encrypt = true;
    CHECK_FALSE(key_for() == sealed);
    // 비운 사용자는 guest로 붙는다.
    CHECK(key_for(u8"192.168.0.10", u8"") == key_for(u8"192.168.0.10", u8"guest"));
}

TEST_CASE("The same key reuses a resting connection and another key opens a new one", "[net][smb][pool]")
{
    smb_connection_pool pool { { 30s, 4 } };
    const smb_lease opened { rest(pool, key_for(), &first, start) };
    CHECK(opened.reused == false);
    CHECK(opened.handle == nullptr);

    std::vector<void*> evicted {};
    const std::optional<smb_lease> again { pool.acquire(key_for(), start + 1s, false, evicted) };
    REQUIRE(again.has_value());
    CHECK(again->reused);
    CHECK(again->handle == &first);
    CHECK(again->id == opened.id);

    // 쓰는 중이라 같은 열쇠라도 새 자리다.
    const std::optional<smb_lease> busy { pool.acquire(key_for(), start + 1s, false, evicted) };
    REQUIRE(busy.has_value());
    CHECK(busy->reused == false);
    // 다른 계정은 새 자리다.
    const std::optional<smb_lease> other { pool.acquire(key_for(u8"192.168.0.10", u8"writer"), start + 1s, false, evicted) };
    REQUIRE(other.has_value());
    CHECK(other->reused == false);
    CHECK(evicted.empty());
    CHECK(pool.size() == 3u);
}

TEST_CASE("A resting connection closes after its idle time", "[net][smb][pool]")
{
    smb_connection_pool pool { { 30s, 4 } };
    static_cast<void>(rest(pool, key_for(), &first, start));
    std::vector<void*> closed {};
    CHECK(pool.expire(start + 29s, closed) == start + 30s);
    CHECK(closed.empty());
    // 지난 연결은 빌려주지 않는다 (장부에서 빼기 전이라도).
    std::vector<void*> evicted {};
    const std::optional<smb_lease> late { pool.acquire(key_for(), start + 30s, false, evicted) };
    REQUIRE(late.has_value());
    CHECK(late->reused == false);
    CHECK(pool.expire(start + 30s, closed).has_value() == false);
    REQUIRE(closed.size() == 1u);
    CHECK(closed[0] == &first);
}

TEST_CASE("A full server waits for a connection or gives up its oldest resting one", "[net][smb][pool]")
{
    smb_connection_pool pool { { 30s, 1 } };
    std::vector<void*> evicted {};
    const std::optional<smb_lease> busy { pool.acquire(key_for(), start, false, evicted) };
    REQUIRE(busy.has_value());
    pool.attach(busy->id, &first);
    // 그 서버의 연결이 모두 쓰는 중이다. 부른 쪽이 기다린다.
    CHECK(pool.acquire(key_for(), start, false, evicted).has_value() == false);
    CHECK(pool.acquire(key_for(u8"192.168.0.10", u8"writer"), start, false, evicted).has_value() == false);
    // 다른 서버는 상한과 상관없다.
    CHECK(pool.acquire(key_for(u8"192.168.0.20"), start, false, evicted).has_value());

    std::vector<void*> closed {};
    pool.release(busy->id, true, start + 1s, closed);
    CHECK(closed.empty());
    // 다른 계정이 오면 쉬는 연결을 비워 자리를 낸다.
    const std::optional<smb_lease> other { pool.acquire(key_for(u8"192.168.0.10", u8"writer"), start + 2s, false, evicted) };
    REQUIRE(other.has_value());
    CHECK(other->reused == false);
    REQUIRE(evicted.size() == 1u);
    CHECK(evicted[0] == &first);
}

TEST_CASE("A fresh lease skips resting connections of the same key", "[net][smb][pool]")
{
    smb_connection_pool pool { { 30s, 2 } };
    static_cast<void>(rest(pool, key_for(), &first, start));
    std::vector<void*> evicted {};
    const std::optional<smb_lease> fresh { pool.acquire(key_for(), start + 1s, true, evicted) };
    REQUIRE(fresh.has_value());
    CHECK(fresh->reused == false);
    CHECK(evicted.empty());
    // 상한이 찼으면 그 쉬는 연결을 비운다.
    const std::optional<smb_lease> full { pool.acquire(key_for(), start + 1s, true, evicted) };
    REQUIRE(full.has_value());
    REQUIRE(evicted.size() == 1u);
    CHECK(evicted[0] == &first);
}

TEST_CASE("A connection that may not be reused closes when it comes back", "[net][smb][pool]")
{
    std::vector<void*> evicted {};
    std::vector<void*> closed {};
    // 다시 쓰지 않는 장부다 (쉬는 시간 0).
    smb_connection_pool once { { 0ms, 4 } };
    const std::optional<smb_lease> lease { once.acquire(key_for(), start, false, evicted) };
    REQUIRE(lease.has_value());
    once.attach(lease->id, &first);
    once.release(lease->id, true, start, closed);
    CHECK(closed == std::vector<void*> { &first });
    CHECK(once.size() == 0u);

    // 깨끗하게 끝나지 않은 연결은 버린다.
    smb_connection_pool pool { { 30s, 4 } };
    closed.clear();
    const std::optional<smb_lease> broken { pool.acquire(key_for(), start, false, evicted) };
    REQUIRE(broken.has_value());
    pool.attach(broken->id, &first);
    pool.release(broken->id, false, start, closed);
    CHECK(closed == std::vector<void*> { &first });

    // 연결을 열지 못한 자리는 손잡이 없이 빠진다.
    closed.clear();
    const std::optional<smb_lease> failed { pool.acquire(key_for(), start, false, evicted) };
    REQUIRE(failed.has_value());
    pool.release(failed->id, true, start, closed);
    CHECK(closed.empty());
    CHECK(pool.size() == 0u);
}

TEST_CASE("Closing idle connections retires the busy ones too", "[net][smb][pool]")
{
    smb_connection_pool pool { { 30s, 4 } };
    static_cast<void>(rest(pool, key_for(), &first, start));
    std::vector<void*> evicted {};
    const std::optional<smb_lease> busy { pool.acquire(key_for(u8"192.168.0.20"), start, false, evicted) };
    REQUIRE(busy.has_value());
    pool.attach(busy->id, &second);

    std::vector<void*> closed {};
    pool.close_idle(closed);
    CHECK(closed == std::vector<void*> { &first });
    // 네트워크가 바뀐 뒤 돌아온 연결은 쉬게 두지 않는다.
    closed.clear();
    pool.release(busy->id, true, start + 1s, closed);
    CHECK(closed == std::vector<void*> { &second });
    // 그 뒤에 빌린 연결은 다시 쓴다.
    static_cast<void>(rest(pool, key_for(), &first, start + 2s));
    CHECK(pool.idle_count() == 1u);
}

TEST_CASE("A closed pool lends nothing and closes what comes back", "[net][smb][pool]")
{
    smb_connection_pool pool { { 30s, 4 } };
    static_cast<void>(rest(pool, key_for(), &first, start));
    std::vector<void*> evicted {};
    const std::optional<smb_lease> busy { pool.acquire(key_for(u8"192.168.0.20"), start, false, evicted) };
    REQUIRE(busy.has_value());
    pool.attach(busy->id, &second);

    std::vector<void*> closed {};
    pool.close_all(closed);
    CHECK(pool.closed());
    CHECK(closed == std::vector<void*> { &first });
    CHECK(pool.acquire(key_for(), start, false, evicted).has_value() == false);
    closed.clear();
    pool.release(busy->id, true, start, closed);
    CHECK(closed == std::vector<void*> { &second });
    CHECK(pool.size() == 0u);
}
