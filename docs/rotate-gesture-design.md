# 두 손가락 회전

확대 보기의 두 접촉이 핀치와 함께 회전도 알리게 한다. 컨트롤러가 두 손가락을 잇는 선의 방향 변화를 재어 `rotate_by(각도, 기준점)` 메시지를 보내고, 각도는 앱이 든다. 돌려서 그리고 누른 자리를 가리는 일은 내용 요소가 맡는다. 키보드는 Shift+좌우 화살표와 Shift+Home이 같은 메시지를 낸다.

공개 형식은 [ui_element.h](../include/luil/ui/ui_element.h)의 `zoom_source::rotate_by`, [zoom_view_element.h](../include/luil/ui/zoom_view_element.h)의 `zoom_view_config::rotate_by`·`rotation`·`rotation_step`과 `rotate_about`·`rotate_point`·`rotated_bounds`, [ui_interaction.h](../include/luil/ui/ui_interaction.h)의 `touch_gesture_config::rotate_enabled`·`minimum_rotation_angle`이다.

## 문제

- 둘째 접촉은 컨트롤러가 가져간다. 같은 확대 보기의 대기·이동 접촉에 합류한 둘째 손가락만 받고 다른 추가 손가락은 삼키므로(`interaction_controller::process_touch_press`), 내용 요소와 앱은 두 손가락의 위치를 받지 못한다. 앱이 회전을 직접 지원할 길이 없다.
- 컨트롤러는 두 점의 가운데 이동(`pan_by`)과 거리 비율(`zoom_by`)만 잰다(`process_touch_move`의 `touch_phase::pinching`). 선의 방향은 읽지 않는다.
- [터치 제스처와 펜 입력](touch-pen-input-design.md)은 회전을 후속 범위로 남겼다. 터치는 `WM_POINTER` 접촉으로만 받고 OS 제스처를 섞지 않는 계약이라 OS가 대신 주지도 않는다.
- 그림을 돌리는 것은 이미 된다. spinner가 `canvas.rotate`로 돌고 그 각도(`spinner_angle`)는 도다. 없는 것은 입력과 계약이다. 내용에 회전을 알리는 메시지, 손가락 아래의 내용을 지키는 식, 키보드 대안이다.

## 공개 형식

```cpp
// include/luil/ui/ui_element.h: `zoom_source`의 마지막 필드다. 앞 필드만 채운 기존 초기화가 그대로 선다.
// 내용을 `degrees`만큼 돌리자는 메시지다. 기준점은 `zoom_by`와 같다 (보기 가운데 기준 논리 픽셀).
// 비어 있으면 컨트롤러가 각도를 재지도 않는다.
std::function<input_action(float degrees, zoom_point anchor)> rotate_by {};
```

```cpp
// include/luil/ui/zoom_view_element.h
struct zoom_view_state
{
    float zoom { 1.0f };
    zoom_point origin {};
    // 도다. 시계 방향이 양수이고 (-180, 180]로 접는다.
    float rotation { 0.0f };
};

// zoom_view_config의 마지막 필드들이다.
std::function<input_action(float degrees, zoom_point anchor)> rotate_by {};
float rotation { 0.0f };       // 앱이 든 지금 각도 (도)
float rotation_step { 15.0f }; // 키 한 번의 각도 (도). 유한한 양수이고 180 이하

// 기준점 아래의 내용을 두고 `degrees`만큼 돌린 상태다. 원점은 규칙의 식대로 움직이고 `rotation`은 접은 합이다.
[[nodiscard]] zoom_view_state rotate_about(const zoom_view_config& config, zoom_point viewport, float degrees, zoom_point anchor) noexcept;
// 원점 둘레로 `degrees`만큼 돌린 점이다. 누른 점을 내용 쪽으로 되돌릴 때는 `-degrees`를 준다. 유한하지 않은 각도는 점 그대로다.
[[nodiscard]] zoom_point rotate_point(zoom_point point, float degrees) noexcept;
// 네 꼭짓점을 원점 둘레로 돌린 경계 상자다. 0도면 그대로다.
[[nodiscard]] rect_f rotated_bounds(const rect_f& bounds, float degrees) noexcept;
```

