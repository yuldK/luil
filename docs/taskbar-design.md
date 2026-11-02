# 작업 표시줄 진행률과 오버레이

작업 표시줄 단추에 진행률(없음·불확정·정상·오류·일시정지와 0..1 값)과 오버레이 아이콘을 앱이 선언하는 길이다. 앱은 `ui_frame::taskbar`에 지금 바라는 상태를 싣고, UI thread가 바뀐 것만 `ITaskbarList3`로 옮긴다. Win32 전용이고, Android는 능력 플래그가 거짓이며 필드를 받고 무시한다. 공개 형식은 새 헤더 `include/luil/app/taskbar.h`의 `taskbar_request`·`taskbar_progress`·`taskbar_overlay`, `ui_platform_capabilities::taskbar`, Win32의 `taskbar_report`다.

이 노드는 Win32 창 계층의 공통 자리도 정리한다. `WM_APP` 번호표, `TaskbarButtonCreated` 같은 등록 메시지의 수신, 능력 플래그를 게시하는 자리다. 뒤에 오는 tray와 hotkey가 같은 자리를 쓴다. 점프 목록은 이번 범위가 아니다.

## 문제

- 작업 표시줄 단추를 만지는 코드가 없다. `src/`·`include/`·`examples/`·`tests/` 어디에도 `ITaskbarList3`, `TaskbarButtonCreated`, 앱 식별(AUMID) 설정, `FlashWindowEx`가 없다.
- 앱이 직접 부를 수도 없다. `ITaskbarList3`는 `HWND`를 받는데 공개 헤더는 `HWND`를 드러내지 않고, 창 프로시저를 앱에 열어 주는 훅도 없다([win32_window.h](../include/luil/win32/win32_window.h)).
- 오래 걸리는 일(내려받기, 내보내기)의 진행이 창이 최소화된 동안 어디에도 보이지 않는다. 주 창을 숨기고(`ui_frame::main_hidden`) 독립 창만 남기는 앱은 창마다 단추가 따로 서는데([multi-window-design.md](multi-window-design.md)) 그 단추에 상태를 걸 길이 없다.
- 단추는 만들어진 뒤에야 호출을 듣는다. 셸이 단추를 만들면 등록 메시지 `TaskbarButtonCreated`를 창에 보내고, 탐색기가 다시 뜨면 단추가 새로 선다. 상태를 다시 걸려면 창 프로시저에서 그 메시지를 받아야 한다.
- Win32 창 계층의 공통 자리가 흩어져 있다.
  - `WM_APP` 번호가 `win32_window.cpp`의 익명 namespace(+1~+4)와 [window_surface.h](../src/win32/window_surface.h)(+5)에 나뉘어 있다. 같은 창에서 겹치면 안 된다는 주석뿐이다.
  - Win32는 `ui_platform_capabilities`를 하나도 켜지 않는다. `publish_popup_support()`가 `rounded_popup_corners`만 적는다.
  - `RegisterWindowMessage`로 받는 메시지가 하나도 없다.

## 공개 형식

`include/luil/app/taskbar.h`, namespace `luil`. [platform_services.h](../include/luil/app/platform_services.h) 옆이다. `include/luil/app`은 core 검사 범위라([verify_core_portability.cmake](../cmake/verify_core_portability.cmake)) OS 헤더를 들이지 않는다. `app_host.h`와 `luil.h`가 include한다.

```cpp
namespace luil {
    enum class taskbar_progress_state
    {
        none,          // 막대 없음 (기본)
        indeterminate, // 값을 모르는 진행
        normal,        // value가 뜻을 갖는다
        error,         // 같은 값, 빨간 막대
        paused,        // 같은 값, 노란 막대
    };

    struct taskbar_progress
    {
        taskbar_progress_state state { taskbar_progress_state::none };
        // 0..1이다. normal·error·paused에서만 읽는다. 범위 밖은 자르고 NaN은 0이다.
        float value { 0.0f };
    };

    struct taskbar_overlay
    {
        // 비면 오버레이가 없다. 같은 이미지의 복사본은 같은 그림으로 본다 (결정 4).
        ui_image icon {};
        // 보조 기술이 읽는 글이다. 앱이 준다. 기본은 빈 글이다.
        std::u8string description {};
    };

    // 한 작업 표시줄 단추의 바라는 상태다. 기본값이면 셸을 만지지 않는다.
    struct taskbar_request
    {
        taskbar_progress progress {};
        taskbar_overlay overlay {};
    };
}
```

