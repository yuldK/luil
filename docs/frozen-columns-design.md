# 고정 열

표의 앞쪽 열 N개를 가로 스크롤에서 제자리에 세운다. 나머지 열만 `scroll_x`를 따라 흐르고, 머리글과 본문이 같은 경계를 쓴다. 공개 형식은 `table_config::frozen_columns`, 순수 함수 `table_frozen_width`·`table_frozen_fits`·`table_reveal_delta`, `table_element`의 `frozen_columns()`·`frozen_width()`·`scroll_delta_to_reveal`이다. 흐르는 열을 고정 구역 밖으로 자르는 레인 kind `table_header_lane`·`table_row_lane`은 비공개 부품의 이름표다.

[표 문서](table-design.md)가 "고정 열"을 뒤로 미룬 확장으로 적었고 이 문서가 닫는다. 열 폭 끌기·순서 바꾸기·셀 커서는 그 문서의 규칙을 따른다.

## 문제

앱이 고정 열을 만들 길이 없다.

- 칸의 가로 자리는 비공개다. `column_rect`와 `table_row_element`는 익명 이름공간에 있고([table_element.cpp](../src/ui/table_element.cpp) 20~32행, 118행) 가로 상태는 `scroll_x` 하나뿐이다([table_element.h](../include/luil/ui/table_element.h) 105행). 머리글과 모든 열이 함께 흐른다.
- 표 둘로 흉내 내면 행 모델·선택·커서·세로 스크롤을 두 표가 맞춰야 하고, 휠·키·정렬·초점이 어느 표의 것인지 앱이 가른다. 행의 자리표가 표 owner를 담으므로([table-design.md](table-design.md) 95행) 보조 기술은 DataGrid 둘과 행 두 벌을 읽는다.
- 가려진 칸을 가릴 도구가 앱에 없다. 자르기는 `clip_children`과 `bounds()` 하나뿐이고 그리기(`draw_children`, [ui_element.cpp](../src/ui/ui_element.cpp) 816행), hit(566행), 놓을 자리(617행), 보이는 자리(`visible_bounds_of`, [ui_tree.cpp](../src/ui/ui_tree.cpp) 578행), 접근 좌표 질의([accessibility.cpp](../src/ui/accessibility.cpp) 44행), 휠·팬 임자([ui_interaction.cpp](../src/ui/ui_interaction.cpp) 853·882행)가 모두 그 규칙만 따른다.
- 키보드 들이기도 창 전체를 가정한다. `reveal_delta`(table_element.cpp 704행)는 보이는 창을 `[scroll_x, scroll_x + viewport]`로 본다. 고정 구역 뒤에 숨은 칸을 이미 보인다고 판단해 들이지 않는다.

R1 문서 아홉 편과 R2 문서 열세 편에서 이 노드로 넘긴 항목은 없다. [virtual-list-grid-design.md](virtual-list-grid-design.md) 9행의 "고정 열 수"는 격자의 열 수라 다른 것이다.

## 공개 형식

```cpp
// include/luil/ui/table_element.h (table_config 끝에 더한다. 기존 칸 순서는 바꾸지 않는다)
struct table_config
{
    // 앞(시작 쪽)에서 이 수만큼의 열은 가로로 흘러도 제자리에 선다.
    //  - 0이면 지금과 같다. 열 수보다 크면 열 수로 본다.
    //  - 고정 열이 창을 거의 채우면(table_frozen_fits가 거짓) 그 배치에서는 고정이 풀려 모든 열이 흐른다.
    //  - 앞 열을 숨기거나 옮기는 앱이 이 수를 함께 맞춘다.
    std::size_t frozen_columns { 0 };
};

// 앞 count개 열의 폭 합이다 (논리 픽셀). count가 열 수보다 크면 열 수로 다듬는다.
[[nodiscard]] float table_frozen_width(std::span<const table_column> columns, std::size_t count) noexcept;
// 고정 폭을 두고도 흐르는 창이 table_key_scroll_step(48) 이상 남는가.
[[nodiscard]] bool table_frozen_fits(float frozen_width, float viewport_width) noexcept;
// 그 열을 흐르는 창 [frozen_width + scroll_x, scroll_x + viewport_width]로 들이는 가로 스크롤 변화량이다 (이미 보이면 0).
//  - frozen_width가 0이면 지금의 reveal_delta와 같은 값이다.
[[nodiscard]] float table_reveal_delta(const table_column_span& span, float scroll_x, float frozen_width, float viewport_width) noexcept;

class table_element
{
    // 이번 배치에 서 있는 고정 열 수와 폭 (논리 픽셀)이다. 고정이 없거나 풀렸으면 0이다. arrange 뒤에 유효하다.
    [[nodiscard]] std::size_t frozen_columns() const noexcept;
    [[nodiscard]] float frozen_width() const noexcept;
    // 가로 창 폭이다 (논리 픽셀, 세로 막대를 뺀 본문의 폭). arrange 뒤에 유효하다. 고정 열 손잡이의 한도가 이 값으로 잰다.
    [[nodiscard]] float viewport_width() const noexcept;
    // 칸 안 컨트롤에 초점이 갈 때 흐르는 창으로 들이는 변화량이다 (결정 8). 왼쪽 끝이 경계 이상인 대상에만 답하고 나머지는 0이다.
    [[nodiscard]] float scroll_delta_to_reveal(const rect_f& target) const override;
};

// include/luil/ui/ui_element_id.h, end_of_library_kinds 바로 위
// 표 머리글과 행에서 흐르는 열을 담아 고정 구역 밖으로 자르는 레인이다. 구조일 뿐이다.
// 행 레인의 owner는 표와 행 키를, 머리글 레인의 owner는 표 owner와 "lane"을 단위 구분자(U+001F)로 이은 것이다.
table_header_lane,
table_row_lane,
```

