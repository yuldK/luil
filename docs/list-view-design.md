# 목록과 tree view

`list_element`는 선택 가능한 행, 세로 스크롤 viewport, 선택적 scrollbar, focus group 탐색을 하나의 컴포넌트로 묶는다. 같은 형식으로 평평한 목록과 앱이 평탄화한 tree view를 표현한다.

공개 API는 [list_element.h](../include/luil/ui/list_element.h)에 있다. 공통 입력 의미는 [interaction.md](concepts/interaction.md)와 [ui-element.md](concepts/ui-element.md)를 따른다.

## 상태 소유권

목록의 지속 상태는 앱이 소유한다.

- `items`: 현재 보이는 행의 순서
- `selected`: 선택된 항목 key
- `scroll_offset`: 세로 스크롤 위치
- 각 tree 항목의 `expansion`

Element는 이 값을 받아 tree를 만들고 action factory를 통해 intent만 반환한다. 선택, 펼침, 순서, 스크롤을 내부에 기억하지 않는다.

```cpp
luil::list_config config {
    .owner = u8"files",
    .items = visible_items,
    .selected = selected_key,
    .row_height = 28.0f,
    .scroll_offset = scroll,
    .select = [](const std::u8string& key) {
        return luil::make_app_action(select_file { key });
    },
    .scroll = [](float delta) {
        return luil::make_app_action(scroll_files { delta });
    },
};
```

`owner`는 같은 화면의 여러 목록을 구분한다. 각 행의 `key`는 앱 모델 안에서 안정적이어야 하며 선택과 reorder 메시지에 그대로 사용된다.

## 행 모델

`list_item`은 다음 표시 정보를 담는다.

| 필드 | 의미 |
| --- | --- |
| `key` | 앱 정의 항목 식별자 |
| `label` | 화면과 typeahead에서 쓰는 이름 |
| `icon` | 선택적 앞 글리프 |
| `depth` | tree 들여쓰기 단계 |
| `expansion` | 잎, 접힘, 펼침 상태 |
| `enabled` | 선택과 초점 가능 여부 |

`enabled == false`인 행은 흐리게 그리고 선택하지 않으며 focus group 구성원에서도 제외한다.

## Tree view 평탄화

Tree 모델과 펼침 상태는 앱이 소유한다. 앱은 현재 보이는 노드를 depth-first 순서로 평탄화해 `items`에 전달한다. 접힌 가지의 자손은 목록에 넣지 않는다.

`depth`는 `list_indent_step` 단위의 들여쓰기를 정하고 `expansion`은 삼각형의 상태를 정한다. `none`은 잎이거나 펼침 UI를 쓰지 않는 행이다.

이 모델에서 화면 순서, keyboard 순서, 접근성 형제 순서가 같은 `items` 배열에서 나온다.

## 펼침 action

펼침은 두 factory 중 하나로 선언한다.

- `toggle(key)`: 현재 상태를 뒤집으라는 intent
- `set_expanded(key, expanded)`: 목표 상태를 명시하는 intent

`set_expanded`가 있으면 UIA Expand/Collapse처럼 절대 상태가 필요한 요청을 처리할 수 있다. 이것만 설정한 경우 포인터 삼각형도 현재 상태의 반대 값을 넣어 같은 factory를 사용한다.

두 factory가 모두 없으면 펼침 삼각형을 만들지 않는다. 하나라도 있으면 잎 행도 expander 폭을 비워 label 열을 맞춘다.

## 선택과 focus group

`select` factory가 있는 행은 `left_click` action을 가지므로 기본 `tab_stop()` 규칙에 따라 focusable하다. Factory가 없으면 목록은 표시 전용이며 행이 Tab 순서에 들어가지 않는다.

목록 컨테이너는 세로 focus group이다. Tab은 `selected` 행에 진입하고, 선택된 행이 유효하지 않으면 첫 활성 행에 진입한다. Up/Down은 보이는 행 사이를 순환하고 Home/End는 처음과 끝으로 이동한다. Label은 typeahead 검색에 사용된다.

초점 이동과 선택 상태는 별개다. 앱이 focus 이동에 맞춰 선택도 바꾸고 싶다면 policy와 action 모델에서 명시한다.

고른 행의 그림은 [draw_row_selection()](../include/luil/ui/draw_primitives.h) 하나다 — 행 안쪽의 둥근 옅은 채움(`accent_soft`의 낮은 알파)과 왼쪽 가장자리의 키 컬러 표식이다. [가상 목록](virtual-list-design.md)의 행과 앱이 지은 행이 같은 함수를 부르므로 한 화면의 고름이 한 모양이다. 표식은 고대비 팔레트가 옅은 채움을 접어도 어느 행인지 남긴다.

## 스크롤 구성

목록 내부는 두 칸으로 배치된다.

- `scroll_view_element`: 모든 행을 담은 세로 stack을 잘라 보여 준다.
- `scrollbar_element`: `scroll` factory가 있을 때 오른쪽에 선다.

