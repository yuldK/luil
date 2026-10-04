# 기기 요청과 플랫폼 능력

앱이 휴대폰·태블릿에 바라는 것(화면 방향 등)을 플랫폼을 가리지 않는 값으로 말하는 계약이다.
요청은 [`ui_frame::device`](../include/luil/app/app_host.h)의 `device_request`에 싣고, 플랫폼이 그중
무엇을 실제로 하는지는 [`ui_platform::capabilities`](../include/luil/ui/ui_platform.h)가 답한다.

## 공통 계약

### 요청은 frame의 앱 상태다

`device_request`는 frame마다 실리는 값이다. 앱은 자기 모델에 "지금 바라는 것"을 두고 매 frame 그대로
싣는다. UI thread는 직전에 플랫폼에 옮긴 값과 견줘 **바뀐 것만** 옮긴다.

- **기본값은 전부 "플랫폼에 맡긴다"다.** 이 값을 건드리지 않는 앱은 동작이 같다. Android host는 기본값에서
  시작하므로, 아무것도 싣지 않은 앱의 매니페스트 방향·막대·키 처리를 건드리지 않는다.
- **마지막 frame이 이긴다.** 요청이 여러 번 바뀌어도 UI thread가 보는 것은 최신 frame 하나다.
- **어느 thread에서든 바꿀 수 있다.** frame은 logic thread가 짓지만, 다른 thread는
  `app_host::post_app_message`(thread 안전)로 상태를 바꾸는 메시지를 보낸다. 실제 플랫폼 호출은 UI thread가
  하고, Android의 창 조작은 그 thread가 다시 Java 메인 thread로 넘긴다 (`post_to_main_thread`).
- **상태라서 저절로 다시 선다.** 홈에 갔다 오거나 회전해도 다음 frame이 같은 값을 다시 싣는다. Activity가
  다시 만들어지면(언어 바꿈 등) 새 host가 기본값에서 시작해 첫 frame의 요청을 처음부터 옮긴다.

`app_host`에 setter를 두지 않은 이유는 셋이다. 뒤로 가기(`ui_frame::back`)가 이미 frame에서 오고, setter로
두면 Activity가 다시 설 때 앱이 다시 불러야 하며, "마지막 요청만 유효"를 위해 별도의 최신 값 slot이 필요하다.
frame은 그 셋을 이미 다 한다.

### 능력 플래그

`ui_platform::capabilities`는 host가 시작할 때 한 번 정한다. 기본값은 전부 거짓이다.

| 플래그 | 뜻 | Windows | Android |
| --- | --- | --- | --- |
| `screen_orientation` | 화면 방향 요청을 옮긴다 | 거짓 | 참 |
| `system_bars` | 시스템 막대를 숨기고 컷아웃 밑까지 그린다 | 거짓 | 참 |
| `volume_keys` | 볼륨 올림·내림을 앱에 준다 | 거짓 | 참 |
| `haptics` | 햅틱을 낸다 | 거짓 | 참 |

하지 못하는 플랫폼도 요청을 **받는다.** 받고 아무 일도 하지 않는다. 그래서 앱은 플랫폼마다 frame을 다르게
지을 필요가 없고, 설정 화면에 그 항목을 둘지만 플래그로 고른다 (mobile demo의 기기 페이지).

## 화면 방향

`device_request::orientation`은 `screen_orientation`이다.

| 값 | 뜻 | Android `setRequestedOrientation` |
| --- | --- | --- |
| `system` (기본) | 시스템을 따른다 | Activity가 처음 가졌던 값 (매니페스트) |
| `portrait` | 세로 | `USER_PORTRAIT` (12) |
| `landscape` | 가로 | `USER_LANDSCAPE` (11) |
| `current` | 요청이 처음 실린 순간의 방향에 고정 | 지금 회전으로 고른 고정 값 |

- **`system`은 미지정(-1)이 아니다.** 앱이 매니페스트에 `screenOrientation`을 두었으면 그것으로 되돌린다.
  처음 바꾸기 직전에 `getRequestedOrientation()`을 읽어 둔다.
