# 둘째 인스턴스의 인자 넘기기

같은 이름의 두 번째 실행이 첫 실행에 명령 줄 인자와 작업 폴더를 넘기는 길이다. 둘째는 첫째의 주 창에 `WM_COPYDATA`로 보내고, 첫째는 파일 드롭처럼 `window_delegate` 콜백으로 앱에 건넨다. 첫 창을 앞으로 세울 권한도 둘째가 미리 넘긴다. Win32 전용이고 Android는 `unsupported`다. 공개 형식은 [platform_services.h](../include/luil/app/platform_services.h)의 `instance_arguments`·`instance_send_options`·`single_instance::send_to_primary`와 [win32_window.h](../include/luil/win32/win32_window.h)의 `window_delegate::on_instance_arguments`·`window_environment::instance`다.

이 길은 [플랫폼 도구](platform-services-design.md)가 "앱마다 달라 넣지 않았다"고 한 판단을 뒤집는다(G7 확정, 2026-11-02). 구현 커밋이 그 문서를 같이 고친다(플랫폼 절).

## 문제

- 둘째가 첫째에 닿을 길이 없다. `single_instance`는 이름 있는 뮤텍스 `Local\luil-instance:<이름>`으로 첫째 여부만 가른다([platform_services_win32.cpp](../src/win32/platform_services_win32.cpp)의 생성자는 `ERROR_ALREADY_EXISTS`만 본다). 데이터를 나르는 길이 없다.
- 앱이 직접 짤 수도 없다. 주 창의 `HWND`와 프로시저는 공개 헤더에 없다. 앱은 파이프나 숨은 창을 따로 세우고 첫 창을 찾는 법, 크기 상한, 관리자 권한 창의 UIPI, 앞으로 세우기 권한을 앱마다 다시 푼다.
- 쓰임은 모두 "새 프로세스 + 인자"다. 탐색기에서 연결된 파일을 여는 것과 점프 목록의 작업이 그렇다. 알림 클릭은 os_notify의 COM 활성화기가 받아 이 길을 쓰지 않는다. 지금 데모는 둘째도 막지 않고 제목만 바꿔 띄운다([demo_main.cpp](../examples/demo_main.cpp)의 `single_instance`). [작업 표시줄 문서](taskbar-design.md) 217줄은 점프 목록을 이 길이 선 뒤로 미뤘다.
- 첫째는 앞 창이 아니어서 스스로 앞으로 서지 못한다. `focus_request::activate`는 앞 창이 아니면 OS가 막고 단추만 깜빡인다([app_host.h](../include/luil/app/app_host.h) 326~329줄). 둘째는 사용자의 동작으로 떴으므로 그 권한을 첫째에 넘겨 줄 수 있다.

## 공개 형식

`platform_services.h`는 `<chrono>`만 더한다. `include/luil/app`은 core 검사 범위라([verify_core_portability.cmake](../cmake/verify_core_portability.cmake)) OS 헤더가 없다. 새 공개 헤더는 없어 `luil.h`를 고치지 않는다.

```cpp
namespace luil {
    // 둘째가 첫째에 건넨 값이다. 라이브러리는 해석하지 않는다 (경로를 풀지도, 열지도 않는다).
    struct instance_arguments
    {
        // 보낸 쪽이 준 목록 그대로다. 실행 파일 경로(argv[0])는 앱이 넣어야 들어간다.
        std::vector<std::u8string> arguments {};
        // 보낸 프로세스의 작업 폴더다. 상대 경로를 풀 때 쓴다. 보내는 쪽이 늘 채운다.
        std::u8string working_directory {};
    };

    struct instance_send_options
    {
        // 첫 창이 설 때까지의 기다림과 답을 기다림을 합친 시한이다.
        std::chrono::milliseconds timeout { 3000 };
        // 첫째의 보이는 주 창을 앞으로 세운다 (규칙).
        bool activate { true };
    };

    class single_instance
    {
        // ... 기존 멤버
        [[nodiscard]] std::u8string_view name() const noexcept;
        // 첫째에 인자를 보내고 앱이 받을 때까지 기다린다. 둘째(`primary() == false`)가 어느 thread에서나 부른다.
        [[nodiscard]] platform_result send_to_primary(std::span<const std::u8string> arguments, const instance_send_options& options = {}) const;
    };
}

namespace luil::win32 {
    class window_delegate
    {
        // 같은 이름의 둘째가 보낸 인자다. UI thread의 주 창 프로시저 안에서 불린다. true면 앱이 받았다는 뜻이다.
        [[nodiscard]] virtual bool on_instance_arguments(app_host& host, const instance_arguments& arguments)
        {
            return false;
        }
    };

    struct window_environment
    {
        // ... driver, policy, delegate
        // 이 프로세스의 단일 인스턴스다. 첫째이면 주 창이 인자를 받는다. 창보다 오래 살아야 한다.
        const single_instance* instance { nullptr };
        // 관리자 권한으로 도는 첫째가 낮은 권한 프로세스의 인자도 받는다 (결정 6).
        bool instance_from_lower_integrity { false };
    };
}
```