```cpp
// include/luil/ui/ui_interaction.h: `touch_gesture_config`의 마지막 필드들이다 (`operator==`는 기본이라 따라온다).
// 같은 확대 보기의 핀치가 각도도 알린다. 보기가 `rotate_by`를 줘야 선다.
bool rotate_enabled { true };
// 쌓인 각도(도)가 이만큼 되어야 회전이 선다.
float minimum_rotation_angle { 6.0f };
```

앱은 `pan_by`·`zoom_by`처럼 지금 상태에 메시지를 적용한다. 돌린 내용의 경계 상자는 앱이 `content_bounds`에 준다.

```cpp
else if (const auto* const rotate { message.get<rotate_intent>() }; rotate != nullptr)
    state_ = luil::rotate_about(config(), viewport_, rotate->degrees, rotate->anchor);
// config()
.rotate_by = [](const float degrees, const luil::zoom_point anchor) { return luil::make_app_action(rotate_intent { degrees, anchor }); },
.rotation = state_.rotation,
.content_bounds = luil::rotated_bounds(page_bounds, state_.rotation),
```

- 기존 도우미(`zoom_about`·`pan_by`·`zoom_to_fit`)와 `zoom_view_element::state()`는 `rotation`을 접어 그대로 돌려준다. `{ 1.0f, {} }` 같은 기존 초기화는 0도다.
- `rotate_about`은 경계를 다듬지 않는다. 각도가 바뀌면 `rotated_bounds`도 바뀌므로, 다듬기는 새 각도의 경계를 담은 config로 `zoom_about(config, viewport, 1.0f, {})`를 불러 한다. 데모의 `build`가 이미 하는 일이다. 배치도 같은 식으로 다듬는다.
- 생성자는 유한하지 않은 `rotation`과 범위 밖 `rotation_step`을 `invalid_argument`로 거절한다.

## 규칙

각도와 식.

- 각도는 도이고 시계 방향이 양수다. `SkCanvas::rotate`와 같은 쪽이라 내용 요소가 `canvas.rotate(rotation)`에 그대로 먹이고, 값이 커지는 쪽이 양수라는 [스프링](spring-design.md)의 규칙과도 같다.
- 내용 좌표 `p`의 위치는 `origin + zoom × R(rotation) p`다 ([확대 보기](zoom-view-design.md)의 식에 `R`을 더한 것). `R`은 화면(아래가 +y)에서 양수가 시계 방향인 회전이고, 중심은 내용 좌표의 원점이다. 가운데에서 돌리려면 경계를 원점 중심으로 둔다.
- 기준점 고정 식은 `origin' = anchor − R(δ)(anchor − origin)`이다. 기준점 아래의 내용은 제자리이고, 배율과 지금 각도에 상관없다.
- 원점·기준점·이동량은 보기 가운데 기준 논리 픽셀이다. 회전한 뒤에도 `pan_by`의 이동량은 손가락이 움직인 화면 방향 그대로다.

컨트롤러.

