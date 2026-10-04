#pragma once

#include "luil/net/smb_client.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// SMB client의 순수 규칙이다. libsmb2를 모르므로 어느 구성에서나 세우고 Windows의 core test에서도 돈다.
namespace luil::net {
    // NT 상태 값이다 (MS-ERREF). libsmb2의 smb2-errors.h와 같은 값이고, 그 헤더 없이 쓰려고 옮겨 둔다.
    inline constexpr std::uint32_t smb_status_no_such_file { 0xC000000F };
    inline constexpr std::uint32_t smb_status_access_denied { 0xC0000022 };
    inline constexpr std::uint32_t smb_status_object_name_not_found { 0xC0000034 };
    inline constexpr std::uint32_t smb_status_object_path_not_found { 0xC000003A };
    inline constexpr std::uint32_t smb_status_wrong_password { 0xC000006A };
    inline constexpr std::uint32_t smb_status_logon_failure { 0xC000006D };
    inline constexpr std::uint32_t smb_status_account_restriction { 0xC000006E };
    inline constexpr std::uint32_t smb_status_password_expired { 0xC0000071 };
    inline constexpr std::uint32_t smb_status_account_disabled { 0xC0000072 };
    inline constexpr std::uint32_t smb_status_bad_network_name { 0xC00000CC };

    // 요청을 본다. 어기면 `invalid_request`이고 아무것도 보내지 않는다.
    //  - 서버·공유는 비지 않고, 제어 문자·공백·`/`·`\`가 없다. 공유 이름은 80자 이하다.
    //  - 경로·도메인·사용자 이름에 제어 문자가 없고, 경로는 `\` 대신 `/`로 나눈다.
    //  - `read`의 길이는 1 이상 `max_read_bytes` 이하이고, 시작 자리와 더해 넘치지 않는다.
    //  - 시간 상한은 음수가 아니다.
    [[nodiscard]] bool check_smb_request(const smb_request& request, std::size_t max_read_bytes, smb_error& failure);

    // 공유 안의 경로를 libsmb2가 받는 꼴로 다듬는다. 앞뒤의 `/`를 떼고 겹친 `/`를 하나로 줄인다. 맨 위는 빈 글이다.
    [[nodiscard]] std::string smb_share_path(std::u8string_view path);

    // 경로의 마지막 조각이다. 맨 위면 빈 글이다.
    [[nodiscard]] std::u8string smb_last_component(std::u8string_view path);

    // libsmb2의 결과(-errno)와 NT 상태로 오류 갈래를 정한다. `connecting`은 연결·로그온·공유 연결 단계의 실패다 —
    // 그 단계의 갈래 없는 실패는 서버에 닿지 못한 것으로 본다.
    [[nodiscard]] smb_error_kind smb_error_kind_for(int status, std::uint32_t nt_status, bool connecting) noexcept;
} // namespace luil::net
