# 초점 진입과 복귀

초점 진입은 새로 나타난 focus trap이 명시한 element에 논리 초점을 두는 규칙이다. 초점 복귀는 trap이 사라진 뒤 앱이 명시한 바깥 element로 초점을 돌려보낸다. 두 동작은 tree의 선언을 `interaction_controller`가 해석한다.

관련 API는 [ui_element.h](../include/luil/ui/ui_element.h), [ui_tree.h](../include/luil/ui/ui_tree.h), [modal_host_element.h](../include/luil/ui/modal_host_element.h)에 있다.

## 선언 모델

Focus trap element는 세 값을 가진다.

- `focus_trap()`: 자손 밖으로 키보드 초점이 나가지 못하게 한다.
- `focus_entry()`: trap이 유효해졌을 때 자동으로 초점을 받을 자리를 이름 짓는다.
- `focus_return()`: trap이 사라진 뒤 초점을 돌려보낼 바깥 자리를 이름 짓는다.

```cpp
luil::modal_host_config dialog {
    .owner = u8"rename",
    .dismiss = luil::make_message_action(close_dialog {}),
    .focus_entry = { luil::ui_element_kind::text_input, u8"name" },
    .focus_return = { luil::ui_element_kind::text_button, u8"rename" },
};
```

빈 id는 해당 동작을 요청하지 않는다는 뜻이다. `focus_entry`가 비어 있으면 사용자가 Tab을 누를 때 trap 안의 첫 초점 자리에서 시작한다. `focus_return`이 비어 있으면 dialog가 사라진 뒤 초점 없이 남는다.

## 진입점 해석

`ui_tree::focus_trap_entry()`는 유효한 trap의 진입점을 계산한다.

- trap이 없거나 `focus_entry`가 비어 있으면 `nullopt`다.
- 이름 지은 element가 trap 안에서 `focusable()`이면 그 id를 반환한다.
- 이름 지은 element가 없거나 초점을 받을 수 없으면 trap 안의 첫 초점 자리로 물러선다.
- trap 안에 초점 자리가 하나도 없으면 `nullopt`다.

Fallback은 조건부 필드 때문에 dialog 전체가 키보드로 막히는 일을 방지한다. 자동 진입 자체는 반드시 비어 있지 않은 `focus_entry`로 요청한다.

## 진입 시점

Controller는 tree와 surface tree 목록을 받은 뒤 `update_focus()`에서 진입을 판정한다.

1. 활성 표면의 tree에 trap이 있다.
2. 현재 논리 초점이 없다.
3. `focus_trap_entry()`가 유효한 id를 반환한다.

한 번 초점을 두면 두 번째 조건이 거짓이 되므로 같은 tree를 다시 받아도 초점을 덮어쓰지 않는다. 사용자가 trap 안의 다른 element를 먼저 누른 경우에도 현재 초점이 유지된다.

## 기존 초점 정리

Tree가 갱신되면 다음 조건을 먼저 검사하고 위반하면 `clear_focus()`를 호출한다.

- 초점 표면의 tree와 element가 존재한다.
- element 자신과 조상 경로가 보인다.
- element가 활성이다.
- element가 유효한 trap 안에 있다.

정리와 진입은 같은 `update_focus()` 안에서 순서대로 실행된다. Dialog를 연 버튼의 초점을 거둔 직후 dialog 진입점을 세울 수 있다.

## 초점 표시

자동 진입은 tree 변화에서 시작되므로 `focus_visible`을 `false`로 둔다. Enter, Space, 화살표는 논리 초점을 사용할 수 있지만 초점 테는 그리지 않는다. 사용자가 Tab이나 방향키로 이동하면 `focus_visible`이 켜진다.

자동 진입에는 이벤트 시각이 없으므로 `focus_started_at`은 비어 있다. 텍스트 입력 caret과 update 예약 코드는 이 상태를 허용한다.

## 복귀 수명

Controller는 유효한 trap이 있는 동안 그 trap의 `focus_return`과 활성 표면을 복사해 둔다. Trap이 사라지고 저장된 표면이 현재 활성 표면과 같으면 복귀를 시도한다.

대상이 현재 tree에서 `focusable()`이면 초점을 두고 `focus_visible`을 켠다. 대상이 없거나 비활성이면 복귀하지 않는다. 저장된 return 값은 성공 여부와 관계없이 한 번 소비한다.

## 중첩 trap과 여러 표면

여러 trap이 보이면 그리기 순서에서 마지막 trap이 유효하다. 안쪽 dialog가 사라지면 바깥 trap이 다시 유효해지고 바깥 trap의 `focus_entry`가 적용된다. 최종 trap이 사라졌을 때 그 trap이 제공한 바깥 return 지점으로 복귀한다.

진입과 복귀는 활성 최상위 표면의 tree에만 적용한다. 복귀 id와 표면 id를 한 쌍으로 저장하므로 다른 보조 창을 활성화해도 이전 창의 복귀가 실행되지 않는다. 자세한 표면 모델은 [active-surface-design.md](active-surface-design.md)를 참고한다.

## 반드시 유지할 불변식

- 자동 진입은 비어 있지 않은 `focus_entry`로 요청한다.
- 유효하지 않은 진입점은 trap 안 첫 초점 자리로 fallback한다.
- 현재 초점이 있으면 자동 진입이 덮어쓰지 않는다.
- 초점 유효성 검사 후 진입 판정을 실행한다.
- 자동 진입은 초점 테를 끄고 복귀는 켠다.
- 복귀 id와 표면 id는 함께 저장하고 한 번만 소비한다.
- 모든 초점 변경은 snapshot의 관련 필드를 함께 갱신한다.

## 검증 지침

[ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)에서 진입점 성공, fallback, 빈 진입점, 사용자 선점, 숨김·비활성화, 중첩 trap, 복귀 성공과 실패, 여러 표면을 검증한다. [modal_host_element_tests.cpp](../tests/modal_host_element_tests.cpp)는 host의 trap, entry, return 속성을 확인한다.