- **세로·가로는 사용자 회전 설정을 따른다** (`USER_*`). 자동 회전이 켜져 있으면 반대쪽으로도 돌고, 꺼져
  있으면 기본 쪽에 선다. 사용자가 자동 회전을 끈 까닭을 앱이 덮지 않는다.
- **`current`는 회전을 읽어 고정 값으로 바꾼다.** `Display.getRotation()`(0°·90°·180°·270°)과 지금 구성이
  가로인지를 함께 본다. 같은 회전이 자연 방향(휴대폰은 세로, 태블릿은 흔히 가로)에 따라 다른 방향이라서다.

  | 구성 | 0° | 90° | 180° | 270° |
  | --- | --- | --- | --- | --- |
  | 가로 | `LANDSCAPE` | `LANDSCAPE` | `REVERSE_LANDSCAPE` | `REVERSE_LANDSCAPE` |
  | 세로 | `PORTRAIT` | `REVERSE_PORTRAIT` | `REVERSE_PORTRAIT` | `PORTRAIT` |

  자연 방향이 세로인 휴대폰에서는 90°가 가로(`LANDSCAPE`), 자연 방향이 가로인 태블릿에서는 90°가
  세로(`REVERSE_PORTRAIT`)가 되는데 표 하나가 둘 다 맞힌다. 회전을 읽지 못하면 시스템의 "지금 회전에
  고정"(`LOCKED`, 14)으로 물러선다. `LOCKED`를 처음부터 쓰지 않은 이유는 그것이 값이 아니라 "디스플레이의
  마지막 회전"이라서다. 다른 앱이 화면을 가로로 돌려 놓은 뒤 돌아오면 그 회전에 다시 묶인다. 고른 방향을
  값으로 들고 있어야 돌아왔을 때도 같은 방향이다.
- **고정한 방향은 요청이 이어지는 동안 그대로다.** 다른 값을 실었다가 다시 `current`를 실으면 그때의 방향을
  새로 읽는다. 창이 다시 생기거나 초점을 되찾는 것으로는 다시 읽지 않는다.
- 호출은 메인 thread에서 한다 ([android_window_control.cpp](../src/android/android_window_control.cpp)).
  값 변환은 JNI 없는 순수 함수라 Windows의 core test도 돌린다
  ([device_request.cpp](../src/android/device_request.cpp)).
- Windows는 하지 않는다. 데스크톱 창에는 기기 방향이 없다.

방향이 바뀌면 표면 크기가 바뀌고, 그것은 기존 경로(`APP_CMD_CONFIG_CHANGED` → 창 metrics 메시지)로 앱에
간다. 매니페스트의 `configChanges`에 `orientation|screenSize`가 있어 Activity는 다시 만들어지지 않는다.

## 시스템 막대와 컷아웃

`device_request::system_bars`는 `system_bars_mode`, `draw_under_cutout`은 bool이다.

| 값 | 뜻 |
| --- | --- |
| `visible` (기본) | 막대가 보이고 안전 영역이 막대를 비킨다 |
| `immersive` | 막대를 숨긴다. 가장자리에서 쓸면 막대가 내용 위에 잠시 나타났다가 다시 숨는다 |
| `draw_under_cutout` 참 | 디스플레이 컷아웃(카메라 구멍)이 덮는 가장자리까지 그린다 (기본은 비킨다) |

- Android 11부터 `WindowInsetsController.hide(systemBars())`와
  `BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE`, 그 아래(minSdk 26~29)는 `SYSTEM_UI_FLAG_IMMERSIVE_STICKY`와
  `HIDE_NAVIGATION`·`FULLSCREEN`이다. 아래쪽 경로는 막대 아이콘 색의 플래그와 같은 칸이라 몰입 플래그만 바꾼다.
- **숨긴 막대는 안전 영역에서 뺀다.** 요청이 바뀌는 순간 다시 재어 기존 창 metrics 메시지로 알린다. 다시 보인
  막대는 시스템이 알려 오는 inset 그대로 센다.