## 규칙

**가로 자리.** 고정 폭 `F`는 앞 N열의 폭 합이다. 구역은 둘이다. 고정 `[slot.x, slot.x + F * scale]`, 흐름 `[slot.x + F * scale, 오른쪽 끝]`.

- `column_rect`는 바뀌지 않는다. 고정 열은 `scroll_x` 자리에 0을, 흐르는 열은 `scroll_x_`를 넣는다. 흐르는 칸의 x는 레인 원점이 아니라 지금 식 그대로(`row.x + (begin - scroll_x) * scale`)라 머리글 칸과 행 칸의 bounds가 같다는 기존 약속이 유지된다.
- 스크롤 값과 범위가 같다. 흐르는 창이 보이는 내용은 `[F + s, viewport + s]`이고 끝까지 흘리면 마지막 열의 끝이 창 끝에 닿으므로 최대값은 고정 유무와 상관없이 `content_width - viewport_width`다. `table_clamp_scroll`, `scroll_source`(table_element.cpp 805~817행), 가로 막대의 `set_metrics`, `scroll_horizontal_to`가 바뀌지 않는다.
- 경계 `edge = slot.x + F * scale`은 한 식이다. 머리글·행·경계선·들이기가 같은 값을 쓴다. 분수 배율에서 마지막 고정 칸의 오른쪽 끝과 레인의 왼쪽 끝 사이에 한 픽셀 이음매가 생기지 않는다.

**자르기와 자식 순서.** 레인은 구성 때 정한 수 `n`(`frozen_columns`를 열 수로 다듬은 값)이 `0 < n < 열 수`일 때만 선다. 창 폭이 레인의 있고 없음을 정하지 않으므로 tree 모양이 frame마다 달라지지 않는다. `n`이 0이면 tree 모양과 그림이 지금과 같다. `n`이 열 수 이상이면 흐르는 열이 없어 레인도 경계선도 서지 않고 tree 모양은 `n`이 0일 때와 같다.

- 레인은 `clip_children`이고 bounds는 `[edge, 행(머리글)의 오른쪽 끝]`이다. 레인은 자기 자리만 정하고 부품 배치는 행과 머리글이 한다.
- 행의 자식 순서는 고정 부품(칸·삼각형·체크), 레인(흐르는 부품)이다. Tab 순서가 tree 앞에서 뒤라([ui_tree.cpp](../src/ui/ui_tree.cpp) 403~420행) 열 순서와 맞는다. 두 구역은 겹치지 않아 hit 우선순위와 무관하다.
- 머리글의 자식 순서는 고정 칸, 레인(흐르는 칸, 흐르는 손잡이), 고정 손잡이다. hit와 접근 질의는 뒤 자식이 이기므로, 경계에 걸친 마지막 고정 열 손잡이의 흐르는 쪽 절반(`table_grip_width / 2`)이 흐르는 칸을 이긴다. 머리글의 부품은 Tab 자리가 아니라 순서가 초점에 닿지 않는다. 칸(HeaderItem)이 손잡이보다 앞이고 칸끼리는 열 순서라 보조 기술이 읽는 머리글 칸의 순서는 고정이 없을 때와 같다. 레인을 맨 앞에 두면 접근 트리가 레인을 접어 흐르는 칸이 고정 칸보다 앞서 읽히므로 이 순서로 한다.
- 행은 `places_`(304행)로, 머리글은 칸 뒤 손잡이 인덱스 산술(501~508행)로 부품의 열을 찾는다. 둘 다 부품마다 (부품, 열, 쓰임)을 든 표로 바꾼다. 흐르는 열의 부품은 레인의 자식이라 자식 인덱스가 아니라 부품 포인터를 든다. 구역은 열이 `n` 이상인가로 가른다.
- 행 글은 고정 열과 흐르는 열을 구역 사각형으로 따로 `clipRect`해 그린다. 셀 커서 테(`draw_cell_cursor`)도 칸이 속한 구역 안에서만 그린다. 레인의 `accessibility()`는 `{}`(역할 `none`)다.

