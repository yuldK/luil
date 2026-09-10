# 흘리는 영역

`scroll_area_element`는 세로 스크롤 viewport, 선택적 scrollbar, 치수 보고, 휠 라우팅, 초점 되살리기를 하나의 컴포넌트로 묶는다. [list_element](list-view-design.md)의 조립에서 행 모델을 뺀 것이라, 목록이 아닌 내용(글 미리보기, 설정 판, 그림 묶음)도 같은 값을 누린다.

공개 API는 [scroll_area_element.h](../include/luil/ui/scroll_area_element.h)에 있다. 공통 입력 의미는 [interaction.md](concepts/interaction.md)와 [ui-element.md](concepts/ui-element.md)를 따른다.

## 상태 소유권

영역의 지속 상태는 앱이 소유한다.

- `content_height`: 내용 전체 높이 (논리 픽셀)
- `scroll_offset`: 세로 스크롤 위치 (논리 픽셀)

Element는 이 값을 받아 tree를 만들고 factory를 통해 intent만 반환한다. 스크롤 위치를 내부에 기억하지 않는다. 측정 단계가 없으므로 `content_height`는 담는 쪽이 아는 상수다.

`owner`는 같은 화면의 여러 영역을 구분한다. 영역이 스스로 조립하는 부품(`scroll_area_view`, `scroll_area_bar`)이 그 키를 그대로 물려받으므로, 한 표면에 영역이 둘 이상 서면 `owner`가 서로 달라야 한다. 겹치면 `ui_tree::duplicate_ids()`에 나타나고 hover와 초점이 첫 id로 고정된다.

## 무엇을 한 자리로 묶는가

[scroll_view_element](../include/luil/ui/scroll_view_element.h)는 자르고 흘리기만 하는 원시 도구다. 그 옆에 막대를 세우려면 앱이 다음을 손으로 맞춰야 했다.

| 앱이 손으로 하던 것 | 어긋나면 |
| --- | --- |
| 내용 높이를 창과 막대에 두 번 넘기기 | thumb 길이가 실제 내용과 다르다 |
| 창 높이 **추측** | 흘릴 수 있는 양과 thumb가 어긋난다 |
| `clamp_scroll()`을 앱에서 다시 쓰기 | 창과 막대가 다른 offset을 본다 |
| 같은 스크롤 factory를 막대와 표 두 곳에 적기 | 한쪽만 고친 변경이 조용히 남는다 |
| 휠 표와 되살리기 표를 따로 짓기 | 두 표의 id가 갈려 마지막 줄이 막대 밑에 남는다 |

창 높이는 배치가 정해져야 알 수 있는 값이다. 앱이 그것을 추측하고 추측이 틀리면 막대와 내용이 어긋나므로, 저장소 안의 흘리는 창 절반이 막대 없이 서 있었다.

영역은 이 다섯을 자기 `arrange` 안으로 가져간다.

1. **막대를 안에 담는다.** 창이 실제로 받은 높이가 곧 막대가 재는 높이다.
2. **치수를 내준다.** `metrics()`가 네 값을 한 자리에서 답한다.
3. **휠을 받는다.** 스스로 `scroll_source`를 세우므로 앱의 휠 표에 오르지 않는다.
4. **초점을 따라간다.** `scroll_delta_to_reveal()`을 안쪽 창에 넘긴다.
5. **폭을 좁힌다.** 막대가 가져간 칸만큼 창이 실제로 좁아진다.

## 스크롤 막대 표시

`scrollbar_visibility`가 막대를 언제 세울지 정한다.

| 값 | 막대 | 내용 폭 |
| --- | --- | --- |
| `automatic` | 흘릴 것이 있을 때만 세운다 | 짧은 내용에서는 칸을 통째로 내준다 |
| `always` | 흘릴 것이 없어도 자리를 지킨다 | 언제나 막대 칸만큼 좁다 |
| `never` | 세우지 않는다 | 언제나 전체 폭이다 |

