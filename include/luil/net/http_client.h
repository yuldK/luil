#pragma once

#include "luil/net/http_message.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace luil::net {
    struct http_client_config
    {
        // 다 풀린 답을 앱에게 건네는 **유일한** 자리다.
        //
        // **client가 소유한 thread에서 불린다** — logic thread도 UI thread도 아니다.
        // 앱이 여기서 할 일은 값을 자기 thread로 나르는 것 하나이고, 이 저장소에서
        // 그 길은 `app_host::post_app_message`다 (thread-safe, 막히지 않는다):
        //
        //     config.deliver = [&host](luil::net::http_response response) {
        //         host.post_app_message(luil::app_message { fetch_done { std::move(response) } });
        //     };
        //
        //  - **요청마다 callback을 받지 않는 이유가 여기 있다.** 그러면 앱 상태를
        //    이 thread에서 만지는 코드가 자연스러워지고, 이 라이브러리에서 스레드를
        //    건너는 길은 메시지 하나여야 한다 (docs/concepts/threading-model.md).
        //  - 짧아야 한다. 여기서 오래 머물면 다음 답의 파싱과 되풀이 요청의 다음
        //    회차가 그만큼 늦는다 (thread가 하나다).
        //  - 여기서 `send`·`cancel`·`start_heartbeat`을 불러도 된다 (이어지는
        //    요청을 거는 자연스러운 자리다). **`stop`만은 부르면 안 된다** —
        //    자기 자신을 join하게 된다. debug 빌드가 assert로 잡는다.
        //  - 던지면 삼키고 그 답만 버린다 (thread 진입 함수를 벗어난 예외는 곧
        //    `terminate`다). 계약은 던지지 않는 것이다.
        //  - 비어 있으면 답이 조용히 버려진다 (test의 자리다).
        std::function<void(http_response)> deliver {};

        // User-Agent 헤더다.
        std::u8string user_agent { u8"luil" };
        // 시스템 proxy 설정을 따른다. 거짓이면 곧바로 나간다 —
        // 되돌이 주소를 쓰는 test와 데모의 자리다 (CI 기계의 proxy가 127.0.0.1을
        // 가로채면 test가 소음이 된다).
        bool use_system_proxy { true };

        // 동시에 선을 타고 있을 수 있는 요청 수다.
        //
        // **상한을 넘긴 `send`는 큐에 쌓지 않고 그 자리에서 거절한다.** 숨은 큐는
        // 문제를 메모리로 미루는 것이고 (messaging.md가 무한 큐에 내린 판정),
        // 무엇보다 앱이 "지금 밀렸다"를 봐야 물러설 수 있다.
        std::size_t max_in_flight { 32 };

        // UTF-8이 아닌 charset을 옮기는 갈고리다.
        // 비어 있으면 `codepage_text_transcoder()`를 쓴다 — 앱은 아무것도 하지
        // 않아도 euc-kr이 읽힌다.
        http_text_transcoder transcode {};

        // `stop()`이 남은 손잡이가 닫히기를 기다릴 상한이다.
        //
        // 기본값이 1초인 것은 종료 예산의 산술 때문이다: `app_host::shutdown`이
        // 1·2단계에 3초, join 앞 확인에 1초를 쓰고 그 사이 3단계에서 이것이
        // 불린다 — 셋을 합쳐 세션 종료 유예(기본 5초) 안이다. 실제로는
        // `logic_driver::cancel()`에서 `cancel_all()`을 먼저 부르면 수 ms다.
        std::chrono::milliseconds stop_budget { 1000 };
        // 아직 마지막 응답을 배달하지 않은 ticket의 상한이다. 대기 중인 heartbeat도
        // 포함한다. 마지막 배달 callback이 반환하면 자리가 난다.
        std::size_t max_pending_responses { 128 };
    };

    // WinHTTP 위에 선 비동기 HTTP client다.
    //
    // **thread를 하나 소유한다.** threading-model.md절은 일반 worker pool을 앱
    // 정책이라 했고 그 판단은 그대로다 — 이것은 pool이 아니라 **OS의 HTTP API를
    // 옳게 쓰는 데 필요한 부품**이다. 완료 callback은 OS의 thread pool 위에서
    // 불리고 "짧아야 하며 막히면 안 된다"가 그 API의 계약인데, 우리가 몸에 해야
    // 하는 일(JSON 수십 ms, 움직이는 그림 초 단위)이 정확히 그 계약을 어긴다.
    // 남는 자리는 둘뿐이었다.
    //  - logic thread에서 푼다 → 서버가 정하는 크기의 일이 frame 고리 안으로
    //    들어온다. 큰 답 하나가 화면을 멈춘다.
    //  - client가 thread 하나를 든다 → 그 일이 앱의 어느 thread에도 없다.
    // 뒤엣것을 골랐고, 그래서 이 thread는 **하나뿐이고 client의 것이며 밖에서
    // 일감을 받지 않는다.** 하는 일은 셋이다: 몸을 풀고, `deliver`를 부르고,
    // 시계를 돈다(전체 마감과 되풀이 회차).
    //
    // **종료는 앱의 종료 순서에 낀다.** `logic_driver::stop_workers()`가 `stop()`을
    // 부르는 자리이고(threading-model.md의 3단계), `logic_driver::cancel()`에서
    // `cancel_all()`을 먼저 불러 두면 그 3단계가 짧아진다.
    //
    // 모든 메서드는 어느 thread에서든 부를 수 있다. `stop`만이 막힌다 (기다리는
    // 것이 그 함수의 일이다).
    class http_client
    {
    public:
        http_client(const http_client&) = delete;
        http_client(http_client&&) = delete;
        http_client& operator=(const http_client&) = delete;
        http_client& operator=(http_client&&) = delete;
        // `stop()`을 부른다 (멱등이다).
        ~http_client();

        // client 하나를 세운다. 실패하면 null이고 `error`에 이유를 적는다
        // (`create_renderer`와 같은 규약이다 — 생성자가 값으로 실패할 수 없어서
        // 문을 함수로 낸다).
        //  - 여기서 세션이 서고 thread가 돈다. 앱 하나에 하나면 넉넉하다 —
        //    연결은 host별로 안에서 다시 쓰인다.
        [[nodiscard]] static std::unique_ptr<http_client> create(http_client_config configuration, std::u8string& error);

        // 한 번짜리 요청이다. 답은 `deliver`로 온다.
        //
        // 실패는 **두 자리에서만** 나온다.
        //  - **여기**: 손잡이가 서기 전의 실패다 (URL·scheme·헤더가 틀렸다, 동시
        //    상한을 넘었다, 이미 멈췄다). 빈 표를 돌려주고 `error`에 적는다.
        //    답은 오지 않는다.
        //  - **답**: 손잡이가 선 뒤의 실패다 (연결·시한·몸 상한·취소·`stop`).
        //    `http_response::error`로 온다.
        // 가르는 선은 "손잡이가 섰는가" 하나다. 선 뒤로는 무슨 일이 있어도 답이
        // 정확히 한 번 나가므로, 앱의 상태 기계는 "표를 받았으면 답을 기다린다"
        // 한 줄이면 된다 (성공하면 `error`는 건드리지 않는다 — `load_image_file`과
        // 같은 규약이다).
        [[nodiscard]] http_ticket send(http_request request, std::u8string& error);

        // 되풀이하는 요청이다 (heartbeat·주기 조회). 회차마다 답이 하나씩 온다
        // (`sequence`가 1부터, 마지막 회차가 `last == true`).
        //
        // 한 번짜리와 다른 것은 동사와 시간표뿐이고 `http_request`는 같은 값이다 —
        // 요청을 짓는 코드가 두 벌이 되지 않는다. 표도 같은 `http_ticket`이라
        // 멈추는 것은 같은 `cancel` 하나다.
        //  - **전송 오류로 멈추지 않는다.** 네트워크가 돌아오는 것을 보는 것이
        //    되풀이 요청이 있는 이유다. 4xx·5xx도 마찬가지다 (답이 온 것은 사고가
        //    아니다). 멈추는 것은 `cancel`·`max_rounds`·`stop`과 영원히 실패할 것
        //    (`invalid_url`·`unsupported_scheme`·`invalid_header`)뿐이다.
        //  - 몇 번 틀리면 그만둘지는 앱 정책이라 손잡이를 두지 않는다. 앱은
        //    `sequence`와 `error`를 세어 `cancel`한 뒤 더 긴 `interval`로 다시
        //    부르면 된다.
        [[nodiscard]] http_ticket start_heartbeat(http_request request, http_heartbeat schedule, std::u8string& error);

        // 요청 하나(또는 되풀이 하나)를 멈춘다. 모르는 표는 아무 일도 하지 않는다.
        //
        // **답은 그래도 온다** — `cancelled`를 실은 답 하나가 여느 때처럼 간다.
        // 취소가 답을 지우면 앱은 "아직 오는 중"과 "접었다"를 상태로 갈라 들고
        // 있어야 하고, 그 상태가 어긋나는 순간 화면에 영원히 도는 표시가 남는다.
        // 이미 끝난 요청에는 아무 일도 일어나지 않는다 (답은 이미 갔다).
        void cancel(http_ticket ticket) noexcept;
        // 날아가 있는 것 전부를 접고 되풀이표를 비운다.
        // `logic_driver::cancel()`(종료 0단계)에서 부르면 `stop()`의 기다림이 짧아진다.
        void cancel_all() noexcept;

        // 아직 답이 나가지 않은 요청 수다 (되풀이는 날아가 있는 회차만 센다).
        // 화면의 "보내는 중" 표시와 test의 기다림이 쓴다.
        [[nodiscard]] std::size_t in_flight() const noexcept;

        // 멈춘다. 멱등이고 어느 thread에서든 부를 수 있다.
        //
        // 하는 일의 차례가 계약이다.
        //  1. 새 `send`·`start_heartbeat`을 거절한다 (`error`에 "client stopped").
        //  2. 되풀이표를 비우고 thread를 멈춘다 — 이 뒤로 `deliver`도 새 회차도 없다.
        //  3. 날아가 있는 요청을 모두 접고, 시스템이 그 손잡이들을 **다 놓을
        //     때까지** `stop_budget` 안에서 기다린다.
        //  4. 세션을 닫는다.
        // 3번이 이 함수가 막히는 이유이고 건너뛸 수 없는 이유다 — callback이 아직
        // 우리 문맥을 보고 있는데 그것을 지우면 그 자리가 곧 use-after-free다.
        // 둘이 함께 부르면 뒤엣것은 앞엣것이 끝날 때까지 기다린다 (그래야 아래 문장이
        // 둘 모두에게 참이다).
        //
        // **돌아온 뒤에는 `deliver`가 다시 불리지 않는다.** 그것이 이 함수가
        // 소멸자와 따로 있는 이유다: 앱은 이 줄 뒤에서 배출구가 붙잡고 있던 것들을
        // 마음 놓고 부순다.
        //  - 아직 넘기지 않은 완료는 버린다. 닫히는 중인 앱에게 초 단위 디코딩을
        //    시켜 종료 예산을 태울 이유가 없다.
        //  - `stop_budget`을 넘겨도 돌아온다. 그때는 **문맥과 세션을 일부러
        //    남긴다** — 살아 있을지 모르는 callback이 볼 메모리를 푸는 것보다 새는
        //    편이 낫고, 이 시점의 프로세스는 어차피 끝나는 중이다.
        void stop() noexcept;

    private:
        struct engine;
        explicit http_client(std::unique_ptr<engine> owned) noexcept;

        std::unique_ptr<engine> engine_ {};
    };
} // namespace luil::net