- `ui_frame::taskbar`는 주 창, `ui_window::taskbar`는 독립 창(`independent`)의 것이다. 소유된 창은 단추가 없어 읽지 않는다 (결정 1).
- `ui_platform_capabilities`는 끝에 `bool taskbar { false }`를 더한다. 참은 이 프로세스가 요청을 셸에 낼 수 있다는 뜻이다. 단추가 실제로 섰는지는 보고가 말한다.
- Win32 보고는 [win32_window.h](../include/luil/win32/win32_window.h)의 `backdrop_report` 옆에 둔다.

```cpp
namespace luil::win32 {
    enum class taskbar_status
    {
        off,         // 요청이 기본값이다. 셸에 걸린 것이 없다
        waiting,     // 요청이 있으나 단추가 아직 없다 (설 때까지, 주 창이 숨은 동안)
        applied,     // 셸 호출이 모두 성공했다
        unavailable, // ITaskbarList3를 만들지 못했다 (com_sta_scope가 없는 앱 등)
        failed,      // 셸 호출이 실패했다. error에 HRESULT가 있다
    };

    struct taskbar_report
    {
        std::u8string surface {}; // 비면 주 창, 아니면 ui_window::id
        taskbar_status status { taskbar_status::off };
        std::u8string error {};
    };
    // window_delegate::make_taskbar_message(const taskbar_report&)  기본은 빈 메시지
}
```

비공개 파일은 넷이다.

| 파일 | 몫 |
| --- | --- |
| `src/host/taskbar_sync.h`·`.cpp` (core) | 바라는 상태와 적용한 상태를 견줘 셸에 할 호출 목록을 낸다. OS 헤더가 없다 |
| `src/win32/taskbar_button.h`·`.cpp` | `ITaskbarList3`를 부르고 창별 기록과 아이콘 핸들을 쥔다 |
| `src/win32/window_messages.h`·`.cpp` | `WM_APP` 번호표, 등록 메시지, `shell_event_of`, UIPI 허용 |
| `src/win32/icon_from_image.h`·`.cpp` | `ui_image`를 `HICON`으로 옮긴다. tray가 같이 쓴다 |

## 규칙

**선언 모델.** [device_request](device-request-design.md)·popup과 같다. 앱은 지금 바라는 상태를 frame마다 그대로 싣는다. UI thread는 `snapshot_wake_message`에서 `apply_main_visibility()` 다음, 창 대조(`synchronize_windows`) 뒤에 마지막으로 적용한 것과 견주어 바뀐 것만 보낸다. 마지막 frame이 이긴다. 다른 thread는 앱 메시지로 앱 상태를 바꾼다. 상태라서 단추가 새로 서도 같은 값이 다시 적용된다.

**진행률 호출.** 값은 1000 단계다. `units = lround(clamp(value) * 1000)`이고 자르는 식은 [progress_element](../include/luil/ui/progress_element.h)의 `clamp_value`와 같다. 그 함수가 NaN을 그대로 두므로 NaN은 먼저 0으로 바꾼다.

| 요청 | 셸 호출 |
| --- | --- |
| `none` | `SetProgressState(TBPF_NOPROGRESS)` |
| `indeterminate` | `SetProgressState(TBPF_INDETERMINATE)` |
| `normal`·`error`·`paused` | `SetProgressValue(units, 1000)` 다음 `SetProgressState(...)` |

- 상태와 `units`가 둘 다 같으면 호출이 없다. 한 진행 동안 값 호출은 많아야 1001번이다.
- 값을 보내면 같은 상태를 이어서 다시 보낸다. `SetProgressValue`가 불확정·없음 상태를 정상으로 바꾼다고 알려져 있어 호출 순서에 기대지 않는다. 실측 전이다.
- 값 없는 상태에서 값 있는 상태로 들어올 때도 값부터 보낸다.

