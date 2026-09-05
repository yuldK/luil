# Focus group과 Tab 순서

Focus group은 여러 관련 컨트롤을 Tab 순회에서 한 자리로 접는다. Tab은 group 안의 대표 항목에 들어오고, 방향키와 Home/End는 group 구성원 사이에서 초점을 옮긴다. 라디오 묶음, 탭 막대, 목록이 이 모델을 사용한다.

공개 계약은 [ui_element.h](../include/luil/ui/ui_element.h), [ui_tree.h](../include/luil/ui/ui_tree.h), [ui_interaction.h](../include/luil/ui/ui_interaction.h)에 있다.

## Group 선언

컨테이너는 `set_focus_group()`으로 탐색 축을 정한다.

```cpp
group->set_focus_group(luil::focus_axis::horizontal);
group->set_focus_entry(selected_id);
```

`focus_axis`의 의미는 다음과 같다.

| 값 | group이 처리하는 방향키 |
| --- | --- |
| `none` | group이 아님 |
| `horizontal` | Left, Right |
| `vertical` | Up, Down |
| `both` | 네 방향 모두 |

Home과 End는 축과 무관하게 group의 처음과 끝으로 이동한다. Control 또는 Alt가 함께 눌린 키는 앱 단축키로 흐른다.

## Tab에서 한 자리로 접기

`ui_tree::focus_order()`는 보이는 element를 그리기 순서로 수집한다. Focus group을 만나면 자손의 모든 tab stop을 펼치지 않고 group entry 하나만 순서에 넣는다.

Entry 선택 규칙은 다음과 같다.

- `focus_entry`가 group 구성원이며 `focusable()`이면 그 항목을 사용한다.
- 지정한 항목이 유효하지 않으면 첫 focusable 구성원을 사용한다.
- 구성원이 없으면 group은 Tab 순서에 자리를 만들지 않는다.

Group 컨테이너 자체에는 초점이 서지 않는다. 실제 항목에 초점이 있어야 초점 테와 접근성 상태가 정확한 대상을 가리킨다.

## Group 구성원

`focus_group_of(id)`는 해당 element를 감싸는 가장 안쪽 group과 그 구성원 목록을 반환한다. 구성원은 보이는 focusable element의 그리기 순서다. 중첩 group은 바깥 group에서 한 자리로 접힌다.

구성원이 둘 이상이면 방향키는 끝에서 반대쪽 끝으로 순환한다. Home과 End는 순환하지 않으며 이미 끝에 있어도 group이 키를 소비한다. 키가 앱 스크롤 단축키로 새지 않게 하는 규칙이다. 구성원이 하나뿐이면 이동할 곳이 없으므로 이 키들을 소비하지 않는다.

방향이 맞지 않는 화살표는 group이 소비하지 않는다. 예를 들어 세로 목록에 초점이 있을 때 Left와 Right는 앱 정책까지 흐를 수 있다.

## 텍스트 입력과 값 컨트롤

키 우선순위는 초점 element의 의미를 group보다 앞에 둔다.

1. 텍스트 입력의 caret 이동과 편집
2. `key_step_target`의 값 변경
3. focus group 이동

따라서 group 안의 텍스트 칸에서 Left와 Right는 caret을 옮기고, slider에서 방향키는 값을 바꾼다. 남은 방향키만 group 탐색에 쓰인다.

## 글자 탐색

텍스트 입력에 초점이 없고 focus group 안에 있으면 `character_typed_event`가 typeahead 탐색에 쓰인다. 각 구성원의 `search_label()`이 누적 질의로 시작하는지 확인한다.

첫 글자는 현재 항목 다음부터 찾아 같은 첫 글자를 반복해 누를 때 후보들을 순환한다. 연속 입력은 현재 후보부터 다시 찾아 여러 글자 접두사를 완성한다. `interaction_config::typeahead_reset_time`이 지나거나 초점이 다른 경로로 이동하면 질의를 비운다.

## Tab 순서 정책

앱은 `interaction_policy::order_focus()`에서 tree가 만든 순서를 필터하거나 다시 배열할 수 있다.

```cpp
std::vector<luil::ui_element_id> order_focus(
    const luil::ui_tree& tree,
    std::vector<luil::ui_element_id> order) override;
```

이 hook은 Tab을 누를 때만 호출된다. Group 내부 순서는 바꾸지 않는다. Group의 자식 순서가 방향키 순서의 정본이다.

별도의 tab index를 element에 저장하지 않는 이유는 화면 구조와 순서를 한 tree에 유지하기 위해서다. 특별한 워크플로만 policy에서 전체 순서를 명시한다.

## Tab을 직접 쓰는 element

`set_takes_tab(true)`는 현재 초점 element가 Tab을 문자나 자체 명령으로 소비하도록 한다. 이 경우 controller는 Tab 순회를 실행하지 않는다. 빠져나가는 키 조합이나 명령은 앱이 해당 element의 입력 계약으로 제공해야 한다.

## 기본 group 구성 요소

다음 컴포넌트는 group 의미를 자체 구성에 포함한다.

- `choice_group_element`: 선택 항목 사이의 탐색
- `tab_bar_element`: 탭 사이의 가로 탐색
- `list_element`: 보이는 행 사이의 세로 탐색
- `grouped_list_element`: 그룹과 항목을 포함한 목록 탐색

선택 상태는 앱 상태이고 `focus_entry`는 그 선택된 항목을 가리킨다. 키보드 초점 이동 자체가 선택 메시지를 자동으로 만들지는 않는다.

## 반드시 유지할 불변식

- Group은 Tab 순서에서 한 자리다.
- 초점은 group 컨테이너가 아니라 실제 구성원에 선다.
- 구성원 순서는 보이는 tree의 그리기 순서다.
- 유효하지 않은 entry는 첫 구성원으로 fallback한다.
- 구성원이 둘 이상이면 방향키는 순환하고 Home/End는 처음과 끝으로 고정 이동한다.
- 텍스트 편집과 값 step이 group 이동보다 우선한다.
- `order_focus()`는 group 내부 순서를 바꾸지 않는다.

## 검증 지침

[ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)는 group 접기, entry fallback, 축별 방향키, 순환, Home/End, typeahead, 중첩 group, `takes_tab`, policy 순서를 검증한다. 각 컴포넌트 test는 선택된 항목이 entry로 설정되는지 확인해야 한다.