- 핀치의 이동마다 두 점을 잇는 선의 이전·현재 방향에서 `δ = atan2(외적, 내적)`을 얻는다. 값은 (-180, 180]이라 선이 왼쪽을 가리키는 방향(±180도)을 건너도 작은 값이다. 이전 방향은 이동을 반영하기 전의 `contact.last_x`·`last_y`와 `second` 좌표에서 구한다. 거리(`old_distance`)와 같은 방식이라 저장하는 필드가 없다.
- 거리 규칙은 배율과 같다. 이전·현재 간격이 모두 `minimum_pinch_distance` 이상일 때만 `δ`를 재고 쌓는다. 겹친 손가락의 방향은 불안정하다.
- 문턱. 잰 `δ`를 부호째 쌓는다. 합의 절댓값이 `minimum_rotation_angle`에 닿으면 회전이 서고 그 쌍이 끝날 때까지 유지한다. 서는 이벤트가 쌓은 합 전체를 한 번에 보내고 이후는 이벤트마다 `δ`를 보낸다. 서기 전에는 보내지 않는다. 확대와 이동은 문턱과 무관하게 나간다.
- 쌓는 값과 서 있는지는 접촉 쌍의 것이다. 하나를 떼면 지워지고, 남은 접촉에 새 둘째가 합류하면 0에서 다시 쌓는다.
- 발행 순서는 `pan_by`, `zoom_by`, `rotate_by`다. 기준점은 `zoom_by`와 같은 현재 가운데다. 앱은 셋을 지금 상태에 차례로 적용한다.
- 보기에 `rotate_by`가 있어야 잰다. 접촉 쌍은 지금처럼 `zoom_by`·`pan_by`가 있어야 선다. `rotate_by`가 없는 보기는 핀치만 한다. 접촉 중에 최신 tree의 보기가 `rotate_by`를 잃으면 회전만 멈추고 접촉은 취소하지 않는다.
- 진행 중에 `rotate_enabled`를 끄면 그 접촉의 회전만 멈추고 핀치와 이동은 이어진다. 다시 켜도 되살아나지 않는다. 설정은 `pinch_enabled`처럼 누른 접촉이 든 것이라, 첫 손가락을 떼고 새 손가락이 합류한 쌍도 그 접촉이 끝날 때까지 돌리지 않는다. `pinch_enabled`를 끄면 지금처럼 두 접촉을 함께 취소한다.
- `minimum_rotation_angle`은 유한한 양수여야 한다(`valid_touch_gesture_config`). 잘못된 설정은 거절되고 기존 설정이 남는다.
- 취소, 입력 큐 유실, 표면 소멸, 마우스·펜의 새 누름은 핀치와 같이 두 접촉을 함께 거둔다. 마우스·펜에는 회전이 없다. 핀치가 섞인 접촉이 경계 쓸기를 알리지 않는 규칙도 그대로다.

내용.

- `zoom_view_element`는 `rotation`을 배치·그리기에 쓰지 않는다. 경계(`overlap`·`contain`), `zoom_to_fit`, 두 번 탭 맞춤, 경계 쓸기가 돌린 내용을 보려면 앱이 `content_bounds`에 `rotated_bounds`를 준다. 안 주면 돌리기 전의 경계를 본다. `contain`은 돌린 상자의 빈 모서리를 막지 못한다.
- 내용은 지금처럼 한 번 배치된다. slot은 `content_bounds`가 정하고, 내용은 자기 원점의 화면 자리를 `slot − 돌린 경계의 왼쪽 위 × 배율`로 얻는다.
- 자식을 돌릴 때는 자리만 돌린다. 글자가 선 채 회전한 자리의 축 정렬 slot을 주면 기본 `hit_test`, 접근성 bounds, popup 앵커, IME caret이 그대로 맞는다. 글을 눕히지 않는다.
- 요소 하나를 통째로 돌려 그리는 내용은 가상 함수 `ui_element::hit_test`를 재정의해 누른 점을 `rotate_point(점, -rotation)`으로 되돌린다. 그 요소의 접근성 bounds는 축 정렬인 slot이다.

키보드.

- 초점 있는 보기에 `rotate_by`가 있을 때만 쓴다. Shift+←는 `-rotation_step`, Shift+→는 `+rotation_step`, Shift+Home은 0도 복귀다 (`rotate_by(-rotation, {})`). 발행본이 이미 0도면 키만 소비하고 메시지는 없다. 기준점은 보기 가운데다.
- `rotate_by`가 없으면 Shift+화살표는 지금처럼 이동이다. Ctrl이 든 키는 앱 단축키로 남는다(`zoom_source::on_key`의 기존 규칙). 키는 `zoom_view_element::keys`에서 처리하고 문자 입력 경로는 고치지 않는다.

움직임·접근성.

- 손가락을 따르는 직접 조작이라 [움직임 줄이기](reduce-motion-design.md)와 닿지 않는다. 키 한 번의 각도를 앱이 부드럽게 잇는다면 그 앱이 `motion_duration`·`spring_to`를 거친다. 거리는 `zoom_source::scale`로 나눈 논리 픽셀이고 각도는 배율과 무관하다. 테마·고대비와 닿는 곳이 없다.
- `access_range`는 배율 그대로다. 각도를 접근성에 싣지 않는다(결정 6).

