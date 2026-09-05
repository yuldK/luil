# 접근성 동작

UI Automation 동작은 pointer와 keyboard 입력이 사용하는 element 동작 및 application dispatch 경로를 함께 사용한다. 접근성 계층은 UIA 요청을 플랫폼 중립적인 `access_request`로 변환하고, 현재 element에 실행 계획을 요청한 뒤 생성된 `input_action`을 소유 window의 정상 처리 순서에서 전달한다.

공개 계획 API는 [`include/luil/ui/accessibility.h`](../include/luil/ui/accessibility.h)에 있으며, UIA pattern 실행은 [`src/win32/uia_provider.cpp`](../src/win32/uia_provider.cpp)에 구현되어 있다.

## 명령

`access_command`는 다음 여섯 동작을 정의한다.

- `invoke`: 상태가 없는 명령을 실행한다.
- `toggle`: checked 상태를 반전한다.
- `select`: 단일 선택 항목을 선택한다.
- `expand`, `collapse`: disclosure 상태를 절대값으로 요청한다.
- `set_value`: 숫자 값을 절대값으로 요청한다.

`access_request::value`는 `set_value`에서만 의미가 있다.

`plan_access_request(element, request)`의 결과는 세 가지로 구분된다.

- `nullopt`: element가 동작을 수행할 수 없다.
- 빈 vector: 요청은 유효하지만 이미 요청한 상태다.
- 비어 있지 않은 vector: 이 동작들을 dispatch한다.

이 구분은 UIA의 절대 상태 연산에 필요하다. 이미 선택된 항목을 선택하거나 열린 그룹을 다시 열어도 반대 상태로 바뀌지 않고 성공한다. Invoke에는 이미 실행된 상태가 없으므로 빈 invoke 계획은 유효하지 않다.

## Element 구현

Element는 `access_actions(request)`를 override하고 일반 입력과 같은 callback으로 가능한 동작을 만든다. `plan_access_request`는 공개된 semantic 상태가 요청을 지원하는지 먼저 확인한 뒤 element에 동작을 요청한다.

상태와 기능은 서로 일치해야 한다.

- `checked`가 있으면 Toggle을 제공할 수 있다.
- `selected`가 있으면 SelectionItem을 제공할 수 있다.
- `expanded`가 있으면 ExpandCollapse를 제공할 수 있다.
- `range`가 있으면 RangeValue를 제공할 수 있다.
- invoke 동작이 있는 상태 없는 button, link, menu item은 Invoke를 제공할 수 있다.

상태를 공개하는 것만으로 쓰기가 허용되지는 않는다. Progress bar는 range를 공개하면서 set-value 동작을 반환하지 않아 읽기 전용일 수 있다. Dropdown은 일반 toggle click과 별도로 절대 open/close 동작을 제공해야 신뢰할 수 있는 Expand와 Collapse를 공개할 수 있다.

Radio button과 list 또는 tab item은 toggle이 아니라 selection 의미를 사용한다. 이를 포함하는 list, tab list, radio group은 Selection을 제공한다. Luil은 단일 선택만 지원한다. `CanSelectMultiple`은 false이고, 선택 항목 제거와 현재 선택을 대체하지 않는 항목 추가는 유효하지 않다.

## UIA pattern 계약

Provider는 element의 현재 semantic 정보가 지원하는 pattern만 공개한다.

| UIA pattern | 조건 | 동작 |
| --- | --- | --- |
| Invoke | invoke 계획이 있는 상태 없는 button, link, menu item | `invoke` |
| Toggle | `checked`가 있음 | `toggle` |
| SelectionItem | `selected`가 있음 | `select` |
| Selection | role이 list, tab list, radio group | 선택된 child 조회 |
| ExpandCollapse | `expanded`가 있음 | `expand` / `collapse` |
| RangeValue | `range`가 있음 | range 조회, 계획이 있을 때만 쓰기 |
| Value | edit, combo box 또는 비어 있지 않은 text value | 값 조회, edit는 text 경로로 쓰기 |
| Text | editable element에 text-input document가 있음 | text range 공개 |

