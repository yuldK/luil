# 알림 영역(트레이) 아이콘

작업 표시줄 알림 영역에 앱 아이콘 하나를 세우고, 그림과 툴팁을 바꾸고, 누름·두 번 누름·오른쪽 클릭을 앱에 돌려주는 길이다. 앱은 `ui_frame::tray`에 지금 바라는 아이콘을 싣고, UI thread가 바뀐 것만 `Shell_NotifyIconW`로 옮긴다. 오른쪽 클릭 메뉴는 앱이 준 `menu_config`로 라이브러리가 시스템 메뉴를 띄운다. Win32 전용이고 Android는 unsupported다(받고 무시한다. 알림은 [OS 알림](os-notification-design.md)이 맡는다). 공개 형식은 새 헤더 `include/luil/app/tray_icon.h`의 `tray_icon`·`tray_icon_event`·`tray_icon_action`, `ui_frame::tray`, `ui_platform_capabilities::tray`, Win32의 `tray_status`·`tray_report`·`window_delegate::make_tray_message`다.

이 노드는 [작업 표시줄](taskbar-design.md)이 tray 몫으로 남긴 셋을 닫는다. `TaskbarCreated` 방송(`shell_event::taskbar_created`)을 받는 가지, `icon_from_image`의 둘째 사용자, 그 문서 결정 6이 미룬 "숨은 주 창의 대체 표시"다. 다음 문서 hotkey가 같은 줄에서 같은 주 창과 같은 꼬리 자리를 이어 쓴다.

## 문제

- 알림 영역을 만지는 코드가 없다. `src/`·`include/`·`examples/`·`tests/` 어디에도 `Shell_NotifyIconW`, `NOTIFYICONDATAW`가 없다.
- 앱이 직접 부를 수 없다. `Shell_NotifyIconW`는 콜백을 받을 `HWND`와 창 메시지 번호를 요구한다. 공개 헤더는 `HWND`를 드러내지 않고 창 프로시저 훅도 없다([win32_window.h](../include/luil/win32/win32_window.h)). 주 창의 `WM_APP` 번호는 [window_messages.h](../src/win32/window_messages.h)가 쥔다.
- 탐색기가 다시 뜨면 아이콘이 사라진다. 다시 달려면 `TaskbarCreated` 방송을 받아야 한다. 등록과 UIPI 허용(`allow_shell_events`)은 R1이 세웠지만 [win32_window.cpp](../src/win32/win32_window.cpp)의 `handle_shell_event`(602줄)는 `taskbar_button_created`가 아니면 돌아간다.
- 오른쪽 클릭 메뉴를 세울 길이 없다. `ui_popup`은 표면 id 앵커만 있고 화면 좌표 앵커가 없다(`window_screen_anchor`는 `ui_window`의 것이다). popup 창은 `WS_EX_NOACTIVATE`(2640줄)라 다른 앱을 누를 때의 닫힘을 스스로 받지 못한다. 트레이 앱은 주 창을 숨긴(`main_hidden`) 채 메뉴를 띄우는 일이 흔한데, 숨은 창은 앵커가 될 수 없다.
- 그림은 반만 있다. 창 아이콘은 실행 파일 리소스 id(`window_config::icon_resource`, `create()`의 `LoadIconW`)뿐이다. 실행 중 지은 그림은 `make_icon`([icon_from_image.h](../src/win32/icon_from_image.h))이 `HICON`으로 옮기지만 작업 표시줄 오버레이만 쓴다. 알림 영역은 작업 표시줄의 테마(`SystemUsesLightTheme`)를 따르는데 코드는 앱 모드(`AppsUseLightTheme`, 174줄)만 읽는다.
- "닫으면 트레이로 숨기고 트레이에서 끝낸다"의 나머지는 이미 있다. `ui_frame::close`·`main_hidden`([app_host.h](../include/luil/app/app_host.h) 396·400줄)과 `ui_command::application_exit`다. 빠진 것은 아이콘과 그 사건이다.

## 공개 형식

`include/luil/app/tray_icon.h`, namespace `luil`. core 검사 범위라([verify_core_portability.cmake](../cmake/verify_core_portability.cmake)) OS 헤더가 없다. `image_element.h`(`ui_image`)·`menu_element.h`(`menu_config`)·`ui_events.h`(`input_action`·`screen_point`)를 들인다. `app_host.h`와 `luil.h`가 include한다.