앱의 모양은 이렇다. 둘째는 인자를 보내고 끝내고, 첫째는 `environment.instance = &instance`로 받을 준비를 한다.

```cpp
const luil::single_instance instance { u8"my-app" };
if (instance.primary() == false)
    return instance.send_to_primary(std::span { arguments->data() + 1, arguments->size() - 1 }).ok() ? 0 : 3;
```

`win32_window.h`는 `platform_services.h`를 include한다. 비공개 파일은 `src/win32/instance_message.h`·`.cpp`다. 운반 형식의 짓기(`encode_instance_message`)와 읽기(`decode_instance_message`), 창 속성 이름(`instance_property_name`), 상한 상수가 든다. OS 호출이 없는 순수 변환이라 보내는 쪽과 받는 쪽이 같은 규칙을 쓰고 한 test로 본다. UIPI 열기 `allow_instance_message(HWND)`는 [window_messages.h](../src/win32/window_messages.h)의 `allow_shell_events` 옆에 둔다.

## 규칙

**보내기.** 아래 순서다.

1. 입력을 본다. 인자가 잘못된 UTF-8이거나 NUL을 품었거나, 항목이 4096개를 넘거나, 실을 크기가 256 KiB를 넘거나, 이름이 UTF-16 241 코드 단위를 넘으면 `failed`다. 아무것도 보내지 않는다. 객체가 서지 못했거나(빈 이름, 뮤텍스 오류: `status()`가 `ok`가 아니다) 옮겨 간 뒤의 빈 객체여도 `failed`다. 이름을 거르는 것은 첫째 여부를 보기 전이라 첫째 객체도 같은 답을 받는다.
2. `primary()`이면 `not_found`다. 보낼 첫째가 없다.
3. 시한 안에서 50 ms마다 첫 창을 찾는다. `EnumWindows`로 최상위 창을 훑어 `GetPropW(창, L"luil-instance:<이름>")`가 있는 첫 창이다. 숨은 창도 훑는다. 시한이 지나도 없으면 `not_found`다.
4. `activate`면 그 창의 프로세스에 `AllowSetForegroundWindow`를 건다. 둘째가 사용자 동작으로 뜬 것이 아니면 OS가 거절한다. 그래도 보내고 결과에 싣지 않는다.
5. `SendMessageTimeoutW(WM_COPYDATA, SMTO_ABORTIFHUNG | SMTO_BLOCK)`로 보내 답을 받는다. 부른 thread는 그동안 다른 메시지를 풀지 않는다. 그 사이 창이 사라졌으면(`ERROR_INVALID_WINDOW_HANDLE`) 50 ms 쉬고 3으로 돌아간다. 시한은 처음 하나라 돌아가도 늘어나지 않고, 쉴 틈이 없으면 `not_found`다.

| 상황 | 결과 |
| --- | --- |
| 첫째의 앱이 true | `ok` |
| 앱이 false, 또는 모양이 거절됨 | `failed` (받지 않았다) |
| 시한 안에 첫 창이 없다, 이 객체가 첫째 | `not_found` |
| 시한이 지났거나 첫째가 응답 없음 | `failed` (시한. 첫째가 나중에 받았을 수 있다) |
| `ERROR_ACCESS_DENIED` | `failed` (UIPI. 첫째가 더 높은 권한이다) |
| 입력 위반 | `failed` |

시한이 지난 보내기는 첫째가 나중에 받을 수 있다고 알려져 있다(실측 전). 앱은 같은 인자를 되보내려면 중복을 견뎌야 한다.

**받기.**