**고정이 풀릴 때.** `arrange`가 `viewport_width_`를 정한 뒤(800행 다음) 한 번 `table_frozen_fits`를 묻는다. 거짓이면 그 배치에서 `frozen_columns()`는 0이고 레인 bounds는 행 전체이며 고정 열도 `scroll_x_`를 따라 흐른다. 가로 막대 판정은 열 폭의 합으로 하므로 고정과 맞물려 돌지 않는다. 창이 넓어지면 고정이 돌아온다. `scroll_x`는 앱 상태 그대로다.

**입력.**

| 입력 | 규칙 |
| --- | --- |
| ←·→ (셀 탐색 없음) | 지금처럼 `scroll_horizontal(±48)`이고 끝에서는 키만 삼킨다 |
| ←·→ (셀 탐색), F2 | `move_column`·`begin_edit` 뒤에 `table_reveal_delta`로 들인다. 고정 열이 목표면 스크롤 메시지가 없다. 흐르는 열은 고정 폭을 뺀 창으로 들인다 |
| 가로 휠·터치 팬·막대 | `scroll_source`가 표에 있으므로 고정 칸 위에서 시작해도 흐르는 열만 민다 |
| 머리글 누름·우클릭 | 고정 칸도 같다 |
| 열 경계 끌기 | 고정 열도 끈다. 넓히는 몫은 고정이 서 있는 한도 `viewport - 48 - F`에서 멈춘다. 줄이기는 `min_width`까지다. 끝 알림은 끌기마다 한 번이다 |
| 머리글 칸을 끌어 놓기 | 같은 구역 안만 받는다. 구역 경계를 넘는 놓기는 `accepts`가 거짓이다 |

- `scroll_delta_to_reveal`(결정 8)은 **왼쪽 끝이 `edge` 이상인 대상에만** 답한다(0.5 물리 픽셀의 여유를 둔다). 본문·행·머리글·고정 칸, 고정 구역에 걸친 칸은 모두 0이다. 본문에 초점이 가면 `route_reveal`이 본문 bounds(왼쪽 끝이 `slot.x`)를 묻는데, 경계와 맞춰 답하면 `-F`가 나와 Tab마다 `scroll(0)`이 logic을 깨우거나 `scroll_x > 0`에서 표가 0으로 되돌아간다. 한계: 고정 구역 아래에 일부 숨은 흐르는 칸은 Tab으로 들이지 못한다. 셀 커서와 F2 길(`table_reveal_delta`)은 들인다. `frozen_columns`가 0이면 본문은 시작과 폭이 창과 같거나 더 넓어 0이라 지금과 같다.
- 고정 열 손잡이의 한도는 손잡이가 행처럼 표를 가리키는 포인터를 들고 `on_move`의 변화량에만 건다. `table_resize_delta`는 바꾸지 않는다. 작업대가 그 함수를 직접 부른다([workbench_page.cpp](../examples/demo/workbench_page.cpp) 677행).
- 경계선은 표가 `draw_children` 뒤에 긋는다. 머리글과 본문을 함께 가로지르는 곳이 `table_element::draw`뿐이다.
- 드래그 앤 드롭 자동 스크롤의 띠는 표의 가장자리다(`auto_scroll_flows`가 `window.bounds()`로 잰다). 왼쪽 띠가 고정 구역 안에 있다. 알려진 한계로 두고 범위 밖에 적는다.
- 놓기 판정 람다는 고정 열 키 목록을 값으로 담는다. 기존 람다(`key`·`header`)와 같고 표를 가리키는 포인터를 담지 않는다. 탭과 목록이 함께 쓰는 `reorder_drop.h`의 규칙 위에 구역 판정만 더한다.

**접근성.**

