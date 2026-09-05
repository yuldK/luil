# 키보드로 값 조절하기

`key_step_target`은 포인터로 끌어 바꾸는 값을 keyboard로도 조절하는 element 역할이다. Slider, scrollbar, split handle이 이 계약을 사용한다. 초점은 element에 남고 action은 값 자체가 아니라 변화량을 앱에 전달한다.

공개 형식은 [ui_element.h](../include/luil/ui/ui_element.h), 처리 순서는 [ui_interaction.h](../include/luil/ui/ui_interaction.h)에 있다.

## 값 step 어휘

`value_step`은 key 의미를 element와 분리한다.

| 값 | 일반 key |
| --- | --- |
| `decrease` | Left 또는 Up |
| `increase` | Right 또는 Down |
| `decrease_page` | Page Up |
| `increase_page` | Page Down |
| `minimum` | Home |
| `maximum` | End |

Arrow key는 target의 `focus_axis`와 일치할 때만 step으로 바뀐다. Page Up, Page Down, Home, End에는 방향 축을 적용하지 않는다.

Control 또는 Alt가 눌린 key는 값 step이 소비하지 않고 앱 단축키로 흐른다. Shift는 step 의미를 바꾸지 않는다.

## Target 선언

사용자 정의 element는 build 중 `set_key_step_target()`을 호출한다.

```cpp
luil::key_step_target steps {
    .axis = luil::focus_axis::horizontal,
    .on_step = [change](luil::value_step step)
        -> std::optional<std::vector<luil::input_action>> {
        const float delta = delta_for(step);
        return std::vector<luil::input_action> { change(delta) };
    },
};

set_tab_stop(true);
set_key_step_target(std::move(steps));
```

`key_step_target`이 있다고 자동으로 Tab stop이 되지는 않는다. 독립 컨트롤이면 `set_tab_stop(true)`를 함께 설정한다. 목록 내부 scrollbar처럼 컨테이너가 keyboard 탐색을 소유하는 경우 담는 쪽에서 tab stop을 끌 수 있다.

## 결과와 키 소비

`on_step`의 반환값은 세 상태를 구분한다.

| 반환 | 의미 |
| --- | --- |
| `nullopt` | 이 element가 해당 step을 지원하지 않으므로 key를 계속 전달 |
| 빈 action 목록 | Step은 지원하지만 현재 값에서 움직일 수 없어 key만 소비 |
| action 목록 | Step을 소비하고 앱에 변화를 요청 |

범위 끝에서 빈 목록을 반환하면 Home이나 방향키가 앱의 화면 단축키로 새지 않는다. Split handle은 전체 범위를 모르므로 Home과 End에 `nullopt`를 반환할 수 있다.

## 처리 순서

Controller는 현재 `focused_surface`의 tree에서 `focused` element를 찾는다. Element가 활성이고 target과 callback이 있으면 key를 `value_step`으로 변환해 `on_step`을 호출한다.

값 step은 텍스트 편집 뒤, focus group 이동 앞에 처리된다. 따라서 text input의 Left/Right는 caret을 옮기고, group 안 slider의 Left/Right는 group 초점 대신 slider 값을 바꾼다.

초점은 step 전후에 같은 element에 남는다. App action이 새 frame을 만들더라도 element id와 focusability가 유지되면 controller가 초점을 이어 간다.

## 변화량 계약

Action은 현재 값에 더할 delta를 반환한다. 포인터 drag와 keyboard가 같은 앱 메시지 factory와 부호 규칙을 사용할 수 있다.

Slider는 범위와 현재 값을 알고 있으므로 다음 값을 계산한다.

- Arrow: 작은 step
- Page: 큰 step
- Home: `minimum - value`
- End: `maximum - value`

Scrollbar는 line step, viewport 길이, 현재 offset과 최대 offset을 사용한다. Split handle은 고정 keyboard step을 resize delta로 바꾸고 `split_grows`에 따라 부호를 뒤집는다.

앱은 delta를 상태에 적용할 때 범위로 clamp한다. Element도 현재 snapshot에서 delta가 0임을 알 수 있으면 빈 action 목록을 반환해 불필요한 frame 갱신을 막는다.

## 기본 컴포넌트

### Slider

가로 axis를 사용한다. 기본 범위 기반의 작은 step과 page step을 계산하고 Home/End를 지원한다. `change` factory가 있을 때 pointer drag와 keyboard action을 만든다.

### Scrollbar

세로 axis를 사용한다. Arrow는 `scrollbar_key_line_step`, Page는 viewport 높이, Home/End는 최소 및 최대 offset까지의 delta다. 스크롤할 내용이 없으면 key를 소비하고 action은 내지 않는다.

### Split handle

분할 방향에 맞는 axis를 사용한다. Arrow와 Page step은 지원하지만 전체 pane 범위를 소유하지 않으므로 Home/End는 처리하지 않는다. 증가의 의미는 `split_grows` 설정에 맞춰 앱의 resize 부호로 변환된다.

## 접근성과의 관계

Keyboard step은 접근성 RangeValue action과 같은 앱 상태를 바꾸지만 호출 경로는 다르다. 접근성 provider는 절대 목표 값을 요청할 수 있고 keyboard는 상대 step을 요청한다. 두 경로는 같은 clamp와 메시지 적용 규칙을 공유해야 한다.

## 반드시 유지할 불변식

- 값 step은 현재 초점 element에만 적용한다.
- Arrow는 target axis와 일치할 때만 처리한다.
- Page와 Home/End는 축과 무관하다.
- 결과 action은 변화량 계약을 사용한다.
- 빈 목록과 `nullopt`는 서로 다른 키 소비 의미다.
- 텍스트 편집이 값 step보다 먼저이고 group 이동은 뒤다.
- Step은 초점을 다른 element로 옮기지 않는다.

## 검증 지침

[slider_element_tests.cpp](../tests/slider_element_tests.cpp), [scrollbar_element_tests.cpp](../tests/scrollbar_element_tests.cpp), [split_handle_element_tests.cpp](../tests/split_handle_element_tests.cpp)는 key mapping, delta, 범위 끝, 축, Home/End 지원 여부를 검증한다. [ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)는 수정자와 focus group 우선순위를 확인한다.