- **표식.** `run()`이 `on_started` 뒤 창을 보이기 전에 주 창에 `SetPropW(L"luil-instance:<이름>", 1)`을 건다. 표식이 서면 받을 준비가 된 것이다(host와 delegate가 있다). `environment.instance`가 비었거나 첫째가 아니거나 host가 없는 smoke 모드거나 delegate가 없으면 걸지 않는다. 이름이 비었거나 241 코드 단위를 넘어 표식을 지을 수 없을 때도 걸지 않고 디버그 출력에 까닭을 남긴다. 같은 자리에서 결정 6의 길에 따라 `allow_instance_message`를 건다. host를 거두기 직전(`WM_CLOSE`·`WM_ENDSESSION`·`WM_DESTROY`)에 먼저 표식을 지운다. 닫기를 앱이 막아 창이 남으면 표식도 남는다.
- **받는 자리.** `application_window::handle_window_message`의 `case WM_COPYDATA`다. 주 창만 받는다. 표식이 없거나 `dwData`가 표식 상수가 아니면 기본 처리로 넘긴다.
- **검증.** 모양이 맞지 않으면 FALSE다. `lpData`는 이 호출 안에서만 유효해 그 안에서 `instance_arguments`로 옮긴다.
- **콜백.** UI thread에서 `on_file_dropped`처럼 불린다. 던진 예외는 삼키고(디버그 출력에 남긴다) false로 본다. 보낸 쪽이 끝나기를 기다리므로 곧 돌아와야 한다. 앱은 `host.post_app_message_ticketed`로 게시하고 표가 0이 아닌지를 답한다([file-watch-design.md](file-watch-design.md)의 `deliver`와 같다). 대화 상자나 막는 호출을 하지 않는다. 받은 메시지는 크기 조절 루프나 UIA 호출 안에도 도착하는데([window_messages.h](../src/win32/window_messages.h) 22~25줄) 게시만 하는 콜백은 안전하다.
- **앞으로 세우기.** 콜백 뒤 표지가 켜져 있고 주 창이 보이면(`IsWindowVisible`) 최소화는 `SW_RESTORE`로 되살리고 `SetForegroundWindow`를 부른다. 이 세 줄은 `advance_focus_request`의 활성화와 같아 helper 하나(`bring_window_forward(HWND)`, `win32_window.cpp`의 익명 namespace)로 합친다. 숨은 주 창(`ui_frame::main_hidden`)은 보이게 하지 않는다. 콜백이 true를 답하든 false를 답하든 같다. 보이게 하는 것은 앱이 메시지로 `main_hidden`을 풀고 `focus_request::activate`를 같이 싣는다.

**운반 형식.** `COPYDATASTRUCT`의 `dwData`는 표식 상수 `instance_message_tag`이고 형식 판을 값에 담는다. `lpData`는 UTF-16이다. 첫 코드 단위가 표지(bit 0은 앞으로 세우기, 나머지 비트는 0이어야 하고 아니면 거절한다)이고, 이어 작업 폴더와 인자가 각각 NUL로 끝난다. 인자 개수는 싣지 않고 끝까지 NUL로 끊어 읽는다. 인자가 없으면 작업 폴더의 NUL에서 끝나고, 빈 인자 하나는 NUL 하나라 이 둘이 갈린다. 글자 단위가 OS의 글과 같고 짝 없는 surrogate를 [utf8.h](../src/win32/utf8.h)가 걸러 준다. 크기는 256 KiB 이하, 항목은 4096개 이하다. 한 명령 줄이 32,767자를 넘을 수 없어 실제 크기는 상한 안에 든다. 항목 수는 한 글자 인자 만여 개로 넘길 수 있으나 파일 연결이나 점프 목록의 쓰임은 미치지 못한다. 보내는 쪽이 막고 받는 쪽도 거절한다. `WM_APP` 번호표는 프로세스 안 신호용이라 쓰지 않는다.

**신뢰.** 같은 데스크톱의 어느 프로세스든(UIPI가 막지 않는 한) 표식 창에 보낼 수 있고, 받는 쪽은 보낸 프로세스를 알 길이 없다. 라이브러리는 모양만 검사한다. 앱은 이 인자를 명령 줄과 같이 믿을 수 없는 입력으로 다룬다. 파일 연결이나 URL 프로토콜로 뜬 둘째의 인자는 문서와 웹 링크에서 온다([open_url](platform-services-design.md)이 scheme 허용 목록을 둔 이유와 같다). 뮤텍스는 세션 단위이고 창은 데스크톱 단위다. 같은 세션의 다른 데스크톱에 첫째가 있으면 `not_found`다.

