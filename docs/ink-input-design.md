# 잉크 입력

펜의 압력·기울기·지우개 표식을 포인터 이벤트에 싣고, 합쳐 온 과거 표본을 하나씩 건넨다. 지우개 끝의 접촉을 삼키는 자리는 추적기에서 컨트롤러로 옮긴다. 일반 컨트롤은 지금처럼 지우개를 삼키고 [잉크 캔버스](ink-design.md)만 받는다.

공개 형식은 [ui_events.h](../include/luil/ui/ui_events.h)의 `pen_sample`, `pointer_moved_event`·`pointer_pressed_event`·`pointer_released_event`의 마지막 필드 `pen`, `pointer_cancelled_event`의 마지막 필드 `button_switch`다. 이 문서는 [터치 제스처와 펜 입력](touch-pen-input-design.md)을 고칠 계획도 담는다.

## 문제

- 공개 이벤트에 펜 값이 없다. `pointer_moved_event`·`pressed`·`released`(ui_events.h 76~146행)는 장치·포인터 id·접촉·수식 키·배율뿐이다.
- Win32는 펜 값을 읽고 버린다. `handle_pointer_message`(window_surface.cpp 1152행)가 `GetPointerPenInfo`로 `POINTER_PEN_INFO`를 복사하지만(1171행) `pointer_sample`로 옮기는 것은 배럴과 지우개뿐이다(1282~1283행). 합쳐 온 과거 표본(`GetPointerPenInfoHistory`)은 읽지 않는다.
- Android는 축을 켜지 않는다. `copy_motion`(android_app.cpp 199행)은 x·y와 휠 값만 옮기고, `start`는 `GameActivityPointerAxes_enableAxis`로 휠 축 둘만 켠다(653~654행). GameActivity는 켠 축만 옮긴다. `motion_pointer`(android_input.h 13행)는 id·도구·x·y뿐이다.
- 지우개는 추적기가 통째로 삼킨다. `pointer_sequence_tracker::accept`의 누름(pointer_sequence.cpp 44행), 취소(72행), 이동(76행), 뗌(105행)과 `cancel`(146행)이 지우개 접촉이면 아무 이벤트도 내지 않는다. 그 아래의 컨트롤러는 지우개를 볼 수 없다.
- 접촉 중 배럴 전환의 취소와 시스템 취소가 같은 `pointer_cancelled_event`다(pointer_sequence.cpp 82행). 그리던 획을 남길지 버릴지 가를 수 없다.

## 공개 형식

```cpp
// include/luil/ui/ui_events.h: 펜 하나의 표본 값이다. 펜이 낸 이동·누름·뗌만 싣는다.
//  - pressure는 0~1이다. 장치가 알리지 않으면(Win32 `PEN_MASK_PRESSURE`가 없다) 비어 있다.
//  - 기울기는 도(-90~90)이고 W3C Pointer Events의 tiltX·tiltY와 같다. 오른쪽·사용자 쪽으로 눕힌 것이 양수다.
//  - eraser는 지우개 끝이다. hover에도 실린다. 접촉이면 일반 컨트롤은 실행하지 않는다.
struct pen_sample
{
    std::optional<float> pressure {};
    float tilt_x { 0.0f };
    float tilt_y { 0.0f };
    bool eraser { false };

    [[nodiscard]] bool operator==(const pen_sample&) const noexcept = default;
};

// pointer_moved_event·pointer_pressed_event·pointer_released_event의 마지막 필드다.
// 앞 필드만 채운 기존 초기화가 그대로 마우스이고 펜 값이 없다.
std::optional<pen_sample> pen {};

// pointer_cancelled_event의 마지막 필드다. 접촉 중 배럴 버튼이 바뀌어 거둔 취소다.
// 바로 뒤에 같은 포인터의 새 누름이 온다.
bool button_switch { false };
```

비공개 형식.