- **몰입을 요청한 동안에는 막대 inset을 아예 세지 않는다.** 쓸어 꺼낸 막대는 원래 inset이 0이지만, 화면을
  켜거나 알림 창에서 돌아오는 순간 시스템이 막대를 실제로 잠깐(실기기에서 20~120ms) 다시 보였다가 host가 도로
  숨기는 일이 있다. 시스템이 알려 오는 대로 세면 그때마다 내용이 한 번 튄다. 컷아웃과 키보드는 그대로 비킨다.
- **다시 숨긴다.** 시스템이 다른 창에 다녀오며 막대를 다시 보일 수 있어 창이 생길 때(`INIT_WINDOW`),
  돌아올 때(`RESUME`), 초점을 되찾을 때(`GAINED_FOCUS`) 몰입을 다시 건다. 같은 값을 거듭 걸어도 해가 없다.
- **컷아웃은 창 장식이 아니라 안전 영역의 셈이다.** 참이면 컷아웃 inset을 비키지 않는다. 창이 컷아웃까지
  펼쳐져 있어야 뜻이 있다. 예제 테마는 `windowLayoutInDisplayCutoutMode=always`이고, targetSdk 35부터는
  시스템이 창을 화면 끝까지 편다. 막대와 따로 정하는 이유는 가로 화면의 옆 컷아웃이 막대가 보일 때도 비어 있기
  때문이다.
- 뒤로 가기·IME·popup은 그대로다. 몰입 중에도 키보드가 올라오면 아래 가장자리가 키보드만큼 준다.
- Windows는 하지 않는다. 데스크톱의 전체 화면 창(`ui_command::window_toggle_fullscreen`)과는 별개다.

## 볼륨 키

`device_request::volume_keys`가 참인 동안 볼륨 올림·내림이 시스템이 아니라 앱에 간다. 쪽을 넘기는 콘텐츠
화면에서 사용자가 켜는 기능이다. 기본은 거짓이라 시스템이 소리 크기를 바꾼다.

- **뒤로 가기와 같은 방식이다.** `ui_frame::back`처럼 frame의 앱 상태이고, host가 그것을 원자 값으로 옮겨
  GameActivity의 키 필터(Java thread)가 읽는다. 받을 화면에 있는 동안만 싣는다.
- **이름 있는 키다.** `key_code::volume_up`·`volume_down`으로 와서 다른 키처럼 controller와
  `interaction_policy::on_key`를 지난다. Windows 키보드의 볼륨 키는 지금처럼 플랫폼 대역
  (`platform_key_code(VK_VOLUME_*)`)으로 온다 — 이름 키로 옮기면 그 값을 이미 받던 앱이 보는 것이 바뀐다.
- **누르고 있으면 이어 온다.** 반복은 `key_pressed_event::repeat`가 참인 누름이다. luil에는 키 뗌 이벤트가
  없어 뗌은 앱에 가지 않는다. 받기로 한 키의 뗌은 시스템에도 가지 않는다 (누름과 뗌이 갈리지 않게 둘 다 앱이
  가진다).
- **보이지 않으면 놓는다.** 필터는 "앱이 받겠다고 했고 Activity가 보이는 중"(`APP_CMD_START`~`STOP`)일 때만
  참이다. Activity가 사라지면 거짓으로 돌린다. 백그라운드의 키는 원래 우리 Activity에 오지 않지만, 필터의
  값이 다음 Activity로 새지 않게 둔다.
- **범위 밖의 키는 늘 시스템의 것이다.** 음소거·미디어·헤드셋·카메라·전원·홈·앱 전환이다. 키 판정은 순수
  함수(`app_takes_key`)라 기기 test가 받겠다는 값마다 결과를 확인한다.
- Windows는 하지 않는다. 능력 플래그가 거짓이고 키보드의 볼륨 키 동작은 그대로다.

## 햅틱

햅틱은 요청이 아니라 사건이라 frame에 싣지 않는다. 뜻(`haptic_feedback`)만 말하고 진동의 모양은 플랫폼이 고른다.