**둘째의 순서.** (1) `single_instance`를 만든다. 첫째이면 평소대로 뜨고 `environment.instance`를 단다. (2) 둘째이면 argv[0]을 뺀 인자를 `send_to_primary`로 보낸다. (3) `ok`면 끝낸다. `failed`(받지 않았다)는 앱이 자기 창을 따로 열지 끝낼지 정한다. (4) `not_found`는 첫째가 막 끝났을 수 있다. 이 객체가 뮤텍스 핸들을 쥔 채라([platform_services_win32.cpp](../src/win32/platform_services_win32.cpp) 156줄 주석) 첫째가 끝나도 이 객체는 둘째로 남는다. 놓고 새로 만들면, 첫째가 이미 끝났을 때 새 객체가 첫째다. 되풀이할지는 앱의 몫이다.

화면이 없는 기능이라 배율·테마·고대비·움직임 줄이기와 닿지 않는다. 앞으로 선 창의 UIA 초점은 OS의 활성화를 따라 옮겨 간다.

## 결정

**1. 첫 창을 찾는 법.** 질문: 둘째가 첫째의 주 창을 어떻게 찾는가.
- 길 a: 주 창에 이름 표식(창 속성)을 걸고 `EnumWindows`와 `GetPropW`로 찾는다. 되는 것: 창 클래스 이름과 무관하고, 숨은 주 창도 찾으며, 찾은 창이 앞으로 세울 창이다. 대가: 다른 프로세스의 창 속성을 읽는 동작은 알려진 방식이나 이 저장소에서 실측 전이다. 이름이 속성 이름 한계(255자)에 묶인다.
- 길 b: 이름 있는 클래스의 메시지 전용 창을 세워 `FindWindowExW(HWND_MESSAGE)`로 찾는다. 되는 것: 찾기가 확실하다. 대가: 클래스·창·프로시저가 하나 더 서고 받은 것을 주 창으로 넘기는 연결이 든다. G7이 정한 "주 창이 받는다"에서 벗어난다.
- 길 c: 등록 메시지를 방송한다. 되는 것: 없다. 대가: 모든 최상위 창을 깨우고, 방송은 데이터를 못 나르며, 답을 받을 창이 둘째에도 있어야 한다.
- 권장: a. 보내는 모양(`WM_COPYDATA`)이 같아 실측에서 막히면 b로 바꿀 수 있다. 대가: 실측 전 가정이 하나 선다. **권장대로 진행.**

**2. 첫째가 이름을 아는 법.** 질문: 주 창이 어떤 이름으로 표식을 거는가.
- 길 a: `window_environment::instance`에 `single_instance` 포인터를 준다. 되는 것: 이름을 두 번 적지 않아 어긋날 수 없고, 첫째일 때만 표식이 선다. 대가: 창보다 오래 산다는 수명 약속이 하나 늘고 `single_instance::name()`이 공개 API에 선다.
- 길 b: `window_config::instance_name`에 이름 글을 준다. 되는 것: 없다. 대가: 같은 이름을 두 곳에 적어 오타가 조용히 `not_found`가 된다. 둘째로 계속 도는 앱이 표식을 둘 걸 수 있다.
- 권장: a. 대가: 수명 약속 하나와 접근자 하나. `window_config`를 고치지 않아 os_notify의 앱 식별 필드와 겹치지 않는다. **권장대로 진행.**

**3. 인자의 모양.** 질문: 무엇을 보내는가.
- 길 a: 인자 목록과 보낸 쪽 작업 폴더를 늘 싣는다. 되는 것: 경계가 살아 첫째가 다시 쪼개지 않는다. `launch_program`·`command_line_arguments()`와 같은 모양이다. 상대 경로를 풀 수 있다. 대가: 작업 폴더를 끌 수 없고 NUL을 품은 인자는 못 보낸다.
- 길 b: 한 줄 글이다. 되는 것: 없다. 대가: 첫째가 따옴표 규칙으로 다시 쪼개야 하고 앱마다 `CommandLineToArgvW`를 부른다.
- 길 c: 목록만 싣는다. 되는 것: 없다. 대가: 상대 경로가 첫째의 작업 폴더에서 풀려 틀린다. 어느 인자가 경로인지 라이브러리가 모른다.
- 권장: a. 대가: 작업 폴더가 늘 실린다(같은 사용자의 경로라 새는 것은 없다). **권장대로 진행.**