## 결정

**1. 회전의 뜻.** 질문: 내용을 누가 돌리고 각도는 누가 드는가.
- 길 a: 알림만. 컨트롤러가 `rotate_by`를 보내고 앱이 각도를 든다. 보기는 내용을 돌려 주지 않는다. 내용 요소가 돌려서 그리고, 자식은 회전한 자리의 slot에 놓고, 통째로 돌려 그리는 요소는 `hit_test`를 재정의한다. 대가: 내용 요소마다 그림과 역변환을 짓는다. 글자와 칸이 든 화면을 통째로 돌릴 수는 없다. 경계 상자는 앱이 `rotated_bounds`로 준다.
- 길 b: 보기가 내용을 돌린다. 아무 화면이나 돌릴 수 있다. 대가: 지금은 변환 개념이 없다. `ui_element::bounds()`가 축 정렬 `rect_f`이고 `ui_tree::hit_test`, 접근성 bounds, popup 앵커, IME caret이 모두 그 사각형을 읽는다. 확대 보기의 불변식(그림 배율, 배치 배율, hit test 좌표가 같다)도 깨진다. 돌린 글은 LCD 서브픽셀(`text_render_quality::subpixel_lcd`)을 쓰지 못할 수 있고 웹뷰는 돌리지 못한다. 크기가 a의 몇 배다.
- 길 c: 회전하는 내용 요소(이미지 한 장 같은 것)를 라이브러리가 짓는다. 대가: 요소마다 공개 형식이 는다. a 위에 얹을 수 있으므로 a가 선 뒤의 후속이다.
- 권장: a. `rotate_point`·`rotated_bounds`와 데모의 내용 요소 예가 내용 요소마다 짓는 몫을 줄인다. 대가: 위의 하나는 a의 것이다. **승인: 길 a (2026-11-02).**

**2. 대상.** 질문: 어느 요소가 두 손가락 회전을 받는가.
- 길 a: `zoom_source`를 확장한다. 핀치와 접촉 쌍, 수명, 취소, 설정 변경이 같은 코드를 쓴다. 보기 하나가 확대·이동·회전의 임자다. 대가: 확대 보기에서만 선다. 돌릴 내용은 `zoom_view_element`로 감싸야 한다.
- 길 b: 범용 '두 손가락 대상' 역할을 새로 세운다. 확대 보기가 아닌 요소도 받는다. 대가: 임자 찾기(`zoom_owner_at`)와 접촉 수명을 하나 더 짓고, 한 쌍을 누가 갖는지 규칙이 는다. `ui_element` 접근자와 `ui_interaction.cpp` 변경이 커서 같은 파일을 고치는 ink와 겹친다.
- 권장: a. 핀치의 접촉 쌍·수명·취소가 그대로 서고 보기 하나가 셋의 임자다. 대가: 위의 하나는 a의 것이다. **승인: 길 a (2026-11-02).**

**3. 한 쌍이 하는 일과 문턱.** 질문: 한 쌍이 확대·이동·회전을 함께 내는가, 문턱 앞에서 쌓은 각도는 어떻게 하는가.
- 길 a: 한 쌍이 셋을 다 낸다. 회전만 문턱으로 잠그고, 서는 순간 쌓은 합을 한 번에 보낸다. 손가락 아래의 내용이 손가락을 따른다. 대가: 서는 이벤트에서 내용이 문턱 이상(약 6도)만큼 한 번에 돈다. 손가락 간격 100 논리 픽셀이면 손가락 하나가 5 논리 픽셀쯤 움직인 몫이다.
- 길 b: 서는 순간 쌓은 합을 버리고 0에서 보낸다. 점프가 없다. 대가: 내용이 손가락보다 문턱만큼 덜 돈 채 남아 손가락과 어긋난다.
- 길 c: 처음 우세한 쪽으로 확대 전용·회전 전용을 잠근다. 대가: 같은 손놀림에서 둘을 함께 못 하고 판정 규칙이 는다.
- 권장: a. 손가락 아래의 내용이 손가락을 따른다. 대가: 위의 점프 하나이고, 그 크기는 터치 허용치(12 논리 픽셀)보다 작다. **승인: 길 a (2026-11-02).**

