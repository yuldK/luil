# 키보드 선택 규칙

여러 항목을 고르는 목록에서 기준점(anchor)과 커서를 갈라 두고, 키보드와 포인터가 같은 규칙으로 선택을 바꾼다. Qt의 ExtendedSelection, WPF DataGrid의 Extended 모드, Windows 탐색기와 같은 규칙이다.

이 규칙은 [ui_element.h](../include/luil/ui/ui_element.h)의 `key_step_context`, [virtual_list_element.h](../include/luil/ui/virtual_list_element.h)의 `move_with`·`toggle_at_cursor`·`select_all`, [selection_model.h](../include/luil/ui/selection_model.h)의 순수 함수에 정의된다.

## 왜 필요한가

가상 목록은 이미 다중 선택 상태(`multiple_selection`·`selection`)와 Ctrl·Shift+클릭의 문맥(`pick`)을 가졌다. 비어 있던 것은 키보드다.

- 커서를 옮기는 `move`는 키만 받는다. Shift+↓와 ↓가 같은 `value_step`이라 앱은 범위를 늘리라는 것인지 알 수 없었다.
- Ctrl+↓는 단축키 수정자라 목록에 오지 않았다. "선택을 두고 커서만 옮겨 Ctrl+Space로 하나 더 고르기"를 할 수 없었다.
- Ctrl+A는 앱 정책이 받아야 했고, 앱은 지금 초점이 어느 목록인지 tree에서 다시 찾아야 했다.

그래서 앱은 목록의 키를 가로채 기준점과 범위를 직접 짜게 되고, 그 규칙은 앱마다 조금씩 틀린다.

## 책임

**상태는 앱이 든다.** 목록은 입력의 뜻을 메시지에 싣고, 계산은 순수 함수가 한다.

| 누가 | 무엇을 |
| --- | --- |
| controller | Shift·Ctrl을 `key_step_context`로 옮긴다. Ctrl+단계 키를 `on_step_with`를 든 element에 준다 |
| 가상 목록 | 걸음을 모델 위의 자리로 바꾸고 `move_with(key, context)`를 낸다. Ctrl+Space·Ctrl+A를 자기 키로 받는다 |
| `selection_model.h` | 다음 선택을 계산한다 (`selection_pick`·`selection_move`·`selection_toggle`·`selection_select_all`·`selection_reconcile`) |
| 앱 | `item_selection`을 들고, 메시지를 받으면 도우미를 부르고, 다음 frame에 `selection`·`cursor`를 싣는다 |

## 규칙

| 몸짓 | 고른 것 | 기준점 | 커서 |
| --- | --- | --- | --- |
| 누름, 이동 | 그것 하나 | 그리로 | 그리로 |
| Ctrl+누름 | 그것을 뒤집는다 | 그리로 | 그리로 |
| Shift+누름, Shift+이동 | 기준점부터 그것까지만 | 그대로 | 그리로 |
| Ctrl+Shift+누름, Ctrl+Shift+이동 | 지금 것에 기준점부터 그것까지를 더한다 | 그대로 | 그리로 |
| Ctrl+이동 | 그대로 | 그대로 | 그리로 |
| Ctrl+Space | 커서 항목을 뒤집는다 | 커서로 | 그대로 |
| Ctrl+A | 활성 항목 전부 | 그대로 | 그대로 |

- 비활성 항목은 고르지 않는다. 범위 안에 있어도 빠진다. 커서는 비활성 항목에도 설 수 있다.
- 기준점이 모델에 없으면(필터로 사라졌다) 커서를, 그것도 없으면 옮겨 간 자리를 기준점으로 삼는다.
- 고른 키는 **모델 순서**로 둔다. 같은 선택이 언제나 같은 값이다.

## 모델이 바뀔 때

필터·정렬·삭제 뒤에는 `selection_reconcile(selection, previous, items)`로 맞춘다.

- 새 모델에 없거나 비활성이 된 키는 고른 것에서 빠진다.
- 커서가 사라졌으면 옛 모델에서 커서 뒤쪽의 가장 가까운 남은 항목으로, 뒤에 없으면 앞쪽으로 옮긴다. 옛 모델을 모르면 새 모델의 첫 활성 항목이다.
- 기준점도 같은 규칙으로 옮기고, 찾지 못하면 커서로 둔다.

## 키가 오는 길

- 화살표·Page·Home·End는 값 단계 경로다 ([value-step-design.md](value-step-design.md)). `move_with`를 든 목록은 `on_step_with`를 세워 Shift·Ctrl의 뜻까지 받는다.
- Ctrl+Space·Ctrl+A는 목록의 `key_handler`다 ([키 조상 전파](key-propagation-design.md)). 초점이 행 안의 부품에 있어도 그 부품이 쓰지 않으면 목록까지 올라온다. 초점이 텍스트 칸이면 칸의 전체 선택이 먼저다.
- 행 누름은 고르기를 `pick`이 이미 정했으므로 `move_with`는 커서만 옮기는 문맥(`keep_selection`)으로 온다.
- 커서가 제자리여도 고른 것이 바뀌는 걸음(그냥·Shift)은 메시지를 낸다. 첫 항목에서 Home을 누르면 선택이 그 하나로 접힌다. 커서만 옮기는 Ctrl 걸음은 제자리에서 내지 않는다.

## 호환

`move`만 든 목록은 바뀌지 않는다. Shift는 보이지 않고, Ctrl+화살표·Ctrl+A는 앱 정책으로 흐른다. `list_element`는 단일 선택이라 이번 규칙의 대상이 아니다.

## 검증

- [selection_model_tests.cpp](../tests/selection_model_tests.cpp): 표의 각 줄, 비활성 항목, 기준점 대체, 전체 선택, 모델이 바뀐 뒤의 정합.
- [keyboard_selection_tests.cpp](../tests/keyboard_selection_tests.cpp)
  - `move`만 든 목록의 회귀
  - `move_with`의 걸음과 수식 키
  - 제자리 걸음의 메시지 규칙
  - Ctrl+A·Ctrl+Space가 목록에 오는 것과 행 안 단추에서도 올라오는 것
  - 행 누름과 글자 탐색의 문맥