**4. 소비의 뜻.** 질문: 콜백이 무엇을 답하고 보낸 쪽이 무엇을 아는가.
- 길 a: `bool`이다. true는 `ok`, false는 `failed`다. 되는 것: 조용한 유실이 없다. 둘째는 거절되면 자기 창을 열 수 있다. 대가: 기본 구현이 false라 콜백을 만들지 않은 앱에 보내면 `failed`다.
- 길 b: `void`이고 창이 받으면 `ok`다. 되는 것: 없다. 대가: 앱이 못 받아도 둘째는 끝나 인자가 사라진다.
- 권장: a. 대가: `environment.instance`를 달고 콜백을 만들지 않은 앱은 보내는 쪽이 늘 `failed`를 받는다(창은 앞으로 선다). **권장대로 진행.**

**5. 앞으로 세우기.** 질문: 누가 첫 창을 앞으로 세우는가.
- 길 a: 둘째가 권한을 넘기고 첫째의 라이브러리가 콜백 직후 보이는 주 창을 세운다. 되는 것: 앱이 아무것도 안 해도 더블 클릭이 창을 보여 준다. 둘째가 아직 살아 있는 동안 세워 권한이 확실하다. 대가: 활성화 코드가 라이브러리에 하나 선다. 다르게 하려는 앱은 `activate = false`로 보내고 자기 `focus_request`를 쓴다.
- 길 b: 권한만 넘기고 앱이 콜백에서 `focus_request::activate`를 싣는다. 되는 것: 새 활성화 코드가 없다. 대가: 잊으면 더블 클릭이 아무것도 안 보인다. 권한은 사용자 입력 전까지라고 알려져 있어 logic thread와 frame을 도는 시간 안에 써야 하며 실측 전이다.
- 길 c: 세우지 않는다. 되는 것: 없다. 대가: 가장 흔한 쓰임을 앱마다 짠다.
- 권장: a. 대가: 라이브러리 활성화 하나. **권장대로 진행.**

**6. 관리자 권한 첫째의 UIPI.** 질문: 낮은 권한 둘째가 보낸 인자를 받게 여는가.
- 길 a: 표식을 건 주 창에 `ChangeWindowMessageFilterEx(WM_COPYDATA, MSGFLT_ALLOW)`를 늘 건다(`allow_shell_events`와 같다). 되는 것: 관리자로 연 앱에 탐색기에서 연 파일이 온다. 대가: 셸 메시지와 달리 값을 싣는다. 샌드박스 안의 프로세스를 포함해 낮은 권한 프로세스가 높은 권한 앱에 인자를 먹일 수 있다.
- 길 b: 기본은 닫고 `instance_from_lower_integrity`를 켠 앱만 연다. 되는 것: 인자가 높은 권한 앱의 입력이 된다는 경계를 앱이 알고 열어, 켜지 않은 관리자 권한 앱에는 낮은 권한 프로세스가 인자를 먹일 길이 없다. 켠 앱은 길 a와 같이 탐색기에서 연 파일을 받는다. 대가: 관리자로 도는 앱이 한 줄을 켜야 한다. 안 켜면 둘째가 접근 거부 `failed`를 받는다.
- 길 c: 열지 않는다. 되는 것: 없다. 대가: 관리자 권한 앱은 이 길을 못 쓴다.
- 권장: b. 인자가 높은 권한 앱의 입력이 되는 보안 기본값이라 닫아 두고 앱이 위험을 알고 켠다. 대가: 관리자 권한 앱은 한 줄을 더 쓴다. **승인: 길 b (2026-11-02).**

**7. 첫 창이 아직 없을 때.** 질문: 둘째가 첫 창이 설 때까지 기다리는가.
- 길 a: 시한(기본 3초) 동안 50 ms마다 다시 찾는다. 되는 것: 첫째가 막 떠서 `on_started`가 느린 경우를 덮는다. 대가: 첫째가 끝나는 중이거나 이름이 어긋나면 시한만큼 기다린 뒤에야 `not_found`다.
- 길 b: 한 번 찾고 바로 `not_found`다. 되는 것: 없다. 대가: 연달아 띄우면 둘째의 인자가 사라진다.
- 권장: a. 대가: 최대 시한만큼 둘째가 늦게 끝난다. **권장대로 진행.**