**오버레이.** `icon_from_image`가 `ui_image`를 `HICON`으로 옮긴다. `read_image_pixels`([image_pixels.h](../src/ui/image_pixels.h))가 낸 픽셀을 알파 32비트 DIB로 바꾸고 `CreateIconIndirect`를 부른다.

- 크기는 `GetSystemMetricsForDpi(SM_CXSMICON, 창 배율)`의 정사각형이다. 원본이 다르면 이 크기로 맞춘다. 창 배율이 바뀌면 오버레이만 새 크기로 다시 낸다.
- 같은 그림의 판정은 `ui_image::backing()` 포인터 비교다. 앱은 같은 `ui_image` 복사본을 frame마다 싣는다. 새로 지으면 새 그림이다.
- 핸들은 교체·해제·창 파괴까지 쥔다. 셸이 복사하는지 참조하는지에 기대지 않는다.
- 빈 이미지는 `SetOverlayIcon(hwnd, nullptr, L"")`로 해제한다. 설명만 바뀌면 같은 핸들로 다시 부른다. 변환에 실패하면 이전 오버레이를 두고 `failed`로 알린다. 설명이 잘못된 UTF-8이면 빈 글로 보내고 `error`에 적는다.

**단추가 설 때까지.** 창마다 `ready`가 있다. 처음은 거짓이고 그 창이 `TaskbarButtonCreated`를 받으면 참이다.

- 거짓인 동안은 호출하지 않고 요청을 쥔다 (`waiting`). 참이 되면 지금 요청을 한 번에 보낸다.
- 같은 메시지가 또 오면 적용 기록을 지우고 요청을 처음부터 다시 보낸다. 탐색기가 다시 뜨면 단추가 새로 서고 메시지가 다시 온다고 알려져 있다. 구현 때 실측한다. 요청이 기본값이면 호출이 없다.
- 호출이 실패해도 적용한 것으로 기록하고 재시도하지 않는다. 다음 변화나 단추 재생성에서 다시 한다. `failed`로 한 번 알린다.
- 관리자 권한으로 도는 앱은 UIPI가 셸이 보내는 등록 메시지를 막을 수 있다. 창을 만든 직후 `allow_shell_events`가 `ChangeWindowMessageFilterEx(MSGFLT_ALLOW)`를 건다. 두 메시지 모두 값을 싣지 않는다. 관리자 권한 실행은 사람 확인에서 본다.

**주 창이 숨은 동안.** `main_hidden`이면 주 창의 단추가 없다. 이 동안 주 창 요청은 쥐기만 하고 호출하지 않는다 (`waiting`). 숨기는 순간 `ready`를 거두고 적용 기록을 지운다. 보이면 단추가 새로 서므로 메시지를 기다린다. 이 경우에 메시지가 다시 오지 않는 판이 실측되면 숨김을 푼 직후 한 번 더 적용하는 길을 더한다.

**창 단위.** 독립 창이 사라지면 그 창의 기록과 아이콘이 함께 사라진다. 같은 앱의 창이 한 단추로 묶일 때 진행률이 어떻게 합쳐지는지는 실측 전이다.

**접근성·테마·움직임.** 막대의 색과 불확정의 움직임은 셸이 그린다. 테마·고대비·배율을 luil이 줄 수 없다. 움직임 줄이기를 지키려는 앱은 `indeterminate` 대신 `normal`과 값을 싣는다. 막대에는 읽는 글이 없다. 앱은 같은 정보를 창 안에 글로도 둔다. 오버레이만 `description`을 셸의 접근 글로 넘긴다.

## 결정