| 뜻 | 쓰는 때 | Android 상수 (API 34 이상 / 30 이상 / 그 아래) |
| --- | --- | --- |
| `long_press` | 길게 눌러 메뉴가 열렸다 | `LONG_PRESS` |
| `drag_start` | 끌기를 시작했다 | `DRAG_START` / `GESTURE_START` / `LONG_PRESS` |
| `tick` | 경계·눈금에 닿았다 | `SEGMENT_TICK` / `CLOCK_TICK` / `CLOCK_TICK` |
| `confirm` | 동작이 받아들여졌다 | `CONFIRM` / `CONFIRM` / `VIRTUAL_KEY` |
| `reject` | 동작이 거절됐다 | `REJECT` / `REJECT` / `LONG_PRESS` |

- **출력은 host가 꽂는다.** `app_host::config::haptic`은 thread 안전한 함수다. Android host는 기기의 API 수준에
  맞는 상수를 골라 메인 thread에서 decor view의 `performHapticFeedback(상수)`를 부른다. 플래그를 주지 않아
  사용자의 터치 피드백·진동 설정을 따른다 — 껐으면 시스템이 `ignored_for_settings`로 버린다. Windows는 출력이
  없다.
- **두 길로 낸다.** element 액션은 `haptic_request`를 다른 액션과 함께 돌려준다 (input thread가 출력으로 바로
  넘기고 앱 inbox에는 넣지 않는다). logic thread나 그 밖의 thread는 `app_host::perform_haptic`을 부른다 (쪽이
  넘어감, 끌어 놓기 완료처럼 앱 상태에서 정해지는 순간).
- **길게 누르기는 controller가 낸다.** 길게 누르기로 우클릭 액션이 실제로 나가는 세 자리 — 누르고 있는 동안
  (`advance`), pump보다 뗌이 먼저 온 탭과 손잡이 — 가 같은 함수(`run_long_press`)를 써서, 우클릭이 무언가를
  냈을 때만 그 앞에 햅틱을 하나 붙인다. 대상에 우클릭 액션이 없거나, 허용치를 넘어 자격을 잃었거나, 우클릭이
  아무것도 내지 않으면 울리지 않는다. 마우스·펜의 우클릭은 몸짓이 아니라 울리지 않는다.
- **끄는 설정은 `touch_gesture_config::long_press_haptic`이다.** 기본값은 플랫폼마다 다르다.

  | 플랫폼 | 기본 | 까닭 |
  | --- | --- | --- |
  | core·Windows (`interaction_config`, `window_config`) | 끔 | Windows에는 햅틱 출력이 없다. 켜도 아무 일이 없지만 controller의 액션 목록을 바꾸지 않는다 |
  | Android (`application_config::touch`) | 켬 | 길게 눌러 메뉴가 열릴 때의 짧은 진동은 Android의 관례다. 사용자가 시스템에서 터치 피드백을 끄면 울리지 않는다 |

  Android에서는 새 기능을 쓰지 않는 앱도 길게 누르기에 진동이 생긴다. 다른 기본값은 모두 "지금과 같음"인데
  이것만 다른 이유는 진동이 없는 것이 그 플랫폼의 관례를 어기는 쪽이기 때문이다. 앱이 끄려면
  `application_config::touch.long_press_haptic`을 거짓으로 둔다.

## 창 폭 등급

배치를 기기 방향이 아니라 창 폭으로 고르면 휴대폰 가로, 화면 분할, 자유 창, 접는 기기, 태블릿, 데스크톱 창을 한
규칙으로 다룬다. [`classify_window_width`](../include/luil/ui/layout_metrics.h)는 논리 폭(Android의 dp)을 등급으로
옮기는 순수 함수다.

| 등급 | 기본 경계 | 흔한 자리 |
| --- | --- | --- |
| `compact` | 600 미만 | 휴대폰 세로, 좁게 나눈 화면 |
| `medium` | 600 이상 840 미만 | 휴대폰 가로, 펼친 접는 기기, 작은 태블릿 |
| `expanded` | 840 이상 | 태블릿 가로, 데스크톱 창 |

- 경계는 인자(`window_width_breakpoints`)로 바꾼다. 폭이 유한하지 않으면 compact이고, 경계가 뒤집히면 expanded는
  서지 않는다.