- 레인은 `none`이라 `collect_access_children`(accessibility.cpp 25행)이 접고 자식이 그 자리로 오른다. DataGrid·Header·HeaderItem·DataItem의 부모와 순서, 이름, 자동화 id(`kind:owner`)가 `n`이 0일 때와 같다. 하나 다른 것은 머리글 손잡이(Handle) 형제의 순서다: 흐르는 열의 손잡이가 고정 열의 손잡이보다 앞선다.
- 가려진 흐르는 칸은 `visible_bounds`가 레인 clip 밖이라 nullopt이고 UIA에서 화면 밖으로 읽힌다. 일부만 가려지면 교집합이다. 고정 구역 좌표의 접근 질의는 고정 칸과 행이 답한다.
- 고정이라는 사실은 알리지 않는다(결정 7). `access_info`에 칸을 더하지 않는다. 가로 막대의 RangeValue와 `SetValue` 규약은 같다.
- 칸 안 컨트롤의 UIA ScrollIntoView([uia_provider.cpp](../src/win32/uia_provider.cpp) 1315행)는 `route_reveal`을 거치므로 결정 8을 그대로 받는다.

**배율·테마·고대비·움직임 줄이기·기본 문구.** 배율은 경계 한 식이 맡는다. 경계선은 `palette.divider`이고 두께는 `scale`이다. 고대비에서 `divider`는 `window_foreground`다([ui_theme.cpp](../src/theme/ui_theme.cpp) 242행). 선은 머리글 마지막 고정 칸의 오른쪽 선(`box.x + box.width - scale`)과 같은 x에서 본문 높이만큼 이어진다. 움직임이 없어 움직임 줄이기와 닿지 않는다. 기본 문구는 더하지 않는다.

## 결정

**1. 고정을 지정하는 모양.** 질문: 어느 열이 고정인지 어떻게 받는가.

- 길 a: `table_config::frozen_columns`, 앞에서 N개. 되는 것: 고정이 늘 앞쪽 연속 구간이라 고정 사이에 흐르는 열이 끼는 모순이 없다. 고정 폭이 `table_column_spans` 앞 N칸의 합이고 설정이 한 줄이다. 대가: 앞 열을 숨기거나(앱이 `columns`에서 뺀다. table-design.md 43행) 앞 열의 순서를 바꾸는 앱이 N을 함께 맞춰야 한다. 안 맞추면 옆의 흐르는 열이 말없이 고정이 된다.
- 길 b: `table_column::frozen`, 열마다. 되는 것: 표시가 열 모델과 함께 숨김·순서 바꾸기·저장한 배치를 따라간다. 대가: 연속이 아닌 표시(고정, 흐름, 고정)를 풀어야 한다. 앞에서 이어진 구간만 고정이라는 규칙이 앱이 쓴 표시를 말없이 버린다. 순서 바꾸기 판정도 인덱스가 아니라 표시를 읽는다.
- 길 c: 열마다 `table_pin { none, start, end }`. 되는 것: 오른쪽 고정까지 한 모양에서 자란다. 대가: 쓰지 않는 `end`의 규칙(구역 셋, 레인 둘, 양쪽 들이기)을 미리 정해야 하고 결정 5와 겹쳐 이번 단계가 커진다.
- 권장: a. 대가: 앞 열을 빼거나 옮기는 앱이 N을 맞춘다. 승인: 길 a (2026-11-03).

**2. 자르는 구조.** 질문: 흐르는 칸이 고정 구역에 번지지 않게 어디서 자르는가.

- 길 a: 흐르는 부품을 레인(`clip_children` 자식)에 담는다. 되는 것: 그리기·hit·놓을 자리·`visible_bounds`·접근 좌표 질의·휠과 팬 임자가 모두 `clip_children`과 `bounds()` 하나를 따르므로 한 번 세우면 같이 잘린다. 선례가 있다([tab_bar_element.cpp](../src/ui/tab_bar_element.cpp) 768행 `tab_lane_element`, [virtual_list_element.cpp](../src/ui/virtual_list_element.cpp) 260행). 대가: kind 둘이 늘고 행·머리글의 자식 인덱스 계산을 표로 바꿔야 한다.
- 길 b: 행마다 `clipRect`를 두 번 걸고 행의 `hit_test`를 재정의한다. 되는 것: tree 모양이 그대로다. 대가: 그리기와 hit만 맞는다. 접근 좌표 질의·`visible_bounds`·휠·팬 임자는 `clip_children` 규칙을 직접 읽어 가려진 칸이 보조 기술에 보이는 칸으로 남고, 각각을 고치면 controller 파일(줄 3)에 닿는다.
- 길 c: 고정 구역을 불투명 판으로 덮는다. 되는 것: 가장 작다. 대가: 가린 칸이 hit와 접근에 그대로 남고 선택 색이 비칠 수 있다.
- 권장: a. 대가: 인덱스 표 바꾸기다. 회귀는 N이 0일 때의 동일성 시험으로 잡는다. 권장대로 진행.

