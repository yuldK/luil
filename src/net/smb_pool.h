#pragma once

#include "luil/net/smb_client.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// SMB client가 연결을 다시 쓰는 장부다 (docs/smb-client-design.md의 "연결 다시 쓰기").
//
// libsmb2를 모른다 — 연결은 부른 쪽이 준 손잡이(`void*`)로만 들고, 열고 닫는 일은 부른 쪽이 한다. 잠금도 없다: client가
// 제 잠금 안에서 부른다. 시각은 부른 쪽이 넣는다. 그래서 Windows의 core test에서도 그대로 돈다.
namespace luil::net {
    // 서버(호스트와 포트)를 견주는 꼴로 다듬는다. ASCII 글자를 소문자로 내리고 기본 포트(`:445`)를 뗀다
    // ("NAS:445"와 "nas"는 같다, "[FE80::1]:445"는 "[fe80::1]"이다). 괄호 없는 IPv6 주소는 통째로 호스트다.
    [[nodiscard]] std::u8string smb_server_key(std::u8string_view server);

    // 연결의 열쇠다. 하나라도 다르면 다른 연결이다 (서버·공유·도메인·사용자·암호·암호화 요구).
    //  - 공유·도메인·사용자는 그대로 견준다. 대소문자를 접으면 서버마다 다른 규칙에서 다른 계정의 연결을 다시 쓸 수 있다.
    //  - 비운 사용자는 `guest`와 같다 (client가 그렇게 붙는다).
    //  - 암호는 견주기에만 쓰고 어디에도 적지 않는다. 열쇠가 사라질 때 그 자리를 지운다 (복사본까지 지운다고 보장하지는
    //    않는다).
    struct smb_connection_key
    {
        std::u8string server {};
        std::u8string share {};
        std::u8string domain {};
        std::u8string user {};
        std::u8string password {};
        bool encrypt { false };

        smb_connection_key() = default;
        smb_connection_key(const smb_connection_key&) = default;
        smb_connection_key(smb_connection_key&&) noexcept = default;
        smb_connection_key& operator=(const smb_connection_key&) = default;
        smb_connection_key& operator=(smb_connection_key&&) noexcept = default;
        ~smb_connection_key();

        [[nodiscard]] bool operator==(const smb_connection_key&) const = default;
    };

    // 요청의 위치와 붙을 공유로 열쇠를 짓는다. 공유 목록은 `IPC$`에 붙으므로 공유를 따로 받는다.
    [[nodiscard]] smb_connection_key make_smb_connection_key(const smb_location& location, std::u8string_view share);

    // 빌린 연결 하나다.
    struct smb_lease
    {
        std::uint64_t id { 0 };
        // 쉬던 연결의 손잡이다. 새로 열 자리면 nullptr이다 — 부른 쪽이 열고 `attach`로 건다.
        void* handle { nullptr };
        bool reused { false };
    };

    struct smb_pool_config
    {
        // 쉬는 연결을 닫기까지의 시간이다. 0이면 다시 쓰지 않는다 (돌려받으면 늘 닫는다).
        std::chrono::milliseconds idle_timeout { 30000 };
        // 서버 하나에 동시에 두는 연결(쓰는 중·쉬는 중·여는 중)의 상한이다. 1 이상이다.
        std::size_t max_per_server { 4 };
    };

    class smb_connection_pool
    {
    public:
        using time_point = std::chrono::steady_clock::time_point;

        explicit smb_connection_pool(smb_pool_config config = {}) noexcept;

        // 연결을 빌린다.
        //  - 같은 열쇠의 쉬는 연결이 있으면 그것이다 (`reused`). `fresh`면 쉬는 연결을 보지 않는다 (끊긴 연결의 재시도).
        //  - 없으면 서버의 상한 안에서 새 자리를 잡는다. 상한이 찼으면 그 서버의 쉬는 연결 하나를 비워(`evicted`에
        //    손잡이를 넣는다 — 부른 쪽이 닫는다) 자리를 낸다.
        //  - 그래도 없으면(그 서버의 연결이 모두 쓰는 중) nullopt다. 부른 쪽이 기다렸다 다시 묻는다. 닫힌 장부도 nullopt다.
        [[nodiscard]] std::optional<smb_lease> acquire(const smb_connection_key& key, time_point now, bool fresh, std::vector<void*>& evicted);
        // 빌린 자리에 연 연결을 건다 (nullptr이면 자리만 남긴다 — 끊긴 연결을 버리고 새로 열 때).
        void attach(std::uint64_t id, void* handle) noexcept;
        // 돌려준다. `keep`이고 다시 쓸 수 있으면 쉬게 두고, 아니면 장부에서 빼고 손잡이를 `closed`에 넣는다 (부른 쪽이
        // 닫는다). 다시 쓸 수 없는 때: 쉬는 시간이 0, 닫힌 장부, 빌린 뒤에 `close_idle`이 불렸다, 손잡이가 없다.
        void release(std::uint64_t id, bool keep, time_point now, std::vector<void*>& closed);
        // 쉬는 시간이 지난 연결을 빼 `closed`에 넣는다. 다음으로 지날 시각이다 (쉬는 연결이 없으면 nullopt).
        [[nodiscard]] std::optional<time_point> expire(time_point now, std::vector<void*>& closed);
        // 쉬는 연결을 모두 빼고, 쓰는 중인 연결은 돌려받을 때 닫는다 (네트워크가 바뀌었다, 앱이 뒤로 갔다).
        void close_idle(std::vector<void*>& closed);
        // 장부를 닫는다. `close_idle`과 같고, 그 뒤의 `acquire`는 nullopt, `release`는 늘 닫는다.
        void close_all(std::vector<void*>& closed);

        [[nodiscard]] bool closed() const noexcept
        {
            return closed_;
        }

        // 장부에 있는 연결(쓰는 중·쉬는 중·여는 중)과 그 가운데 쉬는 연결의 수다.
        [[nodiscard]] std::size_t size() const noexcept
        {
            return entries_.size();
        }

        [[nodiscard]] std::size_t idle_count() const noexcept;

    private:
        struct entry
        {
            std::uint64_t id { 0 };
            smb_connection_key key {};
            void* handle { nullptr };
            bool busy { true };
            // 쉬기 시작한 때다.
            time_point idle_since {};
            // 빌린 때의 세대다. `close_idle` 뒤에 돌려받은 연결은 쉬게 두지 않는다.
            std::uint64_t generation { 0 };
        };

        smb_pool_config config_ {};
        std::vector<entry> entries_ {};
        std::uint64_t next_id_ { 0 };
        std::uint64_t generation_ { 0 };
        bool closed_ { false };
    };
} // namespace luil::net
