# 탭 막대 스타일과 공용 스타일

탭을 많이 여는 데스크톱 앱(브라우저, 편집기, 터미널)이 흔히 기대하는 동작을 [`tab_bar_element`](../include/luil/ui/tab_bar_element.h)의 기본기로 둔다. 모양 셋, 닫기 단추 보이기 정책, 탭 상태 표시, 배지 글리프, 가운데 버튼 닫기, 넘침 전의 줄이기, 한 탭씩 넘기기, 견본 막대다. 항목마다 따로 켜고 끄며 기본값은 처음부터의 동작이다.

이 문서의 앞 절은 모양을 고르는 자리를 정한다. 모양을 config의 enum 하나로만 열면 앱 안의 막대마다 같은 값을 되풀이해 적어야 하고, 한 곳을 빠뜨리면 그 막대만 다른 모양이 된다. 그래서 모양은 **앱 스타일의 컴포넌트 칸**에 둔다.

## 공용 스타일 — 컴포넌트 칸

[`ui_style`](../include/luil/theme/ui_style.h)은 이미 앱의 것인 중립 색·tone·치수를 담는다 ([테마와 글꼴](concepts/theming.md)). 여기에 컴포넌트마다 한 칸을 더한다. 첫 칸이 `ui_style::tab_bar`(`tab_bar_style`)다.

| 값 | 뜻 | 기본 |
| --- | --- | --- |
| `variant` | `underline`·`connected`·`flat` | `underline` |
| `close_visibility` | `always`·`selected_or_hovered` | `always` |
| `indicator_placement` | `icon_corner`·`close_slot` | `icon_corner` |
| `bar_background`·`selected_background` | 이어지는 모양의 막대·선택된 탭 바탕 (`palette_role`) | 창 바탕·표면 |

규칙은 넷이다.