`content_height()`는 행 수와 `row_height`에서 계산된다. `maximum_scroll()`과 `scroll_offset()`은 arrange 뒤에 유효하다. 입력으로 준 offset이 범위를 벗어나면 arrange가 다듬은 값을 반환한다.

앱은 frame을 만들기 전에 `clamp_scroll()`로 상태를 다듬어 viewport, scrollbar, 목록이 같은 offset을 보게 하는 것이 좋다.

`scroll_to` factory를 설정하면 scrollbar의 절대 위치 요청과 접근성 RangeValue 동작을 앱 메시지로 보낼 수 있다.

`edges`는 창의 위·아래 가장자리 표시다 — [흘리는 영역](scroll-area-design.md)의 가장자리 표시와 같은 설정, 같은 그림이다. 목록이 `arrange`가 다듬은 offset과 최대치로 직접 그리므로 앱이 겹쳐 그리지 않는다. 기본값은 전부 거짓이다.

## 전체 행과 가상화

`list_element`는 viewport 밖의 행도 모두 tree에 포함한다. Focus group 탐색, Home/End, typeahead, 접근성 순회가 전체 보이는 모델을 읽기 때문이다. 스크롤만으로 초점 행이 tree에서 사라지지 않는다.

**이것은 `list_element`의 계약이지 목록 일반의 계약이 아니다.** 보이는 행만 `list_element`에 넣으면 End와 방향키의 의미가 데이터 전체가 아니라 앱이 만든 subset으로 조용히 좁아진다. 그러니 이 컴포넌트에서는 행을 잘라 넣지 않는다.

행 수가 tree의 크기와 무관해야 하는 화면은 [`virtual_list_element`](virtual-list-design.md)를 쓴다. 그쪽은 모델을 tree 밖에 두고 키보드를 그 모델 위에서 돌리므로, 창에 걸치는 행만 세우면서도 Home/End와 글자 탐색이 데이터 전체를 본다. 대신 Tab의 자리가 행이 아니라 목록 자신이 되고 커서가 앱 상태로 나오며, 보조 기술의 형제 순회가 지금 지어진 행으로 좁아진다 — 계약이 다른 컴포넌트라 플래그가 아니라 형식이 갈린다.

고르는 기준은 행 수가 아니라 이 계약의 차이다. 몇백 줄까지는 `list_element`가 낫다.

## 순서 바꾸기

`reorder(moved, target)` factory가 있으면 각 행에 drag source, drop target, grab handle을 구성한다. Factory가 없으면 이 세 역할을 만들지 않는다.

Reorder는 평평한 목록에서 `moved` 항목을 `target` 자리로 옮기라는 intent다. Tree view에서는 target의 앞, 뒤, 자식 중 어느 의미인지 추가 정책이 필요하므로 앱이 factory에서 해석해야 한다.

`row_active_cursor`는 drag 중 행의 커서를 지정한다. `inherit`이면 기본 grabbing cursor를 사용한다.

## 접근성

목록은 list 역할을, 행은 선택 가능 항목 역할을 제공한다. 선택, 비활성, 펼침 상태는 element snapshot에서 나온다. `set_expanded`와 `scroll_to`가 있을 때 보조 기술의 절대 action을 지원할 수 있다.

항목 key는 frame 간 접근성 element id를 안정적으로 유지한다. 행 id는 `list_row` kind와 항목 key로 만들어지고 목록 owner를 포함하지 않는다. 따라서 같은 표면 tree에 놓인 모든 목록 사이에서도 행 key가 고유해야 한다. 중복은 `ui_tree::duplicate_ids()`에 나타나며 focus와 action 라우팅은 첫 id로 고정되므로 앱 오류로 취급한다.

## 반드시 유지할 불변식

- 선택, 펼침, 스크롤, 순서는 앱 상태다.
- `items` 배열은 표시와 keyboard 순서의 정본이다.
- Tree 자손의 포함 여부는 앱의 평탄화가 결정한다.
- 비활성 행은 선택과 초점 대상이 아니다.
- `list_element`는 목록 전체 행을 tree에 남긴다. 가상화가 필요하면 다른 컴포넌트를 쓴다.
- 스크롤 factory가 있을 때만 scrollbar를 만든다.
- Reorder factory가 있을 때만 drag 역할을 만든다.
- 항목 key는 같은 표면의 목록 전체에서 유일하고 frame 간 안정적이다.

## 검증 지침

[list_element_tests.cpp](../tests/list_element_tests.cpp)는 행 배치, 선택, tree 들여쓰기와 expander, 스크롤 값, group entry, disabled 행, reorder를 검증한다. [ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)는 방향키, Home/End, typeahead, focus reveal을 확인한다. [raster_draw_tests.cpp](../tests/raster_draw_tests.cpp)는 목록과 가상 목록의 고른 행이 같은 자리에 같은 표식을 두는 것을 픽셀로 잠근다.

사용 예는 [lists_page.cpp](../examples/demo/lists_page.cpp)에서 볼 수 있다.