```cpp
namespace luil {
    enum class tray_icon_action
    {
        select,       // 왼쪽 클릭, 키보드로 고름(Enter·Space). NIN_SELECT·NIN_KEYSELECT
        double_click, // 왼쪽 두 번 클릭. 앞에 select가 하나 먼저 온다
        context,      // 오른쪽 클릭·Shift+F10·메뉴 키. `menu`가 있으면 오지 않는다
    };

    struct tray_icon_event
    {
        tray_icon_action action { tray_icon_action::select };
        bool keyboard { false }; // 키보드로 냈다
        screen_point point {};   // 셸이 준 화면 물리 좌표다 (아이콘 자리나 커서)
    };

    // 알림 영역의 아이콘 하나다. `ui_frame::tray`에 실으면 서고 빼면 사라진다.
    struct tray_icon
    {
        // 그림이다. 비면 창 아이콘(`window_config::icon_resource`, 없으면 시스템 기본)이다.
        ui_image icon {};
        // 밝은 작업 표시줄에서 쓸 그림이다. 비면 `icon`이다.
        ui_image light_icon {};
        // 고대비에서 쓸 그림이다. 비면 위 규칙을 따른다.
        ui_image high_contrast_icon {};
        // 툴팁이자 보조 기술이 읽는 이름이다. 비면 창 제목이다. 127 UTF-16 코드 단위에서 자른다.
        std::u8string tooltip {};
        // 오른쪽 클릭 메뉴다. 있으면 라이브러리가 띄우고 고른 키를 `menu->select`로 낸다. 비면 `context` 사건이 온다.
        std::optional<menu_config> menu {};
        // 사건이다. UI thread에서 그 순간의 frame 것을 부른다. 비면 사건을 버린다.
        std::function<input_action(const tray_icon_event&)> activate {};
    };

    // ui_frame 끝:  std::optional<tray_icon> tray {};          비면 아이콘이 없다 (기본)
    // ui_platform_capabilities 끝:  bool tray { false };      Win32에서 셸에 낼 수 있으면 참
}
```

Win32 보고는 [win32_window.h](../include/luil/win32/win32_window.h)의 `jump_list_report` 옆에 둔다.

```cpp
namespace luil::win32 {
    enum class tray_status
    {
        off,     // 요청이 없다. 셸에 걸린 것이 없다
        waiting, // 요청이 있으나 셸이 받지 않았다 (탐색기가 아직 없다). TaskbarCreated를 기다린다
        shown,   // 아이콘이 섰다 (Windows 11은 넘침 칸에 숨어 있을 수 있다)
        failed,  // 셸 호출이나 그림 변환이 실패했다. error에 까닭이 있다
    };

    struct tray_report
    {
        tray_status status { tray_status::off };
        std::u8string error {};
        [[nodiscard]] bool operator==(const tray_report&) const = default;
    };
    // window_delegate::make_tray_message(const tray_report&)  기본은 빈 메시지
}
```

비공개 파일은 둘이다. `src/host/tray_sync.h`·`.cpp`(core)는 바라는 아이콘과 적용한 아이콘을 견줘 할 호출(더하기·바꾸기·지우기)을 낸다. `src/win32/tray_icon_win32.h`·`.cpp`는 `NOTIFYICONDATAW`를 짓고, 콜백을 풀고(`tray_event_of`), 메뉴를 짓고(`make_tray_menu`), `Shell_NotifyIconW`를 함수 객체 seam 뒤에서 부른다. 그림은 `make_icon`을 그대로 쓴다.

## 규칙

**선언 모델.** [작업 표시줄](taskbar-design.md)과 같다. 앱은 frame마다 지금 바라는 아이콘을 싣는다. UI thread는 `snapshot_wake_message`에서 `synchronize_taskbar()` 다음에 마지막으로 적용한 것과 견주어 바뀐 것만 보낸다. 툴팁은 값, 그림은 `ui_image::backing()` 포인터로 견준다. 메뉴와 `activate`는 견주지 않는다. 사건이 올 때 그 순간의 frame에서 읽는다.

| 바뀐 것 | 셸 호출 |
| --- | --- |
| 없음 → 있음 | `NIM_ADD` 다음 `NIM_SETVERSION(NOTIFYICON_VERSION_4)` |
| 고른 그림·툴팁·아이콘 크기 | `NIM_MODIFY` (바뀐 칸의 `NIF_ICON`·`NIF_TIP`만) |
| 있음 → 없음 | `NIM_DELETE` |

