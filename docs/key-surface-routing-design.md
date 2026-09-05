# 키 이벤트의 표면 라우팅

모든 `key_pressed_event`는 이벤트가 발생한 최상위 표면 id를 싣는다. Controller는 논리 초점이 있으면 그 초점의 표면을 우선하고, 초점이 없을 때만 이벤트 표면을 시작점으로 사용한다.

이 계약은 [ui_events.h](../include/luil/ui/ui_events.h)와 [ui_interaction.h](../include/luil/ui/ui_interaction.h)에 정의된다.

## 표면 id 규칙

- 빈 문자열은 주 창이다.
- 값이 있는 문자열은 popup 또는 보조 창의 id다.
- `surface_tree(id)`는 주 tree와 `surface_tree_list`에서 해당 tree를 찾는다.
- 알려지지 않은 id는 `nullptr`로 해석하며 다른 tree로 fallback하지 않는다.

포인터, 휠, 파일 끌기 이벤트도 같은 표면 id 규칙을 쓴다. 좌표가 있는 이벤트의 좌표는 해당 표면 client 좌표다.

## 키 이벤트 생성

최상위 `window_surface`는 Win32 key message를 정규화할 때 자기 `id()`를 `key_pressed_event::surface`에 넣는다.

```cpp
luil::key_pressed_event event {
    .key = luil::key_code::tab,
    .shift = shift_down,
    .repeat = repeated,
    .time = posted_at,
    .surface = surface_id,
};
```

Popup은 `WS_EX_NOACTIVATE`라 keyboard focus를 받지 않는다. Popup 안에 논리 초점이 있어도 물리 key message는 앵커 창에서 발생하며 이벤트 표면은 앵커 id다.

## 초점 우선 규칙

키 라우팅이 tree를 고를 때 다음 식을 사용한다.

```cpp
const auto& surface = has_logical_focus
    ? snapshot.focused_surface
    : event.surface;
```

이 규칙은 Tab, Enter 기본 버튼, Esc focus trap, activation, 값 step, group 탐색에 적용된다.

Popup 안 텍스트 입력이나 메뉴 항목에 논리 초점이 있으면 popup tree가 계속 키를 받는다. 이벤트 표면을 검문 조건으로 사용하면 앵커 창에서 온 정상 키를 거부하게 되므로 두 값이 다른 것은 오류가 아니다.

## 초점이 없을 때

논리 초점이 없으면 event surface가 시작 tree다.

- Tab은 그 표면의 `focus_order()`에서 시작한다.
- Enter는 그 표면의 `default_button()`을 찾는다.
- Esc는 그 표면의 `focus_trap()`과 dismiss action을 찾는다.

이 동작 덕분에 보조 창에서 첫 Tab을 눌렀을 때 주 창이 아니라 보조 창의 첫 초점 자리로 들어간다.

내장 키 처리가 모두 지나간 뒤 `interaction_policy::on_key()`에는 주 tree 포인터, 원래 event, 현재 snapshot이 전달된다. 이 callback의 tree 인자는 선택된 popup이나 보조 창 tree로 바뀌지 않는다. Policy는 `event.surface`와 `snapshot.focused_surface`로 출처와 논리 초점 표면을 구분한다.

## 문자 이벤트

`character_typed_event`에는 surface가 없다. 문자는 현재 `focused_input`에만 전달되며 controller는 `focused_surface`의 tree에서 해당 element를 찾는다. 초점 텍스트 입력이 없으면 문자는 group typeahead에 쓰이거나 무시된다.

## 활성 표면과의 구분

키 event surface는 이벤트가 어디서 왔는지 답한다. 활성 표면은 tree 변화로 modal 진입을 판정할 때 사용한다. 이벤트가 없는 갱신에서는 key surface를 재사용하지 않는다.

활성 표면의 자세한 수명은 [active-surface-design.md](active-surface-design.md)에 정리되어 있다.

## Surface tree 갱신

`set_surface_trees()`는 주 창 밖 표면의 전체 목록을 교체한다. 사라진 표면에서 시작한 press, text selection drag, pointer drag, drag-and-drop은 즉시 취소한다. 해당 표면의 release event를 기다리면 사라진 tree의 gesture가 다음 입력을 가로챌 수 있기 때문이다.

논리 초점은 이어지는 `update_focus()`에서 검증한다. 초점 표면 tree나 element가 사라졌으면 초점을 거둔다.

## 반드시 유지할 불변식

- 모든 key press는 발생한 최상위 표면 id를 싣는다.
- 논리 초점이 있으면 `focused_surface`가 event surface보다 우선한다.
- 초점이 없을 때만 event surface가 탐색 시작점이다.
- 모르는 표면 id는 주 tree로 fallback하지 않는다.
- Popup key의 event surface와 focus surface가 다른 것은 정상이다.
- 문자 입력은 `focused_surface`에서만 대상을 찾는다.
- `on_key()`의 tree 인자는 주 tree이며 event와 snapshot이 표면 문맥을 전달한다.

## 검증 지침

surface_input_tests.cpp는 Win32 key 정규화와 surface 전달을 확인한다. [ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)는 보조 창 첫 Tab, 표면별 기본 버튼과 dismiss, popup 논리 초점, 사라진 표면을 검증한다.

플랫폼 연결은 surface_input.cpp, controller 선택 로직은 [ui_interaction.cpp](../src/ui/ui_interaction.cpp)에 있다.