`never`는 `scroll` factory가 없는 것과 다르다. 그쪽은 흘리는 것 자체를 하지 않겠다는 뜻이고, 이쪽은 흘리되 막대를 보이지 않겠다는 뜻이다. `never`에서도 휠과 초점 되살리기는 그대로 산다.

`always`가 있는 이유는 폭의 흔들림이다. 내용이 늘고 주는 창에서 글의 폭이 frame마다 바뀌면 한 줄 늘어난 것만으로 문단 전체가 다시 접힌다.

`automatic`이 막대를 세우지 않은 frame에서도 막대 element 자체는 tree에 남고 `set_visible(false)`로 숨는다. 숨긴 것은 그려지지도 눌리지도 않으므로 "보이지 않는데 잡히는" 칸이 생기지 않는다. `never`에서는 element를 아예 만들지 않는다.

막대는 `bar_tab_stop`이 참일 때 Tab의 자리다 (기본값). 홀로 서는 영역에서는 참이 옳다 — 막대가 자리를 가져야 키보드만으로도 긴 글을 훑을 수 있고, 그것이 `scrollbar_element`가 스스로 세우는 기본값이다. 거짓으로 두는 것은 담는 쪽이 이미 세로 키를 가진 자리다(목록·[가상 목록](virtual-list-design.md)). 그러지 않으면 같은 목록에 Tab의 자리가 둘 선다.

`bar_tab_stop`이 참이어도 **흘릴 것이 없는 frame에서는 자리가 아니다.** `always`로 자리를 지키는 막대는 짧은 내용에서도 보이지만 끌 수도 누를 수도 없으므로(`draggable()`이 거짓), 그것이 Tab의 자리로 남으면 사용자는 아무 일도 하지 않는 자리를 한 번 더 지나고 거기서 누른 키는 조용히 사라진다.

막대는 이름표를 받지 않는다. 설정에 도움말 글을 두는 칸이 없고, 보조 기술이 읽는 것은 이름이 아니라 값의 범위다(`metrics()`의 네 값이 그대로 간다) — 창마다 다른 이름을 지어 붙이면 같은 부품이 화면마다 다르게 읽힌다.

막대의 폭은 **영역의 폭을 넘지 않는다.** slot 폭으로 다듬지 않으면 아주 좁은 칸에서 막대의 왼쪽 끝이 영역 밖으로 나가, 화면에는 남의 자리에 그려지고 휠은 영역 밖이라 아무도 받지 않는다. 다듬은 frame에서 창에 남는 폭은 0이다 — 좁기는 어느 쪽이든 마찬가지지만 이쪽은 남의 자리를 침범하지 않는다.

## 가장자리 표시

`scroll_edges`가 창의 위·아래 가장자리를 정한다. 기본값은 전부 거짓이라 지금까지의 영역은 아무것도 더 그리지 않는다.

| 필드 | 그림 |
| --- | --- |
| `shadows` | 흘린 만큼 위에, 더 흘릴 만큼 아래에 그림자를 드리운다 (`content_shadow`) |
| `top_rule` | 위 가장자리에 늘 1px 구분선을 긋는다 (`divider`) |
| `bottom_rule` | 아래 가장자리에 늘 1px 구분선을 긋는다 |

그림자는 잘린 줄이 경계 밖으로 이어진다는 표시다. 맨 위에 선 창은 위가 깨끗하고 끝까지 흘린 창은 아래가 깨끗하다 — 흘릴 것이 없는 창은 어느 쪽에도 없다. 깊이는 10 논리 픽셀이고 창이 그보다 얕으면 절반까지라 위·아래가 겹치지 않는다.

구분선은 머리글이나 단추와 맞닿는 쪽에 긋는다. 그것이 없으면 흘러가는 행이 그 위의 머리글과 섞인다. 흘린 양과 무관하게 늘 선다.

