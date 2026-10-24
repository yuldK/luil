# Stack, grid, layer, strip, wrap 배치

`luil`의 배치는 부모가 자식의 slot을 정하는 모델이다. 전면적인 측정 단계는 없고, 내용 크기가 필요한 컨테이너만 자식에게 원하는 크기를 묻는다(`preferred_size`). `stack_element`는 한 축의 고정·유연·맞춤 배치, `grid_element`는 행과 열의 칸, `layer_element`는 한 자리에 겹치기, `strip_element`는 가로 스크롤 viewport, `wrap_element`는 같은 크기 항목의 줄바꿈을 맡는다.

공개 API는 [stack_element.h](../include/luil/ui/stack_element.h), [grid_element.h](../include/luil/ui/grid_element.h), [layer_element.h](../include/luil/ui/layer_element.h), [strip_element.h](../include/luil/ui/strip_element.h), [wrap_element.h](../include/luil/ui/wrap_element.h), [layout_metrics.h](../include/luil/ui/layout_metrics.h)에 있다.

## 공통 단위와 원칙

Config의 길이, 간격, padding, scroll offset은 논리 픽셀이다. `arrange_context::scale`을 적용해 물리 bounds를 만든다.

부모는 기본적으로 자식의 원하는 크기를 묻지 않는다. 자식을 담는 코드가 길이를 명시하고, 내용 전체 크기가 필요한 스크롤 컨테이너는 config로 값을 받는다. 묻는 것은 맞춤 항목(`add_fit`, `grid_track::fit`, 정렬된 칸·겹침 자식)뿐이다. 고정·유연 길이만 쓰는 tree는 묻지 않으므로 배치 결과가 바뀌지 않는다.

## 내용 크기

`ui_element::preferred_size(measure_context, available_width)`는 내용이 원하는 크기를 논리 픽셀로 답한다. 기본은 답하지 않음(`nullopt`)이다.

| element | 답 |
| --- | --- |
| `label_element` | 글 한 줄 폭(올림) + 양옆 `padding`, `height_for` |
| `text_button_element` | 아이콘 + 글 + 양옆 `text_button_padding`(12), 최소 `text_button_minimum_width`(64), 높이 `text_button_height`(28). link는 여백·최소 폭이 없다 |
| `check_element` | 안쪽 여백 + 표시 부분 + 간격 + 라벨 + 안쪽 여백, `height_for` |
| `glyph_element` | 글리프 크기의 정사각형 |
| `stack_element`·`grid_element`·`layer_element` | 자식의 원하는 크기를 쌓거나 겹친 크기 |

- 측정은 그리기와 **같은 글자 크기**로 한다. `measure_context::text_width`가 `scaled_font_size`(시스템 글자 크기 설정 포함)와 배율로 물리 크기를 재서 논리로 되돌린다. 글꼴의 힌팅이 크기마다 달라 논리 크기로 재면 그린 폭과 어긋난다.
- 버튼·체크 라벨처럼 스타일의 본문 글자로 그리는 element는 `measure_context::style_metrics()`로 잰다. 비어 있으면 내장 스타일이다. 스타일을 바꾼 앱은 `make_arranged_tree(..., &style.metrics)`로 그리기와 같은 치수를 넘긴다(`arrange_context::metrics`).
- 측정기는 배치 문맥의 것이다(`arrange_context::measurer`, 비면 UI 글꼴의 기본 측정기). Test는 고정 폭 가짜를 넣는다.
- `available_width`는 줄 수가 폭에 달린 내용이 높이를 정하는 폭이다. 한 줄 내용은 무시한다. 여러 줄 label은 Codex 트랙의 label 정렬·말줄임(X0) 위에 더한다.
- arrange 전에 config만으로 답한다. 배치 결과(bounds)를 읽지 않는다.

보이지 않는 자식도 배치 공간을 차지한다. `visible(false)`는 그리기와 hit test를 끄는 상태이며 layout collapse가 아니다. 조건부로 공간까지 제거하려면 그 frame의 tree에서 element를 빼거나 길이를 다르게 구성한다.

## Stack 설정

```cpp
luil::stack_config config {
    .direction = luil::stack_direction::row,
    .spacing = 8.0f,
    .padding = luil::edge_insets::symmetric(12.0f, 8.0f),
    .cross_alignment = luil::stack_alignment::center,
    .main_alignment = luil::stack_main_alignment::start,
};
```

