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

**먼저 일치하는 route에서 끝난다.** 답은 언제나 action 하나 아니면 빈 목록이고, 중첩 스크롤 컨테이너에서는 안쪽 route를 앞에 두면 안쪽 viewport가 먼저 초점을 드러낸다. 바깥까지 이어서 들이는 것은 아래의 표 없는 짝이다 — 두 짝은 답의 **모양**이 다르고, 그것을 감추면 표를 쓰는 앱이 오지 않을 두 번째 메시지를 기다린다.

같은 `scroll_route` 형식은 `route_wheel()`에도 쓰인다. 휠과 reveal의 route 목록은 서로 달라도 된다. 예를 들어 탭 막대의 휠은 바깥 컨테이너를, focus reveal은 실제 탭 lane을 대상으로 할 수 있다.

## 표 없는 짝

컨테이너가 `ui_element::set_scroll_source()`로 자기 스크롤 메시지를 들고 있으면 표가 필요 없다. `route_wheel(tree, x, y, delta)`와 `route_reveal(tree, focused)`는 표 대신 `ui_element::scroll()`에서 임자를 찾는다.

컨테이너가 드는 것은 **변화량 메시지 하나**다. 절대 자리는 그것을 실제로 내주는 스크롤 막대에 있다 (`scrollbar_config::scroll_to`, 보조 기술의 `SetValue`가 읽는 자리). 휠도 reveal도 변화량으로만 말하므로 `scroll_source`에는 그 칸이 없다 — 아무도 읽지 않는 칸을 두면 채워 넣은 앱이 무언가 켜졌다고 믿는다.

```cpp
return luil::route_reveal(tree, focused);
```

임자를 고르는 규칙은 표 있는 짝과 같은 질문을 tree에서 직접 묻는다.

- 휠은 좌표를 **덮는** 가장 안쪽, 가장 위의 컨테이너를 찾고 **거기서 끝난다.** 흘리지 않지만 포인터를 막는 것(modal scrim 같은 `hit_opaque`)이 먼저 걸리면 아무 일도 하지 않는다 — 포인터를 막는 것이 modal의 몫이라면 휠도 포인터다. 비활성인 가지는 통째로 지나친다.
- Reveal은 초점을 **품은** 흘리는 조상을 안쪽부터 바깥으로 **잇는다.** 안쪽이 초점을 들인 다음에는 그 안쪽 컨테이너 자신이 바깥의 대상이 된다. 자리는 안쪽이 흘러도 달라지지 않으므로 지금 bounds로 재도 옳다.
- 그래서 **답의 모양이 표 있는 짝과 갈린다.** 그쪽은 처음 맞은 한 줄로 끝나 메시지가 많아야 하나지만, 이쪽은 겹마다 하나씩 내어 0에서 겹의 수만큼이다.
- 한 겹만 보고 끝내면 초점이 화면 밖에 남는 자리가 있다 — 행은 안쪽 목록 안에서 보이는데 그 목록이 바깥 판에서 밀려 나가 있는 경우다.
- Delta가 0인 겹은 메시지를 내지 않고, 전부 보이면 빈 목록이다. 표 있는 짝과 같은 방벽이다.

[scroll_area_element](scroll-area-design.md)가 이 값을 스스로 세운다. 그 영역의 bounds는 스크롤 막대 칸까지 품어 안쪽 창보다 넓지만 `scroll_delta_to_reveal()`을 안쪽 창에 그대로 넘긴다. 그래서 휠과 reveal이 **같은 element를 이름 대고도** 마지막 행이 막대 밑에 남지 않는다. 표를 둘로 나눠야 했던 이유가 이 컨테이너에서는 사라진다.

표는 지워지지 않는다. `scroll_source`를 세우지 않은 컨테이너는 여전히 표로 이름 댄다.

- 앱이 직접 만든 스크롤 컨테이너.
- 휠의 임자와 reveal의 임자가 **진짜로 다른** 컨테이너. 넘침 버튼이 있는 탭 막대가 그 자리다. 휠은 바깥 막대를, reveal은 실제로 흘리는 안쪽 lane을 대상으로 해야 하므로 두 표가 서로 다른 줄을 담는 것이 옳은 답이다.

한 컨테이너는 둘 중 하나에만 오른다. `scroll_source`를 세운 컨테이너를 표에도 적고 policy가 두 짝을 모두 호출하면 같은 스크롤 메시지가 두 번 나간다.

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
- 중첩 route는 표의 앞쪽 항목이 우선하고, 표 있는 짝은 그 한 줄에서 끝난다.
- 표 없는 짝에서는 초점을 품은 컨테이너가 안쪽부터 바깥으로 이어지고, 겹마다 하나씩 action이 난다.
- 표 없는 휠은 가장 안쪽·가장 위의 하나에서 끝나고, `hit_opaque`인 것에 가로막히면 아무 일도 하지 않는다.
- 한 컨테이너는 표와 `scroll_source` 중 하나에만 오른다.

## 검증 지침

[scroll_view_element_tests.cpp](../tests/scroll_view_element_tests.cpp)와 [strip_element_tests.cpp](../tests/strip_element_tests.cpp)는 앞, 뒤, 내부, viewport보다 큰 대상을 검증한다. [ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)는 Tab, group 이동, typeahead, 접근성 초점에서 callback과 action이 한 번만 발생하는지 확인한다.

[scroll_area_element_tests.cpp](../tests/scroll_area_element_tests.cpp)는 표 없는 짝을 검증한다. 나란한 두 영역, 겹친 두 영역에서 휠의 안쪽 우선, 이미 보이는 대상의 빈 목록, 막대 칸까지 품은 바깥 element를 이름 댔을 때 안쪽 창과 같은 delta가 나오는지, 겹친 영역에서 reveal이 안쪽부터 바깥으로 이어지는지(안쪽만·둘 다·아무것도 아닌 세 갈래), 그리고 modal scrim이 덮은 자리의 휠이 아무 일도 하지 않는지를 확인한다.