**3. 고정 열의 조작과 경계.** 질문: 고정 열의 폭 끌기, 순서 바꾸기, 구역 경계를 넘는 끌기를 어떻게 하는가.

- 길 a: 폭 끌기는 허용하되 고정이 서 있는 한도에서 멈춘다. 순서 바꾸기는 같은 구역 안만 받는다. 머리글 칸의 `drop.accepts`에 표의 구역 판정을 더하고 탭과 공유하는 `reorder_drop.h`는 고치지 않는다. 되는 것: N이 늘 맞고 표가 고정 집합을 바꾸는 일이 없다. 대가: 끌어서 고정하고 푸는 몸짓이 없다. 앱이 `column_menu`에서 N을 바꾼다.
- 길 b: 경계를 넘는 놓기도 `reorder_column`으로 낸다. 되는 것: 끌어서 고정 집합이 바뀐다. 대가: N은 앱 상태라 표는 옮긴 열이 고정에 들어가는지 마지막 고정 열이 흐르게 되는지 모르고, 앱마다 다르게 푼다. 고정에 들어올 열의 폭이 창을 넘어도 막을 수 없다.
- 길 c: 고정 열의 폭 끌기와 순서 바꾸기를 모두 막는다. 되는 것: 고정 구역이 앱 코드가 정한 대로 변하지 않는다. 대가: 가장 흔한 고정 대상(이름·경로 열)의 폭을 사용자가 못 바꾼다.
- 권장: a. 대가: 위. 권장대로 진행.

**4. 고정 폭이 창보다 넓을 때.** 질문: 고정 열이 창을 거의 채우면 어떻게 하는가.

- 길 a: 흐르는 창이 `table_key_scroll_step`(48) 아래로 좁아지면 그 배치에서 고정을 푼다. 되는 것: 흐르는 열에 닿을 길이 늘 있고 창 폭만으로 정해져 앱 상태가 늘지 않는다. 48은 새 상수 없이 ←·→ 한 걸음이다. 대가: 창 폭을 줄이는 도중 고정이 풀릴 때 열이 한 번 움직인다.
- 길 b: 고정 구역을 창 안으로 잘라 둔다. 되는 것: 고정이 늘 켜 있다. 대가: 잘린 고정 열과 흐르는 열에 닿을 길이 없다.
- 길 c: 고정 열 폭을 비례해 줄여 그린다. 되는 것: 고정과 흐름이 함께 산다. 대가: 앱 폭(상태)과 다른 폭을 그리고 끌기와 `min_width`의 기준이 어긋난다.
- 권장: a. 대가: 위. 권장대로 진행.

**5. 오른쪽 고정.** 질문: 끝쪽 열도 고정하는가.

- 길 a: 범위 밖으로 둔다. `frozen_columns`는 시작 쪽 앞 N개다. 되는 것: 구역이 둘이고 레인이 하나다. 대가: 합계·동작 열을 오른쪽에 붙인 표는 못 만든다.
- 길 b: `frozen_end_columns`를 같이 낸다. 되는 것: 합계·동작 열. 대가: 구역 셋, 레인 둘, 양쪽 들이기를 풀어야 하고 세로 막대가 서는 오른쪽 끝과 겹친다. 시험이 갑절이다.
- 권장: a. 대가: 위. 이름을 `frozen_columns`로 둬 뒤에 `frozen_end_columns`를 끝에 더할 수 있다. 권장대로 진행.

**6. 경계 표시와 가로 막대 자리.** 질문: 경계를 어떻게 보이고 막대는 어디에 두는가.

- 길 a: 경계에 `divider` 선을 긋고 막대는 전체 폭 그대로다. 되는 것: 막대 값·범위·`scroll_horizontal_to`와 UIA 값이 그대로다(`set_metrics(content, viewport, scroll_x)` 불변). 대가: 막대 길이가 흐르는 구역 몫이 아니라 전체 몫이다. 끝점은 맞는다.
- 길 b: 선과 함께 막대를 흐르는 구역 아래에만 세운다. 되는 것: 막대 길이가 보이는 흐름의 비율이다. 대가: 값 범위는 같은데 막대 슬롯이 고정 켬·끔·풀림에 따라 바뀌고 구석 계산이 하나 늘며 시험이 는다.
- 길 c: 선 없이 머리글의 기존 구분선만 둔다. 되는 것: 그릴 것이 없다. 대가: 흐르기 전에는 본문에서 고정 경계가 보이지 않는다.
- 권장: a. 대가: 위. 그림자는 흐름 상태에 따라 그림이 달라 raster 시험이 늘어 두지 않는다. 권장대로 진행.