**1. 창 단위.** 질문: 어느 창의 단추에 거는가.
- 길 a: 주 창만(`ui_frame::taskbar` 하나). 대가: `main_hidden`으로 주 창을 숨기고 독립 창만 남기는 앱은 걸 단추가 없다.
- 길 b: 주 창과 독립 창마다(`ui_window::taskbar`). 대가: 필드 하나와 "소유된 창은 읽지 않는다"는 규칙이 는다. 창마다 기록과 아이콘을 쥔다.
- 길 c: 창 id를 키로 한 표를 frame에 싣는다. 대가: 소유된 창·popup id도 실을 수 있어 틀린 id를 가려야 한다.
- 권장: b. 독립 창을 쓰는 앱이 이 기능을 가장 필요로 한다. 소유된 창은 단추가 없다(`secondary_extended_style(false)`가 0이고 소유자가 주 창이다). **승인 필요.**

**2. 썸네일 도구 막대 단추.** 질문: 이번 범위에 넣는가.
- 길 a: 뺀다. 대가: 썸네일에 재생·이전·다음 같은 단추를 못 낸다.
- 길 b: `taskbar_request::buttons`(최대 7개: id·그림·툴팁·사용·숨김)를 넣는다. 대가: `ThumbBarAddButtons`는 창마다 한 번만 부를 수 있고 7개가 상한이라, 목록 길이를 첫 적용에 못 박거나 7개를 만들어 숨김으로 가려야 한다. 눌림이 `WM_COMMAND`의 `THBN_CLICKED`로 와서 창 프로시저에 case가 늘고, 관리자 권한에서는 `WM_COMMAND`의 UIPI 허용도 필요하다. 그림마다 `HICON`을 쥐고 단추가 새로 설 때마다 다시 단다. 대략 300줄과 test가 는다.
- 권장: a. 선언 모양(길이 고정)이 소비자 없이 정하면 틀리기 쉽다. 필요해지면 `taskbar_request`에 필드를 더하는 것으로 닫힌다. **승인 필요.**

**3. 선언 모델.** 질문: 앱이 어떻게 건네는가.
- 길 a: frame 필드(바뀐 것만 적용). 대가: 바뀌지 않는 값도 매 frame 싣는다. 비교는 값 몇 개라 싸다.
- 길 b: `app_host`의 setter. 대가: 단추가 새로 설 때마다 앱이 다시 불러야 하고, 마지막 요청만 유효하게 하려면 최신 값 slot이 따로 필요하다. [기기 요청](device-request-design.md)이 setter를 두지 않은 이유와 같다.
- 권장: a. **권장대로 진행.**

**4. 오버레이 그림의 원천.** 질문: 그림을 무엇으로 받는가.
- 길 a: `ui_image`. 대가: 변환 코드(`icon_from_image`)가 든다. 작은 그림을 크게 늘리면 흐리다. 크기는 라이브러리가 고른다.
- 길 b: 실행 파일 아이콘 리소스 id. 대가: Win32 값이 플랫폼 중립 요청에 들어온다. 실행 중에 지은 그림(숫자 배지)을 못 쓴다.
- 길 c: 불투명 `HICON` 핸들. 대가: 공개 API에 Win32 타입이 선다.
- 권장: a. tray가 같은 변환을 쓴다. **권장대로 진행.**

**5. 상태 보고.** 질문: 셸에 무엇이 닿았는지 앱이 아는가.
- 길 a: 알리지 않는다. 대가: `ITaskbarList3`에는 읽는 길이 없다. 데모와 UIA는 앱이 낸 요청만 보고, 반영 실패는 디버거 출력에만 남는다.
- 길 b: `window_delegate::make_taskbar_message`가 창마다 상태가 바뀔 때 한 번 알린다 (`make_backdrop_message`와 같은 모양). 대가: 공개 타입 하나와 가상 함수 하나.
- 권장: b. **권장대로 진행.**

**6. 숨은 주 창의 진행률.** 질문: 주 창이 숨은 동안 그 요청을 어디에 보이는가.
- 길 a: 쥐기만 한다. 대가: 어디에도 보이지 않는다.
- 길 b: 가장 앞의 독립 창에 대신 건다. 대가: 어느 창이 대표인지 라이브러리가 정해야 하고, 그 창의 요청과 겹친다.
- 권장: a. 대체 표시(트레이 툴팁 등)는 앱이 tray(R3)로 고른다. **권장대로 진행.**

