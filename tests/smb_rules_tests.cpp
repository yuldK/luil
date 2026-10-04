#include "net/smb_rules.h"

#include "luil/net/smb_client.h"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <string>
#include <vector>

namespace {
    [[nodiscard]] luil::net::smb_request list_request()
    {
        luil::net::smb_request request {};
        request.location.server = u8"192.168.0.10";
        request.location.share = u8"photos";
        request.location.user = u8"reader";
        request.location.password = u8"secret";
        request.path = u8"2026/summer";
        return request;
    }

    [[nodiscard]] bool refused(const luil::net::smb_request& request, const std::size_t max_read = 1024)
    {
        luil::net::smb_error failure {};
        return luil::net::check_smb_request(request, max_read, failure) == false && failure.kind == luil::net::smb_error_kind::invalid_request
            && failure.message.find(u8"secret") == std::u8string::npos;
    }
} // namespace

TEST_CASE("An SMB request names a server and a share and reads a bounded range", "[net][smb]")
{
    luil::net::smb_error failure {};
    luil::net::smb_request request { list_request() };
    REQUIRE(luil::net::check_smb_request(request, 1024, failure));
    // 포트와 IPv6 꼴의 서버도 받는다. 비운 경로는 공유의 맨 위다.
    request.location.server = u8"[fe80::1]:4445";
    request.path.clear();
    REQUIRE(luil::net::check_smb_request(request, 1024, failure));

    // 서버·공유가 비었거나 공백·구분자·제어 문자가 있으면 거절한다.
    for (const std::u8string server : { u8"", u8"nas box", u8"nas/share", u8"nas\\share", u8"nas\r\n" })
    {
        request = list_request();
        request.location.server = server;
        CHECK(refused(request));
    }
    for (const std::u8string& share : std::vector<std::u8string> { u8"", u8"a/b", u8"a\\b", std::u8string(81, u8'x') })
    {
        request = list_request();
        request.location.share = share;
        CHECK(refused(request));
    }
    // 경로는 `/`로 나눈다. 백슬래시와 제어 문자는 받지 않는다.
    request = list_request();
    request.path = u8"2026\\summer";
    CHECK(refused(request));
    request.path = u8"2026/\tsummer";
    CHECK(refused(request));
    request = list_request();
    request.location.user = u8"reader\n";
    CHECK(refused(request));

    // 읽기의 길이는 1 이상 상한 이하이고, 시작 자리와 더해 넘치지 않는다.
    request = list_request();
    request.operation = luil::net::smb_operation::read;
    request.length = 0;
    CHECK(refused(request));
    request.length = 1025;
    CHECK(refused(request));
    request.length = 1024;
    REQUIRE(luil::net::check_smb_request(request, 1024, failure));
    request.offset = ~std::uint64_t { 0 };
    CHECK(refused(request));
    request = list_request();
    request.timeout = std::chrono::milliseconds { -1 };
    CHECK(refused(request));
}

TEST_CASE("An SMB path is trimmed into the form libsmb2 takes", "[net][smb]")
{
    CHECK(luil::net::smb_share_path(u8"") == "");
    CHECK(luil::net::smb_share_path(u8"/") == "");
    CHECK(luil::net::smb_share_path(u8"/2026//summer/") == "2026/summer");
    CHECK(luil::net::smb_share_path(u8"사진/여름.jpg") == std::string { reinterpret_cast<const char*>(u8"사진/여름.jpg") });
    CHECK(luil::net::smb_last_component(u8"/2026/summer/a.jpg") == u8"a.jpg");
    CHECK(luil::net::smb_last_component(u8"a.jpg/") == u8"a.jpg");
    CHECK(luil::net::smb_last_component(u8"") == u8"");
}

TEST_CASE("SMB failures are sorted by their NT status first and errno next", "[net][smb]")
{
    using luil::net::smb_error_kind;
    using luil::net::smb_error_kind_for;
    // 로그온 실패는 연결 단계든 아니든 같다.
    CHECK(smb_error_kind_for(-ECONNREFUSED, luil::net::smb_status_logon_failure, true) == smb_error_kind::logon_failure);
    CHECK(smb_error_kind_for(-EINVAL, luil::net::smb_status_wrong_password, true) == smb_error_kind::logon_failure);
    CHECK(smb_error_kind_for(-EINVAL, luil::net::smb_status_access_denied, false) == smb_error_kind::access_denied);
    // 없는 공유와 없는 경로는 not_found다.
    CHECK(smb_error_kind_for(-EINVAL, luil::net::smb_status_bad_network_name, true) == smb_error_kind::not_found);
    CHECK(smb_error_kind_for(-EINVAL, luil::net::smb_status_object_name_not_found, false) == smb_error_kind::not_found);
    // NT 상태가 없으면 errno다.
    CHECK(smb_error_kind_for(-ECONNREFUSED, 0, true) == smb_error_kind::cannot_connect);
    CHECK(smb_error_kind_for(-EHOSTUNREACH, 0, false) == smb_error_kind::cannot_connect);
    CHECK(smb_error_kind_for(-ENOENT, 0, false) == smb_error_kind::not_found);
    CHECK(smb_error_kind_for(-EACCES, 0, false) == smb_error_kind::access_denied);
    // 갈래 없는 실패는 연결 단계면 닿지 못한 것, 그 뒤면 그 밖의 실패다.
    CHECK(smb_error_kind_for(-EINVAL, 0, true) == smb_error_kind::cannot_connect);
    CHECK(smb_error_kind_for(-EINVAL, 0, false) == smb_error_kind::failed);
}