**7. 고정을 보조 기술에 알릴지.** 질문: 고정 열임을 읽히는가.

- 길 a: 알리지 않는다. 되는 것: 머리글 이름·역할·자동화 id가 그대로라 이름으로 칸을 찾는 보조 기술 설정과 script가 깨지지 않는다. 대가: 고정은 시각 단서뿐이다. UIA에 열 고정의 표준 속성이 없다.
- 길 b: 머리글 이름에 낱말을 붙인다. 되는 것: 읽힌다. 대가: 이름이 바뀌어 검색이 깨지고, 낱말을 `default_text`에 두고 앱이 정한 이름과 잇는 규칙이 는다.
- 길 c: 머리글 칸의 `access_info::value`에 낱말을 둔다. 되는 것: 이름은 그대로다. 대가: 머리글 칸이 UIA Value 패턴을 내 읽기 소음이 되고, `value`는 현재 값 칸이라 뜻이 어긋난다.
- 권장: a. 대가: 위. 권장대로 진행.

**8. 칸 안 컨트롤 초점의 가로 들이기.** 질문: Tab이나 UIA ScrollIntoView로 흐르는 칸의 컨트롤에 초점이 가면 가로로 들이는가.

- 길 a: `table_element::scroll_delta_to_reveal`을 모든 표에서 재정의하되 왼쪽 끝이 경계 이상인 대상에만 답한다(규칙의 입력 절). 되는 것: `route_reveal`이 흐르는 창 기준 변화량을 받는다. 본문·행·머리글·고정 칸과 이미 보이는 대상은 0이라 본문에 초점이 가도 표가 되돌아가지 않는다. controller는 그대로다. 대가: 고정 없는 표의 동작도 바뀐다. 오른쪽 밖에 숨은 컨트롤에 초점이 가면 이제 흐른다. 왼쪽 밖이나 고정 구역 아래에 일부 숨은 칸은 이 길로 들이지 못한다.
- 길 b: 고정이 서 있는 표에서만 재정의한다. 되는 것: 기존 표가 불변이다. 대가: 같은 규칙이 표마다 다르고 고정 없는 표의 같은 빈틈이 남는다.
- 길 c: 재정의하지 않는다. 되는 것: 변경이 없다. 대가: 고정 구역 뒤에 숨은 컨트롤에 초점이 가도 테가 안 보인다. 셀 커서와 편집(`reveal_delta` 길)만 들인다.
- 권장: a. 대가: 위. 흐르는 컨테이너는 이 함수를 재정의한다고 적은 규약([ui_element.h](../include/luil/ui/ui_element.h) 1380~1391행)에서 표가 빠져 있던 것을 채우는 일이기도 하다. 권장대로 진행.

## 플랫폼

**Win32.** `win32_window.cpp`·`window_surface.cpp`·`uia_provider.cpp`를 고치지 않는다. 표는 플랫폼 독립 코드(`src/ui/table_element.cpp`)다. UIA는 위 접근성 규칙 그대로 DataGrid·Header·HeaderItem을 낸다. 터치·펜의 가로 팬은 `scroll_source` 길 그대로고 고정 칸 위에서 시작해도 표가 받는다.

**Android.** Java 소스는 0줄이다. JNI도 unsupported도 없다. 같은 코어 코드를 쓴다. 경고를 오류로 보는 빌드는 `inspect_names.cpp`의 `default` 없는 `switch`에 case 둘이 있어야 서고, `float`과 `size_t` 사이 변환은 `static_cast`로 쓴다. `examples/mobile_demo`에는 표가 없어 Android 시연은 없다. Android의 luil에는 접근성 계층이 없어 접근성 규칙은 Win32 UIA 기준이다.

## 다른 노드와의 약속