**4. 켜는 길.** 질문: 보기마다 회전을 어떻게 켜는가.
- 길 a: 보기의 `rotate_by`가 비면 끈다(`edge_swipe`와 같다). 설정 쪽 `rotate_enabled`는 창 전체의 끔이다. 쓰지 않는 앱의 동작이 같다.
- 길 b: 보기 설정에 `enabled` bool을 따로 둔다(`double_tap`처럼). 대가: 메시지 없이 켠 보기, 켜지 않고 메시지만 있는 보기가 생긴다. 두 번 탭의 `enabled`는 내용의 기존 동작과 부딪혀서 둔 것이고 회전에는 그런 부딪힘이 없다.
- 권장: a. 쓰지 않는 앱의 동작이 같고 `edge_swipe`와 한 모양이다. 대가: 보기 하나만 끄려면 factory를 빼야 하는 것이다. **승인: 길 a (2026-11-02).**

**5. 키보드 대안.** 질문: 두 손가락 없이 어떤 키로 돌리는가.
- 길 a: Shift+←/→/Home. 키 경로(`zoom_source::on_key`)만 고친다. 배열과 문자 입력 상태를 타지 않는다.
- 길 b: `[`·`]` 문자. 맨 `[`·`]`는 키가 아니라 문자 입력으로 온다([ui_events.h](../include/luil/ui/ui_events.h)의 `key_code` 설명). 컨트롤러의 문자→키 표가 `+`·`=`·`-`·`0`만 알아서 그 표를 고쳐야 하고, 그 파일은 ink도 고친다.
- 길 c: 키 없이 앱의 단추만. 대가: 키보드만 쓰는 사람이 회전하지 못한다. 두 손가락 몸짓의 한 손가락 대안이 없다.
- 권장: a. 키 경로 한 곳만 고치고 문자 입력 경로를 건드리지 않는다. 대가: `rotate_by`를 켠 보기에서 Shift+화살표의 이동이 회전으로 바뀌고 Shift+Home을 보기가 가져가는 것이다. 켠 앱만 겪는다. **승인: 길 a (2026-11-02).**

**6. 보조 기술.** 질문: 회전을 UIA 같은 보조 기술에 내보이는가.
- 길 a: 노출하지 않는다. 키가 대안이다. UIA의 Transform 패턴은 이동·크기·회전을 한 패턴으로 내걸고 현재 각도를 읽는 속성이 없다. 이동을 내걸지 않은 [확대 보기](zoom-view-design.md)와 함께 정할 일이다.
- 길 b: Transform의 `Rotate`를 `rotate_by(degrees, {})`에 잇는다. UIA의 회전도 상대 각도라 메시지와 맞는다. 대가: `access_info`·`access_command`와 `uia_provider.cpp`에 패턴을 새로 짓고(지금은 없다), 이동·크기는 할 수 없다고 답해야 한다.
- 권장: a. 키가 대안이고, 메시지가 상대 각도라 b는 나중에 더해도 계약이 깨지지 않는다. 대가: 포인터도 키도 없이 UIA 명령만 쓰는 도구가 회전하지 못하는 것이다. **승인: 길 a (2026-11-02).**