- **창 metrics에 싣는다.** host는 `app_delegate::make_metrics_message(const window_metrics&)`를 부르고, 그
  `width_class`는 앱이 배치할 폭(Android는 안전 영역)을 배율로 나눈 논리 폭의 등급이다. 기본 구현은 기존의 세
  값짜리 `make_window_metrics_message`를 불러 그것만 넓힌 앱은 그대로 돈다. 이름을 달리한 것은 같은 이름의 가상
  함수 하나만 넓히면 나머지가 가려지기 때문이다 (clang의 `-Woverloaded-virtual`도 그 자리를 잡는다).
- Windows에서도 같다. 창 client 폭을 DPI 배율로 나눈 논리 폭이다. 플래그가 필요 없다.

## 안전 영역 밖

Android에서 앱은 안전 영역(상태 표시줄·제스처 막대·컷아웃을 뺀 자리)에 배치한다. 내용은 그 안에 있어야 하지만, modal의 scrim과
시트 바탕은 화면 끝까지 닿아야 한다 — 그렇지 않으면 scrim을 띄워도 상태 표시줄 줄이 밝게 남고 아래 시트 밑에 제스처 막대 높이의
띠가 남는다.

- **창 metrics에 싣는다.** `window_metrics::outside`는 앱 자리에서 표면 끝까지의 거리다 (물리 픽셀, 왼·위·오른·아래). Android
  host가 지금 재는 안전 영역 값을 싣고 Windows는 0이다. 크기가 같아도 이 값이 바뀌면 다시 알린다 (막대 숨김, 돌림).
- **배치 문맥으로 넘긴다.** `window_surface(metrics)`가 root를 (0, 0)에 둘 때의 표면 전체를 tree 좌표로 돌려준다 (세로
  1080×2199 자리에 위 75·아래 42면 `(0, −75, 1080, 2316)`). 앱이 overlay를 배치할 때 `arrange_context::surface`로 넘긴다.
  **넘기지 않은 앱은 지금과 같다.**
