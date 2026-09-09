# 가상 목록

`virtual_list_element`는 **창에 걸치는 행만 tree에 세우는** 목록이다. 모델 전체는 값으로 들고 있고 키보드 탐색과 글자 탐색은 그 모델 위에서 돈다. 십만 줄짜리 화면이 element 십만 개가 되지 않게 하는 것이 이 컴포넌트가 하는 일의 전부다.

공개 API는 [virtual_list_element.h](../include/luil/ui/virtual_list_element.h)에 있다. 공통 입력 의미는 [interaction.md](concepts/interaction.md)와 [ui-element.md](concepts/ui-element.md)를 따르고, 스크롤·막대·치수 조립은 [scroll_area_element.h](../include/luil/ui/scroll_area_element.h)가 그대로 맡는다.

## 왜 목록이 둘인가

`list_element`에 `virtualize` 같은 플래그를 두지 않았다. 두 목록은 크기가 다른 것이 아니라 **계약이 다르다.**

| | `list_element` | `virtual_list_element` |
| --- | --- | --- |
| tree에 서는 행 | 전부 | 창에 걸치는 것과 커서 행 |
| Tab의 자리 | 행 하나 (focus group) | 목록 자신 |
| 키보드가 읽는 것 | tree | 모델 (`items`) |
| 커서 | 초점 그 자체 | 앱 상태인 `cursor` |
| 접근성 형제 순회 | 모델 전체 | 지금 지어진 행 |

플래그 하나로 이 다섯 줄이 동시에 뒤집힌다. 그 말은 같은 형식을 채운 앱 코드가 플래그 값에 따라 **다른 의미**를 갖는다는 뜻이다. End가 모델의 끝이 될지 화면의 끝이 될지, `focus_entry`가 가리킨 행이 다음 frame에도 있을지가 그 한 값에 달리면 앱은 두 계약을 머릿속에서 나눠 들고 있어야 한다. 형식을 둘로 가르면 그 나눔이 이름에 적힌다.

고르는 기준도 크기가 아니라 계약이다. 몇백 줄까지는 `list_element`가 낫다 — 보조 기술의 형제 순회가 온전하고, 행이 곧 Tab의 자리라 커서라는 개념 자체가 없다. `virtual_list_element`는 그 둘을 내주고 행 수와 무관한 tree를 얻는다. 무엇을 내주는지 알고 고르는 것이 이 문서의 목적이다.

## 상태 소유권

목록의 지속 상태는 전부 앱이 소유한다. Element는 값을 받아 tree를 만들고 action factory로 intent만 돌려준다.

| 상태 | 형식 | 뜻 |
| --- | --- | --- |
| `items` | `std::vector<virtual_list_item>` | **모델 전체.** 창에 걸치지 않는 항목도 담는다 |
| `selected` | `std::u8string` | 고른 항목의 key |
| `cursor` | `std::u8string` | 키보드 커서가 선 항목의 key |
| `scroll_offset` | `float` | 세로 스크롤 위치 (논리 픽셀) |

담기는 것은 값이지 element가 아니다. 십만 줄이 와도 `items`까지는 복사 한 번이고, tree에 서는 것은 창에 걸치는 몇 줄뿐이다.

**커서를 선택과 갈라 둔 이유**는 그 둘이 앱마다 다르기 때문이다. 화살표로 훑다가 Enter로 고르는 목록과, 화살표가 곧 선택인 목록이 둘 다 흔하다. 커서를 옮기자는 `move` 메시지를 받은 앱이 커서만 옮길지 선택까지 옮길지 정하면 두 목록이 같은 element로 표현된다. 커서가 비어 있으면 `selected`가 커서이고, 그것도 비어 있으면 첫 활성 항목이다.

## 행 모델

`virtual_list_item`은 **행을 짓지 않고도 알아야 하는 것**만 담는다. 화면에 걸치지 않는 항목은 element가 되지 않으므로, 키보드가 알아야 하는 순서·이름·고를 수 있는가가 전부 여기 있어야 한다.

| 필드 | 의미 |
| --- | --- |
| `key` | 앱 정의 항목 식별자. 선택·커서·행 자리표가 전부 이 값이다 |
| `label` | 글자 탐색이 읽는 이름. 비어 있으면 글자로 찾을 수 없다 |
| `height` | 이 행의 높이. 0이면 `row_height` |
| `enabled` | 거짓이면 흐리게 그리고 키보드도 건너뛴다 |

