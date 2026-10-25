# 자석 슬라이더

`slider_element`를 끌 때 앱이 정한 값 근처에서 손잡이가 그 값에 붙게 한다. 볼륨의 100%, 좌우 균형의 가운데, 재생 위치의 장 경계처럼 "정확히 그 값"에 놓고 싶은 자리가 있는 막대가 대상이다. Ctrl을 누른 채 끌면 자석 없이 놓는다.

공개 형식은 [slider_element.h](../include/luil/ui/slider_element.h)에 있다.

## 왜 진행률 막대가 아니라 slider인가

끌어서 값을 바꾸는 막대는 `slider_element`다. `progress_element`는 표시뿐이라 액션도 커서도 갖지 않는다는 계약이 있고([progress_element.h](../include/luil/ui/progress_element.h)), 둘을 합치면 "누를 수 있는 진행률"이라는 조합이 생긴다. 재생 위치처럼 진행률처럼 보이면서 끌 수 있는 막대도 slider로 짓는다.

## 참고한 사례

| 사례 | 하는 일 | 가져온 것 |
| --- | --- | --- |
| Blender 변형 | 끄는 동안 Ctrl을 누르면 증분 스냅이 켜지고 꺼진다. Shift는 1/10 속도의 정밀 조정이다. 떼면 원래대로 돌아간다. | 끄는 도중 수식 키로 자석을 바꾼다. |
| Figma | Ctrl을 누르는 동안 모든 스냅을 끈다 (공식 도움말이 아니라 포럼의 출시 공지 기준). | Ctrl = 자석 끔. |
| Final Cut Pro | N을 누르는 동안 스냅을 반전한다. 끄는 도중에도 바꿀 수 있다. | 누르는 동안만 바뀌고 떼면 돌아온다. |
| JUCE·Qt 포럼의 detent | 창 크기에 따라 0 dB나 100%에 픽셀이 딱 떨어지지 않는 문제를 "지점 + 폭"으로 푼다. | 지점은 값 단위로 받는다. |
| VCV Rack 노브의 detent | 들어오는 영역과 나가는 영역을 따로 두어 붙을 때와 풀릴 때의 거리가 다르다. 양옆 값도 정밀하게 다루면 닿을 수 있다. | 붙는 거리와 풀리는 거리를 나눈다. |

