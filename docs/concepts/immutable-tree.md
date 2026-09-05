# 불변 UI tree

[`ui_tree`](../../include/luil/ui/ui_tree.h)는 frame마다 새로 만들고 게시한 뒤에는 수정하지 않는다. UI 스레드는 그 tree를 그리고, input 스레드는 hit test하며, Win32의 동기 caption hit test도 같은 게시본을 조회한다. 게시본의 element는 읽기 전용으로 공유한다. 게시를 운반하는 slot의 동기화와 별도로, tree 자체를 수정하기 위한 락은 필요하지 않다.

상태가 바뀌면 기존 element를 수정하지 않고 새 tree를 `latest_slot`에 게시한다. 목록은 보이는 행만 tree에 넣어 가상화할 수 있다. 부분 갱신 경로가 없기 때문에 화면의 구조와 앱 상태가 섞이지 않는다.

## 색인과 배치 검증

생성 시 pre-order로 element를 훑어 `find(id)`와 `ids_of_kind(kind)`에 쓰는 색인을 만든다. id는 tree 안에서 유일해야 하며 다른 표면 tree에서는 재사용할 수 있다. 같은 id가 중복되면 `find()`는 먼저 등록한 element를 반환하고 뒤의 중복을 `duplicate_ids()`에 모은다. 또한 `arrange()`가 호출되지 않은 보이는 element는 `unarranged()`에 모은다. 둘은 tree를 변경하지 않고 앱 테스트가 확인할 수 있는 진단이다. 숨긴 element는 배치하지 않아도 된다.

자식을 배치하는 root는 `make_arranged_tree(root, slot, scale)`로 배치와 tree 생성을 묶을 수 있다. caption과 내용을 각각 배치하는 조립은 배치를 마친 뒤 `ui_tree` 생성자를 사용한다. 부모는 `arrange_context::for_child(slot)`로 자식의 자리와 배율·`scroll_offset`을 전달한다. 컨테이너 선택은 다음 규칙을 따른다.

| 특성 | 컨테이너 |
| --- | --- |
| 한 줄 배치 | `stack_element` |
| 여러 줄 배치 | `wrap_element` |
| 자식 영역을 자르지 않음 | `stack_element`, `wrap_element` |
| 넘침을 자르고 스크롤 | `strip_element`, `scroll_view_element` |

표시 전용인 `progress_element`와 값을 바꾸는 `slider_element`처럼 모양이 비슷해도 계산과 상호작용이 다르면 element를 합치지 않는다. `text_input_element`의 내부 아이콘과 지우기 버튼은 텍스트 폭·hit test·IME 좌표가 같은 `inner_width()`를 사용해야 하므로 외부 stack으로 분리하지 않는다. `list_element`는 factory의 유무로 선택·tree view·재정렬 기능을 조합한다.

## Hit test와 오버레이

`hit_test(x, y)`는 자식을 역순으로 검사해 위에 그려진 대상을 우선한다. 상호작용 element뿐 아니라 `hit_opaque`인 element도 hit를 흡수한다. 숨긴 가지와 부모의 clip 밖은 제외한다. `find_drop_target(x, y, payload)`는 같은 순서로 payload를 수락하는 drop target만 찾으므로 위에 놓인 버튼을 통과해 행이 파일이나 다른 행을 받을 수 있다.

초점 테·tooltip·drag 표시는 root와 자식을 그린 뒤 tree가 위에 그린다. `visible_bounds()`는 조상 clip을 반영하므로 drop 강조도 잘린 영역을 넘지 않는다. `next_update()`는 시간이 흐른 뒤 그림이 바뀌는 시각만 반환한다.

- `nullopt`: 시간에 따른 갱신 없음
- 미래 시각: 해당 시각에 한 번 다시 그림
- `now` 이하: 계속 움직이므로 짧은 주기로 다시 그림

플랫폼은 모든 표면의 가장 이른 시각에 timer 하나를 걸고, render 뒤 다음 예고를 다시 계산한다. 앱 상태 전환은 `transition` 값과 `logic_driver::next_tick()`/`tick()`으로 처리한다. element는 전환의 소유자가 아니다.