`height`가 0이 "없음"인 것은 `stack_item`과 같은 규칙이다. 값이 섞여 있어도 되고, 그때 창에 걸치는 범위는 앞에서부터 더한 합으로 찾는다. 항목 수에 비례한 일이 한 번 생기지만 그것은 **더하기뿐**이라 행을 짓는 것과 견줄 비용이 아니다.

### 라이브러리가 쥐는 것과 `build_row`가 쥐는 것

`build_row`가 돌려주는 것은 **행의 내용**이다. 행 자신은 목록이 만들어 그 안에 담는다.

| 목록이 쥔다 | `build_row`가 쥔다 |
| --- | --- |
| 행의 자리표 (`virtual_list_row` + 항목 key) | 글·아이콘·안쪽 컨트롤 |
| 선택 표시와 커서 표시 | 그 위에 얹을 앱만의 표시 |
| 누름 액션 (`select`와 커서 따라가기 `move`) | — |
| 글자 탐색 이름 (`search_label`) | — |
| 접근성 역할과 선택 상태 | — |

이 나눔이 요점이다. 앱이 행을 통째로 만들면 앱마다 선택과 키보드를 다시 짜고 그중 하나를 반드시 틀린다. `build_row`가 비어 있으면 라이브러리가 `label` 한 줄짜리 기본 행을 짓고, `nullptr`을 돌려주면 그 행은 내용 없이 배경과 선택 표시만 선다.

**누른 행은 고르는 것이자 커서가 서는 곳이다.** 그래서 한 번의 누름이 `select`와 `move` 둘을 함께 낸다. 커서를 함께 옮기지 않으면, 누른 다음 누른 화살표가 눌린 행이 아니라 커서가 있던 옛 자리에서 출발한다 — 화면에서는 "방금 누른 곳에서 한 칸"으로 보여야 하는 몸짓이 엉뚱한 데로 뛴다. 그것을 앱의 `select` 처리에 맡기면 반드시 한 앱이 빠뜨린다. 이미 커서가 선 행을 누르면 `move`는 나지 않는다 (제자리로 옮기자는 메시지가 앱을 깨우지 않게 하는, `move_cursor`와 같은 방벽이다).

`build_row`는 **tree를 짓는 동안** 불린다. 게시된 뒤에는 불리지 않으므로 앱 상태를 바꾸지 않는 순수한 함수여야 한다 ([immutable-tree.md](concepts/immutable-tree.md)).

## 실현 규칙

지을 행을 정하는 것은 **배치 시점**이다. 창 높이를 알아야 어디까지 걸치는지 답할 수 있고, 창 높이는 slot이 정해져야 안다. `arrange`가 자식을 만드는 유일한 자리이고, 그때 tree는 아직 게시되지 않았으므로 만드는 것이 허용된다.

1. `clamp_scroll()`로 다듬은 offset과 창 높이로 `virtual_list_visible_range()`를 부른다.
2. 그 반열린 구간 `[begin, end)`의 행을 짓는다.
3. 커서 행이 그 구간 밖이면 **그 행도 따로 짓는다.**

행은 **높이가 있는 첫 배치**에서 한 번만 짓는다. 자식은 tree 하나에 한 벌이라 두 번 지으면 같은 자리표가 둘 서고, 높이가 0인 배치를 계기로 삼으면(담는 쪽이 자리를 잡기 전에 한 번 0으로 재 볼 수 있다) 아무 행도 걸치지 않은 빈 목록이 그대로 굳는다.

구간이 반열린 것이 첫 번째 규칙이다. 경계에 정확히 닿은 행은 다음 행의 것이라, 창 높이가 행 높이의 배수일 때 한 줄을 덤으로 짓지 않는다. `overscan`은 위아래로 더 짓는 행 수이고 모델의 양끝에 붙어 멈춘다. 0이면 걸치는 것만 지으므로 휠을 굴리는 동안 행이 나타나는 것이 눈에 보인다.

**커서 행은 창에 걸치지 않아도 반드시 짓는다.** 그러지 않으면 초점 테를 그릴 자리가 없고, 커스텀 행 안의 컨트롤이 스크롤 한 번에 사라진다. `realized()`는 창의 구간만 답하므로, 커서 행은 그 구간 밖에 한 줄 더 선 것으로 읽는다.

행을 늘어놓는 것은 `stack_element`가 아니라 전용 레인이다. 스택은 자식을 차례로 쌓아 첫 자식이 언제나 맨 위에 서는데, 가상 목록의 첫 자식은 3만 번째 행일 수 있다. 자리를 모델이 정하므로 레인은 받은 slot의 원점에 각 행의 시작점을 더하기만 한다.