Pattern 제공 여부와 쓰기 가능 여부는 별도로 판정한다. RangeValue의 `get_IsReadOnly`는 set-value 계획이 있는지 확인한다. Provider는 NaN, infinity, 공개된 최솟값과 최댓값 범위를 벗어난 값을 거부한다.

Value에서 combo box와 그 밖의 textual value는 읽기 전용이다. Edit는 element가 text input을 공개하고 application의 `interaction_policy`가 `text_input_target`을 찾을 때만 쓸 수 있다. `SetValue`는 TSF와 같은 application 편집 경로를 거치는 replace-all 요청이 된다.

## 도달 가능성과 오류

실행 직전에 provider는 현재 tree에서 id를 다시 찾고 `enabled()`와 `access_reachable(tree, id)`를 확인한다. Element와 모든 ancestor가 visible이고 활성 focus trap이 있다면 그 안에 있을 때만 도달 가능하다. 따라서 automation은 modal overlay 뒤의 control을 실행할 수 없다. Viewport 밖으로 clip된 것만으로는 도달 불가능해지지 않는다. Keyboard focus가 해당 항목을 화면에 나타낼 수 있기 때문이다.

UIA 경계는 실패 원인을 다음과 같이 구분한다.

- element, surface 또는 tree가 없음: `UIA_E_ELEMENTNOTAVAILABLE`
- disabled이거나 현재 도달할 수 없음: `UIA_E_ELEMENTNOTENABLED`
- 지원하지 않는 동작: `UIA_E_INVALIDOPERATION`
- 의미상 읽기 전용인 text 또는 숫자 값: `E_ACCESSDENIED`
- 잘못된 pointer, 숫자 또는 range 인자: 대응하는 COM argument 오류

## Dispatch와 재진입

UIA 호출은 window sizing loop, system menu, paint 또는 다른 automation event 처리 중에 들어올 수 있다. 이때 반환된 window command를 즉시 실행하면 활성 COM stack 아래의 surface가 파괴되거나 바뀔 수 있다.

`window_surface`는 계획된 동작을 pending 목록에 추가하고 `access_dispatch_message`를 post한다. Window가 정상 message 순서에서 이를 처리할 때 각 동작을 `surface_context::dispatch_action`으로 보낸다. App message, UI command, clipboard request는 원래 목적지를 유지한다.

Provider는 동작 목록을 dispatch한 뒤 Invoke event를 알린다. Checked, selected, expanded, range, text value 같은 상태 변화는 이후 published frame의 접근성 snapshot 비교로 알린다. Application logic이 동작을 처리하기 전에 상태가 바뀌었다고 보고하지 않기 위한 순서다.

## Focus 요청

UIA `SetFocus`는 input controller의 mutable interaction state를 바꾸므로 action dispatch와 별도 경로를 쓴다. Surface는 `access_focus_event { id, surface }`를 input pump에 post한다. Controller는 현재 tree를 다시 찾고 focusable이며 도달 가능한 target에만 logical focus를 공개한다. 이 경로는 secondary window와 popup에서도 surface identity를 유지한다. 이미 제거된 target에는 focus가 가지 않는다.

## Text 편집

편집 가능한 Value와 Text provider는 TSF와 같은 document source를 사용한다. Provider는 application text target을 찾고 도달 가능성과 enabled 상태를 검사한 뒤 element를 직접 변경하지 않고 `replace_all` 요청을 전달한다. 다음 application frame이 새 text, caret, selection을 제공한다.

Text range provider는 document pointer 대신 element id와 UTF-8 byte offset을 보관한다. 각 query에서 현재 표시된 document를 다시 찾고 offset을 유효한 character 경계로 제한한다. 따라서 immutable tree가 다시 만들어져도 automation client가 range를 안전하게 보유할 수 있다.

## 검증

[`tests/accessibility_tests.cpp`](../tests/accessibility_tests.cpp)는 command 계획, 멱등적인 select와 disclosure 요청, 읽기 전용 range, pattern 공개, disabled 및 modal 거부, 지연된 action 전달, surface별 focus, selection container 규칙, text replacement, range 이동, geometry, change event를 검증한다.