G6은 설명 글의 기본값을 정하는 결정이다. 이 노드의 `description`은 앱이 주고 라이브러리가 만드는 글이 없어 G6에 기대지 않는다. G6이 정해진 뒤 기본 설명이 필요한 자리가 생기는지 다시 본다.

## 플랫폼

### Win32

- `application_window::create()`가 창을 만든 뒤 `taskbar_button`을 연다. `CoCreateInstance(CLSID_TaskbarList, ...)`와 `HrInit()`이다. COM 포인터는 `Microsoft::WRL::ComPtr`다([file_dialog.cpp](../src/win32/file_dialog.cpp)와 같다). 새로 링크할 라이브러리는 없다. 호출은 UI thread의 STA에서만 한다. 개체를 만들면 능력 플래그가 참이다. `com_sta_scope`가 없으면 만들지 못해 거짓이고 보고는 `unavailable`이다.
- 개체 하나를 모든 창이 나눠 쓴다. `HWND`를 인자로 받는다. `WM_DESTROY`에서 아이콘 핸들을 풀고 개체를 놓는다. `com_sta_scope`는 `wWinMain`의 지역이라 더 늦게 끝난다.
- 등록 메시지는 `window_surface::handle_surface_message` 머리에서 `shell_event_of(message)`로 걸러 `surface_context::shell_event(surface, event)`로 소유자에게 알린다. 주 창과 보조 창의 프로시저가 둘 다 마지막에 이 함수를 부르므로 사본이 하나다. 소유자는 `taskbar_button_created`를 그 표면의 `HWND`로 `taskbar_button`에 넘긴다.
- 독립 창은 같은 적용 자리에서 `frame->windows`를 훑어 `independent`인 것의 `HWND`로 적용한다. 창을 만든 직후(`create_secondary`)와 주 창을 만든 직후에 `allow_shell_events`를 건다.
- 레이어드 표시나 Direct3D 합성 창도 작업 표시줄 단추는 같다. 영향은 사람 확인에서 본다.

### Android

- 능력 플래그는 거짓이다. [android_app.cpp](../src/android/android_app.cpp)의 `set_ui_platform`이 필드를 이름으로 적으므로 새 필드는 손대지 않아도 거짓이다.
- host는 `ui_frame::taskbar`를 읽지 않는다. 받고 무시하고 로그도 남기지 않는다. `ui_frame::windows`는 한 번 오류를 남기지만, 이 필드는 `device_request`처럼 앱이 플랫폼마다 frame을 다르게 짓지 않게 하려는 것이다.
- Android에는 작업 표시줄이 없다. 가까운 기능은 진행 표시가 달린 알림이고 알림은 os_notify가 다룬다. JNI도 Java도 쓰지 않는다. 이 필드의 Android 지원은 unsupported다.

## 다른 노드와의 약속

이 노드가 Win32 창 계층 공통 자리의 첫 사용자다. tray·hotkey·os_notify는 아래를 따른다.

**`WM_APP` 번호표.** `src/win32/window_messages.h`에 모은다. 이름과 값은 바꾸지 않고 자리만 옮긴다.

| 번호 | 이름 | 받는 창 | 옛 자리 |
| --- | --- | --- | --- |
| +1 | `snapshot_wake_message` | 주 창 | `win32_window.cpp` |
| +2 | `ui_command_request_message` | 주 창 | `win32_window.cpp` |
| +3 | `app_ui_command_request_message` | 주 창 | `win32_window.cpp` |
| +4 | `clipboard_request_message` | 주 창 | `win32_window.cpp` |
| +5 | `access_dispatch_message` | 모든 표면 | `window_surface.h` |

- 새 번호는 +6부터 표 끝에만 더한다. 받는 창을 표에 적는다. 같은 창이 받는 번호끼리 겹치면 안 된다.
- `static_assert`가 값이 서로 다르고 `WM_APP`~`0xBFFF` 안인지 본다. 이 노드는 새 번호를 쓰지 않는다.
- hotkey가 `RegisterHotKey`를 고르면 `WM_HOTKEY`는 시스템 번호라 표에 넣지 않는다.
- `SetTimer` 번호(1·2·4는 `win32_window.cpp`, 3은 `window_surface.cpp`)는 이번에 옮기지 않는다. 새 타이머가 필요한 노드가 같은 헤더에 더한다.

