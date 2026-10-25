# 경로 조각 막대와 앞쪽 말줄임

파일 관리자의 주소 막대는 경로를 조각으로 늘어놓고, 조각마다 아래 단계를 여는 꺾쇠를 두며, 폭이 모자라면 앞쪽 조각을 "…" 메뉴로 접는다. 긴 경로 한 줄은 앞을 줄여 파일 이름을 남긴다. 앱이 이 막대와 앞쪽 말줄임 캐시를 직접 만들지 않도록 `breadcrumb_element`와 `text_elide::start`를 둔다.

공개 형식은 [breadcrumb_element.h](../include/luil/ui/breadcrumb_element.h), [label_element.h](../include/luil/ui/label_element.h)의 `text_elide::start`, [draw_primitives.h](../include/luil/ui/draw_primitives.h)의 `elide_text_start`다.

## 막대

```cpp
luil::breadcrumb_config path {};
path.owner = u8"address";
path.segments = { { u8"c", u8"C:", true }, { u8"users", u8"Users", true }, { u8"docs", u8"Documents", false } };
path.navigate = [](const std::u8string& key) { return luil::make_app_action(go_intent { key }); };
path.open_children = [](const std::u8string& key, const luil::ui_action_context& at) { return luil::make_app_action(children_menu_intent { key, at.x, at.y }); };
path.open_collapsed = [](const std::vector<std::u8string>& keys, const luil::ui_action_context& at) { return luil::make_app_action(collapsed_menu_intent { keys }); };
path.name = u8"경로";
path.current_label = u8"현재 위치";
```

- 조각은 맨 위에서 지금 자리까지이고 마지막이 지금 자리다. 지금 자리는 진한 글, 지나온 조각은 흐린 글이며, 지나온 조각은 누르면 그 키로 간다(`navigate` — 왼쪽 누름·Enter·Space). 지금 자리는 누를 것이 없다.
- `has_children`인 조각 뒤에는 꺾쇠 단추가 서고, 누르면 `open_children`이다. 앱이 그 조각의 아래 단계를 메뉴(popup)로 연다. 앵커가 필요하면 `breadcrumb_chevron_id`로 그 단추를 찾는다. 아래 단계가 없는 조각 사이에는 누를 수 없는 흐린 꺾쇠만 긋는다.
- 가운데 버튼(`middle_click` — 새 탭에서 열기)과 오른쪽 버튼(`context_menu`)은 앱이 주면 받는다.

### 접기

**폭이 모자라면 앞에서부터 접는다.** 앞쪽 조각들을 "…" 단추 하나로 바꾸고, 그 단추가 접힌 조각의 키를 앞에서부터 `open_collapsed`로 낸다. 앱은 그 키로 메뉴를 연다.

- 접을 수는 배치가 정한다(`collapsed_count`) — 들어가는 가장 작은 수다. 같은 설정이라도 폭이 바뀌면 다시 접는다.
- 지금 자리는 끝까지 남는다. 그마저 들어가지 않으면 그 글의 끝을 줄인다.
- 조각 하나를 접어도 "…" 단추가 같은 폭을 차지해 줄지 않으면 그다음 수로 간다. 폭이 줄지 않는 접기는 하지 않는다.

### 키보드와 보조 기술

- 막대는 Tab의 한 자리다(`focus_group` 가로). ←/→가 "…"·조각·꺾쇠 사이를 돈다. 지금 자리도 그 차례에 서서 이름을 읽는다.
- 막대는 이름 있는 `access_role::navigation`이다. UIA에서는 Group에 navigation 랜드마크(`UIA_NavigationLandmarkTypeId`)를 단다 — 화면 읽기 프로그램의 랜드마크 이동으로 닿는다.
- 조각은 링크다. 지금 자리는 이름 뒤에 `current_label`을 붙인다("Documents · 현재 위치" — 앱 화면 규칙의 "접근성 이름에는 상태를 함께 쓴다").
- 꺾쇠의 이름·tooltip은 "조각 · `children_label`", "…" 단추는 `collapsed_label`이다. 글은 앱의 언어로 준다.

## 앞쪽 말줄임

`text_elide::start`(label·표 칸)와 `elide_text_start`는 끝을 남기고 앞을 `…`로 줄인다 — "…ments\report.txt". 경로에서 파일 이름이 가장 중요할 때다. 자르는 자리는 글자소 경계다.

## 검증

- `tests/breadcrumb_tests.cpp`: 폭에 따른 접기 수(줄지 않는 접기를 건너뜀, 지금 자리만 남김), 숨은 조각과 "…" 단추가 내는 접힌 키, 조각·꺾쇠의 메시지, 지금 자리의 이름·누를 것 없음, navigation 역할과 가로 묶음, 앞쪽 말줄임의 끝 보존과 글자소 경계.
- `tests/uia_provider_tests.cpp`: navigation 역할의 Group과 랜드마크.
- 데모 "버튼과 컨트롤" 페이지의 경로 막대(창 폭을 줄이면 앞부터 접힌다).