- `pointer_sample`(src/host/pointer_sequence.h)의 끝에 `std::optional<float> pressure`·`float tilt_x`·`float tilt_y`를 더한다. 추적기는 펜 표본이면 세 이벤트에 `pen`을 채운다.
- `motion_pointer`(src/android/android_input.h)의 끝에 `pressure`·`tilt`·`orientation`(Android 축 값 그대로)을 더한다. 극좌표 기울기를 옮기는 순수 함수 `pen_tilt_of(tilt, orientation)`도 같은 헤더에 둔다(host test가 식을 잠근다).
- `src/win32/surface_input.h`에 순수 도우미 둘을 더한다. `pen_values_of(const POINTER_PEN_INFO&)`는 마스크를 보고 압력(0~1024를 0~1로)과 기울기를 옮겨 `pen_values`로 돌려준다. `pen_history_order(entries, last_frame)`는 과거 표본 중 보낼 것의 자리(0번 현재 표본을 뺀, 오래된 것부터)를 돌려준다. frameId는 32비트에서 돌므로 차이를 부호 있는 값으로 본다.

## 규칙

값.

- 압력은 0~1로 접는다. 접기는 추적기가 한 곳에서 한다. Android의 1을 넘는 값도 1이다. 마우스·터치는 `pen`이 없다. 펜 hover의 압력은 0이다(압력을 모르는 장치는 hover도 비어 있다).
- Win32 기울기는 `tiltX`·`tiltY`(도)를 그대로 옮긴다. 마스크가 없으면 0이다. `rotation`(펜 축 회전)은 옮기지 않는다.
- Android 기울기는 극좌표(`AXIS_TILT` θ, `AXIS_ORIENTATION` φ)를 W3C의 두 축으로 옮긴다. `tilt_x = atan(tan θ · sin φ)`, `tilt_y = atan(−tan θ · cos φ)`이고 θ가 90도면 ±90이다. 식은 host test가 잠그고 부호는 실기기에서 사람이 본다.

과거 표본.

- 표본 하나가 이동 이벤트 하나다. Android가 이미 그렇다(android_input.cpp 195~197행). Win32도 같은 순서를 지킨다.
- Win32는 luil이 쥔 펜 접촉의 `WM_POINTERUPDATE`에서 `POINTER_INFO::historyCount`가 1보다 크면 `GetPointerPenInfoHistory`를 부른다. 결과는 최근 것이 앞이라 뒤집어, 현재 표본 앞에 오래된 것부터 추적기에 넣는다. 표본마다 자기 `ptPixelLocation`·`PerformanceCount`(없으면 `dwTime`)로 자리와 시각을, 자기 마스크로 압력·기울기를 옮긴다. 버튼·수식 키는 현재 표본의 값이다.
- 포인터마다 마지막으로 보낸 `frameId`를 들고 그보다 이른 표본은 거른다. 기록 조회가 실패하면 현재 표본만 보낸다. 접촉을 취소하지 않는다.
- 웹뷰가 임자인 접촉은 지금처럼 원본(`historyCount` 포함)을 중계하고 기록을 읽지 않는다.

지우개 삼킴.

- 추적기는 지우개 접촉도 누름·이동·뗌·취소로 낸다. 표식은 `pen->eraser`다. 지우개인지는 누를 때 정하고 접촉 동안 바꾸지 않는다. 지우개 접촉에는 배럴 전환이 없다.
- 컨트롤러가 이벤트를 받는 맨 앞(`interaction_controller::process`의 첫 줄, tooltip 판정보다 앞)에서 지우개 누름의 임자를 가린다. 누른 자리가 지우개를 받는 잉크 캔버스(`ink()->eraser`)면 잉크 접촉이다. 아니면 그 접촉을 "삼킨 접촉"으로 적고 아무 상태도 바꾸지 않는다. 관성·터치 조작·초점·누름 표시·tooltip·policy의 `on_press`가 그대로다. 삼킨 접촉의 이동·뗌·취소도 hover를 바꾸지 않고 사라진다.
- 삼킨 접촉은 그 포인터의 뗌·취소에 잊는다. 같은 포인터의 새 누름도 새 접촉이라 잊는다. 끊긴 잉크 접촉의 나머지(아래 끊긴 접촉)도 같은 자리에 적고, 배럴 전환 뒤의 다시 누름만 함께 삼킨다. 마우스의 이동은 버튼을 싣지 않아 삼키지 않는다(hover로 흐른다).
- 표면 쪽은 바뀌지 않는다. popup 밖 누름(window_surface.cpp 1225행), 조합 확정(1223행), 웹뷰 중계는 지금도 추적기 앞에서 지우개에 돈다.