- **값은 enum과 역할뿐이다.** 색을 직접 받지 않고 [`palette_role`](../include/luil/theme/ui_style.h)로 팔레트의 역할을 고른다. 그래서 테마를 바꿔도 고대비가 켜져도 그리기가 그대로 따라오고([색은 역할로 말한다](concepts/theming.md#색은-역할로-말한다)), `ui_style`의 `operator==`와 JSON이 그대로 성립한다. 선택자(`std::function`)는 비교되지 않고 파일에 적히지 않아 이 칸에 둘 수 없다.
- **한 번 적으면 앱의 모든 막대가 따른다.** 값이 오는 길은 스타일과 같다. 빌드 시점에는 `assets/style.json`의 `tabBar` 객체(또는 `LUIL_STYLE`이 가리킨 파일), 실행 시점에는 앱이 세운 `ui_style`이다. 막대의 config에는 모양을 적지 않는다.
- **config는 그 막대 하나만 덮는다.** `tab_bar_config::style`이 있으면 그 막대는 앱 스타일 대신 그 값이다. 환경 설정의 견본처럼 한 막대만 달라야 하는 자리다.
- **배치에서 읽는다.** 컴포넌트는 `arrange_context::style`(비면 내장 스타일)에서 자기 칸을 읽고, 그 값으로 자리를 잡고 그린다. 그리기 문맥에서 읽지 않는 까닭은 모양이 그림만이 아니라 입력 규칙까지 정하기 때문이다 — 닫기 단추를 숨기는 모양은 터치의 탭이 그 단추를 지나가게 한다(아래). 한 tree가 한 스타일로 배치되고 그려져야 둘이 어긋나지 않는다.

실행 시점에 스타일을 바꾸는 앱은 같은 값을 두 자리에 준다. 팔레트는 `ui_frame::style`이, 컴포넌트 칸은 `make_arranged_tree(..., style)`이 받는다. `make_arranged_tree`에 스타일만 주면 치수(`metrics`)도 그 스타일의 것을 쓴다. 손으로 배치하는 조립은 뿌리의 `arrange_context::style`을 채운다 — 자식 문맥(`for_child`)이 잇는다.

```cpp
auto style { std::make_shared<luil::ui_style>(luil::default_ui_style()) };
style->tab_bar.variant = luil::tab_bar_variant::connected;
style->tab_bar.close_visibility = luil::tab_close_visibility::selected_or_hovered;
frame->style = style;
tree = luil::make_arranged_tree(std::move(root), slot, scale, {}, nullptr, style.get());
```

```json
"tabBar": {
    "variant": "connected",
    "closeVisibility": "selectedOrHovered",
    "indicatorPlacement": "closeSlot",
    "barBackground": "captionBackground",
    "selectedBackground": "surfaceBackground"
}
```

`tabBar`는 빠져도 된다 — 앱이 이미 가진 옛 JSON이 그대로 빌드되고, 빠진 키는 struct의 기본값이다. 낯선 키와 모르는 값은 생성에서 막는다.

다음 컴포넌트도 같은 모양으로 늘린다: `ui_style`에 칸 하나, `assets/style.json`과 `cmake/generate_style.cmake`에 같은 이름의 선택 객체 하나, 컴포넌트 config에 그 칸을 통째로 덮는 `std::optional` 하나, 배치에서 `context.app_style()`로 읽기. 칸에 들어가는 것은 앱이 화면 전체에서 통일하고 싶은 **모양의 선택**이다. 탭 폭처럼 막대마다 다른 배치 값은 config에 남는다.

## 모양

| 모양 | 선택된 탭 | 고르지 않은 탭 | 막대 바탕 |
| --- | --- | --- | --- |
| `underline` | 표면 바탕 + 아래 강조선 | hover·눌림 바탕 | 칠하지 않는다 |
| `connected` | 내용 바탕, 위 모서리 둥글고 아래 끝이 바깥으로 휜다 | 바탕·테두리 없음, 탭 사이 짧은 칸막이, hover는 안으로 들인 둥근 판 | `bar_background` |
| `flat` | 내용 바탕 + 위 강조선, 아래로 열린다 | 세로 칸막이로 맞붙은 칸 | `bar_background` |

- **이어지는 두 모양은 막대 바닥선이 선택된 탭 밑에서 끊긴다.** 막대가 바닥선을 먼저 긋고, 선택된 탭이 그 줄 위까지 내용 바탕으로 덮는다.
- **휨은 탭 자리 안에서 끝난다.** 탭은 맞붙어 차례로 그려지므로 자리 밖으로 휘면 다음 탭이 덮는다. 몸을 휨만큼 들이고 휨이 자리 끝에서 끝난다. 휨이 작아(4 논리 픽셀) 글·단추의 자리는 세 모양이 같다 — 모양이 배치를 흔들지 않는다.
- **선택된 탭에 맞닿은 칸막이는 숨긴다.** 칸막이는 탭의 오른쪽 끝에 서므로, 탭은 자기와 바로 뒤 탭이 선택되지 않았을 때만 긋는다. 이웃 사실은 막대가 탭을 지을 때 알려 준다.
- **고대비는 색이 아니라 선으로 읽힌다.** 고대비 팔레트는 창 바탕과 표면이 같다. 두 바탕 역할이 같은 색으로 접히면(판정은 이름이 아니라 값이다) 선택된 탭에 `divider` 역할의 테를 두른다. 고대비에서 그 역할은 온전한 전경색이라 테와 끊긴 바닥선, 강조선(`accent` = hotlight)이 선택을 말한다. 앱 스타일이 두 역할을 같은 색으로 골라도 같은 규칙이 선다.
- **끌기 표시.** `underline`은 처음부터 끌리는 탭의 바탕만 흐렸다. 이어지는 모양은 바탕이 없는 탭이 있어 바탕만 흐리면 끌리는 탭이 보이지 않으므로 탭 전체를 흐린다. 놓을 자리의 세로 막대(`move`·`receive`)는 탭 자리의 끝에 서서 세 모양 모두 탭 사이 틈에 놓인다. 틈이 아닌 막대(`reorder`만)의 놓을 자리 채움은 모양마다 그 모양의 hover 자리(이어진 탭은 들인 둥근 판)에 깐다.

## 닫기 단추 보이기

`selected_or_hovered`는 선택된 탭과 포인터가 올라간 탭(탭 위이거나 단추 위)에서만 단추를 그린다. 단추에 초점이 가거나 눌린 동안도 그린다.

- **자리는 지킨다.** 숨어도 이름의 오른쪽 여백(28 논리 픽셀)은 그대로라 hover로 이름이 들썩이지 않는다.
- **element는 그대로다.** 단추는 tree에 남아 키보드·보조 기술·우클릭·가운데 버튼이 지금처럼 닿는다. 그리지 않을 뿐이다.
- **터치는 숨은 단추를 누르지 않는다.** 마우스·펜은 누르기 전에 늘 올라가므로 보이는 단추를 누른다. 터치에는 올림이 없어 손가락이 보이지 않는 단추를 누르게 된다. 숨을 수 있는 단추는 [`ui_element::hover_only`](../include/luil/ui/ui_element.h)가 참이고, controller는 터치의 탭이 그런 부품에 떨어지면 그것을 감싼 가장 가까운 누를 수 있는 조상(탭)에 준다 — 탭은 고르기가 된다. 늘 보이는 단추는 처음처럼 터치로도 닫는다.

## 상태 표시

배지와 별개로 탭 하나에 [`tab_indicator`](../include/luil/ui/tab_bar_element.h) 하나를 단다. 종류는 채운 점·속 빈 고리·진행 중(도는 호)·앱의 글리프이고, tone은 배지와 같은 넷이다. 색만으로 뜻을 말하지 않도록 모양이 먼저이고 tone은 그 위의 색이다.

- **아이콘 모서리**: 아이콘 오른쪽 아래에 작은 표시를 찍고, 탭 바탕색 테를 둘러 아이콘과 떨어져 보이게 한다. hover 바탕은 반투명이라 테 위에 같은 겹을 한 번 더 얹는다. 진행 중이면 아이콘을 감싸는 호다. 아이콘이 없는 탭은 표시가 아이콘 칸에 선다.
- **닫기 단추 자리**: 평소에는 단추 자리에 표시가 서고, 그 단추 위에 포인터가 오거나 초점이 가면 닫기 단추로 바뀐다. 닫을 수 없는 탭은 표시만 서고, 그래도 단추 자리만큼 이름을 비운다. 이 자리에서는 단추가 표시 밑에 숨으므로 숨은 단추의 터치 규칙이 같이 선다.
- **보조 기술**: `description`이 배지의 말 뒤에 붙어 탭 이름과 함께 읽힌다 ("보고서, 읽지 않음 3개, 불러오는 중"). 비면 읽을 말이 없다 — 표시의 뜻은 앱만 안다.
- **진행 애니메이션**: 도는 표시가 있는 탭만 `next_update`로 다음 frame을 청한다. 시스템의 움직임 줄이기가 켜지면(`update_context::reduce_motion`·`draw_context::reduce_motion`, Win32는 `SPI_GETCLIENTAREAANIMATION`) 멈춘 긴 호를 그리고 frame을 청하지 않는다. 짧은 호가 멈춰 있으면 "멈췄다"로 읽히므로 멈춘 그림은 거의 닫힌 호다.

## 배지 글리프

neutral·accent 배지는 `tab_badge::glyph`로 글 앞에 앱의 글리프를 붙인다. warning·error는 지금처럼 심각도 글리프가 정한다 — 앱 글리프는 무시된다. 글리프가 진행 중을 뜻하면 `spinning`으로 돌리고, 규칙(필요할 때만 frame, 움직임 줄이기면 멈춤)은 상태 표시와 같다. 그림은 `badge_element`와 같은 알약 그리기(`src/ui/badge_paint.h`)를 쓴다.

## 가운데 버튼 클릭

[`ui_trigger::middle_click`](../include/luil/ui/ui_element.h)을 더한다. 같은 element 위에서 누르고 뗐을 때만 서고, 누른 채 끌기 문턱(`drag_start_distance`)을 넘으면 취소된다. 가운데 버튼은 선택·초점·끌기·연타 판정을 바꾸지 않으므로 controller는 그 누름을 왼쪽·오른쪽 누름과 다른 칸(`middle_press_`)에 적는다. 터치에는 가운데 버튼이 없다.

탭 막대는 `close` factory가 있고 닫을 수 있는 탭에 가운데 클릭을 단다. 닫기 메시지만 내고 선택은 바꾸지 않는다. 닫기 단추 위의 가운데 클릭도 그 탭을 닫는다. `tab_bar_config::middle_click_close`로 끈다. 지금까지 가운데 클릭은 아무 일도 하지 않았으므로 켠 기본값이 기존 동작을 바꾸지 않는다.

## 넘침 (1) 먼저 줄이기

`min_tab_width`가 `tab_width`보다 작으면, 자리가 모자랄 때 모든 탭을 같은 폭으로 `tab_width`에서 `min_tab_width`까지 줄이고 그래도 넘칠 때만 넘침이 시작된다 ([`fitted_tab_width`](../include/luil/ui/tab_bar_element.h)). 줄어든 폭에서는 배지와 상태 표시가 먼저 자리를 갖고 이름이 말줄임된다. 지금 실제 폭은 `tab_bar_element::tab_width()`가 답한다.

탭 폭이 자리에 달리므로 배치에서 정한다. 그래서 레인은 고정 길이 `stack_element`가 아니라 막대가 폭을 알려 주는 레인이고, 띠의 내용 폭도 배치 직전에 알린다(`strip_element::set_content_width`). 측정 단계가 없다는 계약은 그대로다 — 폭을 정하는 것은 여전히 담는 쪽이고 정하는 때만 배치로 늦춘다.

**포인터가 막대 위에 있는 동안 넓히지 않기(권장)는 하지 않았다.** 연달아 닫는 동안 폭을 붙잡으려면 "포인터가 막대를 떠났다"를 앱이나 막대가 알아야 하는데, 불변 tree에는 frame을 넘는 hover 상태가 없고 controller에는 hover가 가지를 떠나는 계기를 메시지로 내는 길이 없다. 폭을 붙잡는 상태를 element에 몰래 두면 tree가 새로 설 때마다 사라진다. 이 동작은 hover 이탈 계기(`ui_trigger`와 같은 자리의 떠남 액션)가 생긴 뒤에 앱 상태(붙잡은 폭)로 연다.

## 넘침 (2) 한 탭씩 넘기기

`overflow_mode = step`이고 `scroll` factory가 있으면 넘칠 때 양 끝에 앞·뒤 단추가 선다. 배치는 `[앞] 탭들 [뒤] [넘침 메뉴] [꼬리]`다.

- 누르면 다음 탭 경계까지 흘린다 ([`tab_step_offset`](../include/luil/ui/tab_bar_element.h)). 앞으로는 오른쪽 끝에 걸친 탭이, 뒤로는 왼쪽 끝에 걸친 탭이 온전히 드러나는 자리다. 끝에 닿은 쪽 단추는 비활성이다.
- 단추는 Tab 자리가 아니다. 키보드는 ←/→로 탭을 옮기고 초점 되살리기가 띠를 흘린다. 이름은 `step_back_tooltip`·`step_forward_tooltip`이고, 비면 넘침 버튼처럼 접근 tree에서 빠진다. 자동화 id가 kind의 수라 새 kind(`tab_step_back`·`tab_step_forward`)는 enum 끝에 더했다.
- **휠**: `wheel_steps`를 켜면 세로·가로 휠 한 칸에 탭 하나다. 칸을 세는 것은 [`scroll_source::step`](../include/luil/ui/ui_element.h)이다 — 휠은 이 메시지를 부르고, 터치 끌기와 초점 되살리기는 지금처럼 `scroll`로 양만큼 흘린다. 터치패드처럼 잘게 오는 휠은 controller가 쌓아 두었다가 한 칸(120)이 차면 낸다. tree는 frame마다 새로 서므로 쌓는 자리는 element가 아니라 controller이고, 창이 바뀌거나 방향이 바뀌면 버린다. 칸을 경계의 변화량으로 바꾸는 것은 실제 폭과 띠 폭을 아는 막대다. 칸 휠은 앱 정책(`on_wheel`)보다 먼저 받는다.
- **가장자리 그림자**: 막대와 띠는 처음부터 가장자리 그림자를 그리지 않는다. 앞·뒤 단추가 넘침을 알린다.
- **꼬리**: `trailing`(factory)과 `trailing_width`로 막대 끝에 따라붙는 element(새 탭 단추)를 둔다. 탭이 적으면 마지막 탭 바로 뒤에 서고, 꼬리까지 들어가지 않으면 넘침이고 꼬리는 오른쪽 끝에 고정된다. 꼬리는 막대의 초점 묶음(←/→) 안에 선다.

## 견본 막대

`tab_bar_config::preview`가 있으면 그림만 남는다. 동작·끌기·놓기·닫기 단추 element·넘침 단추·꼬리를 하나도 짓지 않으므로 누름·hover·초점·접근 tree에서 모두 빠진다. 탭은 구조(`access_role::none`)로 접히고 막대가 `image` 역할로 앱이 붙인 이름 하나로 선다(이름이 비면 그것도 접힌다).

견본은 포인터 없이도 상태를 보여야 하므로 `hovered`·`pressed`·`focused`·`close_hovered`에 탭 키를 적어 그 상태로 고정해 그린다. 초점 테는 tree가 아니라 탭이 그린다 — tree는 초점이 실제로 선 element에만 테를 얹는다. 견본은 막대가 잘라 내므로 테를 탭 안쪽으로 접는다.

## 검증

- 래스터: 모양 셋 × 밝음·어두움·고대비에서 선택 바탕, 끊긴 바닥선, 칸막이, 강조선, 고대비의 테 ([`tab_bar_style_tests.cpp`](../tests/tab_bar_style_tests.cpp)). 고대비 test는 두 바탕이 같은 색인데도 선택된 탭의 테 픽셀이 고르지 않은 탭보다 많음을 본다. 역할로 고른 바탕, 닫기 자리 표시와 단추의 바뀜, 아이콘 모서리 표시, 견본의 고정 hover, 끌리는 이어진 탭의 흐림.
- 입력: 가운데 클릭(같은 탭에서 떼기, 닫기 단추 위, 끌기 문턱 취소, 다른 탭에서 떼기, 끄기, 닫을 수 없는 탭), 칸 휠(한 칸, 잘게 오는 휠 쌓기, 가로 휠), 앞·뒤 단추의 자리·비활성·변화량, 숨은 닫기 단추 위의 터치 탭.
- 배치: 줄이기와 넘침 판정, 꼬리의 자리, 앱 스타일이 배치 문맥으로 닿는 것과 config의 덮기.
- 데모의 탭 페이지가 모양·닫기 단추·상태 표시·넘침 방식·탭 폭을 고르고 세 모양의 견본을 보인다. 본 막대와 분리된 창의 막대는 config에 모양을 적지 않고 페이지의 앱 스타일을 배치 문맥으로 받는다.