**등록 메시지 길.** 이름은 `RegisterWindowMessageW`로 한 번 등록한다. `shell_event`는 지금 `taskbar_button_created`와 `taskbar_created`다.

- 새 등록 메시지는 `window_messages.h`에 `shell_event` 값과 `shell_event_of` 분기를 더한다. UIPI 허용도 같은 자리다.
- 방송되는 `TaskbarCreated`는 최상위 창마다 온다. 처리는 여러 번 와도 같아야 한다. 메시지 전용 숨은 창은 방송을 받지 못하므로 tray는 이 길로 주 창에서 받는다.
- 탐색기 재시작 뒤 다시 적용하는 규칙은 "적용 기록을 지우고 처음부터"다. 이 노드는 `taskbar_button_created`에서, tray는 `taskbar_created`에서 같은 모양으로 한다.

**능력 플래그.** `publish_popup_support()`를 `publish_platform()`으로 바꿔 Win32의 `ui_platform`을 적는 유일한 자리로 둔다 (`create()`와 `WM_SETTINGCHANGE`의 호출이 따라 바뀐다).

- 새 플래그는 `ui_platform_capabilities` 끝에 기본 거짓으로 더한다. Win32 값은 이 함수에 한 줄이고 "이 프로세스가 지금 실제로 하는가"다.
- 첫 frame 전에 정한다. `create()`가 `host_`를 만들기 전에 이 함수를 부르고, 값이 바뀔 때만 적는다.
- 창을 세우는 test는 끝에 `set_ui_platform(previous)`로 되돌린다([popup_reconcile_tests.cpp](../tests/popup_reconcile_tests.cpp)와 같다). 성질은 프로세스 전역이다.

**`surface_context`.** 새 가상 함수는 기본 구현을 갖는다. [secondary_surface_tests.cpp](../tests/secondary_surface_tests.cpp)의 `fake_surface_context`가 순수 가상을 전부 재정의하므로 순수로 더하면 대역이 깨진다. `shell_event`의 기본은 아무 일도 하지 않는다.

**`icon_from_image`.** `make_icon(const ui_image&, int size, std::u8string& error)`와 `HICON` 소유자 `owned_icon`이 이 파일에 있다. tray는 크기만 달리해 같은 함수를 쓴다.

**앱 식별(AUMID).** taskbar는 세우지 않는다. 진행률·오버레이는 `HWND`에 거는 것이라 AUMID 없이 선다고 알려져 있다. 이 저장소에서는 실측 전이다. 빌드 폴더의 실행 파일로 데모에서 확인한다. 서지 않으면 os_notify(R2)의 앱 식별이 먼저 서야 하고, 그때 `window_config`에 필드가 붙는다.

**점프 목록.** G7(두 번째 인스턴스가 첫 인스턴스에 인자 넘기기)이 정해지고 앱 식별이 서면 `taskbar_request`에 `jump_list` 필드를 더한다. 같은 규약(바뀐 것만 다시 구성)이다. 작업은 인자 달린 새 프로세스라 G7 없이는 떠 있는 앱에 닿지 않고 창이 하나 더 뜬다. 인자 따옴표는 `win32::make_command_line`([command_line.h](../src/win32/command_line.h))을 쓰고, `ICustomDestinationList`는 `propsys`를 링크 목록에 더한다.

**비공개 helper.** [file-watch-design.md](file-watch-design.md)가 같은 줄에서 뽑기로 한 Win32 helper(핸들 소유와 OS 오류 글)를 기다리지 않는다. 오류 글은 `win32::make_hresult_error`([win32_error.h](../src/win32/win32_error.h))가 있고 COM은 `ComPtr`이다. `platform_services_win32.cpp`의 `owned_handle`은 `CloseHandle`용이라 `HICON`에 맞지 않아 `owned_icon`을 따로 둔다.

## 확인