끊긴 접촉.

- 접촉 중 배럴이 바뀌면 추적기가 내는 취소에 `button_switch`를 단다(pointer_sequence.cpp 82행). 그 밖의 취소는 거짓이다.
- 입력 큐 유실(`cancel_dropped_gestures`, ui_interaction.cpp 4299행)과 `button_switch` 취소는 잉크 접촉을 받은 표본까지로 닫는다. 남은 이벤트는 삼킨다(결정 4). `button_switch` 뒤에 오는 같은 포인터의 오른쪽 다시 누름도 남은 이벤트라 우클릭이 되지 않는다.

## 결정

**1. 펜 값의 자리.** 질문: 이벤트 어디에 펜 값을 싣는가.
- 길 a: 세 이벤트 끝에 `std::optional<pen_sample> pen` 하나. 펜만 값이 있어 마우스·터치의 뜻이 흐려지지 않는다. 기존 초기화가 그대로 선다. 되는 것: 잉크 캔버스와 앱이 이벤트에서 압력·기울기·지우개를 읽는다.
- 길 b: 세 이벤트 끝에 낱 필드 넷(압력·기울기 둘·지우개). 되는 것: optional을 풀지 않고 읽는다. 대가: 필드 12개가 늘고, 마우스의 압력 0과 "모름"이 갈리지 않는다.
- 길 c: 새 raw 이벤트 종류(펜 이동·누름·뗌). 되는 것: 없다. 대가: `raw_input_event` variant가 늘어 방문하는 곳을 모두 고치고, R3 menu_bar가 같은 variant를 고친다. 펜 경로가 둘이 된다.
- 권장: a. 대가: 펜 값을 읽는 곳이 optional을 푼다. **권장대로 진행.**

**2. 과거 표본.** 질문: 합쳐 온 표본을 어떻게 건네는가.
- 길 a: 표본마다 이동 이벤트 하나. Android와 같고 컨트롤러가 고칠 것이 없다. 되는 것: UI thread가 바쁠 때도 빠른 획이 매끈하다. 대가: raw 입력 큐(`drop_oldest` 4096, app_host.cpp 78행)를 그만큼 더 쓴다.
- 길 b: 이동 이벤트 하나에 `std::vector`로 싣는다. 되는 것: 큐 사용량이 그대로다. 대가: 이동을 읽는 곳이 모두 기록을 알아야 하고, Android의 지금 변환을 되돌린다. 이벤트마다 할당이 생긴다.
- 길 c: 현재 표본만. 되는 것: 없다. 대가: UI thread가 바쁠 때 빠른 획이 각진다.
- 권장: a. 대가: 위의 큐 사용량이다. 펜은 접촉 하나라 큐를 채울 만큼은 아니다. **권장대로 진행.**

**3. 지우개를 삼키는 자리.** 질문: 일반 컨트롤이 지우개를 계속 삼키게 하면서 캔버스에는 어떻게 건네는가.
- 길 a: 컨트롤러가 삼킨다. 추적기는 표식만 단다. tree를 아는 곳이 임자를 가린다. 되는 것: 지우개를 받는 캔버스만 받고 일반 컨트롤은 지금처럼 삼킨다. 대가: 삼킴 규칙이 컨트롤러 누름 경로에 들어가고, 추적기 test와 Android test의 지우개 단언이 뒤집힌다.
- 길 b: 추적기가 삼키되 표면이 "이 자리는 캔버스"라고 알려 준다. 되는 것: 컨트롤러가 그대로다. 대가: UI thread가 tree에서 hit를 해야 해 thread의 몫이 섞인다.
- 길 c: 지우개만 새 이벤트 종류로 낸다. 되는 것: 없다. 대가: 결정 1의 c와 같다.
- 권장: a. 대가: 위의 test 이동이고, 잘못 가리면 일반 컨트롤이 실행된다. 단추·텍스트·확대 보기 위의 지우개 test가 막는다. **권장대로 진행.**