- 식별은 주 창 `HWND`와 `uID` 1이다. `NIF_GUID`는 쓰지 않는다 (결정 3).
- 플래그는 `NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP`이다. 판 4는 `NIF_SHOWTIP`이 없으면 표준 툴팁을 띄우지 않는다. 콜백 번호는 `tray_callback_message`(`WM_APP + 7`, 주 창)다.
- `NIM_ADD`가 실패하면(자동 시작 직후라 탐색기가 없는 경우) `waiting`이다. 다음 `TaskbarCreated`에서 다시 더한다. `MODIFY`·`DELETE` 실패는 `failed`로 한 번 알리고 재시도하지 않는다.
- 탐색기가 바빠 `NIM_ADD`가 `ERROR_TIMEOUT`으로 실패해도 아이콘은 섰을 수 있다고 알려져 있다(실측 전). 이때는 방송이 오지 않으므로 곧바로 `NIM_MODIFY`를 한 번 해 보고, 성공하면 선 것으로 본다. 실패하면 `waiting`으로 두고 다음 요청 변화에서 다시 더한다.
- `TaskbarCreated`가 오면 적용 기록을 지우고 요청이 있으면 처음부터 다시 더한다. `HWND_BROADCAST`는 숨은 소유자 없는 최상위 창에도 간다고 알려져 있어 숨은 주 창도 받는다고 본다(사람 확인). 여러 번 와도 같은 일을 한다.
- 관리자 권한으로 도는 앱은 탐색기가 보내는 콜백이 UIPI에 막힌다. `create()`가 `allow_shell_events` 다음에 `tray_callback_message`도 `ChangeWindowMessageFilterEx(MSGFLT_ALLOW)`로 연다. 콜백은 사건 번호와 좌표만 싣는다.
- 끝날 때 `NIM_DELETE`한다. `WM_CLOSE`·`WM_ENDSESSION`·`WM_DESTROY`에서 `clear_instance_mark()` 옆이다. 지우지 않으면 마우스가 지나갈 때까지 유령 아이콘이 남는다.

**그림.** `make_icon(image, size, error)`로 옮긴다. 핸들은 교체·지우기·창 파괴까지 쥔다.

- 크기는 `GetSystemMetricsForDpi(SM_CXSMICON, dpi)`이고 `dpi`는 주 모니터(`MONITOR_DEFAULTTOPRIMARY`)의 `GetDpiForMonitor`다. 알림 영역은 주 작업 표시줄에 있고 주 창은 숨었거나 다른 모니터에 있을 수 있다. `WM_DISPLAYCHANGE`·`WM_SETTINGCHANGE`·`WM_DPICHANGED`에서 다시 읽고, 크기가 바뀌면 그림만 다시 짓는다. 셸이 다른 크기로 늘려 그리는 판이 있는지는 실측 전이다.
- 고르는 순서는 고대비면 `high_contrast_icon`, 작업 표시줄이 밝으면 `light_icon`, 그 밖은 `icon`이다. 빈 칸은 다음 순서로 물러선다. 고대비는 이미 있는 `read_high_contrast_enabled()`, 밝기는 새로 읽는 `HKCU\...\Themes\Personalize`의 `SystemUsesLightTheme`다(값이 없으면 어두움). `WM_SETTINGCHANGE`·`WM_THEMECHANGED` case에서 다시 읽고 고른 그림이 바뀌면 `NIM_MODIFY`한다.
- 셋이 다 비면 창 아이콘을 `LoadImageW(instance, icon_resource, IMAGE_ICON, size, size, 0)`로 그 크기에 맞춰 읽는다. 리소스가 없으면 `IDI_APPLICATION`이다. 변환이 실패하면 이전 그림을 두고 `failed`로 알린다.

**툴팁과 접근성.** 툴팁은 셸이 아이콘의 접근 이름으로 읽는다. luil의 UIA 계층은 닿지 않는다.

- 비면 창 제목(`caption.title`, 그것도 비면 `luil`)이다. 라이브러리가 짓는 글이 없어 [기본 문구](default-text-design.md)에 더할 함수가 없다.
- `szTip`은 끝 NUL을 넣어 128 `wchar_t`다. 127 코드 단위에서 자르되 서로게이트 쌍을 가르지 않는다. 잘못된 UTF-8은 창 제목으로 물러서고 `error`에 적는다.
- 배율·움직임 줄이기에 걸리는 일은 없다. 셸이 그리고 움직이지 않는다. 고대비는 위 그림 순서가 맡는다.

**사건.** 판 4 콜백은 `LOWORD(lParam)`이 사건, `HIWORD(lParam)`이 `uID`, `wParam`이 화면 좌표다(`GET_X_LPARAM`·`GET_Y_LPARAM`). `tray_event_of`가 순수 함수로 푼다.