**7. 스냅과 0도 복귀.** 질문: 손을 뗄 때 각도를 맞추는가, 0도로는 어떻게 돌아가는가.
- 길 a: 스냅이 없다. 0도 복귀는 Shift+Home과 앱 단추가 보낸 `rotate_by(-rotation)`이다.
- 길 b: 접촉이 끝날 때 가까운 90도 배수로 맞추는 메시지를 낸다. 컨트롤러가 누를 때의 발행본 각도에 접촉 동안 보낸 합을 더해 알아야 한다(`pan_room`과 같은 이유로 발행본이 늦기 때문이다). 대가: `zoom_source`에 각도와 끝 메시지가 늘고 접촉 중 상태가 는다. ink와 겹치는 변경이 커진다.
- 길 c: 앱이 `rotate_by`를 처리하며 스스로 붙인다. 대가: 앱이 날 각도와 붙인 각도를 따로 들어야 한다. 하나만 들면 작은 변화가 0도에 붙은 채 빠져나오지 못한다.
- 권장: a. 후속에서 b를 더해도 기존 메시지가 바뀌지 않는다. 대가: 곧게 맞추려면 키나 단추를 써야 하는 것이다. **승인: 길 a (2026-11-02).**

## 플랫폼

### Win32

플랫폼 호출이 없다. `src/win32`는 고치지 않는다. 두 접촉은 이미 `WM_POINTER`에서 `pointer_sequence_tracker`를 지나 컨트롤러에 온다. 각도는 그 좌표로 잰다. 정밀 터치패드는 기본 처리에 맡기는 기존 계약이라 회전하지 않는다. 키는 Shift+화살표·Home의 기존 키 경로다. UIA Transform은 노출하지 않는다(결정 6).

### Android

같은 코드다. 둘째 손가락은 `AMOTION_EVENT_ACTION_POINTER_DOWN`으로 와 `pointer_phase::down`이 되고(`src/android/android_input.cpp`), 이후는 공용 추적기와 컨트롤러다. Java 소스와 JNI가 필요 없고 unsupported로 둘 것도 없다. 시스템 터치 설정(`apply_system_input_settings`)이 덮는 것은 길게 누르기 시간, 스크롤 시작 시간·거리, 이동 허용 거리 넷뿐이라 새 설정은 `application_config::touch`가 정한 값 그대로다. 하드웨어 키보드의 Shift+화살표는 Win32와 같은 키 어휘를 탄다. Android에는 접근성 계층이 없다.

## 다른 노드와의 약속

같은 줄 R2의 ink가 같은 파일을 고친다. rotate가 먼저 들어가므로 바꾸는 자리를 아래로 줄이고, ink는 그 위에서 시작한다. 병합은 rotate가 먼저다.

| 파일 | rotate가 바꾸는 자리 | 바꾸지 않는 자리 (ink의 몫) |
| --- | --- | --- |
| `ui_element.h` | `zoom_source`의 마지막 필드 하나 | `ui_element`의 접근자와 `zoom_source_` 옆, ink의 새 source 형식 |
| `ui_interaction.h` | `touch_gesture_config` 끝 둘, `second_contact` 끝 둘 | 컨트롤러 클래스 선언 (새 멤버 함수가 없다), 펜 설정 |
| `ui_interaction.cpp` | `valid_touch_gesture_config`에 조건 하나, `process_touch_move`의 pinching 블록에 약 20줄, `set_touch_config`에 `rotate_enabled` 몇 줄, 익명 namespace에 각도 도우미 하나 | `process_touch_press`, 펜·마우스 경로, 키·문자 경로 |
| `touch-pen-input-design.md` | 설정 표의 `minimum_pinch_distance` 행 뒤 두 행, "회전은 후속 범위" 문장에서 회전 빼기, 핀치 설명에 회전 링크 | 펜 절과 "잉크 편집은 후속 기능의 범위다" |