`direction`은 주축을 정한다. Row는 왼쪽에서 오른쪽, column은 위에서 아래로 쌓는다. `spacing`은 모든 entry 사이에 한 번씩 들어가며 gap entry도 entry로 센다.

`padding`은 내부 주축과 교차축 영역을 줄인다. 음수가 되더라도 stack이 별도로 clamp하지 않으므로 호출자는 유효한 크기를 제공해야 한다.

## Stack 항목

`stack_item`은 자식 하나의 배치 값을 이름으로 묶는다.

| 필드 | 의미 |
| --- | --- |
| `length` | 고정 항목의 주축 길이 |
| `weight` | 남은 주축 공간의 배분 비율 |
| `cross_length` | 지정한 교차축 길이, `0` 이하면 전체 사용 |
| `minimum` | 유연 항목이 받을 최소 주축 길이 |
| `maximum` | 유연 항목이 넘지 않는 주축 길이, `0`이면 상한 없음 |
| `fit` | 주축 길이를 자식이 원하는 크기로 잰다 (`add_fit`) |

`weight > 0`이면 유연 항목이고 `length`는 사용하지 않는다. `weight <= 0`이면 고정 항목이다. `minimum`은 유연 항목에만 적용된다.

```cpp
row->add(std::move(icon), luil::stack_item {
    .length = 32.0f,
    .cross_length = 32.0f,
});

row->add(std::move(body), luil::stack_item {
    .weight = 1.0f,
    .minimum = 160.0f,
});
```

`add(child, length)`는 고정 길이 편의 함수다. `add_flexible(child, weight)`는 `weight <= 0`을 `1`로 다듬는다. 구조체를 직접 넘기는 `add(child, stack_item)`은 값을 다듬지 않는다.

## 유연 공간 계산

Stack은 내부 주축 길이에서 고정 길이와 모든 spacing을 뺀 값을 `remaining`으로 계산한다. 음수면 `0`으로 둔다. 유연 항목은 weight 비율로 `remaining`을 나눠 갖는다.

유연 항목은 하한(`minimum`)과 상한(`maximum`)을 지키며 나눈다. 한 걸음마다 아직 서지 않은 항목의 몫을 견주어 하한·상한을 어긴 항목을 그 값에 세우고, 세운 항목이 쓴 자리를 뺀 나머지를 남은 항목이 다시 나눈다. 어긴 양의 합이 늘리는 쪽이면 하한에 걸린 것만, 줄이는 쪽이면 상한에 걸린 것만 세운다(CSS flexbox의 유연 길이 풀기와 같은 절차다, `src/ui/flexible_lengths.h`).

- 하한에 걸린 항목의 부족분은 **다른 유연 항목의 몫에서 나온다.** 하한들의 합이 남은 자리를 넘을 때만 전체가 stack bounds를 넘는다. 예전에는 하한이 다른 항목의 몫을 줄이지 않아, 한쪽이 하한에 걸리면 그만큼 늘 넘쳤다(데모 변경 작업대가 좁은 창에서 오른쪽으로 잘린 원인이다).
- 상한에 걸린 항목이 남긴 자리는 걸리지 않은 항목이 나눈다. 모두 상한에 섰으면 남은 자리는 주축 정렬이 둔다.
- 하한이 상한보다 크면 하한이 이긴다.

맞춤 항목(`fit`)은 고정 항목처럼 계산한다. 가로 stack은 자식이 제한 없는 폭에서 원하는 폭을, 세로 stack은 stack의 교차 폭에서 원하는 높이를 주축 길이로 쓴다. 답하지 않는 자식은 0이다. 교차축 길이를 정하지 않았고 교차축 정렬이 `stretch`가 아니면 교차축도 원하는 크기에 선다.

```cpp
row->add_fit(std::move(save));            // "저장" · "Save" · "Speichern" 어느 것이든 글이 잘리지 않는다
row->add_flexible_gap();
row->add(std::move(preview), luil::stack_item { .weight = 1.0f, .minimum = 120.0f, .maximum = 480.0f });
```

## 주축 정렬

`stack_main_alignment`는 유연 항목이 하나도 없을 때 남은 공간을 배치한다.

