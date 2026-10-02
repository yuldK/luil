# Popup 안의 텍스트 입력과 IME

Popup은 `WS_EX_NOACTIVATE`이므로 독립적인 keyboard focus와 TSF session을 만들지 않는다. Popup 안 텍스트 element의 논리 초점은 popup tree에 두고, 앵커 최상위 표면의 TSF host가 그 popup을 대신 연결한다.

공개 입력 상태는 [ui_interaction.h](../include/luil/ui/ui_interaction.h), popup 선언은 [app_host.h](../include/luil/app/app_host.h)에 있다. 플랫폼 연결은 [window_surface.h](../src/win32/window_surface.h)의 `surface_tsf_host`가 맡는다.

## 초점 모델

Popup의 텍스트 칸을 누르면 snapshot은 다음 값을 갖는다.

```text
focused         = popup 안 text element id
focused_input   = 같은 id
focused_surface = popup id
```

물리 keyboard focus는 앵커 창에 남는다. Key message의 `surface`는 앵커 id지만 controller는 논리 초점의 `focused_surface`를 우선해 popup tree의 텍스트 element에 편집 키를 보낸다.

## TSF host 선택

각 최상위 `window_surface`는 하나의 `surface_tsf_host`를 가진다. Host가 처리할 text surface는 다음 규칙으로 정한다.

1. `focused_surface`가 자기 표면 id면 자기 surface를 사용한다.
2. `focused_surface`가 자기 표면에 앵커된 popup id면 그 popup surface를 사용한다.
3. 그 밖의 표면이면 대상이 없다.

`surface_context::anchored_popup(anchor, id)`는 두 번째 경우의 popup을 찾는다. Popup은 popup에 앵커할 수 없으므로 한 단계 조회로 끝난다.

이 선택은 여러 최상위 창의 TSF host가 같은 논리 초점을 동시에 주장하지 않게 한다.

## 문서와 편집 action

선택된 text surface의 tree에서 `focused_input` id를 찾고 `ui_element::text_input()`으로 확정된 문서, caret, anchor를 읽는다. `interaction_policy::text_target_of()`가 element kind를 앱의 `text_input_target`으로 바꾼다.

TSF edit는 `text_edit_request`로, 조합 표시는 `text_composition_event`로 바뀐다. Policy는 이를 앱 메시지로 변환하고 앱은 다음 frame의 텍스트 view를 다시 만든다.

Popup은 별도의 편집 상태를 소유하지 않는다. 주 창, 보조 창, popup의 텍스트 입력이 모두 같은 앱 상태와 action 경계를 사용한다.

## 후보 창 좌표

`surface_tsf_host::text_screen_rect()`는 논리 초점 element의 `text_span_bounds()`를 조회한다. 이 bounds는 해당 tree의 client 물리 좌표다.

화면 좌표 변환에는 반드시 그 element가 사는 popup HWND를 사용한다. 앵커 HWND로 변환하면 popup의 화면 offset만큼 IME 후보 창이 어긋난다.

Text span 측정은 element의 그리기와 같은 text measurer와 caret scroll 규칙을 사용한다. 조합 밑줄과 후보 창이 보이는 글자 위치에 맞는다.

## Focus 상실

앵커 최상위 창이 실제로 keyboard focus를 잃으면 다음 동작을 수행한다.

- 앵커의 TSF window focus를 해제한다.
- 진행 중 composition을 종료한다.
- 앵커 표면과 그 표면에 붙은 popup의 논리 텍스트 초점을 거둔다.

Popup 자체의 `WM_MOUSEACTIVATE`는 활성화를 요청하지 않는다. Popup을 누르는 동작이 앵커 창의 활성 상태를 유지하면서 논리 초점만 옮긴다.

WebView 같은 자식 창이 focus를 가져간 경우에는 최상위 표면 상실로 취급하지 않는다. 같은 애플리케이션 표면 안의 focus 전환에서 TSF composition을 불필요하게 끝내지 않는다.

Focus 상실은 popup 닫힘 action을 자동으로 만들지 않는다. 앱은 `activation_changed` dismiss reason에 대한 factory 결과로 popup 유지 여부를 정한다.

## Popup 수명 중 composition

Frame에서 popup이 사라지면 `set_surface_trees()`와 focus 검증이 popup 논리 초점을 제거한다. TSF host가 다음 동기화에서 text surface를 찾지 못하면 session target을 비우고 composition을 정리한다.

별도의 popup composition state machine은 필요하지 않다. Surface 존재 여부와 `focused_surface`가 수명을 결정한다.

## 메뉴와 검색 필드

Popup 메뉴가 열려 있을 때 메뉴가 소유한 Up, Down, Enter, Esc가 먼저 처리된다. 메뉴가 소유하지 않은 키는 popup 안의 focused text input에 전달된다. 텍스트 입력도 처리하지 않은 키는 메뉴 뒤의 앱 화면으로 흐르지 않는다.

이 순서로 검색 가능한 메뉴에서 caret 편집과 IME 조합을 유지하면서 메뉴 탐색 키를 보존한다.

## 반드시 유지할 불변식

- Popup은 자체 TSF session을 만들지 않는다.
- 논리 초점 surface는 popup id이고 물리 key surface는 anchor일 수 있다.
- Anchor TSF host는 자기 popup만 대신 처리한다.
- 텍스트 문서와 element bounds는 popup tree에서 읽는다.
- 화면 좌표 변환은 popup HWND를 사용한다.
- Anchor focus 상실은 popup 텍스트 초점을 함께 거둔다.
- Popup 제거는 surface 부재를 통해 focus와 composition을 정리한다.

## 검증 지침

[tsf_input_tests.cpp](../tests/tsf_input_tests.cpp)는 document 동기화와 composition 수명을 검증한다. [ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)는 popup focus surface와 key routing을, Win32 surface test는 anchor host 선택과 후보 창 좌표를 확인한다.

구현은 [window_surface.cpp](../src/win32/window_surface.cpp)와 [popup_surface.cpp](../src/win32/popup_surface.cpp)에 있다.
