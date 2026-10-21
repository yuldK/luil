# 표

`table_element`는 열 머리글이 있고 행을 고를 수 있는 표다. Qt의 `QTreeView`와 `QHeaderView`, WPF의 `DataGrid`에 해당하는 컴포넌트의 1단계다.

정의는 [table_element.h](../include/luil/ui/table_element.h)에 있다.

## 왜 가상 목록 위에 짓는가

행을 다루는 일은 이미 [가상 목록](virtual-list-design.md)이 한다. 가상화, 단일·다중 선택과 키보드 범위 선택([키보드 선택 규칙](keyboard-selection-design.md)), 커서, 글자 탐색, 더블 클릭, 컨텍스트 메뉴, 끌어 옮기기, 선택 읽기가 모두 그렇다. 표가 그것을 다시 짜면 같은 규칙이 두 벌이 되고, 한쪽이 반드시 틀린다.

그래서 표의 본문은 가상 목록 그대로이고(`table_config::rows`), 표가 더하는 것은 **열**이다.

| 표가 더하는 것 | 앱이 갖는 것 |
| --- | --- |
| 머리글과 본문의 열 경계를 같은 계산(`table_column_spans`)에서 낸다 | 열 목록·폭·정렬 표시 |
| 가로 스크롤 값 하나로 머리글과 본문을 함께 민다 | `scroll_x` |
| 열 경계 끌기를 폭 변화량으로 바꾸고, 최소 폭 아래 몫을 잘라 낸다 | 폭 적용과 저장(`resize_column_end`에서) |
| 머리글 누름을 정렬 요청으로 바꾼다 | 정렬 실행, 정렬 표시 갱신 |
| 칸 글 한 줄을 그리고 끝 정렬을 지킨다 | 칸 글(`cell_text`) 또는 칸 element(`build_cell`) |
| 본문을 DataGrid, 행을 DataItem, 머리글 칸을 HeaderItem으로 읽힌다 | 열 이름(`header`)과 행 이름(`label`) |

## 열과 가로 자리

열의 가로 자리는 폭의 누적 합이다. 머리글 칸, 경계 손잡이, 행 안의 칸이 모두 `table_column_spans`의 같은 값에 `scroll_x`를 빼고 배율을 곱해 앉는다. 그래서 어느 배율에서나 머리글과 본문의 열 경계가 물리 픽셀까지 같다.

`scroll_x`는 범위 밖이면 `arrange`가 다듬는다. 내용 폭(열 폭의 합)이 창보다 좁으면 0이다.

## 입력

| 입력 | 표가 내는 것 |
| --- | --- |
| 머리글 칸 누름 | `sort_request(열 키)`. 정렬할 수 없는 열(`sortable` 거짓)은 누를 것이 없다 |
| 열 경계 끌기 | `resize_column(열 키, 변화량)`을 끄는 동안 이어서, 놓으면 `resize_column_end(열 키)` 한 번 |
| 가로 휠 | `scroll_horizontal(변화량)`. 세로 휠은 본문이 받는다 (`route_wheel`의 이벤트 짝) |
| ←·→ | 본문이 쓰지 않으므로 표까지 올라와 `scroll_horizontal(±48)`이 된다 ([키 조상 전파](key-propagation-design.md)). 끝에서는 키만 삼킨다 |
| 그 밖의 키·행 누름 | 본문 가상 목록의 것 그대로다 |

- 머리글 칸과 경계 손잡이를 눌러도 초점은 본문 목록에 선다(`pointer_focus_target`). 정렬하려고 머리글을 누른 뒤에도 ↓가 산다.
- 머리글 칸과 손잡이는 Tab의 자리가 아니다. 열마다 자리가 서면 표 하나에 자리가 열 수만큼 늘기 때문이다. 키보드로 정렬하거나 폭을 바꾸는 길은 2단계의 머리글 메뉴다.
- 폭은 앱 상태라 끄는 동안의 tree는 옛 폭을 든다. 최소 폭 다듬기(`table_resize_delta`)는 마지막으로 게시된 폭을 기준으로 한다. 포인터가 최소 폭 아래로 내려갔다가 돌아오면 경계는 포인터보다 늦게 따라온다.

## 칸

- `build_cell`이 element를 돌려주면 그것이 칸이다. 자리표는 앱이 정하고 표 안에서 유일해야 한다. 행 키와 열 키를 함께 담는 것이 보통이다.
- nullptr이면 표가 `cell_text`의 글을 한 줄로 그린다. 칸마다 label element를 두지 않는다. 그러면 자리표가 행 수 × 열 수만큼 필요하기 때문이다.
- 끝 정렬(`column_align::end`)은 글이 들어갈 때만 오른쪽에 붙는다. 넘치면 시작에서 잘라 그린다. 넘친 숫자를 오른쪽에 붙이면 앞자리가 잘려 다른 숫자로 읽힌다.
- `cell_text`가 없으면 첫 열에 행 이름(`label`)을 보인다.

## 접근성

- 표 자신은 구조(`none`)다. 그 자리로 머리글(Header)과 본문(DataGrid)이 올라온다.
- 본문은 가상 목록의 역할 재지정(`virtual_list_config::role`·`row_role`)으로 DataGrid와 DataItem이 된다. `table`은 `list`처럼 선택 container라 고른 행을 읽는다.
- 머리글 칸은 HeaderItem이고 이름은 열 머리글이다.
- Grid·Table 패턴(행·열 위치, 열 머리글 연결)과 화면 밖 행의 탐색(ItemContainer)은 C5 단계에서 붙인다.

## 범위

이번 단계에 없는 것과 그 단계다.

- 셀 커서와 ←·→ 셀 이동, F2·Enter·Esc 편집 수명, 열 끌어 재배치·숨김, 트리 열(깊이·펼침), 체크 열 (C4)
- 가로 스크롤 막대 (Codex 트랙의 X0 `scrollbar_axis::horizontal`이 들어온 뒤)
- 가운데 말줄임(경로 열) (X0의 `elide_text_middle` 뒤)
- 고정 열, 열 가상화, 셀 범위 선택 (뒤로 미룬 확장)

## 검증

[table_element_tests.cpp](../tests/table_element_tests.cpp)
- 열 기하(누적 합·다듬기·최소 폭)
- 배율 1·1.5·2에서 머리글 칸과 본문 칸의 경계 일치
- 가로 스크롤 다듬기와 행 가상화
- 머리글 누름의 정렬 요청과 초점 유지, 정렬할 수 없는 열
- 경계 끌기의 변화량·최소 폭·끝 알림
- 가로 휠과 세로 휠의 임자
- ←·→의 가로 스크롤과 끝에서 삼키기
- DataGrid·DataItem·HeaderItem 역할