영역이 **자기 치수로** 그린다. 앱이 `metrics()`를 읽어 겹쳐 그리면 그 offset은 앱이 앞서 든 값이라 한 frame 낡고, 다듬은 값과 어긋난 frame에서 그림자가 한 박자 늦게 선다. 같은 그림을 자기 창에 손으로 그리는 앱은 [draw_scroll_edges()](../include/luil/ui/draw_primitives.h)를 쓴다 — 영역과 [목록](list-view-design.md)이 부르는 그 함수라, 한 화면의 흘리는 창이 전부 한 모양이다.

## 치수 계약

`metrics()`는 `scroll_metrics` 네 값을 돌려주고 전부 논리 픽셀이다.

| 필드 | 의미 |
| --- | --- |
| `content_height` | 설정으로 받은 내용 높이 |
| `viewport_height` | 창이 **실제로 받은** 높이 |
| `scroll_offset` | `arrange`가 범위 안으로 다듬은 값 |
| `maximum_scroll` | 흘릴 수 있는 최대치 |

네 값은 `arrange` 뒤에 유효하다. 그 전에는 전부 0이다. `overflowing()`은 `maximum_scroll > 0`과 같은 문장이라, 막대를 세울지 묻는 자리와 흘릴 것이 있는지 묻는 자리가 같은 식을 본다.

앱은 다음 frame을 만들기 전에 이 값을 자기 상태에 되돌린다. 범위를 벗어난 offset을 준 frame에서도 창, 막대, `metrics()`가 전부 같은 다듬은 값을 본다. tree를 만들기 전에 [clamp_scroll()](../include/luil/ui/layout_metrics.h)로 상태를 미리 다듬어도 결과는 같다 — 식이 한 벌이기 때문이다.

`viewport()`는 내용이 보이는 자리를 **물리 픽셀**로 돌려준다. 겹쳐 그리는 앱이 읽는 값이라 배율이 곱해져 있다. 치수는 논리, 자리는 물리라는 갈래는 다른 element와 같다.

**배치는 한 번이다.** 막대를 세울지는 흘릴 것이 있는지에 달렸지만, 그것은 내용 높이와 창 높이만의 함수이고 둘 다 `arrange`가 들어서는 순간 이미 알고 있다 — 내용 높이는 설정이 준 상수이고 창 높이는 받은 slot이다. 그래서 치수를 **창을 배치하기 전에** 셈하고, 막대가 가져갈 칸을 뺀 폭으로 창을 한 번만 배치한다. 그 식을 순수 함수로 떼어 둔 것이 [layout_metrics.h](../include/luil/ui/layout_metrics.h)다.

창을 한 번 배치해 보고 넘침을 물으면 그 답을 얻자고 배치를 두 번 하게 되고, 내용의 `arrange`가 한 frame에 두 번 불린다. **배치 중에 자식을 쌓는 내용에는 그것이 치명적이다** — [가상 목록](virtual-list-design.md)이 행을 짓는 자리가 배치이고, 두 번 불리면 같은 자리표의 행이 둘 선다. "같은 값으로 두 번 불려도 같은 결과여야 한다"를 내용에게 요구하는 대신 두 번 부르지 않는다.

## 표 없는 라우팅

영역은 생성 시점에 자기 `scroll_source`를 세운다. 그래서 앱은 휠 표도 되살리기 표도 짓지 않는다.

```cpp
// policy의 몸통 전부다. 화면에 창이 몇이든, 어느 페이지가 떠 있든 같은 줄이다.
[[nodiscard]] std::vector<luil::input_action> on_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, const float delta) override
{
    return luil::route_wheel(tree, event.x, event.y, delta);
}

[[nodiscard]] std::vector<luil::input_action> on_focus_moved(const luil::ui_tree& tree, const luil::ui_element_id& focused) override
{
    return luil::route_reveal(tree, focused);
}
```

두 질문의 임자를 고르는 규칙은 다음과 같다.