**4. 끊긴 접촉.** 질문: 큐 유실과 배럴 전환에서 그리던 획은 어떻게 되는가.
- 길 a: 받은 표본까지를 `end`에 `interrupted`로 닫고 남은 접촉을 삼킨다. 손잡이의 `on_release`가 유실을 알리는 것과 같다(ui_interaction.cpp 4306~4309행). 시스템 취소(손바닥 판정, 캡처 상실)는 `cancel`이다. 되는 것: 길게 쓴 획이 끝 몇 점만 잃고 남는다.
- 길 b: 다른 몸짓처럼 `cancel`. 되는 것: 규칙이 다른 몸짓과 같다. 대가: 길게 쓴 획이 통째로 사라진다.
- 권장: a. 대가: 끝 몇 점이 빠진 획이 남을 수 있고, `pointer_cancelled_event`에 필드 하나가 는다. **권장대로 진행.**

## 플랫폼

### Win32

- `window_surface::handle_pointer_message`만 고친다. 현재 표본은 이미 복사한 `input.pen`을 `pen_values_of`로 옮긴다. 과거 표본은 위 규칙대로 `GetPointerPenInfoHistory`를 부른다. 포인터별 마지막 `frameId`는 `pointer_owners_` 옆 멤버 하나다(window_surface.h).
- `win32_window.cpp`는 고치지 않는다. 줄 4에 내 달라고 할 것도 없다.
- 합성 입력과 원격 연결은 기록이 비거나 압력 마스크가 없을 수 있다. 그때는 현재 표본과 빈 압력이다.

### Android

- `copy_motion`이 현재 표본은 `GameActivityPointerAxes_getAxisValue`로, 과거 표본은 `GameActivityMotionEvent_getHistoricalAxisValue(event, axis, pointer, position)`로 `AMOTION_EVENT_AXIS_PRESSURE`·`TILT`·`ORIENTATION`을 읽는다. 인자 순서는 축이 먼저다(GameActivityEvents.h 196행).
- `start`가 휠 축 둘 옆에서 세 축을 켠다. 켜기는 프로세스 전체 설정이다.
- 지우개는 `AMOTION_EVENT_TOOL_TYPE_ERASER`(이미 `sample.eraser`), 손바닥 취소는 `FLAG_CANCELED`(이미 `canceled`)다. Java 소스·JNI·framework 호출이 없다. adb 주입은 압력과 기울기를 주지 못한다.

## 다른 노드와의 약속

- **image_clip(줄 4)**: `ui_events.h`에서 ink는 `pen_sample`을 포인터 이벤트 앞에 두고 세 이벤트와 `pointer_cancelled_event`의 끝에만 더한다. `clipboard_*`, `clipboard_request`, `input_action`, `raw_input_event`의 구성원 순서는 건드리지 않는다.
- **os_notify·inspector·image_clip(android_app.cpp)**: ink는 `copy_motion`과 `start`의 축 켜기 세 줄만 고친다. os_notify는 `start()`의 538줄 능력 한 줄과 `APP_CMD_GAINED_FOCUS` 분기를 고치고 콜드 스타트 읽기를 `start()` 밖에 둔다. 읽기가 `start()` 안으로 오면 ink가 먼저 병합된다([OS 알림](os-notification-design.md)의 약속).
- **인자 넘기기(줄 4)**: `window_surface.cpp`·`.h`는 이번 라운드에 ink만 고친다(`handle_pointer_message`와 포인터별 `frameId` 멤버). 인자 넘기기는 받는 자리를 `win32_window.cpp` 주 창에 두어([둘째 인스턴스의 인자 넘기기](single-instance-args-design.md)) 이 파일들을 건드리지 않는다.
- **menu_bar(R3)**: `raw_input_event` variant와 키 메시지 자리를 ink가 고치지 않는다.
- **웹뷰**: 펜 원본 전달은 그대로다. 웹 콘텐츠는 지금처럼 자기 지우개·압력을 쓴다.

