# 활성 표면과 초점 수명

`interaction_controller`는 논리 초점이 있는 표면과 사용자가 활성화한 최상위 표면을 따로 추적한다. 두 값은 대개 같지만 popup이 열리면 갈라질 수 있다. 이 구분은 여러 창의 modal 진입, popup 텍스트 입력, 초점 복귀를 일관되게 만든다.

관련 공개 형식은 [ui_events.h](../include/luil/ui/ui_events.h), [ui_element.h](../include/luil/ui/ui_element.h), [ui_interaction.h](../include/luil/ui/ui_interaction.h)에 있다.

## 두 종류의 표면 상태

`interaction_snapshot::focused_surface`는 논리 초점을 가진 element의 tree를 고른다. 빈 문자열은 주 창이고, 값이 있으면 popup 또는 보조 창의 표면 id다.

`interaction_controller::active_surface_`는 Win32 keyboard focus를 가진 최상위 창을 고른다. 이 값은 공개 snapshot에 포함되지 않는다. tree 갱신 때 어떤 modal 가둠에 진입할지를 결정하는 controller 내부 상태다.

| 값 | 답하는 질문 | 주 소비자 |
| --- | --- | --- |
| `focused_surface` | 논리 초점 element는 어느 tree에 있는가? | 키 라우팅, 텍스트 편집, TSF |
| 활성 표면 | 사용자가 지금 다루는 최상위 창은 어느 것인가? | modal 진입, 초점 복귀 |

Popup은 `WS_EX_NOACTIVATE`로 만들어지므로 활성 표면이 되지 않는다. Popup 안의 텍스트 칸이 논리 초점을 가질 때 `focused_surface`는 popup id이고 활성 표면은 그 popup의 앵커 창이다.

## 활성 표면 이벤트

최상위 표면은 keyboard focus를 얻을 때 `surface_focus_gained_event`를 게시한다.

```cpp
luil::surface_focus_gained_event event {
    .surface = window_id, // 주 창은 빈 문자열
};
```

Controller는 이 이벤트의 `surface`를 활성 표면으로 저장한다. `surface_focus_lost_event`는 논리 초점을 거두는 데 쓰지만 활성 표면 값은 지우지 않는다. 애플리케이션이 다른 프로세스로 전환되었다가 돌아올 때 마지막으로 활성화했던 창이 modal 진입의 기준으로 남아야 하기 때문이다.

닫힌 보조 창의 id가 마지막 활성 표면으로 남을 수 있다. `surface_tree(id)`가 더 이상 tree를 찾지 못하므로 자동 진입이나 복귀가 실행되지 않는다.

## 활성 표면의 modal 규칙

Tree 갱신 후 controller는 활성 표면의 tree에서만 `focus_trap()`을 조회한다. 다른 창에 보이는 dialog는 해당 창이 활성화되기 전까지 keyboard focus를 가져가지 않는다.

활성 tree에 가둠이 있고 논리 초점이 없다면 `focus_trap_entry()`가 가리키는 element에 초점을 둔다. 초점 표면은 활성 표면으로 기록된다. 진입은 tree 변화가 계기이므로 `focus_visible`은 꺼진다.

논리 초점이 이미 있으면 자동 진입은 실행되지 않는다. 사용자가 dialog 안의 다른 컨트롤을 먼저 눌렀다면 그 선택이 유지된다.

## 초점 복귀

가둠이 살아 있는 동안 controller는 다음 두 값을 한 쌍으로 보관한다.

- 가둠 element의 `focus_return()` id
- 그 가둠이 있던 활성 표면 id

가둠이 사라졌을 때 저장한 표면이 여전히 활성 표면인 경우에만 복귀를 시도한다. 대상이 현재 tree에서 `focusable()`이면 그 element에 초점을 두고 초점 테를 켠다. 대상이 사라졌거나 비활성이면 초점 없이 끝난다. 저장한 값은 성공 여부와 관계없이 한 번 소비된다.

표면 id를 함께 저장하는 규칙 때문에 주 창의 dialog가 열린 채 보조 창으로 전환해도 주 창의 복귀가 잘못 실행되지 않는다.

중첩 가둠에서는 `ui_tree::focus_trap()`이 그리기 순서에서 가장 뒤의 가둠을 선택한다. 안쪽 가둠이 사라지면 바깥 가둠이 다시 선택되고, 바깥 가둠의 진입점과 복귀점이 새로운 유효 계약이 된다.

## 초점 상실과 popup

최상위 표면이 keyboard focus를 잃으면 해당 표면과 그 표면에 앵커된 popup의 논리 텍스트 초점을 거둔다. Popup은 독립적인 keyboard focus를 갖지 않으므로 앵커의 상실을 함께 따라야 한다.

상실 이벤트가 popup을 닫지는 않는다. Popup의 닫힘은 `ui_popup::dismiss`가 `activation_changed` 같은 원인을 앱 메시지로 바꾸고, 앱이 다음 frame에서 popup을 제거하는 방식으로 처리한다.

## 반드시 유지할 불변식

- 주 창의 표면 id는 빈 문자열이다.
- 활성 표면은 최상위 창만 가리킨다.
- 논리 초점은 popup을 포함한 어느 표면에나 있을 수 있다.
- 활성 표면 상실은 마지막 활성 표면 값을 지우지 않는다.
- modal 가둠은 활성 표면의 tree에서만 선택한다.
- 초점 복귀 대상과 복귀 표면은 함께 기록하고 함께 비운다.
- 닫힌 표면은 tree 조회 실패로 자연스럽게 무효화된다.

## 검증 지침

[ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)에서 주 창과 보조 창 사이의 활성 전환, 보조 창 modal 진입, popup 논리 초점, 표면별 복귀, 닫힌 표면을 검증한다.

Win32 연결은 [window_surface.cpp](../src/win32/window_surface.cpp), popup과 TSF의 표면 선택은 [window_surface.h](../src/win32/window_surface.h)에서 확인할 수 있다.
