#pragma once

#include "luil/ui/app_message.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace luil {
    // 웹뷰가 앱에 알리는 일이다.
    //
    // **전부 사후 통지다.** 웹뷰의 결정(이 주소로 가도 되는가, 새 창을 열어도 되는가)은
    // 라이브러리가 `webview_policy`를 보고 **그 자리에서** 내린다 — 그 판단은 UI thread의
    // 콜백 안에서 동기로 답해야 하는데, 앱 메시지는 logic thread를 한 바퀴 도는 값이라
    // 제때 돌아오지 못한다. 앱은 정책을 미리 적고 결과를 여기서 안다.
    enum class webview_event_kind
    {
        // 그 주소로 가기 시작했다.
        navigation_started,
        // 그 주소를 다 읽었다.
        navigation_completed,
        // 그 주소를 읽지 못했다. `error`에 까닭이 있다.
        navigation_failed,
        // 정책이 막았다. `url`이 막힌 주소이고 `error`에 어느 규칙인지가 있다.
        //  - 새 창 요청(`window.open`·`target=_blank`)이 막힌 것도 이것이다.
        //  - 페이지가 스스로 가려던 주소(링크·리다이렉트)가 스킴에 걸린 것도 이것이다.
        navigation_blocked,
        // 페이지가 권한(위치·카메라·마이크·알림·클립보드 읽기 …)을 청했고 **거절했다.**
        // `url`이 청한 문서이고 `error`에 어느 권한인지가 있다.
        //  - 언제나 거절한다. 여는 스위치는 일부러 없다 — 소비자가 없고, 브라우저의
        //    기본 대화상자가 합성 호스팅에서 어디에 뜨는지는 문서가 말하지 않는다.
        //    거절은 프로필에 남기지 않아 정책이 생기는 날 다시 물을 수 있다.
        permission_denied,
        // 페이지가 파일을 내려받으려 했고 **막았다.** `url`이 그 파일의 주소다.
        //  - 언제나 막는다. 내려받기는 디스크에 쓰는 일이라 신뢰 경계를 여는
        //    스위치다. 저장 대화상자도 뜨지 않는다.
        download_blocked,
        // 페이지가 `window.chrome.webview.postMessage`로 보냈다.
        // `message`가 그 JSON이고(문자열을 보냈으면 따옴표가 있는 JSON 문자열이다)
        // `url`이 보낸 문서다 — **origin은 앱이 여기서 가린다.** 이 웹뷰가 간 어느
        // 문서든 보낼 수 있다.
        web_message_received,
        // 페이지가 일으킨 사건이 상한을 넘겨 **버려진 것이 있다.** `error`에 처음 버린
        // 것이 무엇인지가 있다 (`web message: size` · `web message: rate` ·
        // `permission request: rate` 같은 꼴).
        //  - 버린 사실을 값으로 알린다. 조용히 버리면 앱은 자기 화면이 왜 낡았는지
        //    알 길이 없다 (`app_inbox_statistics`와 같은 자리다).
        //  - **이 알림도 상한 안에 있다** — 1초에 한 번이다. 버릴 때마다 알리면 페이지가
        //    쏟는 만큼 알림이 inbox로 들어가, 상한이 막으려던 일이 알림으로 일어난다.
        events_dropped,
        // 웹 콘텐츠를 그리던 프로세스가 죽었다. 라이브러리가 다시 읽는다.
        //  - 10초 안에 다시 죽으면 다시 읽지 않는다 — 열자마자 렌더러를 죽이는
        //    페이지가 죽음→다시 읽기의 순환을 돌리지 못하게 한다. `error`가 말한다.
        //  - iframe만의 렌더러가 죽으면 알리기만 한다. 주 문서는 온전하고 다시 읽으면
        //    그 상태를 잃는다.
        //  - GPU·utility 같은 보조 프로세스의 죽음은 런타임이 스스로 되살리므로
        //    알리지 않는다.
        render_process_failed,
        // 브라우저 프로세스가 죽었다. **그 환경의 웹뷰가 전부 사라진다** —
        // 앱이 목록에서 뺐다가 다시 실어야 새로 선다. 그동안 자리표의 placeholder가
        // 그 자리를 지킨다.
        browser_process_failed,
    };

    // 사건 하나다.
    struct webview_event
    {
        // 어느 웹뷰인가 (`ui_webview::id`).
        std::u8string id {};
        webview_event_kind kind { webview_event_kind::navigation_started };
        // 그 사건의 주소다 — navigation 계열은 가려던 주소, `permission_denied`는 청한
        // 문서, `download_blocked`는 그 파일, web message는 보낸 문서다. 프로세스
        // 실패에는 없다.
        std::u8string url {};
        // 페이지가 보낸 JSON이다 (`web_message_received`). 그 밖에는 비어 있다.
        std::u8string message {};
        // 실패·차단의 까닭이다. 성공한 사건에서는 비어 있다.
        std::u8string error {};
    };

    // 앱이 정하는 웹뷰 정책이다.
    //
    // **여기 있는 것은 신뢰 경계를 새로 열지 않는 값뿐이다.** 인증서 오류를 무시하는
    // 스위치, 호스트 객체를 다시 여는 스위치, SmartScreen을 끄는 스위치, 상한을
    // 무제한으로 만드는 스위치는 **일부러 없다** — 공개 API에 그 스위치가 있으면 반드시
    // 켠 채로 출시하는 앱이 생긴다 (http-client-design.md가 인증서 검증에서 세운
    // 결정이고, 웹뷰는 그 표면이 훨씬 넓다).
    //
    // **매 frame 살아 있다.** 바꾸어 실으면 사건을 판정하는 갈래(스킴·새 창·상한)는
    // 곧바로 듣고, 브라우저 설정 갈래(스크립트·개발자 도구·메뉴·대화상자·단축키·
    // 확대·스와이프)는 WebView2의 규칙대로 **다음 항해부터** 든다.
    //
    // 정책을 집행하는 인터페이스가 없는 오래된 런타임(Chromium 120, SDK 1.0.2210
    // 이전)에서는 웹뷰를 세우지 않는다 — 조용히 열린 채로 도는 것보다 placeholder가 낫다.
    struct webview_policy
    {
        // 페이지의 스크립트를 실행한다.
        bool script_enabled { true };
        // F12·개발자 도구를 연다.
        bool developer_tools_enabled { false };
        // 오른쪽 클릭에 브라우저 기본 메뉴를 낸다.
        // 꺼 두는 것이 기본이다 — 그 메뉴는 우리 tree 밖의 창이라 테마도 초점도 우리 것이 아니다.
        bool default_context_menus_enabled { false };
        // `alert`·`confirm`·`prompt`를 브라우저가 낸다.
        bool default_script_dialogs_enabled { true };
        // F5·Ctrl+R·Ctrl+F·Ctrl+P 같은 브라우저 단축키가 듣는다.
        // **꺼 두는 것이 기본이다.** 켜 두면 웹뷰에 초점이 있는 동안 F12가 개발자 도구
        // 창을, Ctrl+F가 찾기 막대를 띄워 **우리 창에서 포그라운드를 가져간다**
        // (webview-composition-design.md).
        bool browser_accelerator_keys_enabled { false };
        bool pinch_zoom_enabled { false };
        bool swipe_navigation_enabled { false };

        // 열어도 되는 스킴이다 (소문자, 콜론 없이 — `https` 형태).
        // **비어 있으면 `https`만 연다.**
        //  - 앱이 여는 주소, **주 문서**가 스스로 가는 주소(링크·리다이렉트), 새 창
        //    요청이 이 목록을 지난다. 걸리면 막고 `navigation_blocked`로 알린다.
        //    https에서 http로 되돌리는 리다이렉트도 기본값에서는 막힌다 —
        //    `http_message.h`가 그 손잡이를 두지 않은 것과 같은 자리다.
        //  - iframe 안의 항해는 이 그물 밖이다. 그것을 막는 정책은 소비자가 생기면 짓는다.
        //  - `javascript`는 넣어도 듣지 않는다 — 여는 것이 아니라 실행하는 것이다. 그것은
        //    실제 네비게이션이 아니라 거르는 이벤트가 아예 나지 않으므로 여는 호출부에서
        //    먼저 거른다.
        std::vector<std::u8string> allowed_schemes {};

        // 새 창 요청(`window.open`·`target=_blank`)을 이 웹뷰에서 연다 — 이 페이지가
        // 그 주소로 간다. 어느 쪽이든 **두 번째 브라우저 창은 뜨지 않는다.**
        // 거짓이면 막고 `navigation_blocked`로 알린다. 참이어도 스킴 검사는 그대로이고,
        // **주 문서가 사용자 손짓으로 낸 요청만** 연다 — iframe이 낸 요청을 주 문서의
        // 항해로 바꾸면 남의 origin의 iframe에게 호스트 문서를 갈아 끼울 힘을 주는
        // 것이고, 손짓 없는 `window.open`은 브라우저의 팝업 차단이 막던 것이다.
        bool open_new_windows_here { false };

        // 페이지가 보내는 web message 하나의 상한이다 (JSON의 UTF-8 크기).
        // 넘기면 버리고 `events_dropped`로 알린다.
        //  - **끌 수 없다.** WebView2는 크기 상한도 초당 건수 상한도 주지 않으므로
        //    (문서 조사 결과이고 폭주 실험은 하지 않았다) 우리가 걸지 않으면 아무도
        //    걸지 않는다. 적대적 페이지가 쏟으면 app inbox가 포화해 **앱의 정상
        //    메시지가 조용히 사라진다** (`app_host::post_app_message`는 reject_newest다).
        //    `http_message.h`의 몸 상한과 정확히 같은 자리다.
        //  - 앱이 페이지로 보내는 것(`post_message`)에는 상한이 없다 — 앱은 믿는다.
        std::size_t maximum_message_bytes { 256 * 1024 };
        // 페이지가 일으키는 사건 **전부**(web message·항해·권한 요청·내려받기)의 초당
        // 건수 상한이다. 넘긴 것은 버리고 `events_dropped`로 알린다.
        //  - web message만 세면 페이지는 권한 요청이나 `about:blank` 항해를 같은 속도로
        //    쏟아 같은 일을 한다 — inbox로 가는 길은 하나라 상한도 하나다.
        //  - 창은 첫 사건부터 1초다 (고정 눈금이 아니다). 눈금 경계에 몰아 보내 두 배를
        //    넘기는 길은 막지 않는다 — 목적은 유량 제어가 아니라 inbox를 살리는 것이다.
        //  - 수치는 보수적으로 잡았고 **측정한 값이 아니다**.
        std::uint32_t maximum_events_per_second { 120 };

        [[nodiscard]] bool operator==(const webview_policy&) const noexcept = default;
    };

    // 창 하나에 얹히는 웹뷰 하나다.
    //
    // 열림·닫힘은 popup·보조 창처럼 앱 상태다: frame에 실으면 서고 빼면 사라진다.
    // **자리는 여기 없다** — tree의 `webview_element`가 정한다. 앱은 `arrange` 결과를
    // 미리 모르고, 흘리는 창 안의 잘림은 tree만 아는 답이기 때문이다
    // (webview-composition-design.md).
    struct ui_webview
    {
        // 같은 frame의 웹뷰를 구분하는 앱 정의 키다.
        // tree의 자리표가 같은 값을 `owner`로 갖는다.
        std::u8string id {};
        // 이 웹뷰가 앉는 표면이다 (`ui_window::id`와 같은 이름 공간).
        // 비어 있으면 주 창이다.
        //  - **바꾸면 다시 만들어진다.** 웹뷰는 그 표면의 합성 visual과 창에 함께 매여
        //    있어 옮기지 못한다. 페이지 상태가 날아간다.
        std::u8string anchor {};

        // 이 웹뷰의 브라우저 프로필(쿠키·저장소·캐시)이 사는 자리다.
        // **비어 있으면 웹뷰를 만들지 않는다.**
        //  - 기본값으로 물러서지 않는 이유: WebView2의 기본은 실행 파일 옆이고, 설치
        //    프로그램이 보호된 자리에 놓은 앱에서는 **반드시 실패한다**. 조용히 실패할
        //    자리를 기본값으로 둘 이유가 없다.
        //  - 신뢰 경계가 다른 두 웹뷰는 다른 자리를 써야 한다. 같은 자리를 나눠 쓰면
        //    쿠키와 저장소가 함께 산다.
        std::u8string user_data_folder {};

        webview_policy policy {};

        // 열 주소다. **revision이 바뀐 frame에만** 연다.
        //  - frame은 매번 다시 게시되므로 주소만 실으면 매 frame 다시 읽는다.
        //    `ui_frame::window_placement_revision`이 세운 규칙 그대로다.
        //  - 0이면 아무 데도 가지 않는다 (빈 웹뷰).
        std::uint64_t navigate_revision { 0 };
        std::u8string navigate_url {};

        // 페이지로 보낼 JSON이다. **revision이 바뀐 frame에만** 보낸다.
        //  - 다리는 이것 하나다. 호스트 객체(`AddHostObjectToScript`)는 열지 않는다 —
        //    "요청 하나에 답 하나"가 `http_client`가 이미 쓰는 어휘이고, 일반 proxy를
        //    여는 것은 그 어휘를 버리고 공격 표면을 넓히는 일이다.
        std::uint64_t post_revision { 0 };
        std::u8string post_message {};

        // 웹뷰가 낸 일을 앱 메시지로 옮긴다.
        // 비어 있으면 알리지 않는다.
        //  - **logic thread로 간다** (`app_host::post_app_message`). 웹뷰 사건은 hit
        //    test 대상이 아니라 입력이 아니고, `http_client_config::deliver`가 답을
        //    넘기는 자리와 같다.
        std::function<app_message(webview_event)> on_event {};
    };
} // namespace luil