| 셸 사건 | `tray_icon_event` |
| --- | --- |
| `NIN_SELECT` | `select` |
| `NIN_KEYSELECT` (Enter·Space) | `select`, `keyboard` |
| `WM_LBUTTONDBLCLK` | `double_click` |
| `WM_CONTEXTMENU` (오른쪽 클릭·Shift+F10·메뉴 키) | 메뉴가 있으면 메뉴, 없으면 `context` |
| 그 밖(`WM_MOUSEMOVE`·`NIN_POPUPOPEN` 등)과 다른 `uID` | 없음 |

- Enter에 `NIN_KEYSELECT`가 두 번 온다고 알려져 있다(실측 전). `GetDoubleClickTime()` 안의 두 번째 `NIN_KEYSELECT`는 버린다.
- `activate`가 낸 `input_action`은 `dispatch_action`이 나른다. 던지면 `report_runtime_error`다(`request_main_close`와 같다).
- 라이브러리는 사건에서 창을 앞으로 세우지 않는다. 앱이 `main_hidden`을 끄고 `focus_request::activate`를 싣는다. 아이콘을 누른 프로세스는 셸이 앞으로 설 권한을 준다고 알려져 있다. frame이 늦게 와도 그 권한이 남는지는 사람 확인에서 본다.

**메뉴.** 결정 1의 길 a다. `WM_CONTEXTMENU`에서 그 순간의 frame에 `menu`가 있으면 띄운다.

- `make_tray_menu`가 `menu_item_config`를 `CreatePopupMenu`·`AppendMenuW`로 옮긴다. `separator_above`는 앞의 `MF_SEPARATOR`, `enabled == false`는 `MF_GRAYED`, `check`·`radio`의 `checked`는 `MF_CHECKED`(라디오는 `MFT_RADIOCHECK`), `children`은 하위 메뉴, `shortcut_text`는 `label\t단축키`다. 글의 `&`는 `&&`로 바꿔 밑줄 글자로 읽히지 않게 한다. `icon` 글리프와 `owner`·`open_submenu`는 쓰지 않는다. 명령 번호는 펼친 항목 순서 + 1이고 그 번호로 키를 찾는다.
- 띄우기는 `SetForegroundWindow(주 창)` → `TrackPopupMenuEx(TPM_RETURNCMD | TPM_RIGHTBUTTON, 콜백 좌표)` → `PostMessageW(주 창, WM_NULL)`이다. 앞으로 세우지 않으면 바깥을 눌러도 메뉴가 닫히지 않는 것이 이 API의 알려진 규칙이다. 숨은 주 창에서 앞으로 세우기가 서는지는 사람 확인에서 본다.
- 메뉴가 떠 있는 동안 UI thread는 메뉴의 modal 루프에서 메시지를 계속 푼다. 고른 키는 연 순간의 `menu_config` 사본의 `select`로 낸다. 그 사이 frame에서 `tray`가 빠지면 `EndMenu()`로 닫는다.
- 메뉴는 OS가 그린다. 배율·고대비·키보드 탐색·보조 기술은 OS 메뉴의 것이다. 문서화된 길로는 다크 모드를 따르지 않아 어두운 테마에서도 밝게 선다.

**풍선과 닫기 정책.** `NIF_INFO` 풍선은 쓰지 않는다. [OS 알림](os-notification-design.md) 결정 1이 풍선(길 c)을 고르지 않았고 알림은 `show_notification`이다. "닫으면 트레이로"는 라이브러리가 정하지 않는다 (결정 8).

## 결정

**1. 오른쪽 클릭 메뉴.** 질문: 메뉴를 누가 무엇으로 띄우는가.
- 길 a: 라이브러리가 `tray_icon::menu`(`menu_config`)를 시스템 `HMENU`로 옮겨 `TrackPopupMenuEx`로 띄운다. 되는 것: 앞으로 세우기·바깥 눌러 닫기·모니터와 배율·키보드·보조 기술·고대비를 OS가 맡는다. 숨은 주 창으로도 선다. popup 계층과 줄 3의 controller를 건드리지 않는다. 대가: 모양이 luil 메뉴와 다르다(테마 색·모서리·배경 효과 없음, 다크 모드에서 밝음). 항목 아이콘 글리프가 빠진다.
- 길 b: luil `menu_popup`을 화면 좌표에 띄운다. 되는 것: 앱 테마와 같은 메뉴다. 대가: `ui_popup`에 화면 앵커를 더하고, 활성화되지 않는 popup이 다른 앱을 누를 때 닫히게 하고, 숨은 주 창을 앵커로 쓰는 길을 popup 대조·popup 표면·`win32_window.cpp`의 popup 구간에 새로 짠다. 메뉴 키보드 탐색은 controller를 지나 이번 라운드 줄 3 menu_bar와 같은 자리를 고친다. 크기가 L로 커진다.
- 길 c: 메뉴 없이 `context` 사건만 넘긴다. 되는 것: 가장 짧다. 대가: 공개 API에 `HWND`가 없고 화면 앵커 popup도 없어 앱이 메뉴를 띄울 길이 사실상 없다.
- 권장: a. 칸은 `menu_config`라 나중에 b를 세워도 앱 코드는 그대로다. 대가: 모양이 luil 메뉴와 다르고 다크 모드에서 밝다. **승인 필요.**

