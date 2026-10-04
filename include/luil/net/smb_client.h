#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// SMB 공유를 읽는 client다 (docs/smb-client-design.md).
//
// **Android 전용이고 `LUIL_ENABLE_SMB`를 켠 구성에서만 선다.** libsmb2(LGPL-2.1, 따로 싼 `libsmb2.so`) 위에
// 선다. 그 밖의 구성(Windows, 끈 Android)에서는 `create`가 nullptr로 답한다 — Windows는 운영체제의 SMB가 있다.
//
// 계약은 `http_client`와 같은 모양이다: 받은 요청마다 표 하나와 답 하나, 답은 client의 thread 하나에서 차례로
// 배달, 취소, `stop`. 같은 서버·공유·계정의 연결은 잠시 쉬게 두었다 다시 쓴다 (`smb_client_config::idle_timeout`).
// 같은 네트워크의 NAS에 붙는 Android 17 앱은 먼저 로컬 네트워크 권한을 받아야 한다
// (`app_host::request_permission(app_permission::local_network)`). 없으면 연결이 시간 상한까지 기다리다 실패한다.
namespace luil::net {
    // SMB 공유 하나에 붙는 정보다.
    struct smb_location
    {
        // 서버다. IP 주소나 DNS 이름이고 포트를 붙일 수 있다 ("192.168.0.10", "nas.example:4445",
        // "[fe80::1]:445"). 기본 포트는 445다.
        //  - Android의 이름 풀기는 `.local`(mDNS) 이름을 풀지 않는다. 집 안 NAS는 IP 주소로 적는다.
        std::u8string server {};
        // 공유 이름이다 ("photos"). 슬래시를 넣지 않는다.
        std::u8string share {};
        // 도메인(작업 그룹)이다. 비면 서버가 정한다.
        std::u8string domain {};
        // 사용자 이름이다. 비면 `guest`로 붙는다.
        std::u8string user {};
        // 암호다. **client는 이것을 기록에 남기지 않는다** (오류 글·logcat에도).
        std::u8string password {};
        // SMB3 암호화를 요구한다. 서버가 하지 못하면 연결이 실패한다. 끄면 서버가 요구할 때만 서명한다.
        bool encrypt { false };
    };

    enum class smb_operation
    {
        // 폴더의 항목을 읽는다 (`.`과 `..`은 뺀다).
        list,
        // 항목 하나의 상태를 읽는다.
        stat,
        // 파일의 한 구간을 읽는다 (`offset`부터 `length`까지).
        read,
    };

    // 요청 하나다. 값이고, client에 넘기면 복사되어 client가 든다.
    struct smb_request
    {
        smb_location location {};
        smb_operation operation { smb_operation::list };
        // 공유 안의 경로다. `/`로 나누고 앞뒤의 `/`는 있어도 없어도 된다. 비면 공유의 맨 위다.
        std::u8string path {};
        // `read`의 시작 자리와 최대 길이다 (바이트). 파일 끝을 넘으면 거기까지만 온다. 길이는 1 이상이고
        // `smb_client_config::max_read_bytes` 이하여야 한다.
        std::uint64_t offset { 0 };
        std::size_t length { 0 };
        // 연결부터 답까지의 상한이다. 0이면 걸지 않는다.
        std::chrono::milliseconds timeout { 30000 };
    };

    // 폴더의 항목이나 상태 하나다.
    struct smb_entry
    {
        std::u8string name {};
        bool directory { false };
        // 바이트다. 폴더는 0이다.
        std::uint64_t size { 0 };
        // 마지막으로 고친 시각이다.
        std::chrono::system_clock::time_point modified {};
    };

    enum class smb_error_kind
    {
        // 오류가 없다.
        none,
        // 요청이 규칙에 맞지 않는다 (선을 건드리지 않았다).
        invalid_request,
        // 이 구성에는 SMB가 없다.
        unsupported,
        // 서버에 닿지 못했다 (이름 풀기, 연결 거절, 닿지 않음, 로컬 네트워크 권한 없음).
        cannot_connect,
        // 사용자 이름이나 암호가 맞지 않는다.
        logon_failure,
        // 공유나 항목에 접근할 권한이 없다.
        access_denied,
        // 공유나 경로가 없다.
        not_found,
        // `timeout`을 넘겼다.
        timed_out,
        // 앱이 `cancel`했다.
        cancelled,
        // `stop()` 중에 끊겼다.
        stopped,
        // 그 밖의 SMB·시스템 오류다. `message`에 libsmb2의 글이 그대로 있다.
        failed,
    };

    // 실패는 예외가 아니라 값이다 (`http_error`와 같은 성격).
    struct smb_error
    {
        smb_error_kind kind { smb_error_kind::none };
        // 개발자가 읽을 진단 영문이다 (libsmb2의 `smb2_get_error` 그대로). 암호는 들어가지 않는다.
        std::u8string message {};
        // libsmb2가 돌려준 값이다 (-errno). 0이면 없다.
        int native_error { 0 };

        [[nodiscard]] bool empty() const noexcept
        {
            return kind == smb_error_kind::none;
        }
    };

    // 요청 하나에 붙는 표다. 0은 "받아들여지지 않았다"는 뜻이다.
    struct smb_ticket
    {
        std::uint64_t value { 0 };

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return value != 0;
        }