- `start`: 처음부터 쌓는다.
- `center`: 남은 공간의 절반만큼 앞을 비운다.
- `end`: 남은 공간을 앞에 모두 둔다.

유연 항목이 있으면 그 항목들이 남은 공간을 사용하므로 세 정렬 값의 결과가 같다. 내용이 넘칠 때도 free space가 0이므로 음수 offset을 만들지 않는다.

항목 사이를 유연하게 벌리려면 `add_flexible_gap()`을 사용한다. 바깥 남은 공간의 정렬은 `main_alignment`, 항목 사이 공간은 gap으로 구분한다.

## 교차축 정렬

`cross_length <= 0`이면 자식은 교차축 전체 길이를 사용한다. 양수이면 `cross_alignment`에 따라 start, center, end 위치를 계산한다.

`stretch`는 교차 길이를 지정하지 않은 기본 동작과 같다. 지정된 `cross_length`를 강제로 늘리지 않는다.

교차 길이가 사용 가능 길이보다 크면 center와 end offset은 음수가 될 수 있다. Stack은 잘라내지 않으며 clipping 여부는 상위 element가 결정한다.

## Gap

`add_gap(length)`와 `add_flexible_gap(weight)`은 자식 없는 entry를 추가한다. Tree element가 아니므로 draw와 hit test 비용 없이 공간만 차지한다.

Gap도 spacing 계산에 포함된다. 두 자식 사이에 고정 gap을 넣으면 `자식 - spacing - gap - spacing - 자식` 구조가 된다. 정확한 한 칸 간격만 필요하면 stack의 `spacing`을 사용한다.

## Grid

`grid_element`는 자식을 행과 열의 칸에 놓는다. 줄 규칙은 `grid_track`이다.

| 규칙 | 길이 |
| --- | --- |
| `grid_track::fixed(length)` | 그 값 |
| `grid_track::fit(minimum)` | 그 줄 하나에만 놓인 자식이 원하는 크기 중 가장 큰 것 (하한 이상) |
| `grid_track::flexible(weight, minimum)` | 고정·맞춤 줄과 간격을 뺀 나머지를 비율로 (하한 이상. 부족분은 다른 유연 줄에서 나오고, 하한들의 합이 넘칠 때만 넘친다) |

- 열 폭을 먼저 정하고 행 높이를 나중에 정한다. 맞춤 행은 자식이 **자기 칸 폭에서** 원하는 높이다.
- 여러 줄에 걸친 자식(`row_span`·`column_span`)은 맞춤 줄을 넓히지 않고 줄들이 정해진 자리를 받는다. 넓힐 몫을 어느 줄에 줄지가 규칙마다 달라 읽는 사람이 예측할 수 없기 때문이다.
- 선언보다 뒤의 칸에 놓은 자식이 있으면 모자란 줄은 맞춤 줄이다.
- 칸 안의 자리는 `layout_alignment`다. `stretch`(기본)는 칸을 채우고, 나머지는 원하는 크기(칸보다 크지 않게)에 선다.
- 원하는 크기는 유연 줄도 잰 크기로 본 합이다.

폼은 grid의 한 사용법이다.

```cpp
luil::grid_config config { .columns = { luil::grid_track::fit(), luil::grid_track::flexible() }, .column_spacing = 8.0f, .row_spacing = 6.0f };
auto form { std::make_unique<luil::grid_element>(id, config) };
form->add(std::move(name_label), { .row = 0, .column = 0, .vertical = luil::layout_alignment::center });
form->add(std::move(name_input), { .row = 0, .column = 1 });
```

라벨 열은 가장 긴 라벨 폭이고, 언어가 바뀌어도 손으로 고칠 폭이 없다.

## Layer

`layer_element`는 자식들을 같은 자리에 겹친다(그림 위 배지, 빈 목록 위 안내 글, 떠 있는 버튼). 자식마다 `layer_item`으로 정렬·길이·여백을 따로 정한다.

- 담은 순서가 그리는 순서이고, 뒤에 담은 것이 위에 그려지고 먼저 눌린다.
- `stretch`가 아니고 길이를 정하지 않은 축은 원하는 크기에 선다(안쪽 영역보다 크지 않게). 답이 없으면 채운다.
- 원하는 크기는 자식들의 원하는 크기(정한 길이와 여백 포함) 중 가장 큰 폭과 높이다.

## 가로 스크롤 strip