### 순수 함수

판정이 element 안에 숨으면 test가 창도 tree도 없이 짚을 수 없다. 다섯 함수가 밖에 나와 있다.

| 함수 | 답 |
| --- | --- |
| `virtual_list_content_height` | 모델 전체의 높이 |
| `virtual_list_row_span` | `index`번 행이 서는 자리 (범위 밖이면 `{0, 0}`) |
| `virtual_list_visible_range` | 창에 걸치는 행의 반열린 구간 |
| `virtual_list_step_target` | 키가 커서를 옮길 자리 |
| `virtual_list_search_target` | 글자 질의로 찾을 항목 |

## 키보드 모델

### 자리가 행이 아니라 목록인 이유

[focus-group-design.md](focus-group-design.md)는 "초점은 group 컨테이너가 아니라 실제 구성원에 선다"를 불변식으로 세운다. 테를 두를 자리가 항목이어야 어디에 있는지 보이기 때문이다. `list_element`가 바로 그 모델이다 — 목록은 세로 묶음이고, Tab은 `selected` 행으로 들어가며, 초점은 행에 선다.

가상 목록은 그 규칙을 따를 수 **없다.** 초점이 설 행이 다음 frame에 tree에서 사라질 수 있기 때문이다. 초점을 가진 element가 자기 표면의 tree에서 사라지면 controller가 초점을 거둔다 (`update_focus`, [keyboard-focus-design.md](keyboard-focus-design.md)). 그 규칙은 옳다 — 닫힌 dialog의 버튼이 초점을 쥔 채 Space를 받으면 안 된다. 하지만 가상 목록에서는 **휠을 한 번 굴리는 것만으로** 초점 행이 tree에서 사라지고, 그러면 초점이 조용히 없어진다. 묶음으로 만들면 Tab이 들어갈 자리(`focus_entry`)도 같은 이유로 흔들린다.

그래서 가상 목록은 묶음이 아니라 **컨트롤 하나**다. slider가 그렇듯 초점은 목록에 서고 값(커서)만 바뀐다.

행을 클릭해도 초점은 목록에 선다. 행의 `pointer_focus_target()`이 목록 ID를 반환하므로 Tab 자리 수를 늘리지 않고 클릭 뒤 키 탐색을 이어 간다. 사용자 정의 행 안의 별도 컨트롤은 기본적으로 자신의 초점을 받는다.

- 자리는 목록 자신 하나다. 행은 `set_tab_stop(false)`라 누를 수 있어도 Tab에 서지 않는다 — 걸치는 것만 자리가 되면 자리의 **수**가 스크롤에 따라 달라진다. 스크롤 막대도 자리가 아니다 (`bar_tab_stop = false`) — 세로 키를 가진 것은 목록 자신이라, 막대까지 자리가 되면 같은 목록에 자리가 둘 선다.
- 화면에 보이는 초점 표시는 둘로 나뉜다. 목록 전체를 두르는 초점 테는 `ui_tree`가 얹고, 그 안의 어느 행에 서 있는지는 행이 그리는 커서 표시가 말한다. 둘 다 테면 한 화면에서 구별되지 않는다 (기본 버튼의 채움과 초점 테를 가른 것과 같은 판단이다).
- `move`도 `activate`도 없으면 목록은 **Tab의 자리조차 아니다.** 커서는 앱 상태라 라이브러리가 고칠 수 없고, 옮길 길도 실행할 길도 없는 초점은 아무 일도 하지 않는 자리가 된다. 둘 중 하나만 있어도 자리다 — `activate`만 든 목록은 훑을 수는 없어도 커서 행을 실행할 수는 있다.

### 키가 오는 길

화살표·Page·Home/End는 `key_step_target`이 받는다 ([value-step-design.md](value-step-design.md)). 그 경로는 controller에서 묶음보다 **앞에** 서므로, 목록을 감싼 묶음이 있어도 목록이 키를 먼저 가진다. 묶음이 갖지 못하는 PageUp/PageDown이 여기서는 저절로 온다.

| 키 | `value_step` | 목록의 답 |
| --- | --- | --- |
| Up / Down | `decrease` / `increase` | 한 칸 (비활성은 건너뛴다) |
| Page Up / Page Down | `decrease_page` / `increase_page` | 이동 방향의 실제 행 높이로 창 하나만큼 |
| Home / End | `minimum` / `maximum` | 모델의 처음·끝 **활성** 항목 |