- **줄 3(X-3·X-4·menu_bar).** `ui_interaction.cpp`·`.h`를 고치지 않는다. 셀 커서의 들이기는 `table_element` 안(`move_cell`·`start_edit`이 `table_reveal_delta`를 부른다)이고, 결정 8의 `route_reveal` 호출은 기존 길이다. 약속할 것이 없다. 받아 둘 한계 하나는 범위 밖에 적었다.
- **줄 4(tray·hotkey).** `win32_window.cpp`와 `app_host.h`에 닿지 않는다.
- **줄 1 color_picker(다음 문서)·줄 2 rich_edit.** 파일이 겹치지 않는다. 이 노드는 `table_element.*`, `ui_element_id.h`, `inspect_names.cpp`, 표 시험, 작업대 데모만 고친다.
- **같은 라운드 같은 파일.** `ui_element_id.h`는 `end_of_library_kinds` 바로 위에 kind 둘을 더한다. 다른 줄이 kind를 더하면 같은 자리를 고치지 않고 끝에 더한다. `inspect_names.cpp`의 case 둘은 같은 커밋에 끝에 더한다. [ui_element_tests.cpp](../tests/ui_element_tests.cpp) 84행 `last[]`는 병합 때 마지막 kind로 맞추므로 이 노드가 건드리지 않는다. kind 숫자를 test·UIA script에 박지 않는다.
- **데모.** `examples/demo/workbench_page.*`는 다른 줄도 고칠 수 있다. 라이브러리 커밋과 데모 연결 커밋을 나누고 연결은 통합 때 직렬로 쌓는다. 이 노드의 자리는 `columns_`(273행 근처), `make_changes_table`(1164행), `make_history_bar`(1061행) 곁이다.
- **R2 undo_stack(변경 작업대).** 열 폭 끌기가 `merge_key`로 걸음을 합치고 `resize_column_end`가 끊는 규약은 그대로다. 고정 열의 폭을 한도에서 멈추게 해도 끝 알림은 끌기마다 한 번 온다. 고정 토글은 보기 상태라 걸음을 만들지 않는다.

**미룬 것을 닫는다.** 이 단계에서는 기존 문서를 고치지 않고 구현 커밋이 아래를 고친다.

| 위치 | 닫는 방법 |
| --- | --- |
| table-design.md 105행 | "고정 열"을 빼고 이 문서로 잇는다. 22~34행 "열과 가로 자리"에 이 문서를 가리키는 문장을 더한다. 입력 절에는 결정 8이 고정 없는 표의 동작도 바꾼다는 한 줄(오른쪽 밖에 숨은 칸 안 컨트롤에 초점이 가면 표가 흐른다)을 적는다 |
| table_element.h 194행 주석 | 아직 없는 것의 목록에서 "고정 열"을 지운다 |
| table_element.h 105행 `scroll_x` 주석 | 고정 열이 있으면 흐르는 열만 이만큼 간다고 더한다 |

## 확인

자동 test는 모두 `luil_core_tests`라 Windows와 Android 실기기에서 함께 돈다. UIA 번역 코드를 고치지 않고 레인은 코어에서 접히므로 `luil_tests`는 바뀌지 않는다.