- `tests/taskbar_sync_tests.cpp`(core, `luil_core_tests`): 기본 요청은 호출 없음. 단추가 서기 전 보류, 서면 요청을 한 번 보냄. 없음→불확정→정상→오류→일시정지→없음의 호출 순서와 값 뒤 상태. 같은 요청과 1000단계 아래 변화는 호출 없음. 자르기와 NaN. 값 없는 상태에서 들어올 때 값부터. 메시지 재수신과 숨김 해제 뒤 처음부터 다시 보냄(기본값이면 없음). 같은 이미지는 호출 없음, 다른 이미지는 교체, 빈 이미지는 해제, 설명만 바뀜. 실패한 호출을 같은 요청으로 다시 하지 않음.
- `tests/taskbar_button_tests.cpp`(`luil_tests`, Windows): 가짜 `ITaskbarList3`를 주입해 인자(창·값·총량·상태·아이콘·설명)를 본다. 숨은 창에 등록 메시지를 보내 쥐고 있던 요청이 나가는지 본다. 개체를 만들 수 없을 때 `unavailable`. `icon_from_image`의 크기, 투명·불투명·반투명 픽셀(`GetIconInfo`와 `GetDIBits`), 알파 가장자리가 어두워지지 않는지, 잘못된 이미지. 창 파괴 뒤 GDI 개체 수가 늘지 않는지(`GetGuiResources`). 잘못된 UTF-8 설명.
- `tests/window_messages_tests.cpp`(`luil_tests`): 번호표의 값(+1~+5)과 서로 다름, 등록 메시지가 `0xC000` 이상이고 두 번 물어도 같음, `shell_event_of`의 매핑, `allow_shell_events`가 실패해도 던지지 않음.
- `tests/device_request_tests.cpp`의 기본 능력 단언에 `taskbar == false`를 더한다.
- 데모 `examples/demo/windows_page.cpp`에 "작업 표시줄" 구역을 둔다. 상태 단추 다섯, 값 슬라이더, 오버레이 켜고 끄기(`make_rgba_image`로 지은 작은 원), 독립 창을 여는 단추와 그 창의 진행률 단추다. label 셋이 UIA로 읽힌다. 요청(`작업 표시줄 요청: 정상 42% · 오버레이 켬`), 반영(`taskbar_report`의 상태), 능력(`작업 표시줄 능력: 있음`)이다. `examples/demo_main.cpp`가 `make_taskbar_message`를 받는다.
- `examples/mobile_demo/device_page.cpp`는 `capabilities.taskbar == false`일 때 "이 플랫폼에는 작업 표시줄이 없다." 줄을 보인다. 기존 항목과 같은 방식이고 `mobile_demo_page_tests.cpp`가 읽는다.
- 격리 데스크톱에는 작업 표시줄이 없을 것으로 보여(추정) UIA 되읽기는 `waiting`까지다. `applied`와 실제 모양은 사람이 실제 세션에서 본다.
- 사람 확인: 상태별 막대 색과 불확정의 움직임. 최소화 중에도 진행률이 보이는지. 오버레이가 100%·150%·200% 배율에서 또렷한지, 접근 이름이 읽히는지. 빌드 폴더의 실행 파일에서 AUMID 없이 서는지. `main_hidden`을 켰다 끌 때 복원되는지(단추 재생성과 메시지 재수신 실측). 탐색기를 다시 시작한 뒤 복원되는지. 관리자 권한으로 띄운 앱에서 서는지. 독립 창마다 따로 서는지와 한 단추로 묶였을 때의 모양. 레이어드 주 창과 Direct3D 합성 창에서 서는지.

## 범위 밖

- 점프 목록과 최근·자주 목록(파일 형식 등록이 필요하다). 점프 목록은 G7과 앱 식별 뒤다.
- 썸네일 도구 막대 단추(결정 2), 썸네일 자르기·툴팁, 탭 썸네일.
- `FlashWindowEx`로 주목을 끄는 것. 활성화 요청은 [나중에 서는 초점](deferred-focus-design.md)이 다룬다.
- 숨은 주 창을 대신하는 표시(트레이 툴팁 등)와 알림 배지 숫자.
- 앱 식별(AUMID) 설정, Android 런처 숏컷·배지.