- **ink**: 회전한 보기 위의 점을 내용 좌표로 되돌리는 규약은 이 노드가 세운다. 내용 좌표 `p = R(-rotation)((점 − 보기 가운데 − origin × scale) / (scale × zoom))`이고 `R(-rotation)`이 `rotate_point(…, -rotation)`이다. rotate는 `zoom_source` 끝에 `rotate_by` 하나만 더한다. ink가 `zoom_source`를 넓히면 그 뒤에 더한다.
- **menu_bar**: 이 노드는 컨트롤러의 키 경로를 고치지 않는다. 키는 `zoom_source::on_key` 안이라 함수가 겹치지 않는다.
- **스프링**: 손을 뗄 때의 각속도는 건네지 않는다. 건네게 되면 [스프링](spring-design.md)의 규칙대로 도/초이고 시계 방향이 양수다.
- **움직임 줄이기 연결**: 이 노드가 새로 가진 시간 기반 값이 없다. 키로 바뀐 각도를 앱이 잇는다면 [움직임 줄이기 연결](reduce-motion-design.md)의 `motion_duration`을 거친다.
- **i18n**: 새 사용자 글이 없다. `zoom_controls_element`의 박힌 글(축소·확대·맞춤)을 건드리지 않아 겹치는 곳은 `zoom_view_tests.cpp`에 덧붙인 test뿐이다. 회전 단추를 그 요소에 더하는 일은 G6(라이브러리 문자열 정책)이 정해지면 한다.
- **zoom 계열 문서**: 구현할 때 [확대 보기](zoom-view-design.md)에 입력 표 한 줄, 두 접촉의 수명 한 문단, 불변식 한 줄을 더하고 "남은 열린 질문"의 Transform 줄에 회전을 덧붙인다.

## 확인

자동 test. 새 파일은 없다. CMake는 모바일 데모의 확대 보기 페이지를 읽는 test 때문에 `tests/CMakeLists.txt`의 모바일 데모 블록에 `zoom_page.cpp` 한 줄을 더한다. `zoom_view_tests.cpp`와 `mobile_demo_page_tests.cpp`는 `luil_core_tests`라 기기에서도 돈다.

- `tests/zoom_view_tests.cpp`:
  - 식: 기준점 아래의 내용 좌표가 임의의 배율·각도에서 그대로, 0도·NaN·무한대는 현재 상태, ±180 접기, `rotate_point`의 역변환, `rotated_bounds`(0도는 같은 경계, 90도는 가로세로 교환, 45도는 대각), `zoom_about`·`pan_by`·`zoom_to_fit`이 `rotation`을 지킨다.
  - 핀치 확장(`[zoom][pinch]`): 한 이동에서 `pan_by`, `zoom_by`, `rotate_by` 순이다. (100,100)·(200,100)에서 둘째가 (200,120)으로 가면 +11.3도다. 둘째가 (200,110)까지(5.7도)는 보내지 않고 (200,111)에서 6.3도를 한 번에 보낸다. 문턱 앞에서 반대로 흔들면 상쇄된다. 두 점을 잇는 벡터가 (-100,-1)에서 (-100,1)로 넘어가면 -1.1도다.
  - 가까운 접촉(24 미만)은 각도를 쌓지 않는다. 하나를 떼면 쌓은 값이 지워지고 새 둘째가 0에서 쌓는다. `rotate_by`가 없는 보기는 핀치만 한다. 진행 중 `rotate_enabled`를 끄면 회전만 멈추고 핀치는 이어진다. 취소·큐 유실은 둘을 거둔다.
  - 키: Shift+←/→가 `∓rotation_step`, Shift+Home이 `-rotation`, 0도에서는 소비만 하고 메시지가 없다. `rotate_by`가 없으면 Shift+←는 이동이고 Ctrl이 든 키는 앱 몫이다. `access_range`는 배율 그대로다. 생성자의 잘못된 `rotation`·`rotation_step` 거절.
- `tests/touch_input_tests.cpp`("Touch settings are validated"): `minimum_rotation_angle`의 0·음수·NaN·무한대 거절, `operator==`가 새 필드를 보는지, 거절된 설정이 기존 설정을 지키는지.
- `tests/pointer_integration_runner.cpp`(`luil_pointer_integration`): 기존 합성 핀치 시나리오에 회전 다리를 더한다. 같은 프레임의 두 PT_TOUCH를 가운데 둘레로 돌려 `rotation`의 부호(시계 방향이 양수)와 크기를 보고, 배율이 거의 그대로임을 본다. 동시 두 접촉이 서지 않으면 기존대로 SKIP이고 통과로 세지 않는다. `pointer_integration_fixture.cpp`의 상태 JSON에 `rotation`을 더하고 `zoom_config`에 `rotate_by`를 잇는다.
- `tests/mobile_demo_page_tests.cpp`(`[mobile-demo][zoom][rotate]`): 단추를 실제 입력 경로로 눌러 label `zoom-rotation`을 되읽는다. right 둘이면 "회전 30°", left 셋이면 "회전 -15°", reset이면 "회전 0°"이고, 이미 0도면 reset은 메시지가 없다. 노드의 경계는 옮겨 가고 크기는 그대로이며 reset이면 처음 자리다. 초점 있는 보기의 Shift+→/←/Home과 두 손가락(오른쪽 손가락이 40 내려가면 "회전 11°")이 같은 label을 바꾼다. 쪽을 넘기면 새 쪽은 곧게 선다.
- `luil_source_style`(`scripts/check_source_style.ps1`). 새 공개 헤더가 없어 우산 헤더 검사는 그대로다.