- `route_wheel()`은 좌표를 **덮는** 컨테이너를 찾는다. 그리기 순서를 거슬러 가장 안쪽, 가장 위의 것이 임자이고 **거기서 끝난다.**
- 흘리지 않고 **삼키기만 하는 것**이 먼저 걸리면 휠은 아무 일도 하지 않는다 (modal scrim 같은 `hit_opaque`). 포인터를 막는 것이 modal의 몫이라면 휠도 포인터다. 비활성인 가지도 통째로 지나친다 — 흐리게 그려 놓고 굴러가면 그것은 비활성이 아니다.
- `route_reveal()`은 초점을 품은 **흘리는 조상 전부**를 안쪽부터 바깥으로 잇는다. 안쪽의 논리 스크롤 변화량에 `scroll_source::scale`을 곱해 대상의 물리 좌표를 옮긴 뒤, 뷰포트에서 보이는 부분을 다음 조상의 대상으로 넘긴다. 따라서 안쪽 창이 바깥보다 커도 초점 행이 실제로 보일 위치까지 바깥을 흘린다. `scroll_area_element`는 배치 시 이 배율을 자동으로 설정한다.
- 그래서 **답의 모양이 갈린다.** 휠은 많아야 메시지 하나, 되살리기는 0에서 겹의 수만큼이다 (표 있는 짝은 둘 다 처음 맞은 한 줄로 끝난다 — [focus-reveal-design.md](focus-reveal-design.md)).
- 이미 보이는 겹은 메시지를 내지 않고, 전부 보이면 빈 목록이다. 이 방벽이 없으면 화살표를 누를 때마다 0짜리 메시지가 logic을 깨워 tree를 통째로 다시 짓는다.
- 잘린 컨테이너 밖의 좌표는 그 안을 보지 않는다. 보이지 않는 것은 휠의 임자도 아니다.

**두 질문이 같은 element를 본다는 것이 요점이다.** 표로 이름 대던 시절에는 휠이 막대까지 품은 바깥을, 되살리기가 실제로 자르는 안쪽을 이름 대야 했다. 영역의 bounds는 막대 칸까지 품어 창보다 넓지만, `scroll_delta_to_reveal()`이 안쪽 창에 그대로 넘기므로 바깥을 이름 대도 마지막 행이 막대 밑에 남지 않는다. 표를 둘로 나눠야 했던 이유가 여기서 사라진다.

`scroll_to`를 함께 주면 막대가 보조 기술의 절대 위치 요청(UIA `SetValue`)에 답한다. 없으면 막대가 읽기 전용으로 선다 — 델타로 환산해 보내면 오래된 발행본 기준의 변화량이 겹쳐 쌓인다.

그 절대 메시지는 **막대의 것이다** (`scrollbar_config::scroll_to`, 영역의 설정이 그리로 흘려 넣는다). 휠도 되살리기도 변화량으로만 말하므로 `scroll_source`에는 그 칸이 없다 — 아무도 읽지 않는 칸을 두면 채워 넣은 앱이 무언가 켜졌다고 믿는다.

## 원시 도구를 그대로 쓰는 자리

영역은 **세로 창 하나와 그 오른쪽 막대 하나**라는 조립을 고정한다. 그 조립이 아니면 [scroll_view_element](../include/luil/ui/scroll_view_element.h)와 [scrollbar_element](../include/luil/ui/scrollbar_element.h)를 그대로 옆에 놓는다.

- 가로로 흘리는 띠. [strip_element](../include/luil/ui/strip_element.h)가 이미 그 자리다.
- 막대를 왼쪽에 두거나 내용 위에 겹쳐 그리는 배치.
- 창 여럿이 막대 하나를 나눠 쓰는 배치 (나란한 두 칸이 함께 흐르는 표).
- 휠의 임자와 되살리기의 임자가 **진짜로 갈리는** 컨테이너. 넘침 버튼이 있는 탭 막대가 그렇다 — 휠은 바깥 막대를, 되살리기는 안쪽 레인을 이름 대야 하므로 [scroll_route](focus-reveal-design.md) 표가 남는다.

