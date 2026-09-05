# Stack, strip, wrap 배치

`luil`의 배치는 측정 단계 없이 부모가 자식의 slot을 정하는 모델이다. `stack_element`는 한 축의 고정 및 유연 배치, `strip_element`는 가로 스크롤 viewport, `wrap_element`는 같은 크기 항목의 줄바꿈을 맡는다.

공개 API는 [stack_element.h](../include/luil/ui/stack_element.h), [strip_element.h](../include/luil/ui/strip_element.h), [wrap_element.h](../include/luil/ui/wrap_element.h), [layout_metrics.h](../include/luil/ui/layout_metrics.h)에 있다.

## 공통 단위와 원칙

Config의 길이, 간격, padding, scroll offset은 논리 픽셀이다. `arrange_context::scale`을 적용해 물리 bounds를 만든다.

부모는 자식의 원하는 크기를 묻지 않는다. 자식을 담는 코드가 길이를 명시하고, 내용 전체 크기가 필요한 스크롤 컨테이너는 config로 값을 받는다.

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

유연 항목의 계산 길이가 `minimum`보다 작으면 minimum으로 올린다. 다른 유연 항목의 몫을 다시 줄이지 않으므로 전체가 stack bounds를 넘을 수 있다. Minimum은 overflow를 허용하는 하한이다.

상한과 재분배는 제공하지 않는다. 남는 공간의 소유 관계가 필요한 배치는 더 작은 stack을 조합해 표현한다.

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
- 항목 길이 설정은 논리 픽셀이다.
- 유연 minimum은 다른 항목을 재분배하지 않고 overflow를 만든다.
- `visible(false)`는 layout 공간을 유지한다.
- 주축 바깥 정렬과 항목 사이 gap은 별도 개념이다.
- Strip은 가로 offset을 `arrange_context::scroll_offset`에 넣지 않는다.
- Wrap의 높이 계산과 arrange는 같은 열 계산을 사용한다.

## 검증 지침

[stack_element_tests.cpp](../tests/stack_element_tests.cpp)는 고정 및 유연 길이, minimum, 정렬, 교차축, gap, visibility를 검증한다. [strip_element_tests.cpp](../tests/strip_element_tests.cpp)와 [wrap_element_tests.cpp](../tests/wrap_element_tests.cpp)는 clamp, reveal, 열 수, 높이, overflow를 확인한다.