**2. 콜백을 받는 창.** 질문: 셸의 콜백과 방송을 어느 창이 받는가.
- 길 a: 주 창 `HWND`. 되는 것: [작업 표시줄](taskbar-design.md) 약속("tray는 이 길로 주 창에서 받는다")과 같고 `handle_shell_event`에 가지 하나다. 주 창은 숨어도 앱이 끝날 때까지 산다. hotkey의 `WM_HOTKEY`도 같은 창으로 온다. 대가: 주 창 프로시저에 case가 하나 는다. 숨은 창을 앞으로 세우는 일은 실측한다.
- 길 b: 따로 숨은 최상위 창. 되는 것: 주 창과 수명이 갈린다. 대가: 창 클래스와 프로시저가 하나 더 들고, 사건을 결국 `application_window`로 넘긴다. R1 약속과 UIPI 허용을 다시 건다.
- 길 c: 메시지 전용 창(`HWND_MESSAGE`). 대가: 방송을 받지 못해 탐색기 재시작 뒤 아이콘을 잃는다.
- 권장: a. 대가: 주 창 프로시저에 case 하나. **권장대로 진행.**

**3. 아이콘 식별.** 질문: 셸에 아이콘을 무엇으로 알리는가.
- 길 a: `HWND` + `uID` 1. 되는 것: 앱 식별과 무관하게 선다. 실행 파일을 옮겨도 막히지 않는다. 대가: Windows 11의 "늘 보이기" 설정이 실행 파일 경로와 `uID`에 묶인다고 알려져 있다(실측 전).
- 길 b: `NIF_GUID`를 `window_config::identity.id`에서 `name_based_uuid`(판 5)로 짓는다. 되는 것: 앱이 GUID를 고르지 않고 창이 바뀌어도 같은 아이콘이다. 대가: 셸이 GUID를 실행 파일 경로에 묶어, 빌드 폴더와 설치본처럼 경로가 다른 같은 앱은 `NIM_ADD`가 실패한다. 식별 없는 앱은 쓸 수 없다.
- 길 c: 앱이 GUID를 준다. 대가: 길 b의 경로 제약이 그대로이고 예제를 베낀 두 앱이 부딪힌다.
- 권장: a. 대가: 사용자 설정이 경로에 묶인다. **권장대로 진행.**

**4. 그림의 원천과 변형.** 질문: 그림과 테마·고대비 변형을 누가 고르는가.
- 길 a: `ui_image` 셋(기본·밝은 작업 표시줄·고대비)을 받아 라이브러리가 고르고, 다 비면 창 아이콘 리소스다. 되는 것: 앱이 모르는 작업 표시줄 테마를 라이브러리가 읽는다. 지은 그림(숫자 배지)도 쓴다. 대가: 칸이 셋이고 `SystemUsesLightTheme` 읽기가 는다.
- 길 b: `ui_image` 하나. 되는 것: 칸이 하나다. 대가: frame의 테마는 앱 모드라 작업 표시줄이 어두운데 앱이 밝은 경우를 앱이 가리지 못한다.
- 길 c: 리소스 id나 `.ico` 경로만. 되는 것: OS가 크기별 그림을 고른다. 대가: 실행 중 지은 그림을 못 쓴다.
- 권장: a. [작업 표시줄](taskbar-design.md) 결정 4와 같은 원천이다. 대가: 칸 셋과 레지스트리 값 하나. **권장대로 진행.**