## 사용 예

```cpp
luil::scroll_area_config config {
    .owner = u8"notes",
    .content_height = paragraph_height,
    .scroll_offset = scroll_,
    .scroll = [](const float delta) { return luil::make_app_action(scroll_notes { delta }); },
    .scroll_to = [](const float offset) { return luil::make_app_action(scroll_notes_to { offset }); },
    .bar = luil::scrollbar_visibility::always,
};
auto area { std::make_unique<luil::scroll_area_element>(std::move(config)) };
area->set_content(build_paragraphs());
```

앱은 받은 delta를 자기 offset에 더하고 다음 frame에 그 값을 그대로 넘긴다. 다듬기는 `arrange`가 한다.

## 반드시 유지할 불변식

- 내용 높이와 스크롤 위치는 앱 상태다.
- `owner`는 같은 표면의 영역 전체에서 유일하다.
- `metrics()`의 네 값은 전부 논리 픽셀이고 `arrange` 뒤에 유효하다.
- `viewport_height`는 창이 실제로 받은 높이다.
- `metrics()`가 답한 offset은 창과 막대가 쓴 그 값이다.
- `scroll` factory가 없으면 막대도 휠도 되살리기도 없다.
- `bar`가 `never`여도 휠과 되살리기는 산다.
- 막대가 자리를 가져가면 내용의 폭이 실제로 그만큼 좁아진다.
- 막대의 폭은 영역의 폭을 넘지 않는다.
- 막대는 `bar_tab_stop`이 참이고 **흘릴 것이 있는** frame에서만 Tab의 자리다.
- 숨긴 막대는 그려지지도 눌리지도 않는다.
- 영역 하나가 휠과 되살리기 두 질문의 임자다.
- 휠은 겹친 영역에서 안쪽 하나가 임자이고, `hit_opaque`인 것에 가로막히면 아무 일도 하지 않는다.
- 되살리기는 안쪽부터 바깥으로 이어지고, 겹마다 하나씩 메시지를 낸다.
- 이미 보이는 겹에는 메시지를 만들지 않는다.
- 내용의 `arrange`는 한 frame에 한 번 불린다.

## 검증 지침

[scroll_area_element_tests.cpp](../tests/scroll_area_element_tests.cpp)는 배율 1과 2의 치수, 범위를 벗어난 offset의 다듬기와 창·막대·치수의 일치, `scrollbar_visibility` 세 갈래와 그때마다 내용이 받은 폭, 내용이 한 frame에 **한 번만** 배치되는 것, 막대보다 좁은 칸에서 막대가 영역 안에 남는 것, `bar_tab_stop`과 넘침이 함께 정하는 Tab 자리, factory가 없는 영역, 표 없는 `route_wheel`·`route_reveal`(나란한 둘, 겹친 둘, 막대 칸을 품은 바깥, 겹겹이 이어지는 되살리기, scrim이 삼키는 휠), 내용 안 컨트롤의 hit test와 Tab 자리를 검증한다. [raster_draw_tests.cpp](../tests/raster_draw_tests.cpp)는 가장자리 그림자가 흘린 쪽에만 서고(다듬은 offset 기준) 구분선이 흘린 양과 무관하게 서는 것을 픽셀로 잠근다.

[scroll_view_element_tests.cpp](../tests/scroll_view_element_tests.cpp)는 안쪽 창의 다듬기와 reveal 계산을, [scrollbar_element_tests.cpp](../tests/scrollbar_element_tests.cpp)는 thumb 배치와 끌기·키 걸음을 따로 잠근다. 초점 되살리기의 호출 시점은 [ui_interaction_tests.cpp](../tests/ui_interaction_tests.cpp)에 있다.

사용 예는 [network_page.cpp](../examples/demo/network_page.cpp)의 결과 칸(`make_lines_view`)과 [demo_main.cpp](../examples/demo_main.cpp)의 표 없는 라우팅에서 볼 수 있다.