세 규칙이 `virtual_list_step_target()`에 함께 산다.

목록은 PageUp에서 앞쪽 행 높이를, PageDown에서 뒤쪽으로 이동하는 거리를 합산해 활성 행의 이동 칸 수를 계산한다. 비활성 행도 화면 높이는 차지하므로 거리에 포함한다. 한 화면 안에 활성 항목이 없으면 해당 방향의 다음 활성 항목으로 이동한다.

- **비활성 항목은 건너뛴다.** 건너뛴 항목은 칸으로 세지 않는다 — 지나치는 것이지 멈추는 것이 아니다.
- **화살표는 끝에서 멈춘다.** 묶음의 화살표가 도는 것과 갈리는데, 도는 것은 항목이 한 화면에 다 보일 때의 어휘다. 십만 줄에서 ↓ 한 번에 맨 위로 돌아가면 그것은 이동이 아니라 사고다.
- **서 있던 자리를 모르면 첫 활성 항목이다.** 아직 아무 데도 서지 않은 목록에서 ↓ 한 번이 첫 항목을 집는다.

갈 곳이 없으면(끝에 닿았다, 전부 비활성이다) 목록은 **빈 action 목록**을 돌려준다. 키는 소비되고 메시지는 나지 않는다 — 마지막 행에서 ↓를 누를 때마다 앱의 화면 단축키가 함께 터지지 않게 하는 규약이다 ([value-step-design.md](value-step-design.md)의 세 상태).

### 커서를 옮기는 키가 내는 것은 메시지 둘이다

커서를 옮기는 `move` 메시지와, 옮긴 자리를 화면 안으로 들이는 `scroll` 메시지다.

**되살리기를 앱에 미루지 않는다.** `interaction_policy::on_focus_moved`는 초점이 옮겨 갔을 때 불리는데, 여기서 초점은 목록에 그대로 서 있다 — 옮겨 간 것은 앱 상태인 커서뿐이라 그 계기가 아예 오지 않는다 ([focus-reveal-design.md](focus-reveal-design.md)의 되살리기가 닿지 않는 유일한 자리다). 얼마나 흘릴지를 아는 것은 모델과 창 높이를 함께 쥔 목록뿐이다.

옮긴 자리가 **이미 보이면 스크롤 메시지를 내지 않는다.** 0짜리 스크롤도 앱 logic을 깨워 tree를 통째로 다시 짓는다 — `route_reveal`이 같은 자리에 세운 방벽과 같다.

### Space와 Enter는 커서 행을 실행한다

`activate`는 커서가 선 항목을 **실행**하자는 메시지다. 커서와 선택을 갈라 둔 값이 여기서 나온다 — 화살표로 훑고 Enter로 고르는 모델은 이 factory가 있어야 실제로 표현된다. 없으면 키보드에서 고를 길이 아예 없어, 앱은 `move`를 받을 때 선택까지 함께 옮기는 수밖에 없고 그러면 커서와 선택을 가른 뜻이 사라진다.

통상 `select`와 같은 메시지를 낸다. 다르게 두는 자리도 있다 — Enter가 "열기"이고 클릭이 "고르기"인 목록이 그렇다.

목록은 그 메시지를 `left_click` 액션으로 든다. controller가 초점을 가진 element의 `left_click`을 Space·Enter로 실행하기 때문이고(`process_activation_key`), 그래서 두 키가 따로 배선되지 않는다.

커서가 가리키는 항목이 비활성화되면 Space·Enter는 실행 메시지를 내지 않는다. 앱이 보존한 커서나 선택 키가 비활성 항목을 가리키더라도 동일하다.

**그 액션은 키보드만의 문이다.** 포인터는 행을 직접 누르므로 이 자리를 지나지 않는다. 기본 hit test는 액션을 든 element에게 자기 bounds를 돌려주므로, 그대로 두면 마지막 행 **아래 빈 자리**를 누른 것이 커서 행의 실행이 된다 — 누른 자리와 실행된 자리가 다른, 설명할 수 없는 클릭이다. 그래서 목록은 `hit_test`를 재정의해 자기 자신을 답하지 않는다. 답은 언제나 행이거나 없음이다.

## 글자 탐색

초점을 가진 목록에 글자가 오면 `key_search_target`이 받는다. 묶음의 글자 탐색은 tree에 선 항목만 보므로(`search_label`), 창에 걸치는 것만 짓는 목록에서는 그것이 곧 "보이는 것만 찾는다"가 된다. 같은 글자를 쳤을 때 어디로 갈지가 지금 스크롤 자리에 달리게 되는 것이다.