- `tests/table_element_tests.cpp`. 순수 함수: `table_frozen_width`(앞 N, N이 열 수보다 큼, 음수 폭 0), `table_frozen_fits`의 경계, `table_reveal_delta`(왼쪽 밖·오른쪽 밖·창보다 넓음·이미 보임, 고정 폭 0이면 기존 `reveal_delta` 값과 같음). 배치: 고정 1열에 `scroll_x` 40을 배율 1·1.5·2에서 주면 고정 머리글 칸과 행 칸의 x가 변하지 않고 흐르는 칸만 `-40 * scale`만큼 움직이며, 머리글 칸과 행 칸의 bounds가 같고, 레인의 왼쪽이 마지막 고정 칸의 오른쪽과 같고, `duplicate_ids`·`unarranged`가 비어 있다. `frozen_columns`가 0이면 두 레인 kind가 tree에 없고 머리글은 칸들 뒤에 손잡이들, 행은 칸들이다. 열 수 이상으로 고정해도 레인이 서지 않는다. 창이 좁아 고정이 풀린 배치는 고정이 없는 표와 칸 자리가 같고 그림이 한 픽셀도 다르지 않다 (레인은 서 있되 행 전체를 덮는다). 기존 케이스는 모두 고치지 않고 통과한다.
- 같은 파일의 hit와 접근성. 누를 수 있고 이름이 있는 probe로, 흐르는 칸을 고정 구역 아래로 보낸 `scroll_x`에서 고정 구역 좌표의 hit가 고정 칸이고, 경계 손잡이의 흐르는 쪽 절반이 손잡이로 잡히며, 가려진 흐르는 칸의 손잡이는 잡히지 않는다. 같은 자리의 `access_element_at`, 가려진 칸의 `visible_bounds` nullopt, 일부 가림의 교집합, DataGrid·HeaderItem의 역할과 이름 불변.
- 같은 파일의 입력. 셀 커서가 흐르는 열로 갈 때 `scroll_x_intent`의 변화량이 고정 폭을 뺀 창 기준이고 고정 열로 갈 때는 스크롤 메시지가 없다. F2도 같다. 구역 경계를 넘는 놓기는 `accepts`가 거짓이고 같은 구역은 참이다. 고정 열의 넓힘이 한도에서 멈추고 줄임은 `min_width`까지며 끝 알림이 한 번이다. 창이 좁으면 `frozen_columns()`가 0이고 머리글이 스크롤을 따라 움직이며, 넓히면 돌아온다. 가로 막대의 `content_width`·`viewport_width`·값이 `frozen_columns` 0과 같다. `scroll_delta_to_reveal`은 경계 왼쪽에 걸친 대상과 이미 보이는 대상에 0이고 오른쪽에 숨은 칸에 변화량을 낸다. 본문·행·머리글 id로 `route_reveal`을 불러도 `frozen_columns`가 1이고 `scroll_x`가 0보다 클 때 빈 목록이다. `frozen_columns`가 0인 표에서도 같다: 오른쪽 밖 칸의 컨트롤로 `route_reveal`하면 변화량이 고정 폭 0의 `table_reveal_delta`(고정 없는 표의 들이기 값)와 같고, 본문·행·머리글 id에는 빈 목록이다 (결정 8이 바꾸는 고정 없는 표의 동작을 잠근다).
- 같은 파일의 raster(`raster_probe.h`). 색을 채우는 probe를 칸에 두고 배율 2에서 고정 구역에 흐르는 칸의 색이 한 픽셀도 없고 흐르는 구역에는 있으며, 경계 x에 `divider` 색이 선다. `divider`는 알파가 있어 바탕과 섞인 색(`over`)을 본다.
- `tests/workbench_page_tests.cpp` 두 케이스. 고정 토글을 실제 입력 경로로 누르면 `frozen_columns`가 1이 되고 상태 글이 바뀌며 실행 취소 걸음이 늘지 않는다. 고정을 켠 채 경로 열 경계를 끌면 걸음 하나로 합쳐지고 되돌려진다.

데모 칸(`examples/demo` 작업대). 넓은 창에서는 열 합 530이 표 안에 들어가 흐를 일이 없으므로 `columns_`에 폭 300의 "폴더" 열(경로의 폴더 부분)을 더한다. 폴더 열은 맨 끝에 두어 기존 열의 차례가 바뀌지 않고 정렬 대상이 아니다. 기록 줄에 토글 `workbench-freeze`(경로 열 고정, `check_element` 스위치이고 UIA 이름은 "경로 열 고정")와 상태 label `workbench-freeze-state`("고정 열 1: 경로" 또는 "고정 열 없음")를 둔다. 켜면 `frozen_columns`가 1이 된다. 서 있는지는 상태 글이 아니라 UIA로 되읽는다. 가로 막대에 `SetValue`를 준 뒤 "경로" 머리글 칸의 BoundingRectangle x가 그대로고 나머지 열은 움직이며 가려진 칸이 화면 밖으로 읽혀야 한다. `mobile_demo`에는 칸이 없다.

사람 확인: 배율 150%·200%에서 경계선과 이음매, 고대비에서 경계선, 터치 PC의 가로 팬이 고정 열을 두고 흐르는지, Narrator가 머리글과 칸을 열 순서대로 읽는지. UIA 되읽기에 실제 입력이 필요하면 격리 데스크톱에서 먼저 요청하고 허락한 시간만 쓴다.

## 범위 밖

- 오른쪽 고정(결정 5)과 고정 행. 머리글은 이미 세로로 고정이다.
- 끌어서 고정하고 푸는 몸짓과 고정 수의 저장(결정 3). 앱이 한다.
- 고정 열임을 보조 기술에 알리기(결정 7)와 경계 그림자.
- 끌기 자동 스크롤의 띠를 흐르는 창 기준으로 옮기기. `scroll_source` 끝에 시작 쪽 안쪽 여백 칸을 더하고 `auto_scroll_flows`가 읽어야 해서 줄 3의 파일이다. 이번에는 하지 않고 R3 뒤에 다시 본다.
- 열 가상화, 셀 범위 선택, Grid·Table 패턴, 키보드로 열 순서·폭 바꾸기. [표 문서](table-design.md)에서 계속 미룬다.
- Android 접근성 계층.
