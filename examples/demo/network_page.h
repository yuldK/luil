#pragma once

// 네트워크 페이지다: 주소 하나를 던지고 온 답을 **갈래대로** 보여 준다.
//
// **오프라인에서 돈다.** 셸이 데모 프로세스 안에 되돌이 HTTP 서버를 세우고 주소
// 칸의 기본값이 그 주소다 — 예제가 인터넷·회사 프록시·남의 API 사정에 흔들리면
// 예제가 아니다. 밖으로 나가는 주소를 쳐도 되고, 잘못된 주소는 빈 표와 이유로
// 답한다 (공개 API의 실패 규약이 화면에서 값을 내는 자리다).
//
// **페이지는 client를 모른다.** 셸(`demo_driver`)이 서버와 client를 들고 페이지는
// 메시지만 주고받는다 — 파일 dialog가 UI thread의 것이라 이미지 페이지가 그것을
// 모르는 것과 같은 자리다. 셸이 `send`에서 받은 표를 `note_sent`로 되돌려 주고,
// 페이지는 그 표로 자기 흐름을 알아본다: `response.ticket == heartbeat_`면 맥박,
// `pending_`이면 본문이다 (http-client-design.md).

#include "demo/common.h"
#include "loopback_http_server.h"
#include "luil/net/http_message.h"
#include "luil/text/text_edit.h"
#include "luil/ui/image_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_tree.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace demo {
    // --- 이 페이지의 메시지 ---
    // 주소 하나를 보내자는 요청이다 (보내기 단추와 미리 놓은 과녁 단추들).
    struct network_send_intent
    {
        std::u8string url {};
        bool post { false };
    };

    // 날아가 있는 것을 전부 접자는 요청이다 (`http_client::cancel_all`).
    struct network_cancel_intent
    {};

    // 심장 박동을 켜거나 끈다. 값은 **바뀐 뒤의 상태**다 — 확인란은 지금 상태의
    // 반대를 실은 액션을 매 frame 새로 싣는다.
    struct network_heartbeat_intent
    {
        bool on { false };
    };

    // 다 풀린 답 하나다.
    // client가 자기 thread에서 지어 셸의 배출구가 logic thread로 나른 값이다
    // (http_client.h의 `deliver` 계약).
    struct network_response_intent
    {
        luil::net::http_response response {};
    };

    // 과녁 단추 띠와 결과 칸의 가로·세로 스크롤이다 (논리 픽셀).
    struct network_strip_scroll_intent
    {
        float delta { 0.0f };
    };

    struct network_preview_scroll_intent
    {
        float delta { 0.0f };
    };

    // 결과 칸을 이 자리로 흘린다 (막대가 나르는 보조 기술의 SetValue — 절대 offset).
    struct network_preview_scroll_to_intent
    {
        float offset { 0.0f };
    };

    // 데모 서버가 내주는 길들이다 (docs/http-client-design.md의 데모 표).
    //
    // 하나의 handler가 경로로 가른다 — 되돌이 서버의 계약이 그렇다.
    //  - 그림 둘(`/image.png`·`/spinner.gif`)은 실행 파일 옆 `assets/`에서 **한 번**
    //    읽어 이 함수가 돌려주는 물건이 든다. 요청마다 디스크를 두드리면 되돌이
    //    서버가 재는 것이 우리 파일 시스템이 된다. 없으면 404이고 데모는 그대로 뜬다.
    //  - `/beat`의 회차도 여기 산다. 서버 thread에서 불리므로 원자적으로 센다.
    [[nodiscard]] std::function<luil::testing::loopback_response(const luil::testing::loopback_request&)> make_network_handler();

    class network_page
    {
    public:
        // 이 페이지의 메시지를 처리했으면 참이다.
        bool handle(const luil::app_message& message);

        // 페이지 내용이다. width·height는 내용 영역의 논리 픽셀 크기다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 되돌이 서버가 선 뿌리 주소다 ("http://127.0.0.1:<포트>").
        // 셸이 서버를 세운 뒤 한 번 알려 준다. 비어 있으면 서버가 서지 못한 것이라
        // 과녁 단추들이 잠긴다 — 눌러도 아무 일이 없는 단추를 두지 않는다.
        void set_base_url(std::u8string url);
        // 심장 박동이 두드릴 주소다 (뿌리 + "/beat"). 셸이 요청을 지을 때 쓴다.
        [[nodiscard]] std::u8string heartbeat_url() const;

        // 지금 선을 타고 있는 요청 수다 (`http_client::in_flight`).
        // 취소 단추가 잠기는 자리라 frame마다 셸이 물어 넣는다 — 페이지는 client를
        // 모르므로 이 값도 밖에서 온다.
        void set_in_flight(std::size_t count) noexcept;

        // 셸이 `send`가 답한 표를 되돌려 준다.
        //
        // 빈 표면 **손잡이가 서기 전에** 거절된 것이라 답이 오지 않는다 — 이유가
        // 그대로 결과 줄에 뜬다 (http_client.h가 가른 두 실패 자리 중 앞엣것이다).
        void note_sent(const network_send_intent& request, luil::net::http_ticket ticket, const std::u8string& error);
        // 심장 박동을 켠 결과다. 빈 표면 켜지지 못한 것이라 확인란도 꺼진 채 둔다.
        void note_heartbeat(luil::net::http_ticket ticket, const std::u8string& error);
        // 심장 박동을 껐다 (`cancel`은 셸이 이미 불렀다).
        void note_heartbeat_stopped() noexcept;

        // 과녁 단추 띠를 휠로 흘린다.
        //
        // **결과 칸은 여기 없다.** 그쪽은 `scroll_area_element`라 자기 메시지를
        // 스스로 들고 있어(`scroll_source`) 셸의 표 없는 `route_wheel`이 찾는다.
        // 가로로 흘리는 띠는 그 조립이 아니라 표에 남는다 (scroll-area-design.md의
        // "원시 도구를 그대로 쓰는 자리").
        [[nodiscard]] static std::vector<luil::input_action> route_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, float delta);

    private:
        void apply_edit(const luil::text_edit_request& request);
        void apply_composition(const luil::text_composition_event& event);
        // 주소 칸의 글을 통째로 갈아 끼운다 (모두 고르고 넣기 — 실행 취소가 성립한다).
        void replace_url(const std::u8string& url);
        // 답 하나를 표로 갈라 받는다 (맥박이냐 본문이냐).
        //  - **미리 보기 줄은 여기서 한 번 짓는다.** `dump(2)`를 frame마다 다시
        //    부르면 큰 답 하나가 화면을 멈춘다 (그림을 한 번만 읽는 것과 같은 규칙이다).
        void take_response(const luil::net::http_response& response);

        [[nodiscard]] std::u8string url_text() const;
        [[nodiscard]] std::u8string header_text() const;
        [[nodiscard]] std::u8string beat_text() const;
        // `parse_error`·`error.message`가 있으면 그 한 줄이다 (없으면 빈 글).
        [[nodiscard]] std::u8string note_text() const;

        [[nodiscard]] std::unique_ptr<luil::ui_element> make_address_row() const;
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_preset_strip() const;
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_control_row() const;
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_preview() const;
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_lines_view() const;

        std::u8string base_url_ {};
        luil::text::text_edit_state url_ {};
        std::optional<luil::text_composition_event> composition_ {};

        // 본문 흐름이다. 표가 유효한 동안이 "오는 중"이다.
        luil::net::http_ticket pending_ {};
        // 마지막으로 온 본문 답이다 (맥박은 여기 들어오지 않는다).
        luil::net::http_response last_ {};
        bool has_last_ { false };
        // 그 답을 부른 동사와 길이다 ("GET /status").
        std::u8string request_line_ {};
        // `send`가 손잡이도 세우지 못하고 거절한 이유다.
        std::u8string send_error_ {};
        // 결과 칸에 세울 글줄이다 (json의 `dump(2)`·글 몸통·바이트 요약).
        std::vector<std::u8string> preview_lines_ {};
        // 움직이는 그림이 그대로 도는 자리다 (`image_element`의 재생 시계).
        luil::image_playback playback_ {};

        // 맥박 흐름이다. 표는 되풀이 **전체**의 이름이라 회차마다 바뀌지 않는다.
        luil::net::http_ticket heartbeat_ {};
        bool heartbeat_on_ { false };
        std::u8string heartbeat_error_ {};
        std::uint64_t beat_round_ { 0 };
        std::chrono::milliseconds beat_elapsed_ { 0 };
        int beat_status_ { 0 };

        std::size_t in_flight_ { 0 };
        float strip_scroll_ { 0.0f };
        float preview_scroll_ { 0.0f };
    };
} // namespace demo
