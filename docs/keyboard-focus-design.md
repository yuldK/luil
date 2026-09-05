# 키보드 초점과 Tab 순회

`luil`의 키보드 초점은 `interaction_controller`가 소유하는 논리 상태다. 게시된 `ui_tree`는 어떤 element가 초점을 받을 수 있는지 답하고, `interaction_snapshot`은 실제 초점과 표시 방식을 UI thread에 전달한다.

핵심 형식은 [ui_element.h](../include/luil/ui/ui_element.h), [ui_tree.h](../include/luil/ui/ui_tree.h), [ui_interaction.h](../include/luil/ui/ui_interaction.h), [ui_events.h](../include/luil/ui/ui_events.h)에 있다.

## Snapshot의 초점 값

`interaction_snapshot`은 초점과 관련해 다음 값을 함께 관리한다.

| 필드 | 의미 |
| --- | --- |
| `focused` | 논리 초점을 가진 element id |
| `focused_input` | 초점 element가 텍스트 입력일 때 같은 id |
| `focused_surface` | 초점 element가 속한 표면 |
| `focus_started_at` | caret 깜빡임의 기준 시각 |
| `focus_visible` | 키보드 탐색용 초점 테를 그릴지 여부 |

`focused_input`이 비어 있지 않으면 항상 `focused`와 같다. `clear_focus()`는 이 값들을 한 번에 비운다.

## 초점을 받을 수 있는 element

`ui_element::tab_stop()`의 기본값은 `left_click` 액션의 존재 여부다. 마우스로 실행할 수 있는 일반 컨트롤은 별도 설정 없이 Tab 자리도 된다.

구조 element나 caption 버튼처럼 Tab에서 제외해야 하는 대상은 `set_tab_stop(false)`를 사용한다. 반대로 action이 없는 사용자 정의 입력 element는 `set_tab_stop(true)`로 참여시킬 수 있다.

`focusable()`은 element 자신의 다음 조건을 모두 확인한다.

- tab stop이다.
- element가 arrange되었다.
- element가 표시 상태다.
- element가 활성 상태다.

조상 경로의 visibility는 `focus_order()`와 controller의 `visibly_contains()` 검사가 별도로 확인한다. Tree 갱신 뒤 element 조건이나 보이는 경로를 유지하지 못하면 기존 초점을 거둔다.

## Tab 순서

`ui_tree::focus_order()`는 보이는 tree를 그리기 순서로 순회한다. 별도 숫자 index 없이 화면 구조가 기본 순서의 정본이다.

Focus group은 순서에서 entry 하나로 접힌다. 앱은 `interaction_policy::order_focus()`로 전체 순서를 필터하거나 재배열할 수 있다. 이 hook은 Tab을 누를 때 호출된다.

Tab은 앞으로, Shift+Tab은 뒤로 이동하며 끝에서 반대쪽 끝으로 순환한다. 현재 초점이 없거나 policy가 만든 순서에 없으면 각각 첫 항목 또는 마지막 항목에서 시작한다.

순서가 비어 있으면 controller는 Tab을 소비하지 않고 앱의 `on_key()`로 보낸다. 현재 초점 element가 `takes_tab()`이면 Tab 순회도 실행하지 않는다.

## 초점을 만드는 입력

다음 입력이 논리 초점을 세울 수 있다.

- Tab과 Shift+Tab
- focus group의 방향키, Home, End, typeahead
- tab stop element의 포인터 press
- 접근성 `access_focus_event`
- focus trap의 자동 진입
- trap 제거 뒤 초점 복귀

포인터 press는 눌린 표면과 element를 기록하고 `focus_visible`을 끈다. 키보드 탐색과 접근성 focus 요청은 테를 켠다. 자동 진입은 테를 끄고 복귀는 켠다.

## 초점 테와 caret

초점 테는 `ui_tree::draw()`가 element 계층을 모두 그린 뒤 overlay로 그린다. Drag 표시와 tooltip은 그 뒤에 올라온다. 대상은 `focused_surface`가 현재 그리는 표면과 같고 `focus_visible`이 참일 때만 선택된다.

Caret은 `focused_input`과 `focus_started_at`을 사용하며 `focus_visible`과 독립적이다. 마우스로 텍스트 칸을 눌러도 caret은 보여야 하지만 일반 버튼의 초점 테는 숨길 수 있다.

표면별 그리기 전에는 `interaction_for_surface()`가 다른 표면의 hover, press, focus, menu highlight, drag 값을 제거한다. Element id는 tree 안에서만 유일하므로 표면 필터가 필요하다.

## Space와 Enter

Space와 Enter는 초점 element의 `left_click` 액션을 실행할 수 있다. 텍스트 입력에 초점이 있으면 Space는 문자 입력으로 남고 Enter는 기본 버튼 경로로 흐를 수 있다.

키보드 실행은 포인터 클릭과 같은 action과 `interaction_policy::on_click()` hook을 사용한다. 자세한 기본 버튼 규칙은 [enter-default-design.md](enter-default-design.md)를 참고한다.

## Focus group

Group은 여러 항목을 Tab의 한 자리로 접고 내부 탐색을 방향키에 맡긴다. 텍스트 편집과 값 step이 group 이동보다 우선한다. Group 계약은 focus-group-design.md에 정리되어 있다.

## Modal 범위

`ui_tree::focus_trap()`이 있으면 focus order와 기존 초점은 trap 안으로 제한된다. 가둠 밖의 초점은 tree 갱신 때 제거된다. Entry와 return 동작은 [focus-entry-design.md](focus-entry-design.md)를 따른다.

## 여러 표면

포인터 입력은 event surface의 tree에 초점을 둔다. 키 입력은 현재 논리 초점 표면에서 계속되고, 초점이 없을 때만 key event surface에서 시작한다. Popup은 활성화되지 않아도 논리 초점을 가질 수 있다.

각 최상위 창의 TSF host는 자기 표면 또는 자기에게 앵커된 popup의 `focused_input`만 본다. 다른 창의 텍스트 세션과 경쟁하지 않는다.

## 키 처리 우선순위

키 처리는 다음 의미 순서를 따른다.

1. 열린 메뉴
2. 초점 element activation
3. Tab 순회
4. 텍스트 편집
5. 값 step
6. focus group 이동
7. drag 취소
8. modal dismiss
9. 기본 버튼
10. 앱 `on_key()`

앞 단계가 키를 소비하면 뒤 단계는 실행하지 않는다. `nullopt`와 빈 action 목록을 구분해 소비 여부를 표현한다.

## 반드시 유지할 불변식

- 논리 초점은 controller 한 곳에서 갱신한다.
- 초점 id, 텍스트 초점, 표면, 시각, 표시 값은 함께 갱신한다.
- 기본 Tab 순서는 보이는 tree의 그리기 순서다.
- 숨김, 비활성, 제거, trap 이탈은 기존 초점을 무효화한다.
- 포인터 초점은 테를 끄고 keyboard 탐색은 켠다.
- Caret과 초점 테의 표시 조건은 독립적이다.
- 표면마다 snapshot을 필터한 뒤 그린다.

## 검증 지침

[ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)는 Tab 순환, Shift 역방향, 클릭 초점, Space, focus 표시, group, modal, 여러 표면을 검증한다. [ui_element_tests.cpp](../tests/ui_element_tests.cpp)는 `tab_stop()`과 `focusable()` 조건을, raster test는 초점 테가 overlay로 그려지는지 확인한다.