**8. 점프 목록.** 질문: 이번 라운드 줄 4에 점프 목록을 더하는가. [작업 표시줄 문서](taskbar-design.md)가 215·217·240줄에서 G7과 앱 식별 뒤로 미뤘다. G7은 이 문서로 선다. 남은 걸림은 앱 식별(AUMID)이다.
- 길 a: os_notify 뒤 노드로 더한다. `taskbar_request`에 `jump_list`(작업의 제목·인자·아이콘)를 더하고 `ICustomDestinationList`를 쓴다. 되는 것: G7과 앱 식별이 서는 라운드에 같이 닫힌다. 대가: os_notify의 실측(레지스트리 AUMID로 서는가)이 나오기 전에 설계를 세우는 것이라 어긋나면 고친다. 줄 4가 L 노드 뒤에 M을 더하고 R1 파일(`taskbar.h`·`taskbar_sync`)을 다시 연다.
- 길 b: 더하지 않는다. os_notify가 앱 식별을 실측으로 세운 뒤 줄 4의 다음 노드로 따로 정한다. 되는 것: 이 문서의 API는 점프 목록 없이 완결이다. 점프 목록은 같은 `send_to_primary`를 쓰는 또 하나의 보내는 쪽이라 이 API를 고치지 않는다. 대가: 작업 표시줄 문서의 미룬 항목이 이번에도 닫히지 않는다.
- 길 c: 라이브러리에 넣지 않고 앱 몫으로 둔다. 앱이 `wWinMain`에서 AUMID를 걸고 점프 목록을 짓는다. 되는 것: 없다. 대가: 앱마다 COM 호출을 짜고, os_notify가 세울 앱 식별과 어긋난 AUMID를 쓸 위험이 있다.
- 권장: b. 대가: 점프 목록은 R2에서 서지 않는다. 앱 식별의 실측이 이 노드 다음 노드에서 나오므로 그 결과를 보고 정하는 것이 싸다. **승인: 길 a (2026-11-02, 권장 b와 다름). 구현은 docs/jump-list-design.md**

## 플랫폼

### Win32