- **그리기만 늘린다.** modal dialog와 시트의 scrim은 자리 밖의 띠(`bands_outside`)도 같은 색으로 칠하고, 시트 바탕은 자리
  가장자리에 닿은 변만 표면 끝까지 늘린다 ([시트](sheet-design.md#안전-영역-밖)). 손잡이·내용·누르는 자리는 그대로 안전
  영역 안이다. 상태 표시줄과 제스처 막대 위의 손짓은 시스템의 것이다.
- **상태 표시줄 아이콘 색은 바꾸지 않는다.** 밝은 테마에서 scrim이 덮으면 상태 표시줄 바탕이 248에서 168로 어두워지고,
  어두운 아이콘과의 대비는 약 7 : 1이다. 시트를 열고 닫을 때마다 아이콘 색이 바뀌는 깜빡임도 생기지 않는다.
- 앱 바(위 막대)를 상태 표시줄 밑까지 칠하는 데에도 같은 값을 쓸 수 있지만, 이번에는 열어 두기만 했다.

## 권한

같은 네트워크(LAN)의 기기 — 집 안 NAS의 SMB나 http — 에 붙는 앱은 Android 17부터 사용자에게 권한을 받아야 한다.
targetSdk 37 이상인 앱은 이것 없이 로컬 주소로 가는 TCP 연결이 막히고, 막힌 연결은 시간 상한까지 기다리다
실패한다. 프레임 요청과 달리 사용자의 답을 기다리는 한 번의 일이라 `app_host`의 함수로 둔다.

- **묻기와 조회.** `app_host::request_permission(app_permission::local_network)`이 묻고,
  `app_host::permission(...)`이 지금 상태(`not_required`·`granted`·`denied`)를 답한다. 둘 다 아무 thread에서 부를
  수 있다. 묻기는 답을 기다리지 않고 돌아온다. 참이면 물었고 답은 `app_delegate::make_permission_message`로 앱
  메시지가 되어 온다. 거짓이면 묻지 않았다 — 필요 없거나 이미 받았거나 묻는 길이 없다.
- **언제 묻나.** 기기와 앱의 targetSdk가 둘 다 37 이상일 때만 `ACCESS_LOCAL_NETWORK`를 묻는다
  (`android_permission_name`). 그 아래는 `INTERNET`이 로컬 주소까지 열어 주므로 `not_required`다. Windows는 늘
  `not_required`다. 앱의 매니페스트에 `<uses-permission android:name="android.permission.ACCESS_LOCAL_NETWORK" />`가
  있어야 한다 (mobile demo는 flavor 매니페스트에 둔다). 그 아래 판의 기기는 모르는 이름이라 무시한다.
- **Java 없이 답을 받는다.** 묻기는 JNI로 `Activity.requestPermissions`를 부른다. 답은 Activity의
  `onRequestPermissionsResult`로 오는데 그것을 받으려면 Java 클래스가 있어야 하므로 받지 않는다. 대신 host가
  상태를 다시 읽는다.
  - 대화상자가 뜨면 창이 초점을 잃는다. 사용자가 답해 초점을 되찾는 순간(`APP_CMD_GAINED_FOCUS`) 다시 읽은
    상태가 답이다.
  - 이미 "다시 묻지 않음"으로 거절한 권한처럼 시스템이 대화상자 없이 답하면 초점을 잃지 않는다. 물은 뒤 1초가
    지나도록 초점이 그대로면 그때 다시 읽은 상태를 답으로 알린다.
  - 설정 앱에서 권한을 바꾸고 돌아오면(`APP_CMD_RESUME`) 상태를 다시 읽는다. 묻는 중이 아니었으면 메시지는 없다.
- **Android 16의 시험 스위치는 따르지 않는다.** Android 16은 `adb shell am compat enable RESTRICT_LOCAL_NETWORK
  <패키지>`(재부팅 필요)로 같은 차단을 시험할 수 있지만, 그때 보는 권한은 `NEARBY_WIFI_DEVICES`라 실제 기기의
  동작과 다르다. 그래서 Android 16 기기에서는 `not_required`로 답한다.

## 검증

| 대상 | 확인 |
| --- | --- |
| 능력 플래그 | 기본값이 전부 거짓, host가 정한 값의 왕복 (core test) |
| 방향 값 변환 | 시스템은 되돌림, 세로·가로는 사용자 설정을 따르는 값, 휴대폰과 가로 태블릿의 네 회전, 읽지 못한 회전 (core test) |
| frame 기본값 | `ui_frame::device`의 기본이 "시스템을 따름", 막대 보임, 컷아웃 비킴, 볼륨 키는 시스템 (core test) |
| 키 필터 | 받겠다는 값마다 뒤로 가기·볼륨·시스템 키의 결과, 볼륨 키의 이름과 반복, 뗌 (기기 test) |
| 길게 누르기 햅틱 | 메뉴가 열리는 세 자리에서 한 번, 짧은 탭·우클릭 없는 대상·빈 우클릭·자격 잃은 접촉·꺼진 설정·마우스에서는 없음 (controller test) |
| 창 폭 등급 | 600·840 경계의 양쪽, 바꾼 경계, 뒤집힌 경계, 유한하지 않은 폭, 물리 폭과 배율로 지은 metrics, 세 값짜리만 넓힌 delegate로 이어지는 기본 구현 (core test) |
| 안전 영역 밖 | 자식 문맥이 표면을 잇는다, 바깥 띠의 모양, 세로·가로 Android와 Windows의 표면 사각형 (core test). scrim 띠와 늘린 시트 바탕의 픽셀, 표면을 모를 때의 창 바탕 (raster test) |
| 권한 | 기기·targetSdk 판에 따른 권한 이름(37·37만 묻는다, 읽지 못한 targetSdk), 묻는 길이 없으면 `not_required`·묻지 않음, 플랫폼 조회·묻기를 아무 thread에서, 플랫폼 예외는 `denied`·묻지 않음 (core·host test) |
| 햅틱 출력 | 다른 thread의 `perform_haptic`과 input thread의 `haptic_request`가 출력에 닿고 앱 inbox에는 가지 않음, 출력이 없으면 아무 일도 없음 (host test). 뜻별 상수와 API 수준별 물러섬 (core test) |

기기 확인은 [Android 이식 계획](android-port-plan.md)의 "기기 요청" 절에 적는다.
