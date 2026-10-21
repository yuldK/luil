# 항목 모델 도우미

[item_model.h](../include/luil/ui/item_model.h)는 앱 모델을 목록과 표의 항목으로 옮기는 순수 함수들이다. Qt의 `QAbstractItemModel`과 `QSortFilterProxyModel`이 하는 일 중 불변 상태와 메시지 구조에 맞는 것만 가져온다.

## 왜 element가 아니라 함수인가

luil의 tree는 frame마다 새로 지어지고, 상태는 앱이 든다. 모델 객체가 변경 신호(rowsInserted, dataChanged)를 내는 Qt의 구조는 여기서 할 일이 없다 — "무엇이 바뀌었는가"는 항목 키가 말한다. 그래서 도우미는 앱 상태를 받아 목록에 실을 값을 계산하는 함수다.

| 도우미 | 받는 것 | 내는 것 |
| --- | --- | --- |
| `flatten_tree` | 계층 노드, 펼친 키, 정렬·필터 | 깊이·펼침이 든 줄 (`tree_row`) |
| `tree_nodes_to_load` | 계층 노드, 펼친 키 | 불러오기를 시작할 키 |
| `sort_filter` | 항목, 비교, 남길 것 | 보기의 원본 색인 순서 (`view_order`) |
| `request_ledger` | 키마다 낸 요청 | 늦게 온 결과인지의 판정 |

## 계층 평탄화

`tree_node`는 키·이름·자식·불러온 상태(`node_load`)·활성을 든다. `flatten_tree`는 펼친 가지의 자식을 깊이 + 1로 그 아래에 편다.

- 가지는 자식이 있거나 아직 다 불러오지 않은(`loaded`가 아닌) 노드다. 묻지 않은 폴더도 삼각형이 선다.
- 펼친 가지가 `unloaded`·`loading`이면 그 아래에 **불러오는 중 줄**이, `failed`면 **실패 줄**이 선다. 키는 `노드 키 + U+001F + loading/failed`다 (`tree_loading_suffix`·`tree_failed_suffix`).
- 불러오는 중 줄은 비활성 항목이라 키보드가 서지 않는다. 실패 줄은 활성이다 — 그 줄에서 Enter로 다시 시도를 실행할 수 있어야 한다. 무엇을 그릴지(진행 표시, 다시 시도 버튼)는 앱의 `build_row`가 `tree_row::kind`를 보고 정한다. 진행 표시는 Codex 트랙의 스피너(X0)가 그 자리에 선다.
- 상태 줄의 글은 앱이 준다 (`tree_flatten_options::loading_label`·`failed_label`). 라이브러리는 글을 정하지 않는다.
- 형제 정렬(`less`)은 같은 부모 아래에서만 견준다. 계층은 그대로다.
- 필터(`keep`)는 맞는 노드와 그 조상을 남긴다 (Qt의 재귀 필터). 남은 노드의 자손도 맞아야 남는다. `expand_matches`를 켜면 맞는 자손이 있는 길을 펼친 것으로 보고, 앱의 펼침 상태는 바꾸지 않는다 — 필터를 지우면 사용자가 펼쳐 둔 그대로다.

결과 줄의 항목(`tree_row_items`)을 가상 목록이나 표 본문에 싣는다. 펼치기는 목록의 `set_expanded` 메시지로 오고, 앱은 펼친 키 집합을 고친다.

### 지연 로딩의 한 바퀴

1. 사용자가 접힌 폴더를 펼친다. 앱이 `set_expanded(key, true)`를 받아 펼친 집합에 넣는다.
2. 앱이 `tree_nodes_to_load`로 펼쳤지만 묻지 않은 노드를 찾는다. 각 키에 `request_ledger::issue`로 표를 받고 요청을 보낸 뒤 노드를 `loading`으로 바꾼다.
3. 다음 frame의 평탄화가 그 아래에 불러오는 중 줄을 둔다.
4. 결과가 표와 함께 온다. `settle(key, token)`이 참이면 자식을 넣고 `loaded`로, 실패면 `failed`로 바꾼다. 거짓이면 버린다.
5. 결과 전에 접었다면 `cancel(key)`로 거두거나 그대로 둔다. 다시 펼쳐 새로 물으면 옛 요청의 결과는 `settle`에서 버려진다.

## 정렬과 필터

`sort_filter`는 원본을 그대로 두고 보기의 순서(`view_order::source_index`)만 낸다. 정렬은 안정 정렬이라 같은 값이 많은 열로 정렬해도 frame마다 줄이 뒤섞이지 않는다. 정렬 열을 바꾸거나 필터를 지워도 원본을 다시 읽지 않는다.

- 표의 머리글 누름(`sort_request`)을 받은 앱이 정렬 열과 방향을 바꾸고, 다음 frame에 `sort_filter`로 줄을 세운다.
- 보기가 바뀐 뒤 선택은 `selection_reconcile`로 새 모델에 맞춘다 ([키보드 선택 규칙](keyboard-selection-design.md)).
- 항목 형은 첫 인자에서만 정해지므로 람다를 그대로 넘긴다.

## 늦게 온 결과

`request_ledger`는 키마다 마지막으로 낸 요청 표를 든다. 같은 키의 새 요청은 앞 요청을 낡게 한다. 결과를 받을 때 `settle`로 그 키의 마지막 표인지 확인하고, 받아들이면 지운다 — 같은 결과가 두 번 와도 한 번만 받는다. 키 하나만 쓰면 검색어 후보처럼 "마지막 것만 산다"는 요청의 장부다.

## 검증

[item_model_tests.cpp](../tests/item_model_tests.cpp)는 접힌·펼친 평탄화와 깊이, 묻지 않은 가지의 삼각형, 불러오는 중·실패 줄의 키·이름·활성, 부모 안의 형제 정렬, 조상을 남기는 필터와 `expand_matches`, 불러올 노드 찾기(접힌 가지 아래 제외), 안정 정렬·필터·보기 색인, 요청 장부의 늦은 결과·중복 결과·다른 키·취소를 확인한다.