**5. 사건을 넘기는 길.** 질문: 누름·메뉴 요청을 앱에 어떻게 주는가.
- 길 a: `tray_icon::activate` 함수(`input_action`을 낸다). 되는 것: `ui_frame::close`·`menu_config::select`와 같은 모양이다. delegate를 넓히지 않는다. 대가: frame 값에 함수가 하나 든다.
- 길 b: `window_delegate::make_tray_event_message`. 대가: 앱이 사건을 메시지로 바꾸는 코드를 delegate에 따로 둔다. 메뉴 선택(`select`)과 길이 갈린다.
- 길 c: `app_delegate`의 훅. 대가: Android가 부르지 않는 훅이 플랫폼 중립 자리에 선다.
- 권장: a. 대가: frame의 함수 하나. **권장대로 진행.**

**6. 아이콘 수.** 질문: 한 앱이 몇 개를 세우는가.
- 길 a: 하나(`std::optional<tray_icon>`). 되는 것: `uID`가 1로 고정되고 대조가 짧다. 대가: 여러 개가 필요해지면 칸의 형식을 바꾼다.
- 길 b: id를 단 `std::vector<tray_icon>`. 되는 것: popup·창과 같은 목록이다. 대가: id → `uID` 표를 실행 내내 쥐고 탐색기 재시작 뒤 같은 번호로 다시 단다.
- 권장: a. 소비자 없이 정하면 틀리기 쉬운 모양이다. 대가: 넓힐 때 형식이 바뀐다. **권장대로 진행.**

**7. 상태 보고.** 질문: 셸에 닿았는지 앱이 아는가.
- 길 a: `window_delegate::make_tray_message(const tray_report&)`가 상태가 바뀔 때 한 번 알린다. 되는 것: 데모와 UIA가 `waiting`·`shown`·`failed`를 되읽는다. `taskbar_report`와 같은 모양이다. 대가: 공개 타입 둘과 가상 함수 하나.
- 길 b: 알리지 않는다. 대가: 탐색기가 없을 때와 실패가 디버거 출력에만 남는다.
- 권장: a. 대가: 타입 둘과 가상 함수 하나. **권장대로 진행.**

**8. 닫으면 트레이로.** 질문: 라이브러리가 이 정책을 주는가.
- 길 a: 주지 않는다. 앱이 `ui_frame::close`에서 `main_hidden`을 켠다. 되는 것: 지금 구조 그대로이고 끝내기·숨기기를 앱이 고른다. 대가: 트레이 앱마다 몇 줄을 쓴다.
- 길 b: `tray_icon::hide_on_close`. 대가: 닫기·숨기기·끝내기 규칙이 두 자리에 갈린다.
- 권장: a. 대가: 앱의 몇 줄. **권장대로 진행.**

## 플랫폼

### Win32

- `application_window`가 `tray_icon_host` 하나를 쥔다. `create()`가 `allow_shell_events(window_)` 다음에 콜백 번호의 UIPI를 열고 주 창·instance·`icon_resource`·제목을 넘긴다. `Shell_NotifyIconW`는 `shell32`라 새로 링크할 것이 없다. smoke 모드는 host가 없어 적용 자리에 오지 않는다.
- 능력 플래그는 `publish_platform()`에 한 줄이다. 주 창이 섰으면 참이다. 탐색기가 없어도 참이고 실제 상태는 보고가 말한다.
- 레이어드 표시와 Direct3D 합성 주 창도 같다. 아이콘은 창 표면과 무관하다.
- 한계: Windows 11은 새 아이콘을 넘침 칸에 두고 앱이 보이게 할 공개 길이 없다. 사용자가 설정에서 꺼낸다.

### Android

- unsupported다. 알림 영역 아이콘에 해당하는 것이 없다. 상태 표시줄 아이콘은 알림이 만들고 알림은 [OS 알림](os-notification-design.md)이 맡는다. JNI도 Java도 쓰지 않는다.
- host는 `ui_frame::tray`를 읽지 않는다. 받고 무시하고 로그도 없다(`taskbar`와 같다). [android_app.cpp](../src/android/android_app.cpp)의 `set_ui_platform`은 필드를 이름으로 적으므로 `tray`는 손대지 않아도 거짓이다.

## 다른 노드와의 약속

**같은 줄 hotkey(다음).** 공용 꼬리에 tray가 먼저, hotkey가 뒤에 더한다. 같은 줄이라 차례로 쌓여 텍스트 충돌이 없다.