역할은 둘로 갈라 둔다.

- **controller**가 질의를 잇고 끊는 규칙을 쥔다 (글자 이어 붙이기, `typeahead_reset_time`의 시간 끊김, 초점이 다른 길로 옮겨 갔을 때의 초기화).
- **목록**이 그 질의에 무엇이 맞는지 답한다. 모델을 아는 것은 element뿐이다.

그 나눔이 `on_search(query, first)`의 두 번째 인자로 드러난다. `first`는 **이번이 질의의 첫 글자인가**이고, 참이면 커서의 **다음**부터 찾아 같은 글자를 거듭 칠 때 후보를 돌며, 거짓이면 커서부터 찾는다 (글을 더 적은 것이지 다음으로 가자는 뜻이 아니다).

**그 값은 controller가 준다.** element가 질의의 길이로 되짚으면 안 된다 — UTF-8에서 한글 한 글자는 세 byte라 첫 글자부터 "이어 친 글자"로 세어지고, 같은 글자를 거듭 쳐도 후보가 돌지 않는다. 질의를 잇고 끊는 쪽만 이 값을 옳게 안다. `virtual_list_search_target()`이 같은 이름의 인자를 그대로 받는 것도 그 때문이다 — 판정은 순수 함수에 있고, 무엇이 첫 글자인지는 위에서 내려온다.

controller는 초점을 가진 element에게 **묶음보다 먼저** 묻는다. 뒤에 두면 목록을 감싼 묶음이 글자를 통째로 가져가 창에 걸친 행만 찾는다.

맞는 것이 없어도 글자는 목록이 가진다. 맞지 않는 글자가 앱으로 새지 않는 것이 묶음의 글자 탐색과 같은 규약이다.

## 스크롤

흘리기·막대·치수·휠은 `scroll_area_element`가 그대로 한다. 목록이 더하는 것은 모델 위의 키보드뿐이다.

- `content_height()`는 모델 전체의 높이다 (`items`와 `row_height`에서 나온다).
- `metrics()`는 `arrange` 뒤에 유효한 내용·창·offset·최대치 넷이다. 범위 밖 offset을 주면 다듬은 값이 여기로 돌아온다.
- 휠과 초점 되살리기는 표 없이 안쪽 영역을 찾는다 (`scroll_source`).

앱은 frame을 만들기 전에 `clamp_scroll()`로 스크롤 상태를 **자기 손으로** 다듬는다. 그러면 창·막대·실현 범위가 같은 값을 보고, 키가 내는 스크롤 변화량도 앱이 들고 있는 값과 같은 기준에서 나온다 — 목록이 재는 자리는 다듬은 값이고 앱이 델타를 더하는 자리는 앱이 든 값이라, 그 둘이 어긋나 있으면 되살리기가 한 번에 닿지 못한다.

## 접근성

목록은 list 역할로 읽히고, 커서가 선 항목의 이름을 목록의 "지금 값"으로 답한다. 행은 list_item이고 선택 상태를 함께 낸다.

**형제 순회는 지금 지어진 행 안이다.** 보조 기술이 목록의 자식을 훑으면 창에 걸치는 범위와 커서 행만 나온다. 그것이 가상화와 맞바꾼 것이다 — 창 밖의 항목은 그 순회로 닿지 않는다. 그래서 "지금 어디에 서 있는가"는 목록 자신이 값으로 말해야 하고, 그 답이 없으면 화면 밖 커서는 보조 기술에 아무 흔적도 남기지 않는다.

몇백 줄짜리 화면이라면 이 맞바꿈을 하지 않는 쪽이 낫다. `list_element`는 모든 행을 tree에 남기므로 순회가 온전하다 ([list-view-design.md](list-view-design.md)).

행 kind가 `list_row`와 갈리는 것도 접근성 때문이다. 가상 목록의 행은 스크롤할 때마다 tree에서 나고 사라지므로, 같은 kind로 두면 보조 기술이 두 목록의 행을 한 이름 공간에서 본다.

## 사용 예