`strip_element`는 content 한 개를 가로로 이동시키고 자신의 bounds로 clip한다.

```cpp
luil::strip_config strip {
    .content_width = tabs_width,
    .scroll_offset = tab_scroll,
};
```

Content는 `x - scroll_offset * scale`에서 시작하며 최소한 viewport 폭만큼의 slot을 받는다. `maximum_scroll()`과 arrange에서 clamp된 `scroll_offset()`은 arrange 뒤에 유효하다.

Strip은 overflow 버튼이나 scrollbar를 포함하지 않는다. 담는 쪽이 가로 stack으로 고정 버튼과 strip을 나란히 둔다.

`arrange_context::scroll_offset`은 세로 sticky layout을 위한 값이므로 strip은 자신의 가로 offset을 자식 context에 싣지 않는다. 자식은 부모 context의 기존 값을 그대로 받는다.

## 같은 크기 wrap

`wrap_element`는 동일한 `item_width`와 `item_height`를 가진 자식을 왼쪽에서 오른쪽으로 놓고 폭이 모자라면 다음 줄로 넘긴다.

`wrap_columns_for(config, available_width)`는 적어도 한 열을 반환한다. `item_width <= 0`이거나 step이 유효하지 않아도 나누기를 시도하지 않고 한 열로 처리한다.

`wrap_height_for(config, available_width, count)`는 tree를 만들기 전에 필요한 전체 높이를 계산한다. Arrange도 같은 열 계산을 사용하므로 호출자가 잡은 높이와 실제 줄 수가 일치한다.

Wrap에는 항목별 크기, weight, 정렬, padding이 없다. 가변 크기나 여백이 필요하면 stack과 panel을 조합한다. 내용이 slot보다 크면 overflow하며 wrap 자체는 스크롤하거나 clip하지 않는다.

## 조합 패턴

- 고정 sidebar와 유연 content: 가로 stack에 고정 `length`와 `add_flexible`
- 아래쪽 action bar: 세로 stack의 content를 flexible로 두고 action bar를 고정 길이로 추가
- 오른쪽 정렬 버튼: row stack의 `main_alignment = end`
- scrollable chips: `wrap_element`를 `scroll_view_element`의 content로 사용하고 `wrap_height_for()`를 content height로 전달
- overflow tabs: 고정 버튼과 `strip_element`를 row stack에 배치

## 반드시 유지할 불변식

- 부모가 모든 자식 slot을 결정한다.
- 원하는 크기는 맞춤 항목을 가진 컨테이너만 묻는다. 고정·유연 길이만 쓰는 tree의 배치는 바뀌지 않는다.
- 측정은 그리기와 같은 글자 크기(`scaled_font_size`)와 같은 스타일 치수로 한다.
- 상한에 걸린 유연 항목이 남긴 자리는 걸리지 않은 유연 항목이 나누고, 모두 걸렸으면 주축 정렬이 둔다.
- Grid의 맞춤 줄은 그 줄 하나에만 놓인 자식만 잰다.
- 항목 길이 설정은 논리 픽셀이다.
- 유연 minimum은 다른 항목을 재분배하지 않고 overflow를 만든다.
- `visible(false)`는 layout 공간을 유지한다.
- 주축 바깥 정렬과 항목 사이 gap은 별도 개념이다.
- Strip은 가로 offset을 `arrange_context::scroll_offset`에 넣지 않는다.
- Wrap의 높이 계산과 arrange는 같은 열 계산을 사용한다.

## 검증 지침

[stack_element_tests.cpp](../tests/stack_element_tests.cpp)는 고정 및 유연 길이, minimum, 정렬, 교차축, gap, visibility를 검증한다. [content_layout_tests.cpp](../tests/content_layout_tests.cpp)는 label·버튼·체크·글리프의 원하는 크기(배율과 스타일 글자 크기 포함), stack의 맞춤 항목과 상한·재분배, 맞춤과 상한이 없는 stack의 기존 결과, 폼 grid의 라벨 열·칸 정렬·걸친 자식·모자란 줄, layer의 자리와 위쪽 hit을 고정 폭 측정기로 확인한다. [strip_element_tests.cpp](../tests/strip_element_tests.cpp)와 [wrap_element_tests.cpp](../tests/wrap_element_tests.cpp)는 clamp, reveal, 열 수, 높이, overflow를 확인한다.
