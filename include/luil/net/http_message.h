#pragma once

#include "luil/net/http_body.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luil::net {
    enum class http_method
    {
        get,
        head,
        post,
        put,
        patch,
        // `delete`가 예약어라 이 이름이다. 선으로 나가는 동사는 `DELETE`다.
        remove,
        options,
    };

    // 선으로 나가는 동사다 (`http_method::remove`는 `DELETE`).
    // 전송이 쓰는 것이자 test가 어긋난 이름 하나를 잡는 자리다.
    //  - 글로 된 동사 칸을 따로 두지 않는다. 두 자리가 같은 것을 말하면 어긋났을
    //    때 어느 쪽이 이기는지 아무도 기억하지 못한다 — 필요하면 값을 는다.
    [[nodiscard]] std::u8string_view http_method_name(http_method method) noexcept;

    struct http_header
    {
        std::u8string name {};
        std::u8string value {};

        [[nodiscard]] bool operator==(const http_header&) const noexcept = default;
    };

    // 헤더 하나를 찾는다 (이름은 대소문자를 가리지 않는 ASCII 비교).
    // 없으면 빈 view다. 같은 이름이 여럿이면 첫 번째다.
    [[nodiscard]] std::u8string_view find_header(std::span<const http_header> headers, std::u8string_view name) noexcept;

    // 재지정이 다른 출처로 옮길 때도 따라가는 요청 헤더인가 (`http_redirect_policy::follow`).
    // `Accept`·`Accept-Language`·`Cache-Control`·`Range`·`If-Range`·`If-None-Match`·
    // `If-Modified-Since`만 참이다. 그 밖의 이름은 비밀을 담을 수 있다고 보고 지운다.
    [[nodiscard]] bool http_header_crosses_origins(std::u8string_view name) noexcept;

    // 글 하나를 요청 몸의 바이트로 옮긴다 (UTF-8 그대로, 변환 없음).
    // json을 실어 보내는 자리가 이것이다 — `dump()`한 글을 그대로 넣는다.
    [[nodiscard]] std::vector<std::uint8_t> http_text_body(std::u8string_view text);

    // 재지정을 따라갈 것인가.
    enum class http_redirect_policy
    {
        // 따라가지 않는다. 3xx가 그대로 답이 된다 (`Location`은 헤더에 있다).
        none,
        // 따라간다. **https에서 http로 내려가는 것은 따라가지 않는다** — 그것을
        // 따라가면 앱이 요구한 보안이 서버 말 한마디로 사라진다. 그 한 걸음은 `none`과
        // 같이 처리된다: 그 3xx가 그대로 답이 되고 `Location`에 http 주소가 남는다.
        // 다른 출처로 옮길 때는 `headers`에 넣은 헤더를 지운다. 토큰을 담은 이름을
        // 라이브러리가 모두 알 수 없어서, 지울 이름이 아니라 **남길 이름**을 정한다.
        // 남는 것은 비밀을 담지 않고 요청의 뜻을 정하는 표준 헤더뿐이다
        // (`http_header_crosses_origins`) — 이어받기의 `Range`나 조건부 요청이
        // 재지정 한 번에 다른 질문으로 바뀌지 않게 한다.
        //  - 켜는 손잡이를 두지 않는 이유는 인증서 검증을 끄는 손잡이를 두지 않는
        //    이유와 같다: 공개 API에 그 스위치가 있으면 켠 채로 출시하는 앱이 생긴다.
        follow,
    };

    // 받을 몸의 기본 상한이다 (64 MiB).
    //
    // 몸은 **밖에서 오는 값이라** 상한이 필요하다 — 상한이 없으면 서버 하나가 앱의
    // 메모리를 정한다. 디코딩이 픽셀에 상한을 두는 것과 같은 성격이고, 다른 점은
    // 이쪽은 앱이 요청마다 바꾼다는 것이다 (무엇을 받을지 아는 쪽이 앱이라서다).
    inline constexpr std::size_t default_http_body_limit { 64u * 1024u * 1024u };

    // 요청 하나다. 값이고, client에 넘기면 복사되어 client가 든다.
    struct http_request
    {
        // 절대 URL이다 (http·https만). 다른 scheme은 선을 건드리지 않고 거절된다.
        std::u8string url {};
        http_method method { http_method::get };
        // 더 붙일 헤더다. 같은 이름이 있으면 갈아 끼운다.
        //  - 이름·값에 CR·LF·NUL이 있으면 요청 전체가 거절된다 (`invalid_header`).
        //    헤더 주입은 밖에서 온 값이 그대로 실리는 앱이 부르는 사고다.
        //  - `Host`·`Content-Length`는 넣지 않는다. 전송이 채우는 것이라 우리 것이
        //    이기지 못한다.
        std::vector<http_header> headers {};
        // 보낼 몸이다. 비어 있으면 몸 없는 요청이다.
        //  - Android는 GET·HEAD에 몸을 실을 수 없어 그 요청을 선을 건드리기 전에 거절한다
        //    (`system_error`). 플랫폼이 몸 있는 GET을 몰래 POST로 바꾸기 때문이다.
        std::vector<std::uint8_t> body {};
        // 몸의 Content-Type이다. 몸이 있는데 비어 있으면
        // `application/octet-stream`으로 나간다.
        std::u8string content_type {};

        // 받은 바이트를 값으로 바꿀 규칙이다.
        http_body_parse_options parse {};
        // 받아들일 몸의 상한이다. 압축되지 않은 `Content-Length`가 이보다 크면
        // **읽기 전에** 끊고, 그 밖의 답은 읽는 동안 넘는 순간 끊는다.
        //  - 압축을 풀어 받으면 상한은 **푼 뒤의** 크기에 걸린다. 헤더의 길이는
        //    압축된 크기라 상한과 견줄 값이 아니다 — 그런 답은 미리 끊지 않고
        //    누적으로만 잰다 (압축 폭탄도 그 누적이 막는다).
        std::size_t max_body_bytes { default_http_body_limit };

        // **몸이 있는 요청은 이 값과 무관하게 재지정을 따라가지 않는다.**
        // 전송이 이미 보낸 몸을 재지정된 요청에 다시 실어 주지 않으므로,
        // 307·308을 따라가면 몸 없는 요청이 조용히 대신 나간다 — POST가 200을
        // 받았는데 아무것도 안 만들어지는 종류의 버그다. 그래서 그 자리는 앱에게
        // 돌려준다: 3xx를 답으로 받아 `Location`을 보고 다시 보낸다.
        //  - 따라간 303은 GET으로 묻는다 (HEAD는 그대로). 301·302의 POST도 GET이 된다.
        http_redirect_policy redirects { http_redirect_policy::follow };
        // 따라갈 재지정의 최대 횟수다.
        int max_redirects { 10 };
        // gzip·deflate를 받고 자동으로 푼다 (OS가 지원하지 않으면 조용히 꺼지고
        // 요청은 그대로 성공한다).
        bool decompress { true };

        // 각 구간의 상한이다.
        std::chrono::milliseconds connect_timeout { 10000 };
        std::chrono::milliseconds send_timeout { 30000 };
        // 조각 **사이**의 상한이다.
        std::chrono::milliseconds receive_timeout { 30000 };
        // 요청 하나가 처음부터 끝까지 쓸 수 있는 시간이다. 0이면 걸지 않는다.
        //
        // **왜 따로 있는가.** 위 셋은 각각 "이 구간에서 이만큼 아무 일도 없으면
        // 접는다"이지 "언제까지 끝난다"가 아니다. 1초에 한 바이트씩 흘리는 서버는
        // 어느 구간 상한에도 걸리지 않고 영영 붙잡는다 — 이것이 그 구멍을 막는다.
        // 앱이 화면에 "얼마 안에 답이 온다"를 약속할 수 있는 값은 이것뿐이다.
        //  - 바닥이지 천장이 아니다. client가 앞선 답의 몸을 푸는 중이면 그만큼
        //    늦게 끊는다 (thread가 하나다 — http_client.h의 계약).
        std::chrono::milliseconds total_timeout { 60000 };
    };

    enum class http_error_kind
    {
        // 오류가 없다. **4xx·5xx도 여기다** — 답이 온 것은 사고가 아니다.
        none,
        // URL을 읽지 못했다 (선을 건드리지 않았다).
        invalid_url,
        // http·https가 아니다 (선을 건드리지 않았다).
        unsupported_scheme,
        // 헤더 이름·값이 규칙에 맞지 않는다 (선을 건드리지 않았다).
        invalid_header,
        // 이름을 풀지 못했다.
        name_not_resolved,
        // 연결하지 못했다 (거절·닿지 않음).
        cannot_connect,
        // 답을 받는 중에 연결이 끊겼다 (몸이 잘렸다).
        connection_lost,
        // TLS가 성립하지 않았다 (인증서·프로토콜·https에서 http로 내려가는 재지정).
        // `message`에 자세한 이유가 있다.
        secure_failure,
        // 구간 상한이나 `total_timeout`을 넘겼다.
        timed_out,
        // 앱이 `cancel`했다.
        cancelled,
        // 재지정을 `max_redirects`보다 많이 만났다.
        too_many_redirects,
        // `max_body_bytes`를 넘었다.
        body_too_large,
        // `stop()` 중에 끊겼다.
        stopped,
        // 그 밖의 시스템 오류다. `native_error`가 원본 코드다.
        system_error,
    };

    // 실패는 예외가 아니라 값이다 (`utf_conversion_error`와 같은 성격이다).
    struct http_error
    {
        http_error_kind kind { http_error_kind::none };
        // 사람이 아니라 개발자가 읽을 진단 영문이다. 사람에게 보일 문장은 앱이
        // 짓는다 (`load_image_file`의 오류 글과 같은 규약이다). 끝에 원본 코드를
        // 16진으로 붙인다 (`make_hresult_error`가 이미 선 모양이다).
        std::u8string message {};
        // 시스템이 답한 코드다. 타입을 드러내지 않으려고 `unsigned long`이다
        // (`utf_conversion_error::native_error`가 이미 선 자리다). 0이면 시스템이
        // 말한 것이 없다.
        unsigned long native_error { 0 };

        [[nodiscard]] bool empty() const noexcept
        {
            return kind == http_error_kind::none;
        }
    };

    // 요청 하나에 붙는 표다. 취소와 답 맞추기가 이것으로 된다.
    //
    // **되풀이 요청의 표는 그 흐름 전체를 가리킨다** — 회차마다 바뀌지 않는다.
    // 그래서 앱이 자기 맥박을 표 하나로 알아보고, 멈추는 것도 한 번짜리와 같은
    // `cancel` 하나다. 표가 "이 시도"가 아니라 "이 흐름"을 뜻하는 것이 요점이다.
    // 0은 "받아들여지지 않았다"는 뜻이라 유효한 표는 절대 0이 아니다.
    struct http_ticket
    {
        std::uint64_t value { 0 };

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return value != 0;
        }

        [[nodiscard]] bool operator==(const http_ticket&) const noexcept = default;
    };

    // 되풀이 요청의 쉼이 이보다 짧으면 이 값으로 올린다.
    //
    // 0을 넘기는 것은 "가능한 한 빨리"라는 뜻이 되어 서버를 두드리는 고리가 되고,
    // 그것을 막는 것은 라이브러리의 일이다 (`animation_frame_minimum`이 0 ms짜리
    // gif에 이미 선 그 자리다).
    inline constexpr std::chrono::milliseconds http_minimum_heartbeat_interval { 100 };

    // 되풀이하는 요청의 시간표다.
    struct http_heartbeat
    {
        // **답을 받은 뒤부터** 다음 요청까지 쉬는 시간이다.
        //
        // 고정 박자가 아니라 이렇게 정한 것이 이 타입의 유일한 판단이다. 고정
        // 박자면 서버가 느려진 순간 요청이 겹쳐 쌓이고, 그때부터 겹침 정책(건너뛰기·
        // 앞엣것 취소·줄 세우기)이라는 것을 앱이 골라야 한다. 답을 받고 나서 재는
        // 규칙에서는 **겹칠 수가 없어** 정할 것이 아예 없다 — 대신 실제 주기는
        // `interval + 왕복시간`이다. 죽은 서버를 때리지 않는 것은 덤이다.
        //  - `http_minimum_heartbeat_interval` 아래로는 내려가지 않는다.
        std::chrono::milliseconds interval { 5000 };
        // 참이면 첫 회차를 곧바로 보낸다 (로그인 직후의 첫 상태 조회).
        // 거짓이면 `interval`을 쉬고 시작한다.
        bool immediate { true };
        // 몇 회차까지 돌 것인가. 0이면 `cancel`이나 `stop`까지 끝없이 돈다.
        std::uint64_t max_rounds { 0 };
    };

    // 답 하나다.
    //
    // 받아들인 요청 하나에 정확히 하나가 오고, 되풀이 요청은 회차마다 하나씩 온다.
    // **불변이고 복사가 싸다** — 무거운 것은 `http_body` 안에서 참조 공유다.
    // client의 thread가 지어 앱의 logic thread로 건너가는 값이라 그 성질이 곧
    // 요건이다 (`ui_image`·게시된 tree와 같은 계약이다).
    struct http_response
    {
        http_ticket ticket {};
        // 한 번짜리 요청은 0이다. 되풀이 요청은 1부터 센다 — 첫 회차가 나가기 전에
        // 취소된 되풀이의 마지막 답만 0이다 (회차가 없었다는 뜻이다).
        std::uint64_t sequence { 0 };
        // 이 표로 더 오지 않는다. 한 번짜리는 늘 참이고, 되풀이 요청은 취소·회차
        // 소진·`stop`·영원히 실패할 오류에서 참이다 — 앱이 자기 표를 잊어도 되는
        // 시점을 이 한 줄이 말한다.
        bool last { true };

        // HTTP 상태 코드다. **0이면 상태 줄까지도 오지 못한 것이다**
        // (`error`가 이유를 안다).
        int status_code { 0 };
        // 상태 줄의 이유 글이다 ("Not Found"). 서버가 짓는 값이라 비어 있을 수 있다.
        std::u8string reason {};
        // 재지정을 따라간 뒤의 최종 URL이다. 따라가지 않았으면 요청한 URL이다.
        std::u8string final_url {};
        // 답의 헤더 전부다 (온 차례대로, 이름은 서버가 적은 대소문자 그대로).
        std::vector<http_header> headers {};
        // 헤더에서 가른 Content-Type이다 (몸이 비어도 채워진다).
        // `body.kind`가 `assume_kind`로 뒤집혔어도 여기는 서버가 말한 그대로다.
        http_media_type content_type {};
        http_body body {};
        http_error error {};
        // 요청을 낸 때부터 몸을 다 받은 때까지다 (파싱 시간은 빼고 잰다 —
        // 네트워크를 재는 값이라야 앱이 상한을 정할 수 있다).
        std::chrono::milliseconds elapsed { 0 };

        // 답이 왔고 2xx인가.
        //  - 404를 "성공"이라 부르지 않기 위해 `error.empty()`와 갈라 둔다.
        //    통신이 됐는지만 묻고 싶으면 `error.empty()`를 본다. 요즘 서버는
        //    400·404의 몸에 이유를 싣고, 그것을 전송 실패로 뭉개면 앱이 사람에게
        //    아무 말도 못 한다 — 그래서 몸은 어느 쪽이든 평소대로 풀린다.
        [[nodiscard]] bool ok() const noexcept;

        // 헤더 하나의 값이다 (대소문자를 가리지 않는다). 없으면 빈 view다.
        [[nodiscard]] std::u8string_view header(std::u8string_view name) const noexcept;
    };

    [[nodiscard]] constexpr bool http_status_is_success(const int status_code) noexcept
    {
        return status_code >= 200 && status_code < 300;
    }
} // namespace luil::net