- 보내는 쪽은 [platform_services_win32.cpp](../src/win32/platform_services_win32.cpp)에 든다. `state`가 이름을 쥔다. 작업 폴더는 `GetCurrentDirectoryW`로 읽는다. 호출은 `EnumWindows`·`GetPropW`·`GetWindowThreadProcessId`·`AllowSetForegroundWindow`·`SendMessageTimeoutW`이고 모두 user32라 새로 링크하는 것이 없다.
- 받는 쪽은 [win32_window.cpp](../src/win32/win32_window.cpp)의 `application_window`다. `window_surface.cpp`는 고치지 않는다(약속).
- 창 속성 이름은 뮤텍스 이름에서 `Local\`을 뺀 `luil-instance:<이름>`이다. 속성 이름은 대소문자를 가리지 않는 원자라 이름이 대소문자만 다른 두 앱은 뮤텍스가 달라도 표식이 같다(알려져 있으나 실측 전). 이름은 뮤텍스와 달리 241 코드 단위로 묶인다. 생성자는 그대로다.
- 레이어드 표시와 Direct3D 합성 창은 같다. 창이 아니라 메시지만 쓴다.

### Android

`send_to_primary`는 `unsupported`를 답하고 `name()`은 받은 이름을 돌려준다. `primary()`가 늘 참이어도 `not_found`보다 `unsupported`가 먼저다. `window_delegate`는 Win32 헤더에만 있어 Android의 `app_delegate`에 훅이 없다. 받을 길이 없는 까닭은 셋이다.

- OS가 앱마다 프로세스를 하나로 묶는다([platform-services-design.md](platform-services-design.md) 104줄). 둘째 프로세스가 생기지 않고, 앱을 다시 여는 요청은 Activity의 Intent로 온다.
- 살아 있는 Activity가 새 Intent를 받으려면 `onNewIntent`가 필요하다. `GameActivityCallbacks`([GameActivity.h](../third_party/game-activity-prep/include/game-activity/GameActivity.h) 162~298줄)에 새 Intent 콜백이 없다. 4.4.2 AAR의 `GameActivity.class`도 `getIntent`만 있고 `onNewIntent`가 없다(클래스 상수를 읽어 확인했다).
- 재정의하려면 Activity 하위 클래스(Java)가 든다. [Java 0줄 방침](android-port-plan.md) 575줄과 부딪히므로 하지 않는다. 매니페스트도 `launchMode`를 정하지 않고 앱 쪽 Java 코드가 없다([AndroidManifest.xml](../examples/android/app/src/main/AndroidManifest.xml) 21줄).

시작할 때의 Intent(`Activity.getIntent()`)는 JNI로 읽을 수 있다. 이 문서의 길이 아니라 os_notify의 콜드 스타트 몫이다.

## 다른 노드와의 약속

- **platform-services-design.md 개정.** 구현 커밋이 같은 묶음에서 이 문서의 판단으로 고친다. 15줄 표의 단일 인스턴스 행에 인자 보내기를 더하고, 36줄 "알리는 길(IPC)은 앱마다 달라 넣지 않았다"를 이 문서로 잇는 말로 바꾸고, 104줄에 Android의 `send_to_primary`가 `unsupported`임을 적고, 검증 절(108~121줄)의 단일 인스턴스 test와 데모 설명을 새 test로 맞춘다.
- **os_notify(줄 4, 다음).** 앱 식별(AUMID)을 세우는 노드다. 이 노드는 `window_config`를 고치지 않아 그 필드와 겹치지 않는다. 알림 눌림은 os_notify의 COM 활성화기가 받는다([OS 알림](os-notification-design.md) 결정 4). 첫째가 끝나는 중에 COM이 띄운 둘째만 `-luil-notification` 인자를 이 길로 넘기고, 앱은 `notification_launch`로 걸러낸다. 같은 `win32_window.cpp`를 직렬로 고친다. 이 노드는 `run()`의 `on_started` 뒤, `host_.reset()` 세 자리의 앞, `handle_window_message`의 case 하나, `advance_focus_request`의 활성화 helper 추출을 고친다. 병합 순서는 이 노드, os_notify, image_clip이다.
- **작업 표시줄 점프 목록.** 결정 8은 길 a라 os_notify 뒤 노드가 `docs/jump-list-design.md`로 세운다(그 문서는 그 노드가 더한다). 이 노드는 만들지 않는다. 점프 목록은 같은 `send_to_primary`를 쓰는 또 하나의 보내는 쪽이라 이 API는 바뀌지 않는다. 항목의 인자 따옴표는 `win32::make_command_line`을 쓴다.
- **auto_update(줄 4, R4).** 도우미가 부모 종료를 기다려 다시 띄우면 새 프로세스가 첫째라 이 길을 쓰지 않을 수 있다(추측). 쓰면 이 API를 그대로 쓴다.
- **줄 3 ink·inspector.** `window_surface.cpp`는 고치지 않는다. 계획은 이 파일에 `WM_COPYDATA` 자리를 두었으나, 받는 것이 주 창뿐이라 표면 계층(보조 창·popup과 공유)에 `surface_context` 가상 함수를 더할 이유가 없다. ink는 `handle_pointer_message`를 혼자 고친다. inspector가 필요로 하는 `win32_window.cpp`의 한 줄은 줄 4가 내고 이 문서가 그 몫을 맡는다. inspector가 main에 병합된 뒤 통합 단계에서 `application_window::apply_frame_appearance`의 `state.style` 다음에 `state.inspector = frame != nullptr ? &frame->inspector : nullptr;`를 커밋 하나로 낸다. 그 칸은 줄 3 브랜치에만 있어 줄 4 브랜치에서는 컴파일되지 않는다. 이 노드·os_notify·image_clip 커밋과 섞지 않는다.
- **WM_APP 번호표와 UIPI 자리.** 새 번호가 없다(`WM_COPYDATA`는 시스템 번호). `allow_instance_message`가 `allow_shell_events` 옆에 선다.
- **데모 공용 파일.** `examples/demo_main.cpp`는 여러 줄이 고친다. 라이브러리 커밋과 데모 연결 커밋을 나누고, 데모 연결은 통합 때 직렬로 쌓는다. `docs/README.md` 색인도 통합 때 붙인다.

## 확인

- `tests/instance_message_tests.cpp`(새 파일, `luil_tests`): 짓기와 읽기의 왕복(빈 목록, 빈 인자, 한글, BMP 밖 글자, 공백·따옴표 든 인자, 표지 비트), 상한(256 KiB와 4096개는 되고 하나 넘으면 거절), NUL 든 인자의 거절, 깨진 모양의 거절(홀수 바이트, 끝 NUL 없음, 짝 없는 surrogate, 빈 바이트열, 틀린 표식, 모르는 표지 비트), 속성 이름과 241 코드 단위 경계.
- `tests/platform_services_tests.cpp`(추가): 이름은 기존 test처럼 프로세스 번호를 붙인다. `name()`과 옮겨 간 객체의 `failed`, 첫째 객체의 `not_found`, 창이 없을 때 짧은 시한(50 ms)의 `not_found`, test가 별도 thread에 세운 숨은 창(표식 + `WM_COPYDATA`를 기록하는 프로시저)에 보낸 목록·작업 폴더·표지가 그대로 오는지, 프로시저가 FALSE를 답하면 `failed`, 응답하지 않는 프로시저의 시한 `failed`, 상한 초과·NUL·잘못된 UTF-8이 보내기 전에 `failed`, 표식 없는 창과 다른 이름의 표식에는 가지 않는지, 241 코드 단위 경계의 이름, 서지 못한 객체(빈 이름)의 `failed`. 같은 thread의 창은 `SendMessageTimeoutW`가 시한 없이 프로시저를 바로 부르므로 시한 test는 다른 thread의 창을 쓴다. 보내는 test는 `activate = false`로 보내 시험이 앞 창을 가져가지 않게 한다. 표지가 오는지 보는 한 번만 켜 보내는데, 시험 창은 활성화하지 않아 앞 창을 가져가지 않는다.
- `tests/win32_window_tests.cpp`(추가, `placement_driver` 틀. 주 창을 숨기는 `main_hidden` 칸을 driver에 더했다): `window_environment::instance`를 단 창에서 delegate의 `on_started`가 thread를 띄워 둘째 객체로 보낸다. 콜백이 창을 만든 thread에서 불리는지, 인자·작업 폴더가 같은지, true는 `ok`이고 false와 던진 예외는 `failed`인지, `instance`가 비면 표식이 없어 `not_found`인지, 창이 닫힌 뒤 `not_found`인지 본다. 숨은 주 창은 콜백 뒤에도 보이지 않는다.
- `tests/window_messages_tests.cpp`(추가): 없는 창에 `allow_instance_message`가 던지지 않는다.
- 데모 `examples/demo`: `--send-to-first`를 단 둘째는 이 플래그를 뺀 나머지 인자를 첫째에 보내고 끝낸다(종료 코드 `ok` 0, `not_found` 3, `failed` 4). `parse_arguments`가 모르는 인자를 거절하므로 이 분기가 그 앞에 선다. 플래그가 없으면 지금처럼 둘째도 뜬다. 첫째의 "창" 페이지(`windows_page.cpp`)에 label `둘째 인스턴스 인자: 없음`이 서고, 받으면 `둘째 인스턴스 인자: 2개 [a.txt] [b c] · 작업 폴더 <경로>`로 바뀌어 UIA로 되읽힌다. `demo_delegate`가 `on_instance_arguments`에서 게시한 intent를 페이지가 받는다. `tests/windows_page_tests.cpp`가 intent를 직접 주어 label을 본다. UIA 확인은 첫째와 둘째를 같은 격리 데스크톱에서 띄운다. 창은 데스크톱 단위다.
- 모바일 데모는 더하지 않는다. 읽을 값이 `unsupported` 하나이고 `device_page.cpp`는 os_notify가 크게 고친다. Android는 `platform_services_android.cpp`가 JNI를 부르는 파일이라 core test에 넣지 못해, 기존 확인 프로그램 방식([platform-services-design.md](platform-services-design.md) 117줄)으로 실기기에서 `unsupported`를 본다.
- 사람 확인: 탐색기에서 연결된 파일을 여는 것이 떠 있는 창을 앞으로 세우는지(최소화 중 포함), 작업 표시줄 단추가 깜빡이기만 하지 않는지, 관리자 권한 첫째에서 결정 6의 두 갈래, 첫째가 대화 상자나 크기 조절 루프 안에 있을 때, 첫째가 닫히는 중에, 다른 세션에서 띄운 것이 서로 보이지 않는지. 창 속성의 다른 프로세스 읽기와 `SendMessageTimeoutW` 시한 뒤의 도착, 권한의 지속 시간이 알려진 대로인지 실측한다.

## 범위 밖

- 이름 있는 파이프·소켓. luil은 주 창을 늘 만들고 `main_hidden`으로 숨기므로 창 없는 프로세스는 없다. 창 없이 도는 앱이 생기면 그때 길 b나 파이프로 바꾼다.
- 첫째가 둘째에 글을 돌려주는 답. `bool` 하나뿐이다.
- 인자의 해석: 경로 절대화, 파일 열기, scheme 검사. 앱의 몫이다.
- 이름 둘 이상, 첫째 열거, 세션·데스크톱·사용자를 넘는 전달.
- 점프 목록(결정 8의 길 a. 별도 노드와 별도 문서), 알림 클릭과 COM 활성화(os_notify).
- Android의 새 Intent 받기와 `Activity.getIntent()` 읽기.
- OS의 앞 창 규칙을 넘는 편법(`AttachThreadInput` 등).