데모. 상태를 label로 그려 `luil-uia`로 되읽는다. 두 데모의 확대 보기 페이지에 같은 것을 더한다(`examples/demo/zoom_page.cpp`·`.h`, `examples/mobile_demo/zoom_page.cpp`·`.h`).

- 도면의 노드는 글자가 선 채 자리와 격자만 `rotation`으로 돈다(결정 1의 a). `rotate_intent`가 `rotate_about`으로 상태를 바꾸고 `content_bounds`가 `rotated_bounds`를 준다.
- label `zoom-rotation`("회전 0°", 시계 방향 15도면 "회전 15°")과 단추 `zoom-rotate-left`("-15°")·`zoom-rotate-right`("+15°")·`zoom-rotate-reset`("0°")를 둔다. 단추는 키와 같은 `rotate_by` 메시지를 보내 UIA가 Invoke로 되풀이한다. `zoom-rotate-reset`은 이미 0도면 꺼진다. 켜 둔 채 빈 동작을 내면 Invoke가 오류로 끝나기 때문이다. 두 데모 모두 단추 줄 밑에 줄 하나(`zoom-rotation-controls`, 높이 `controls_height`)를 더하고 그만큼을 보기의 논리 크기에서 뺀다.
- UIA(격리 데스크톱): right를 두 번 Invoke하면 "회전 30°", 이어서 left를 세 번이면 "회전 -15°", reset이면 "회전 0°"이다. 한 번 돌린 뒤 노드 라벨(데스크톱 데모의 "노드 1")의 경계가 옮겨 가고, reset이면 처음 자리로 돌아온다. 두 손가락 몸짓과 키는 UIA로 주입하지 못해 아래의 사람 확인과 test가 맡는다.
- 모바일 데모는 Windows 창으로도 뜨므로 같은 UIA 확인을 거친다. Android에는 접근성 계층이 없어 사람이 화면으로 읽는다.

사람 확인.

- 실제 터치 화면에서 확대와 회전을 함께 하는 감촉, 문턱 6도의 떨림과 점프, 가장자리에 닿는 손가락.
- Android 실기기에서 회전과 핀치, 화면 방향이 바뀌는 중의 접촉 취소, 시스템 뒤로 가기 제스처 가장자리.
- 접촉이 보존되지 않는 원격 환경은 [확대 보기](zoom-view-design.md)가 미검증으로 남긴 영역이라 이 노드도 같다.
- 초점 있는 보기에서 Shift+←/→/Home이 도는지, 정밀 터치패드의 두 손가락 회전이 이 보기를 돌리지 않는지, Narrator를 켠 채 Shift+화살표가 보기에 닿는지.

## 범위 밖

- 보기가 내용을 변환하는 것(결정 1의 b). 접근성 bounds, hit test, popup 앵커, IME caret 전반의 역변환이 필요한 별도 설계다.
- 정밀 터치패드·마우스·펜의 회전, 세 손가락 이상, 서로 다른 보기에 걸친 쌍.
- 놓을 때의 스냅, 관성, 각속도 전달(결정 7).
- UIA Transform 패턴(결정 6). 이동과 함께 따로 정한다.
- `zoom_controls_element`의 회전 단추, 회전 각도 제한, 웹뷰 내용의 회전. 웹뷰는 원시 포인터를 받는다.
