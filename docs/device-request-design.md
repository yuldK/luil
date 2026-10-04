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

## 검증

| 대상 | 확인 |
| --- | --- |
| 능력 플래그 | 기본값이 전부 거짓, host가 정한 값의 왕복 (core test) |
| 방향 값 변환 | 시스템은 되돌림, 세로·가로는 사용자 설정을 따르는 값, 휴대폰과 가로 태블릿의 네 회전, 읽지 못한 회전 (core test) |
| frame 기본값 | `ui_frame::device`의 기본이 "시스템을 따름", 막대 보임, 컷아웃 비킴, 볼륨 키는 시스템 (core test) |
| 키 필터 | 받겠다는 값마다 뒤로 가기·볼륨·시스템 키의 결과, 볼륨 키의 이름과 반복, 뗌 (기기 test) |

기기 확인은 [Android 이식 계획](android-port-plan.md)의 "기기 요청" 절에 적는다.