[터치 제스처와 펜 입력](touch-pen-input-design.md)은 구현 커밋에서 이렇게 고친다.

| 자리 | 지금 | 고친 뒤 |
| --- | --- | --- |
| 22~25행 문단 | 지우개 접촉은 삼킨다, 잉크 편집은 후속 | 지우개 접촉은 잉크 캔버스만 받고 일반 컨트롤은 삼킨다, 잉크는 [잉크 캔버스](ink-design.md) |
| Win32 메시지 수신(35~37행) | `GetPointerPenInfo` 조회 | 펜 과거 표본(`GetPointerPenInfoHistory`)과 `frameId` 거르기 한 문단 |
| Android 입력 수신(80행) | 도구 종류와 과거 표본 | 압력·기울기 축 켜기 한 줄 |
| 내부 이벤트 표(92~97행) | 네 필드 | `pen`(이동·누름·뗌), `button_switch`(취소) 두 행 |
| 검증 표(415행) | 펜: 지우개 삼킴 | 펜: 지우개 표식, 일반 컨트롤의 삼킴 |
| 실제 장치 확인(429~435행) | 메시지·DPI·원격 | 필압·기울기 범위와 과거 표본의 빈도 |

`src/android/android_input.h`의 설명(101행 "지우개 끝은 삼키기")과 [Android 이식 계획](android-port-plan.md)의 입력 단계 한 줄도 같이 고친다.

## 확인

- `tests/pointer_sequence_tests.cpp`:
  - 뒤집힌다: "The eraser end runs no control"(114~122행)은 "The eraser end travels as marked events"가 된다. 누름·이동·뗌이 `pen->eraser`로 나오고, 접촉 중 취소와 `cancel()`이 취소를 내고, 지우개 접촉은 배럴이 바뀌어도 다시 누르지 않는다.
  - 늘어난다: "A pen barrel switch cancels the old button before pressing the new one"(86행)이 `button_switch`를 단언하고, 보통 취소는 거짓이다. 압력·기울기가 세 이벤트에 실리고 터치에는 `pen`이 없다. hover에 지우개 표식이 실린다.
  - 그대로다: 터치 접촉·hover 없음·취소·수식 키 test.
- `tests/android_input_tests.cpp`: "A pen hovers, right clicks with the barrel and swallows the eraser"(182행)는 지우개 DOWN·UP이 표식 단 누름·뗌을 내도록 바뀐다. 압력 접기(1.2 → 1), 극좌표 기울기 식(θ 45도·φ 90도면 tilt_x 45), 과거 표본 축 값의 순서를 더한다. Android 구성의 `luil_core_tests`라 기기에서 돈다(Windows 구성에는 없다).
- `tests/surface_input_tests.cpp`(`luil_tests`): `pen_values_of`(마스크 없음은 빈 압력, 512는 0.5, 기울기), `pen_history_order`(최근 것이 앞인 기록을 뒤집기, 보낸 `frameId` 거르기, frameId가 32비트 경계를 넘는 순서).
- 일반 컨트롤의 삼킴은 컨트롤러 test가 맡는다([잉크 캔버스](ink-design.md#확인)의 `tests/ink_canvas_tests.cpp`).
- `luil_pointer_integration`: 기존 "pen eraser / no ordinary click"(pointer_integration_runner.cpp 828행)이 그대로 통과해야 한다. 합성 펜은 이미 `PEN_MASK_PRESSURE|TILT_X|TILT_Y`(355~363행)를 실으므로 상태 JSON에 받은 압력을 더해 0.5를 본다.
- 사람 확인: 실제 압력 펜과 Android 실기기 스타일러스에서 압력 범위, 기울기 부호, 지우개 보고, 빠른 획의 과거 표본 수.

## 범위 밖

- 펜 축 회전(`rotation`·twist), 펜 버튼 둘째(`BUTTON_STYLUS_SECONDARY`를 따로 가르기), 접촉 크기.
- 웹뷰에 보내는 펜 원본의 변경, 마우스·터치의 압력, 움직임 예측.