| 자리 | tray | hotkey |
| --- | --- | --- |
| `ui_frame` 끝(`inspector` 뒤) | `tray` | 그 뒤 |
| `ui_platform_capabilities` 끝(`clipboard_image_copy` 뒤) | `tray` | 그 뒤 |
| `publish_platform()` | 한 줄 | 그 뒤 한 줄 |
| `win32_window.h` 보고 타입(`jump_list_report` 뒤) | `tray_status`·`tray_report` | 그 뒤 |
| `window_delegate` 끝(`on_instance_arguments` 뒤) | `make_tray_message` | 그 뒤 |
| `app_delegate.h` | 고치지 않는다 | 필요하면 끝 |
| `window_messages.h` | `WM_APP + 7` `tray_callback_message`(주 창), 주석을 "+8부터"로 | `WM_HOTKEY`는 시스템 번호라 표에 없다 |

- 받는 창은 주 창이다(결정 2). hotkey도 `RegisterHotKey(주 창)`로 같은 프로시저에서 받는 것을 전제로 둔다.
- `win32_window.cpp`에서 tray가 고치는 자리: `create()`(UIPI와 설정), `handle_shell_event`(`taskbar_created`이고 `surface`가 빈 주 창일 때 `tray_.shell_restarted()`), `handle_window_message`의 `snapshot_wake_message`(`synchronize_taskbar()` 다음 `synchronize_tray()`), 새 case `tray_callback_message`, `WM_DISPLAYCHANGE`·`WM_DPICHANGED`·`WM_SETTINGCHANGE`(배율·테마 다시 읽기), `WM_CLOSE`·`WM_ENDSESSION`·`WM_DESTROY`(지우기), `publish_platform()`. 새 함수는 `synchronize_tray`·`handle_tray_callback`·`show_tray_menu`·`post_tray_report`, 멤버는 `tray_` 하나다. hotkey는 같은 함수에서 tray 줄 뒤에 둔다.

**R1·R2 파일.** `icon_from_image`·`window_messages`의 `shell_event`·`allow_shell_events`·`taskbar_button`은 고치지 않고 쓴다. `handle_shell_event`의 "`taskbar_created` 방송은 이 노드가 쓰지 않는다 (tray가 쓴다)" 주석은 구현 커밋이 실제 가지로 바꾼다. 구현 커밋은 [taskbar-design.md](taskbar-design.md)의 결정 6·범위 밖 "숨은 주 창을 대신하는 표시"를 이 문서로 잇는다. 진행률은 앱이 `tooltip`에, 배지 숫자는 앱이 그린 `icon`에 싣는다. [os-notification-design.md](os-notification-design.md) 결정 1의 길 c(풍선)도 이 문서의 "풍선" 규칙으로 잇는다. 앱 식별은 읽지 않는다(결정 3).

**줄 3 menu_bar.** 이 노드는 `ui_interaction.*`·`window_surface.cpp`·`surface_input.cpp`를 고치지 않는다(길 a). `menu_item_config`는 읽기만 한다. menu_bar가 그 구조체에 칸(밑줄 글자 등)을 더하면 통합 때 `make_tray_menu`가 그 칸을 `&` 표기로 옮길지 정한다. 그 전까지 글의 `&`는 `&&`로 바꾼다.

**검사기·기본 문구.** 새 요소가 없어 `ui_element_id`의 kind, `inspect_names.cpp`의 case, `default_text`의 함수를 더하지 않는다.

**데모 공용 파일.** `examples/demo_main.cpp`는 `frame->taskbar` 다음 줄에 `frame->tray = windows_.make_tray_request();`, `make_jump_list_message` 뒤에 `make_tray_message` override 하나다. `examples/demo/windows_page.*`는 `add_jump_list_section` 뒤에 `add_tray_section`을 둔다. hotkey도 같은 파일을 고친다. 라이브러리 커밋과 데모 연결 커밋을 나누고, 데모 연결은 통합 때 직렬로 쌓는다. `docs/README.md` 색인은 통합 때 붙인다.

## 확인