        [[nodiscard]] bool operator==(const smb_ticket&) const noexcept = default;
    };

    // 답이 쓴 연결이다 (진단·측정용).
    enum class smb_connection_use
    {
        // 서버에 닿기 전에 끝났다 (연결을 기다리다 취소·정지·시간 상한).
        none,
        // 새로 열었다.
        opened,
        // 쉬던 연결을 다시 썼다. 연결·로그온·공유 연결을 건너뛰었다.
        reused,
        // 다시 쓰려던 연결이 끊겨 있어 새로 열었다 (쉬는 동안 끊긴 것을 보았거나, 쓰다가 끊겨 한 번 다시 했다).
        reopened,
    };

    // 답 하나다. 받아들인 요청 하나에 정확히 하나가 온다 (`stop` 뒤는 제외).
    struct smb_response
    {
        smb_ticket ticket {};
        smb_operation operation { smb_operation::list };
        smb_error error {};
        // `list`의 항목이다 (서버가 준 차례).
        std::vector<smb_entry> entries {};
        // `stat`의 항목이다. 이름은 경로의 마지막 조각이다.
        smb_entry entry {};
        // `read`의 바이트다. 파일 끝이면 요청보다 짧다.
        std::vector<std::uint8_t> data {};
        // 요청을 낸 때부터 답을 다 받은 때까지다.
        std::chrono::milliseconds elapsed { 0 };
        // 이 답이 쓴 연결이다.
        smb_connection_use connection { smb_connection_use::none };

        [[nodiscard]] bool ok() const noexcept
        {
            return error.empty();
        }
    };

    struct smb_client_config
    {
        // 유일한 답의 출구다. client의 배달 thread에서 차례로 불린다. 짧게 끝내고 앱 메시지로 넘긴다
        // (`app_host::post_app_message`). 안에서 `send`·`cancel`을 불러도 되고 `stop`은 부르지 않는다.
        std::function<void(smb_response)> deliver {};
        // 동시에 도는 요청의 상한이다. 넘는 `send`는 숨은 줄 없이 바로 거절된다.
        std::size_t max_in_flight { 4 };
        // `read` 하나가 받을 수 있는 최대 길이다 (기본 16 MiB). 큰 파일은 구간으로 나눠 읽는다.
        std::size_t max_read_bytes { 16u * 1024u * 1024u };
        // `stop`이 진행 중인 요청을 기다리는 상한이다.
        std::chrono::milliseconds stop_budget { 1000 };
        // 쉬는 연결을 닫기까지의 시간이다 (연결 다시 쓰기). 같은 서버·공유·도메인·사용자·암호·암호화 요구의 요청이 이 안에
        // 다시 오면 연결·로그온·공유 연결을 건너뛴다. 0이면 다시 쓰지 않고 요청마다 닫는다 (다시 쓰기 전의 동작).
        std::chrono::milliseconds idle_timeout { 30000 };
        // 서버(호스트와 포트) 하나에 동시에 두는 연결(쓰는 중·쉬는 중)의 상한이다. 모두 쓰는 중이면 요청이 하나가 돌아올
        // 때까지 기다리고, 그 시간도 `timeout`에 든다. 기본값은 기본 `max_in_flight`와 같아 줄이지 않으면 기다릴 일이 없다.
        std::size_t max_connections_per_server { 4 };
    };

    class smb_client
    {
    public:
        // 만든다. 실패하면 nullptr이고 `error`에 이유가 있다. SMB를 세우지 않은 구성(Windows, `LUIL_ENABLE_SMB`
        // 끔)도 그렇다.
        [[nodiscard]] static std::unique_ptr<smb_client> create(smb_client_config config, std::u8string& error);
        // 이 구성에 SMB가 있는가.
        [[nodiscard]] static bool supported() noexcept;

        smb_client(const smb_client&) = delete;
        smb_client(smb_client&&) = delete;
        smb_client& operator=(const smb_client&) = delete;
        smb_client& operator=(smb_client&&) = delete;
        // `stop()`을 부른다.
        ~smb_client();

        // 요청을 받는다. 0이면 받지 않았고(`error`에 이유) 답도 오지 않는다 — 규칙에 맞지 않는 요청, 정지한 client,
        // `max_in_flight` 포화다. **어느 thread에서 불러도 된다.**
        [[nodiscard]] smb_ticket send(smb_request request, std::u8string& error);
        // 진행 중인 요청을 끊는다. 그 요청의 답은 `cancelled`로 하나 온다. 모르는·끝난 표는 아무 일도 없다.
        void cancel(smb_ticket ticket) noexcept;
        void cancel_all() noexcept;
        // 받기를 닫고 진행 중인 요청을 끊고 thread를 거둔다. 돌아온 뒤에는 `deliver`가 불리지 않는다. 여러 번 불러도
        // 된다.
        void stop() noexcept;
        // 받았지만 답의 배달이 끝나지 않은 요청의 수다.
        [[nodiscard]] std::size_t in_flight() const noexcept;
        // 쉬는 연결을 모두 닫고, 쓰는 중인 연결은 그 요청이 끝나면 닫는다. 앱이 뒤로 가거나 네트워크가 바뀔 때 부른다.
        // 어느 thread에서 불러도 된다.
        void close_idle() noexcept;

    private:
        struct engine;

        explicit smb_client(std::unique_ptr<engine> implementation) noexcept;

        std::unique_ptr<engine> engine_ {};
    };
} // namespace luil::net
