# 키보드 초점 자동 스크롤

키보드로 초점을 옮겼을 때 대상이 스크롤 viewport 밖에 있으면 앱의 스크롤 상태를 필요한 만큼 갱신한다. Element는 필요한 변화량을 계산하고, 앱 policy는 어느 스크롤 상태에 그 변화를 보낼지 정한다.

공개 API는 [layout_metrics.h](../include/luil/ui/layout_metrics.h), [ui_element.h](../include/luil/ui/ui_element.h), [ui_interaction.h](../include/luil/ui/ui_interaction.h)에 있다.

## 책임 분리

초점 reveal은 세 계층으로 나뉜다.

| 계층 | 책임 |
| --- | --- |
| 스크롤 element | 대상 bounds를 viewport에 넣는 논리 픽셀 delta 계산 |
| `route_reveal()` | 대상을 포함하는 route 선택과 action 생성 |
| 앱 policy | element id와 앱의 스크롤 메시지 factory 연결 |

스크롤 offset은 앱 상태다. `interaction_controller`와 element는 offset을 직접 수정하지 않고 `input_action`만 반환한다.

## 변화량 계산

`scroll_delta_to_reveal()`은 target의 시작점과 길이, viewport의 시작점과 길이를 받아 변화량을 계산한다.

- 대상이 viewport 앞쪽에 있으면 앞쪽 경계를 맞춘다.
- 대상의 끝이 viewport 뒤쪽을 넘으면 뒤쪽 경계를 맞춘다.
- 대상이 viewport보다 길면 앞쪽 경계를 맞춘다.
- 이미 완전히 보이면 `0`을 반환한다.

양수 delta는 scroll offset 증가, 즉 내용이 위쪽 또는 왼쪽으로 이동한다. 이 부호는 휠과 scrollbar action의 규칙과 같다.

`scroll_view_element`는 y축과 높이를, `strip_element`는 x축과 폭을 사용한다. 두 element 모두 arrange에서 얻은 scale로 물리 픽셀 bounds를 논리 픽셀 delta로 바꾼다.

사용자 정의 스크롤 컨테이너는 `ui_element::scroll_delta_to_reveal()`을 override할 수 있다. 기본 구현은 `0`이다.

## Route 표

`scroll_route`는 스크롤 element id와 delta를 앱 메시지로 바꾸는 함수를 묶는다.

```cpp
const std::array routes {
    luil::scroll_route {
        .id = { luil::ui_element_kind::scroll_view, u8"files" },
        .scroll = [](float delta) {
            return luil::make_app_action(scroll_files { delta });
        },
    },
};
```

`route_reveal(tree, focused, routes)`는 순서대로 route를 검사한다. Route element가 focus target을 포함하면 그 element의 reveal delta를 묻는다. Delta가 0이면 빈 action 목록을 반환하고, 움직임이 필요하면 route factory로 action 하나를 만든다.

먼저 일치하는 route가 우선한다. 중첩 스크롤 컨테이너에서는 안쪽 route를 앞에 두면 안쪽 viewport가 먼저 초점을 드러낸다.

같은 `scroll_route` 형식은 `route_wheel()`에도 쓰인다. 휠과 reveal의 route 목록은 서로 달라도 된다. 예를 들어 탭 막대의 휠은 바깥 컨테이너를, focus reveal은 실제 탭 lane을 대상으로 할 수 있다.

## 호출 시점

Controller는 raw input 처리 전후의 `snapshot.focused`를 비교한다. 처리 결과가 keyboard 표시 초점이고 id가 바뀌었으면 새 초점 tree와 id를 `interaction_policy::on_focus_moved()`에 전달한다. Tab, group 방향키, Home/End, typeahead, 접근성 focus 요청이 이 경로를 사용한다.

Policy의 일반적인 구현은 다음 한 줄이다.

```cpp
return luil::route_reveal(tree, focused, reveal_routes);
```

포인터 클릭으로 얻은 초점에는 callback을 호출하지 않는다. 사용자가 누른 element는 이미 화면에 보인다. Focus trap 자동 진입과 복귀는 tree 갱신 중에 실행되고 `on_focus_moved()`를 호출하지 않는다. 진입점과 복귀점은 앱이 보이는 영역 안에 배치해야 한다.

## 앱 상태 적용

앱은 받은 delta를 현재 offset에 더하고 `clamp_scroll(content_length, viewport_length, offset)`으로 범위를 다듬는다. 다음 frame은 새 offset으로 tree를 다시 배치한다.

Delta가 0일 때 action을 만들지 않는 규칙은 불필요한 logic wake와 동일한 tree 재생성을 막는다.

## 반드시 유지할 불변식

- Reveal 결과는 절대 offset이 아니라 변화량이다.
- 부호는 휠과 동일하다.
- 얼마나 움직일지는 스크롤 element가 계산한다.
- 어느 앱 상태를 움직일지는 policy route가 정한다.
- 이미 보이는 대상은 action을 만들지 않는다.
- 포인터로 얻은 초점은 자동 reveal을 요청하지 않는다.
- 중첩 route는 표의 앞쪽 항목이 우선한다.

## 검증 지침

[scroll_view_element_tests.cpp](../tests/scroll_view_element_tests.cpp)와 [strip_element_tests.cpp](../tests/strip_element_tests.cpp)는 앞, 뒤, 내부, viewport보다 큰 대상을 검증한다. [ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)는 Tab, group 이동, typeahead, 접근성 초점에서 callback과 action이 한 번만 발생하는지 확인한다.