- [Blender — Precision](https://docs.blender.org/manual/ka/4.5/scene_layout/object/editing/transform/control/precision.html)
- [Figma 포럼 — Hold Ctrl to temporarily disable all snapping](https://forum.figma.com/t/temporarily-disable-snapping/3358/11)
- [Final Cut Pro 사용 설명서 — 스냅](https://support.apple.com/en-sa/guide/final-cut-pro/ver9f7888dc3/mac)
- [JUCE 포럼 — Adding a detent location to a slider](https://forum.juce.com/t/adding-a-detent-location-to-a-slider/6954)
- [VCV Rack 포럼 — Creating knob detents](https://community.vcvrack.com/t/creating-knob-detents/19287)

## 값

| 항목 | 값 | 이유 |
| --- | --- | --- |
| 지점 | `slider_config::magnets` — 값 단위 목록, 기본은 비어 있음 | 지점은 앱이 안다. 값 단위라 막대 폭이 바뀌어도 같은 값이다. 범위 밖·겹친 값은 버리고 정렬한다. |
| 붙는 거리 | `slider_magnet_stick_distance` 6 논리 px | 손잡이 반지름이다. 포인터가 이 안이면 손잡이가 표식을 덮고 지점에 머문다. |
| 풀리는 거리 | `slider_magnet_release_distance` 12 논리 px | 손잡이 지름이다. 6~12px 사이에서는 손잡이가 포인터를 따라잡고, 12px 밖에서는 포인터와 같다. 그래서 풀릴 때 튀지 않는다. |
| 이웃 지점 | 두 거리 모두 이웃 지점까지 간격의 절반을 넘지 않는다 | 가까운 두 지점의 영역이 겹쳐 서로 당기지 않는다. |
| 자석 끄기 | Ctrl을 누른 채 끌기. 끄는 도중 눌러도, 떼도 된다 | Figma·Blender·Final Cut Pro와 같은 "누르는 동안" 규칙이다. |
| 키보드·보조 기술 | 자석 없음 | 방향키(1%·10%·Home·End)와 UIA 값 정하기는 정확한 값을 고르는 길이라 손의 부정확함을 보정할 이유가 없다. |

거리는 논리 픽셀이고 배치 배율을 곱한다. 손에 닿는 느낌은 포인터가 움직인 거리라 값 단위가 아니라 픽셀로 잰다. 거리를 앱이 바꾸게 하지 않는다. 키 걸음(`slider_key_step_ratio`)을 상수로 둔 것과 같은 이유다 — 손에 닿는 느낌이 앱마다 갈리지 않게 한다.

## 그리기

- 지점마다 트랙 위에 2 × 10 논리 px 표식을 세운다. 손잡이 중심이 도는 자리(양 끝 손잡이 반지름 안쪽)에 놓는다.
- 표식 색은 빈 쪽에서 `control_border`, 채운 쪽에서는 `surface_background`로 판 홈이다. 채운 `accent` 위에 전경 알파를 얹으면 light 테마에서 대비가 무너진다. 비활성은 표식도 `disabled_foreground`다.
- 끄는 동안 지점에 붙었으면 손잡이 뒤에 지름 22 논리 px의 `soft_button_background` 고리를 둔다. 색만이 아니라 모양이 바뀌어 "붙었다"를 알린다. 끌지 않을 때는 그리지 않는다 — 키로 지점 값에 닿았을 때까지 고리가 서면 "붙었다"와 "그 값이다"가 섞인다.
- 고대비에서 `soft_button_background`는 highlight 색의 불투명 채움으로 접힌다. 손잡이(`accent` = hotlight)보다 큰 원이 그 뒤에 서므로 고리가 그대로 보인다.

## 어떻게 붙이나 — 상태 없는 사상

손잡이 자리를 **포인터 자리의 함수**로 정하고, 끌기 메시지는 그 함수의 앞·뒤 차이로 낸다.

- 포인터 x를 트랙 안 거리 `p`로 바꾸고(양 끝에서 자른다), 지점마다 `d = p − 지점`을 본다.
  - `|d| ≤ 붙는 거리` → 손잡이는 지점.
  - `붙는 거리 < |d| < 풀리는 거리` → 손잡이는 `지점 + sign(d) · (|d| − 붙는) · 풀리는 / (풀리는 − 붙는)`. 양 끝에서 앞뒤 구간과 이어진다.
  - 그 밖 → 손잡이는 `p`.
  - Ctrl이 눌렸으면 언제나 `p`.
- `on_move(previous, current)`가 내는 델타는 `F(current) − F(previous)`다. 각 문맥의 Ctrl은 그 문맥의 것을 쓴다. 상호작용 층이 앞 문맥의 수식 키를 `pointer_drag_modifiers_`로 들고 있고, 마우스·펜·터치 경로가 모두 그렇다.
- 델타를 이어 더하면 가운데 항이 지워져 **손잡이 = F(지금 포인터)**가 언제나 성립한다. 끄는 동안의 상태를 element도 앱도 들지 않는다.
- 끌기 중에는 지금 값을 읽지 않으므로 다음 frame 전에 이동이 여럿 와도 겹쳐 쌓이지 않는다. 누름(`on_press`)만 지금 값을 읽어 손잡이를 `F(누른 자리)`로 옮긴다.
- 지점에 붙는 순간은 `change_to`가 있으면 그것으로 지점 값을 그대로 보낸다. 델타를 이어 더한 값은 부동소수 오차로 지점에서 조금 비켜 서기 때문이다. 절대 메시지는 같은 것이 몇 번 겹쳐도 지점 그대로이고, 이어지는 델타가 지점에서 다시 출발하므로 위 성질도 어긋나지 않는다. `change_to`가 없으면 델타로 보낸다.
- 지점을 주지 않은 slider도 같은 길을 타고, F는 `p` 그대로다.

### 바뀐 기존 동작

- **범위 밖으로 끌었다 돌아올 때.** 예전에는 넘친 만큼을 앱이 자르므로, 돌아오는 순간 손잡이가 바로 움직여 포인터보다 앞섰다. 이제는 포인터가 트랙에 돌아올 때까지 손잡이가 끝에 머문다(Windows 트랙바·브라우저 range와 같다). 지점이 없는 slider도 그렇다. 지점 유무로 같은 컨트롤의 끌기 느낌이 갈리지 않게 했다.
- 누른 자리도 트랙 안으로 자른다. 손잡이 반지름 안쪽의 여백을 눌러도 값이 범위를 넘는 델타가 나가지 않는다.
- 앱이 값을 눈금으로 반올림해 되돌려 쓰면 델타 합이 어긋난다. 예전에도 같은 위험이었다. 지점 값은 앱의 눈금 위에 둔다.

## 넣지 않은 것

- **탈출 속도.** 빠르게 끌면 자석이 줄어드는 길은 넣지 않았다. 참고 사례가 모두 거리로 다루고, 그러려면 모든 끌기 대상의 문맥(`ui_action_context`)에 속도를 실어야 한다. 마우스 경로는 이미 이동마다 `pointer_drag_motion_`에 시간과 자리를 쌓아 `on_release`의 `drag_velocity`를 내므로, 필요해지면 그 값을 이동 때도 싣는 것이 출발점이다.
- **Shift 정밀 조정(1/10 속도).** 손잡이를 포인터에서 떼어 놓으므로 "손잡이 = F(포인터)"가 깨지고, 누른 자리를 기억하는 끌기 상태가 필요하다. 미세 조정은 Ctrl(자석 끔)과 방향키 1%로 한다.
- **Page Up·Down이 다음 지점에서 멈추기.** 지점이 없는 막대와 Page의 뜻이 갈린다.
- **터치에서 더 넓은 거리.** 끌기 문맥에 입력 종류가 없다. 터치는 마우스와 같은 거리이고, 터치에는 Ctrl이 없으므로 지점 근처의 값은 방향키·보조 기술로 놓는다.
- **UIA에 지점 알리기.** RangeValue에 그 어휘가 없다.

## 검증

- `tests/slider_element_tests.cpp`의 `[magnet]` test가 붙는 거리 안·밖, 따라잡는 구간의 연속, 배율, 이웃 간격으로 자르기, 범위 밖 지점 버리기, 끄는 도중 Ctrl을 누르고 뗄 때의 델타, 절대 메시지로 붙기, 키에 자석이 없음을 고정한다. 범위 밖에서 돌아오기는 `A slider keeps the knob at the end until the pointer returns`가 본다.
- `tests/raster_draw_tests.cpp`의 `Slider magnets paint a mark and a ring while the knob holds on`이 표식 자리·채운 쪽 홈 색·누르는 동안의 고리를 픽셀로 고정한다.
- 데모의 "버튼과 컨트롤" 페이지에 볼륨(100%)·좌우 균형(가운데)·재생 위치(장 경계) 막대가 있다.