```cpp
luil::virtual_list_config config {
    .owner = u8"log",
    .items = model_rows,
    .selected = state.selected,
    .cursor = state.cursor,
    .row_height = 22.0f,
    .scroll_offset = state.scroll,
    .overscan = 2,
    .build_row = [](const luil::virtual_list_item& item, std::size_t, bool selected)
        -> std::unique_ptr<luil::ui_element> {
        luil::label_config text {};
        text.text = item.label;
        text.color = selected ? luil::label_color_role::primary : luil::label_color_role::dim;
        return std::make_unique<luil::label_element>(
            luil::ui_element_id { luil::ui_element_kind::none, item.key }, std::move(text));
    },
    .select = [](const std::u8string& key) { return luil::make_app_action(select_row { key }); },
    .move = [](const std::u8string& key) { return luil::make_app_action(move_cursor { key }); },
    .activate = [](const std::u8string& key) { return luil::make_app_action(open_row { key }); },
    .scroll = [](float delta) { return luil::make_app_action(scroll_log { delta }); },
    .scroll_to = [](float offset) { return luil::make_app_action(scroll_log_to { offset }); },
};
auto list { std::make_unique<luil::virtual_list_element>(std::move(config)) };
```

`move`와 `activate`를 둘 다 뺀 목록은 눈으로만 보는 목록이다 (Tab의 자리도 아니다). `select`를 뺀 목록은 행을 누를 수 없지만 키보드로 훑을 수는 있다 — `list_element`와 갈리는 자리다. 저쪽은 행이 Tab의 자리라 누를 수 없으면 키보드도 서지 않지만, 이쪽은 목록 자신이 자리다.

## 반드시 유지할 불변식

- `items`, `selected`, `cursor`, `scroll_offset`은 앱 상태다.
- `items` 배열은 화면 순서와 keyboard 순서의 정본이다. 창에 걸치는 범위가 아니라 이 배열이 Home/End와 글자 탐색의 범위다.
- 창에 걸치는 행과 커서 행만 tree에 선다.
- 커서 행은 창 밖이어도 반드시 짓는다.
- 행은 높이가 있는 첫 배치에서 한 번만 짓는다.
- 앱은 tree를 짓기 전에 `clamp_scroll()`로 스크롤 상태를 다듬는다.
- 창에 걸치는 구간은 반열린 구간이고 `overscan`은 모델의 양끝에 붙어 멈춘다.
- Tab의 자리는 목록 자신 하나이고 행도 스크롤 막대도 자리가 아니다.
- `move`도 `activate`도 없으면 목록은 Tab의 자리가 아니다.
- 화살표는 끝에서 멈춘다 (돌지 않는다). 비활성 항목은 건너뛰고 칸으로 세지 않는다.
- 갈 곳이 없는 키는 빈 action 목록으로 **소비된다.**
- 커서를 옮기는 키는 필요할 때만 스크롤 메시지를 함께 낸다 (이미 보이면 내지 않는다).
- Space·Enter는 커서 행의 `activate`를 낸다. 그 액션은 키보드만의 문이라 목록 자신은 포인터 hit의 답이 되지 않는다.
- 행을 누르면 `select`가 나고, 그 행이 커서가 아니면 `move`가 함께 난다.
- 글자 탐색의 `first`는 controller가 준다. 질의의 길이로 되짚지 않는다.
- 행의 자리표·선택 표시·누름·글자 탐색 이름은 `build_row`가 무엇을 돌려주든 목록의 것이다.
- 항목 key는 같은 표면의 목록 전체에서 유일하고 frame 간 안정적이다.

## 검증 지침

[virtual_list_element_tests.cpp](../tests/virtual_list_element_tests.cpp)는 다섯 순수 함수(내용 높이·행 자리·걸치는 구간·걸음·글자 탐색)를 창도 tree도 없이 검증하고, 그 위에서 만 줄 모델의 실현 범위, 창 밖 커서 행, 막대까지 뺀 단일 Tab 자리(`activate`만 든 목록이 자리인 것과 둘 다 없으면 자리가 아닌 것을 함께), 커스텀 행의 소유 경계, 방향키·Page·Home/End의 메시지 짝, Space·Enter가 내는 커서 행의 실행과 행 아래 빈 자리를 누른 것이 아무것도 내지 않는 것, 행 누름이 내는 `select`+`move` 짝, 모델 전체를 도는 글자 탐색과 한글 이름에서 같은 글자가 후보를 도는 것, 범위 밖 스크롤의 다듬기를 확인한다.

키 처리 순서(값 step이 묶음보다 앞이라는 것)와 글자 탐색의 질의 규칙은 [ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)가 함께 확인한다.

사용 예는 [lists_page.cpp](../examples/demo/lists_page.cpp)의 세 번째 판(`make_log_panel`)에서 볼 수 있다 — 만 줄이 넘는 모델이 `list_element`로 만든 두 목록과 한 화면에 나란히 선다.