- `tests/tray_sync_tests.cpp`(core, `luil_core_tests`): 비어 있음은 호출 없음. 처음 실으면 더하기와 판 4. 같은 요청은 호출 없음. 툴팁만·그림만 바뀌면 그 칸의 바꾸기. 밝기·고대비가 바뀌어 고른 그림이 바뀌면 바꾸기, 같은 그림으로 물러서면 없음. 아이콘 크기가 바뀌면 바꾸기. 빼면 지우기. 더하기 실패는 `waiting`이고 다음 요청 변화에는 다시 더하지 않으며 `shell_restarted`에서 다시 더한다. 시간 초과 실패는 바꾸기를 한 번 해 보고, 그것도 실패하면 다음 요청 변화에서 다시 더한다. 재시작 때 요청이 없으면 호출 없음. 바꾸기 실패를 같은 요청으로 다시 하지 않음.
- `tests/tray_icon_tests.cpp`(새 파일, `luil_tests`): 가짜 `Shell_NotifyIconW`로 `NOTIFYICONDATAW`의 `cbSize`·`hWnd`·`uID`·플래그·콜백 번호·`uVersion`을 본다. 툴팁 127 자르기가 서로게이트 쌍을 가르지 않는지, 잘못된 UTF-8이 제목으로 물러서는지. `tray_event_of`의 표(다른 `uID`와 모르는 사건은 없음, 좌표). 겹친 `NIN_KEYSELECT` 버리기. `make_tray_menu`를 `GetMenuItemInfoW`로 되읽어 글·`&&`·탭 단축키·흐림·체크·라디오·구분선·하위 메뉴·번호 → 키를 본다. 숨은 창에 진짜 `tray_callback_message`를 보내 `activate`에 닿는지(창 프로시저 길만). 빈 그림이면 창 아이콘 리소스를 그 크기로 읽는지.
- `tests/window_messages_tests.cpp`(추가): +7이 다른 번호와 다르다. `tests/device_request_tests.cpp`의 기본 능력 단언에 `tray == false`를 더한다.
- `tests/windows_page_tests.cpp`(추가): 알림 영역 구역의 줄 이름, 켜기·끄기·그림 바꾸기가 요청에 닿는지, 사건 intent와 보고 intent가 줄에 닿는지.
- 데모 `examples/demo/windows_page.cpp`의 "알림 영역" 구역: 단추 `트레이 아이콘 켜기`·`트레이 아이콘 끄기`·`트레이 그림 바꾸기`(`make_rgba_image`로 지은 두 색 원). 메뉴는 `창 보이기`·`주 창 숨기기`·`끝내기`(`ui_command::application_exit`)이고 숨기기는 보고가 `shown`일 때만 켠다. label 넷이 UIA로 읽힌다: 능력 `트레이 능력: 있음`, 요청 `트레이 요청: 켬 · 그림 1 · 메뉴 있음`, 반영 `트레이 반영: 보임`(`대기 중`·`끔`·`실패 (<진단>)`), 사건 `트레이 사건: 고름 · 키보드`(`두 번 클릭`, `메뉴: 창 보이기`).
- `examples/mobile_demo/device_page.cpp`는 `capabilities.tray == false`일 때 `tray-none`("이 플랫폼에는 알림 영역 아이콘이 없다. 알림은 OS 알림이 맡는다.")을 보인다. `tests/mobile_demo_page_tests.cpp`(core)가 이름을 읽는다.
- 격리 데스크톱에는 알림 영역이 없을 것으로 보여(추정) UIA 되읽기는 요청과 `대기 중`까지다. 실제 셸 호출을 하는 live test는 두지 않는다.
- 사람 확인: 아이콘이 서는지(Windows 11 넘침 칸 포함)와 툴팁. 밝은·어두운 작업 표시줄에서 그림이 바뀌는지, 고대비, 주 모니터 100%·150%·200%와 혼합 배율. 클릭·두 번 클릭·오른쪽 클릭, 키보드(Win+B → 화살표 → Enter·Space·Shift+F10)와 `NIN_KEYSELECT`가 몇 번 오는지. 메뉴가 알맞은 모니터에 서고 바탕 화면·다른 앱을 누르면 닫히는지. 주 창을 숨긴 채 `창 보이기`가 앞으로 서는지와 `끝내기`. 탐색기를 다시 시작한 뒤 아이콘이 돌아오는지(주 창을 보일 때와 숨겼을 때 모두). 관리자 권한으로 띄운 앱의 사건. 끝낸 뒤 유령 아이콘이 없는지. 내레이터가 툴팁과 메뉴 항목을 읽는지.

## 범위 밖

- luil 테마 메뉴(결정 1의 길 b)와 화면 좌표 앵커 popup.
- 여러 아이콘(결정 6), `NIF_GUID`(결정 3).
- 풍선(`NIF_INFO`)과 풍선 사건. 알림은 [OS 알림](os-notification-design.md)이다.
- 툴팁 대신 앱이 그리는 풍부한 창(`NIN_POPUPOPEN`), 아이콘 깜빡임·움직임. 앱은 그림을 바꿔 상태를 보인다.
- Windows 11 넘침 칸에서 아이콘을 꺼내는 일. 공개 길이 없다.
- 닫으면 트레이로 숨기는 라이브러리 정책(결정 8).
- Android 상태 표시줄 아이콘.