TEST_CASE("A build without SMB answers that it is not supported", "[net][smb]")
{
    // core test는 libsmb2 없이 "지원 안 함" client를 링크한다 (Windows, LUIL_ENABLE_SMB를 끈 Android와 같다).
    REQUIRE(luil::net::smb_client::supported() == false);
    std::u8string error {};
    REQUIRE(luil::net::smb_client::create(luil::net::smb_client_config {}, error) == nullptr);
    REQUIRE(error.empty() == false);
}

TEST_CASE("Only a broken transport or a lost session counts as a dropped connection", "[net][smb]")
{
    using luil::net::smb_connection_lost;
    // 전송이 끊겼다 — 다시 쓴 연결이면 새 연결로 한 번 다시 한다.
    CHECK(smb_connection_lost(-ECONNRESET, 0));
    CHECK(smb_connection_lost(-EPIPE, 0));
    CHECK(smb_connection_lost(-ENOTCONN, 0));
    CHECK(smb_connection_lost(-ECONNABORTED, 0));
    CHECK(smb_connection_lost(-ENETUNREACH, 0));
    CHECK(smb_connection_lost(-ETIMEDOUT, 0));
    // 서버가 세션·트리를 잃었다.
    CHECK(smb_connection_lost(-EIO, luil::net::smb_status_user_session_deleted));
    CHECK(smb_connection_lost(-EIO, luil::net::smb_status_network_name_deleted));
    CHECK(smb_connection_lost(-EIO, luil::net::smb_status_network_session_expired));
    // 서버가 요청에 답한 실패는 끊김이 아니다. 로그온 실패는 다시 해도 같다.
    CHECK_FALSE(smb_connection_lost(-EACCES, luil::net::smb_status_logon_failure));
    CHECK_FALSE(smb_connection_lost(-EACCES, luil::net::smb_status_access_denied));
    CHECK_FALSE(smb_connection_lost(-ENOENT, luil::net::smb_status_object_name_not_found));
    CHECK_FALSE(smb_connection_lost(-ECONNRESET, luil::net::smb_status_wrong_password));
    CHECK_FALSE(smb_connection_lost(-EINVAL, 0));
    CHECK_FALSE(smb_connection_lost(0, 0));
}

TEST_CASE("A share list needs only a server and connects to IPC$", "[net][smb][shares]")
{
    luil::net::smb_request request { list_request() };
    request.operation = luil::net::smb_operation::shares;
    request.location.share.clear();
    // 공유·경로는 보지 않는다.
    request.path = u8"ignored\\path";
    luil::net::smb_error failure {};
    CHECK(luil::net::check_smb_request(request, 1024, failure));
    CHECK(luil::net::smb_connection_share(request) == u8"IPC$");

    // 서버는 여전히 있어야 한다.
    request.location.server.clear();
    CHECK(refused(request));

    // 그 밖의 갈래는 공유가 있어야 한다.
    for (const auto operation : { luil::net::smb_operation::list, luil::net::smb_operation::stat, luil::net::smb_operation::read })
    {
        luil::net::smb_request other { list_request() };
        other.operation = operation;
        other.length = 16;
        CHECK(luil::net::smb_connection_share(other) == u8"photos");
        other.location.share.clear();
        CHECK(refused(other));
    }
}

TEST_CASE("A share keeps its kind and flags from the srvsvc type", "[net][smb][shares]")
{
    using luil::net::smb_share_kind;
    const luil::net::smb_share disk { luil::net::make_smb_share(u8"photos", 0x0u, u8"Family photos") };
    CHECK(disk.name == u8"photos");
    CHECK(disk.kind == smb_share_kind::disk);
    CHECK(disk.comment == u8"Family photos");
    CHECK_FALSE(disk.special);
    CHECK_FALSE(disk.temporary);

    CHECK(luil::net::make_smb_share(u8"printer", 0x1u, {}).kind == smb_share_kind::printer);
    CHECK(luil::net::make_smb_share(u8"COM1", 0x2u, {}).kind == smb_share_kind::device);
    const luil::net::smb_share ipc { luil::net::make_smb_share(u8"IPC$", 0x80000003u, u8"IPC Service") };
    CHECK(ipc.kind == smb_share_kind::ipc);
    CHECK(ipc.special);
    CHECK(ipc.native_type == 0x80000003u);
    const luil::net::smb_share admin { luil::net::make_smb_share(u8"C$", 0x80000000u, {}) };
    CHECK(admin.kind == smb_share_kind::disk);
    CHECK(admin.special);
    CHECK(luil::net::make_smb_share(u8"scratch", 0x40000000u, {}).temporary);
}

TEST_CASE("A refused share list is sorted by its Win32 error", "[net][smb][shares]")
{
    CHECK(luil::net::smb_error_kind_for_win32(5u) == luil::net::smb_error_kind::access_denied);
    CHECK(luil::net::smb_error_kind_for_win32(53u) == luil::net::smb_error_kind::failed);
    CHECK(luil::net::smb_error_kind_for_win32(124u) == luil::net::smb_error_kind::failed);
}
