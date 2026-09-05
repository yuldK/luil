#pragma once

#include "luil/net/http_message.h"

#include <windows.h>

#include <winhttp.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace luil::net {
    // 몸을 읽는 그릇의 크기다. 문맥 안에 붙박이라 요청이 끝날 때까지 자리가 바뀌지
    // 않는다 — `WinHttpQueryDataAvailable`로 크기를 물어 그때그때 잡는 길을 버린
    // 자리이고, 버린 이유는 상태가 하나 늘고 버퍼 수명 함정이 생기기 때문이다.
    inline constexpr std::size_t http_read_chunk_bytes { 64u * 1024u };

    // 원시 헤더 글의 상한이다. 밖에서 오는 값이라 상한이 필요하고, 1 MiB는 어떤
    // 성한 응답보다도 크다.
    inline constexpr std::size_t http_header_limit_bytes { 1024u * 1024u };

    // 미리 잡는 몸 크기의 상한이다. `Content-Length: 4 GB`가 우리 메모리를 예약하게
    // 두지 않는다 (진짜 관문은 누적 검사다 — http-client-design.md).
    inline constexpr std::size_t http_body_reserve_limit_bytes { 1024u * 1024u };

    // 요청 하나가 어디까지 왔는가. 흐름을 정하는 것은 걸쇠이고, 이것이 흐름에 끼는
    // 자리는 하나다 — `preparing`인 동안은 `finish_and_close`가 손잡이를 닫지 않는다
    // (손잡이를 꾸미는 thread만이 그때 손잡이의 임자다).
    enum class request_phase
    {
        preparing,
        sending,
        receiving,
        reading,
        finished,
    };

    // 문맥의 임자다.
    //
    // 문맥이 `http_client`의 engine이 무엇인지 알 필요도, 알 수도 없다 (그 struct는
    // http_client.cpp 안에 산다). 콜백이 임자에게 시키는 일이 하나뿐이라 문도 하나다.
    struct request_owner
    {
        request_owner() = default;
        request_owner(const request_owner&) = delete;
        request_owner(request_owner&&) = delete;
        request_owner& operator=(const request_owner&) = delete;
        request_owner& operator=(request_owner&&) = delete;
        virtual ~request_owner() = default;

        // 손잡이가 닫혔다 — 지도에서 지우고 완료를 내는 자리다.
        // 번호 하나에 정확히 한 번, `close_request`가 표를 쥔 채로 부른다.
        virtual void on_request_closed(std::uint64_t id) noexcept = 0;
    };

    // 콜백 thread가 지어 pump thread로 건너가는 완료 하나다.
    //
    // **몸을 푸는 것은 이 값을 받은 pump의 일이다.** 콜백에서 풀면 OS의 thread pool을
    // 초 단위로 붙잡게 되고 그것이 WinHTTP 계약을 어기는 자리다.
    struct http_completion
    {
        std::uint64_t id { 0 };
        http_ticket ticket {};
        std::uint64_t sequence { 0 };
        int status_code { 0 };
        std::u8string reason {};
        std::u8string final_url {};
        std::vector<http_header> headers {};
        std::vector<std::uint8_t> body {};
        http_error error {};
        http_body_parse_options parse {};
        // 파싱 **전에** 잰 왕복 시간이다 (`http_response::elapsed`의 규약).
        std::chrono::milliseconds elapsed { 0 };
    };

    // 요청 하나의 상태 전부다.
    //
    // `shared_ptr`로 살고 임자의 지도가 그 유일한 소유자다 — 콜백은 번호로 찾아
    // 사본을 받으므로 늦게 온 콜백이 죽은 포인터를 따라갈 길이 아예 없다.
    struct request_context
    {
        request_owner* owner { nullptr };
        // 콜백이 받는 번호다 (한 번짜리는 표와 같고, 되풀이는 회차마다 새로 난다).
        std::uint64_t id { 0 };
        http_ticket ticket {};
        std::uint64_t sequence { 0 };
        http_request request {};
        std::chrono::steady_clock::time_point started {};
        std::chrono::steady_clock::time_point deadline {};
        bool has_deadline { false };

        // 손잡이·단계·결말을 지킨다.
        // **이것을 쥔 채로 `WinHttp*`를 부르지 않는다** — `WinHttpCloseHandle`이 같은
        // thread에서 곧바로 `HANDLE_CLOSING`을 부를 수 있어 자기 자신에 막힌다.
        std::mutex guard {};
        HINTERNET handle { nullptr };
        request_phase phase { request_phase::preparing };
        bool outcome_latched { false };
        http_error outcome {};

        // WinHTTP가 보는 동안 살아 있어야 하는 것들이다.
        std::vector<std::uint8_t> upload {};
        std::array<std::uint8_t, http_read_chunk_bytes> chunk {};
        std::vector<std::uint8_t> body {};

        // 아래는 콜백만 만진다 (한 손잡이의 알림은 겹치지 않는다).
        int status_code { 0 };
        std::u8string reason {};
        std::u8string final_url {};
        std::vector<http_header> headers {};
        std::size_t content_length_hint { 0 };
        int redirects { 0 };
        unsigned long secure_flags { 0 };
    };

    // 결말을 걸쇠에 적고(**먼저 적은 것이 이긴다**) 손잡이를 잠금 **밖에서** 닫는다.
    //
    // 취소·마감·몸 상한·`stop`이 전부 뒤이어 `ERROR_WINHTTP_OPERATION_CANCELLED`로
    // 돌아오므로, 사유를 닫기 전에 적어 두지 않으면 우리가 끊은 압축 폭탄이 "사용자가
    // 취소했다"로 보고된다.
    //  - 부른 뒤에는 문맥도 임자도 만지지 않는다. 닫기가 같은 thread에서 완료 절차를
    //    끝까지 돌 수 있어, 돌아온 시점에는 둘 다 이미 사라졌을 수 있다.
    //  - `preparing` 단계에서는 걸쇠만 적는다 (손잡이는 꾸미는 thread가 닫는다).
    void finish_and_close(request_context& context, const http_error& outcome) noexcept;

    // 지금 손잡이의 사본이다 (닫혔으면 nullptr). 잠금 밖에서 부르기 위한 자리다.
    [[nodiscard]] HINTERNET borrow_handle(request_context& context) noexcept;

    void set_phase(request_context& context, request_phase phase) noexcept;

    // 받은 조각을 누적한다. `max_body_bytes`를 넘으면 거짓이다.
    // **압축을 푼 뒤의 크기라 이쪽이 진짜 관문이다** (헤더의 길이는 압축된 크기다).
    [[nodiscard]] bool append_body(request_context& context, std::span<const std::uint8_t> bytes);

    // 헤더가 왔다 — 상태 줄·헤더 전부·최종 URL·`Content-Length`를 문맥에 옮긴다.
    [[nodiscard]] bool capture_response_head(request_context& context, http_error& error);

    // 재지정 알림이 들고 온 새 주소를 `final_url`에 적는다.
    void capture_redirect_url(request_context& context, const wchar_t* url, std::size_t length);

    // 완료 payload를 짓는다 (몸과 헤더를 문맥에서 옮겨 온다).
    [[nodiscard]] http_completion make_completion(request_context& context);

    // 번호 → 문맥 표다. **프로세스에 하나** 있고, 번호도 프로세스 하나에서 발번한다.
    //
    // 이 표가 있는 이유는 WinHTTP 콜백이 받는 것이 번호 하나뿐이기 때문이다 — 그
    // 번호로 문맥을 찾아야 하고, 임자가 누구인지도 그 문맥을 열어야 안다. client가
    // 여럿이어도 번호가 겹치지 않으므로 표 하나로 충분하다.
    void register_request(std::uint64_t id, const std::shared_ptr<request_context>& context);
    // 손잡이가 서지 않아 `HANDLE_CLOSING`이 오지 않는 실패 경로의 자리다.
    void forget_request(std::uint64_t id) noexcept;
    [[nodiscard]] std::shared_ptr<request_context> find_request(std::uint64_t id) noexcept;
    // `HANDLE_CLOSING`의 자리다. 표에서 지우고, **표를 쥔 채로** 임자의 마무리를
    // 부른다 — 그 사이 임자가 부서지지 않는 것이 이 잠금의 두 번째 일이다.
    void close_request(std::uint64_t id) noexcept;
    // 표를 쥔 콜백이 지금 없음을 보장하는 관문이다. 임자가 부서지기 전에 지난다.
    void wait_for_request_lookups() noexcept;
} // namespace luil::net
