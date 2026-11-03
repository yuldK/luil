# 분할 버튼

기본 동작 단추와 펼침 메뉴를 한 버튼에 담는다. 본체를 누르면 그 동작이 나가고, 오른쪽 화살표를 누르면 메뉴가 열린다. 본체 동작이 없으면 버튼 전체가 메뉴를 여는 드롭다운 단추가 된다. 공개 형식은 [split_button_element.h](../include/luil/ui/split_button_element.h)의 `split_button_config`·`split_button_element`다.

이 노드는 닫힌 필드 하나가 앵커 popup 하나를 여닫는 규약도 처음 세운다. 공통 부분은 [field_popup.h](../include/luil/ui/field_popup.h)의 `place_field_popup`·`field_popup_key_handler`로 올리고, 규약은 "필드 popup 열림 규약" 절에 모았다. 날짜 칸, 색 고르개가 이것을 따르고 메뉴 막대는 여는 키와 항목 사이 이동만 달리 정한다.

## 문제

앱이 분할 버튼을 만들려면 글자 버튼과 화살표 버튼을 나란히 놓고 직접 이어야 한다.

- `text_button_element`는 자기 상자 전체를 둥글게 칠한다([dialog_elements.cpp](../src/ui/dialog_elements.cpp)). 두 버튼이 둥근 상자 하나로 보이게 하는 모서리와 구분선은 앱이 그린다.
- 누를 수 있으면 Tab 자리다(`ui_element::tab_stop`). 한 버튼이 Tab을 두 번 먹는다.
- 접근성이 막힌다. `invokable()`([uia_provider.cpp](../src/win32/uia_provider.cpp))은 `expanded`를 가진 요소의 Invoke를 내지 않는다. 한 노드에 Invoke와 ExpandCollapse를 함께 달 수 없다.
- 여닫는 규약이 필드마다 다르다. `dropdown_element`는 `set_open` 절대 메시지를 갖지만 키 처리기가 없다. `set_key_handler`를 쓰는 곳은 콤보·표·가상 목록뿐이고 Alt+↓와 F4를 받는 곳은 없다.
- 메뉴 자리는 앱이 계산한다. 데모는 build 때 필드 포인터를 남기고 `bounds()`를 배율로 나눠 쓴다([popups_page.cpp](../examples/demo/popups_page.cpp)). 두 host는 아래 공간이 모자라면 popup을 밀기만 한다(Win32 `popup_screen_position`, Android `update_overlays`). 위로 뒤집지 않아 메뉴가 필드를 덮는다.

## 공개 형식

```cpp
// include/luil/ui/split_button_element.h
inline constexpr float split_button_arrow_width { 28.0f };

struct split_button_config
{
    // 세 부분의 owner다.
    std::u8string owner {};
    // 본체의 글·아이콘·모양·기본 버튼·색 선택자다. `selected`는 쓰지 않고 `link`는 normal로 읽는다.
    text_button_config look {};
    // 본체를 누르면 낼 액션이다. 비어 있으면 본체가 없고 버튼 전체가 메뉴를 연다.
    ui_action invoke {};
    // 메뉴가 떠 있는지는 앱 상태다.
    bool open { false };
    // 메뉴를 이 상태로 하라는 절대 메시지다. 없으면 화살표가 눌리지 않고 보조 기술이 열지 못한다.
    std::function<input_action(bool open)> set_open {};
    bool enabled { true };
    // 거짓이면 본체만 막는다. 메뉴는 그대로 열린다.
    bool invoke_enabled { true };
    // 본체의 툴팁이다. 본체 글이 없으면 접근 이름의 대신도 된다. 본체가 없으면 버튼 전체의 툴팁이다.
    std::u8string tooltip {};
    // 화살표의 접근 이름이다. 이름이 비면 본체 글 뒤에 기본 문구(" 옵션")를 붙인다.
    std::u8string arrow_name {};
    // 화살표의 툴팁이다. 본체가 없을 때는 비어 있으면 `tooltip`을 쓴다.
    std::u8string arrow_tooltip {};
    float arrow_width { split_button_arrow_width };
};

class split_button_element final : public ui_element
{
public:
    explicit split_button_element(split_button_config config);
    // arrange·draw는 두 절반의 자리와 한 상자 그리기를 맡는다.
    [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;
};
```

```cpp
// include/luil/ui/field_popup.h
enum class field_popup_alignment { start, end };

struct field_popup_anchor
{
    rect_f field {};  // 필드의 자리. element의 bounds()와 같은 좌표(물리 픽셀)
    rect_f limit {};  // popup이 들어갈 영역. 보통 앵커 표면의 client 사각형
    float gap { 2.0f };  // 논리 픽셀
    field_popup_alignment alignment { field_popup_alignment::start };
};

struct field_popup_position
{
    float x { 0.0f };  // ui_popup::x·y에 그대로 넣는 논리 픽셀
    float y { 0.0f };
    bool above { false };
};

[[nodiscard]] field_popup_position place_field_popup(const field_popup_anchor& anchor, size_f size, float scale) noexcept;  // size: 논리 픽셀(make_menu_popup이 잰 폭·높이)
[[nodiscard]] key_handler field_popup_key_handler(bool open, std::function<input_action(bool open)> set_open, bool down_opens);
```

`menu_popup_config`([menu_popup.h](../include/luil/app/menu_popup.h))에는 `std::optional<field_popup_anchor> below`를 더한다. 값이 있으면 `make_menu_popup`이 메뉴 크기를 잰 뒤 `x`·`y`를 정한다. 폭을 스스로 재는 곳이 거기라 앱이 같은 식을 되풀이하지 않는다.

```cpp
// surface는 앵커 표면의 client 사각형(물리 픽셀)이다.
luil::menu_popup_config popup {};
popup.id = u8"save-menu";
popup.scale = scale;
popup.below = luil::field_popup_anchor { .field = split_->bounds(), .limit = surface };
popup.dismiss = [](const luil::popup_dismiss_reason) { return luil::make_app_action(save_menu_intent { false }); };
menu.after_select = luil::make_message_action(save_menu_intent { false });
popups.push_back(luil::make_menu_popup(std::move(menu), std::move(popup)));
```

## 규칙

**구성.** root(`split_button`)는 구조일 뿐이고 자식이 둘이다. 본체(`split_button_body`)와 화살표(`split_button_arrow`)다. owner는 셋 다 `config.owner`이고 kind가 달라 id가 겹치지 않는다. `invoke`가 없으면 화살표 하나만 서고 글도 그린다. `look.default_button`이면 본체가 Enter의 기본 대상이다. kind 셋은 `ui_element_id.h`의 `end_of_library_kinds` 바로 위에 더한다.

**자리와 그리기.**

- 폭과 높이는 슬롯이다. `preferred_size`는 본체 내용(`text_button`과 같은 식)에 `arrow_width`를 더하고 높이는 `text_button_height`다. 치수는 논리 픽셀이고 그릴 때 배율을 곱한다. 좁으면 본체 글을 말줄임하고 화살표 폭은 줄이지 않는다.
- 상자는 `control_corner_radius`의 둥근 사각형 하나다. 두 절반은 그 모양으로 자른 사각형이라 바깥 모서리만 둥글고 안쪽 가장자리는 곧다. 구분선은 화살표 왼쪽 가장자리의 1 논리 픽셀 세로선이다. 화살표 자리 안에 있어 눌리지 않는 틈이 없다.
- 채움 종류는 `text_button_fill_for(look, enabled)`가 정한다. 그 종류를 색으로 옮기는 판정은 `dialog_elements.cpp`의 draw에서 `text_button_background(fill, state, palette)`로 꺼내 두 곳이 쓴다. 글 색(`text_button_label_color`)과 아이콘·글을 한 덩어리로 놓는 그림(`draw_text_button_label`)도 같은 이유로 함께 꺼냈다. 셋은 비공개 헤더 `src/ui/text_button_paint.h`에 선언하고 동작은 그대로다(기존 raster test가 지킨다). 두 절반이 hover·눌림을 따로 가지고, `look.fill`·`label_color`는 둘에 같이 걸린다.
- 채움 종류는 상자 하나에 하나다. Enter의 대상인 본체가 꺼졌으면(`invoke_enabled`가 거짓) `default_button`이어도 어느 절반도 강조색으로 채우지 않는다. 본체가 없으면 `default_button`은 읽지 않는다.
- 상자는 둥근 사각형 하나를 각 절반의 사각형으로 잘라 칠한다. 안티에일리어싱 없는 사각형 자르기라 이웃한 절반 사이에 틈도 겹침도 없다. 바탕과 구분선은 root가, 글과 chevron은 각 절반이 그린다.
- 열린 동안 화살표는 눌림 바탕이고 chevron이 위를 가리킨다(`dropdown_element`와 같다). 포인터가 본체 위여도 둘은 따로 선다.
- 구분선과 chevron은 팔레트 역할색만 쓴다. 고대비가 따라온다. 움직임이 없어 움직임 줄이기와 무관하다.
- 초점 테는 초점이 선 절반을 두른다(`ui_tree::draw_focus_ring`). 안쪽 가장자리에도 둥근 테가 선다.

**초점과 Tab.** root에 `set_focus_group(focus_axis::horizontal)`와 `set_focus_entry(본체)`를 단다. Tab에서 한 자리이고 본체가 꺼졌으면 화살표가 자리다([focus-group-design.md](focus-group-design.md)). 비활성은 부모에서 이어지지 않으므로(`ui_element::enabled`) 두 절반에 `enabled`를 각각 건다.

**키.** 처리기는 두 절반에 각각 단다. 초점 element부터 root로 묻는 규칙이라([key-propagation-design.md](key-propagation-design.md)) 절반마다 ↓의 뜻을 달리 줄 수 있다.

| 키 | 초점 | 하는 일 |
| --- | --- | --- |
| Enter·Space | 본체 | `invoke`. 실행 키의 클릭 경로라 처리기가 필요 없다 |
| Enter·Space | 화살표 | `set_open(!open)`. 같은 경로다 |
| ↓ | 화살표 | `set_open(true)` |
| Alt+↓, F4 | 어느 쪽이든 | `set_open(true)` |
| ←·→ | 어느 쪽이든 | 다른 절반으로 옮기고 끝에서 돈다 |
| Home·End | 어느 쪽이든 | 본체·화살표로 간다 |

- 본체의 ↓는 앱으로 흐른다. 목록 안 버튼의 ↓까지 가로채지 않는다.
- Alt 조합은 값 단계·묶음 단계가 앱 단축키로 흘려보내므로 처리기 단계에서 받는다. Ctrl·Shift·Meta가 함께면 받지 않는다. Alt+F4는 시스템 몫이라 오지 않는다.
- 드롭다운 단추(본체 없음)는 한 절반이 Enter·Space·↓·Alt+↓·F4를 모두 받는다.
- 화살표에 초점이 있을 때의 Tab도 다음 자리로 간다. 처음에는 `move_focus`(`ui_interaction.cpp`)가 묶음의 대표가 아닌 구성원을 순서에서 찾지 못해 Tab 순서의 처음으로 갔다. `choice_group_element`의 선택하지 않은 칸에서도 같았다. R3에서 고쳤다. `move_focus`가 `ui_tree::focus_stop_of`로 그 자리를 접은 묶음의 대표에서 센다([menu-focus-fixes-design.md](menu-focus-fixes-design.md)의 X-3).

**접근성.** root는 `none`이라 접근 tree에서 접히고(`collect_access_children`) 두 절반이 형제로 선다.

- 본체: `button`, 이름은 `look.text`(글이 없으면 접근 이름), Invoke.
- 화살표: `button`, 이름은 `arrow_name`, `expanded = open`. `expanded`가 있어 Invoke는 빠지고 ExpandCollapse만 서며, `expand`·`collapse`는 `set_open(목표)`로 간다. 드롭다운 단추는 한 노드의 `button`에 `expanded`가 붙고 이름은 `look.text`다.
- `expanded` 변화는 기존 snapshot 비교가 이벤트로 알린다(`uia_provider.cpp`의 `before.expanded != after.expanded`).

## 필드 popup 열림 규약

닫힌 필드가 앵커 popup을 여는 요소(분할 버튼, `dropdown_element`, 날짜 칸, 색 고르개, 메뉴 막대)가 따른다. 새로 세우는 것은 열기 키 처리기와 popup 자리 helper다. 나머지는 `dropdown_element`와 `combo_box_element`에 이미 있는 것을 글로 모았다.

**상태와 메시지.**

1. 열림은 앱 상태다. 필드는 `open`을 받아 그리고 `expanded`로 알린다. popup은 `ui_frame::popups`에 실린 동안만 있다.
2. 여닫는 메시지는 절대 상태 하나다. `std::function<input_action(bool open)> set_open`이고 토글 메시지는 따로 두지 않는다. 클릭은 build 때의 `open` 반대를 담아 이 factory를 부르고, 접근성 expand·collapse는 목표를 담아 같은 factory를 부른다(`dropdown_element` 생성자와 `access_actions`). 같은 명령이 거듭 와도 결과가 같다.
3. 닫는 계기는 모두 `set_open(false)`다.

| 계기 | 오는 길 |
| --- | --- |
| 바깥 누름·휠·Esc·창 이동·크기 변경·활성 전환 | popup의 `dismiss` factory. 메뉴는 어느 계기에도 닫는다. 글을 치는 popup만 이동·활성 전환에 남긴다(데모 검색 드롭다운) |
| 항목 선택 | `menu_config::after_select` |
| 필드를 다시 누름 | 클릭이 누를 때의 `set_open(!open)`을 낸다(열려 있었으면 닫는다) |
| 보조 기술 | `collapse` |
| 키보드 초점이 필드를 떠남 | 열린 동안에만 root에 `set_focus_leave_action`을 단다(콤보와 같다) |

- Esc는 필드가 처리하지 않는다. 열린 popup이 Esc를 먼저 가져간다(`window_surface.cpp`의 `escape_key` dismiss).
- 앱 정책이 `menu()`를 돌려주면 메뉴가 열린 동안 Tab을 포함한 모든 키를 메뉴가 가져간다(`process_key`). 메뉴 키 경로(`process_menu_key`)가 먼저 받고 갖지 않은 키는 삼킨다. 최상위 메뉴의 ←/→는 하위 메뉴를 여닫을 때만 거기서 쓰이므로, 메뉴 막대의 이웃 메뉴 이동은 항목의 키 처리기가 아니라 이 경로에 넣어야 닿는다. 돌려주지 않는 앱에서는 Tab이 popup을 둔 채 초점을 옮기므로 초점 떠남 훅이 닫는다.

**열기 키.** `field_popup_key_handler(open, set_open, down_opens)`가 만든다. 필드의 키 처리기는 닫혀 있을 때만 일한다.

- Alt+↓는 Alt만 눌렸을 때, F4는 수식 키 없이 `set_open(true)`를 낸다. `down_opens`가 참이면 수식 키 없는 ↓도 연다. 화살표 단추처럼 ↓가 달리 쓰이지 않는 자리에 준다.
- 이미 열려 있으면 그 키들을 빈 목록으로 삼켜 앱 단축키로 새지 않게 한다. 그 밖의 키는 `std::nullopt`다.
- 메뉴가 열려도 키보드 초점은 필드에 남는다. 메뉴 항목은 초점을 받지 않고(`set_tab_stop(false)`) 강조는 따로 선다. 닫힌 뒤에도 초점은 그 자리다. 메뉴가 아닌 popup(달력·색 고르개)의 키 경로는 그 노드가 정한다.

**한 번에 하나만.** 라이브러리가 강제하지 못한다. popup 목록이 앱 상태이기 때문이다. 앱이 어느 필드의 `set_open(true)`를 처리할 때 다른 anchored popup의 열림을 모두 지운다. 지금 데모는 `dropdown_toggle_intent`가 `menu_open_`을, `card_menu_intent`가 `close_dropdown()`을 부르는 식으로 짝마다 적었다. 이 노드를 더하면서 열린 필드의 owner 하나(`open_popup_`)를 두는 식으로 바꾼다. 값이 하나라 둘이 함께 열릴 수 없다.

**앵커 자리.** 앱은 build 때 필드 포인터를 남기고 frame을 다 지은 뒤 `bounds()`를 읽는다. 그 포인터는 그 frame이 게시되기 전까지만 유효하다. 자리는 `place_field_popup`이 정한다.

- 기본은 필드 아래다. 아래가 `limit`을 넘고 위가 들어가면 위로 뒤집는다. 둘 다 안 들어가면 아래에 두고 나머지는 host가 민다.
- `start`는 popup 왼쪽을 필드 왼쪽에, `end`는 popup 오른쪽을 필드 오른쪽에 맞춘다. 가로는 `limit` 안으로 민다.
- `limit`은 앱이 준다. Android는 내용 영역이고 Win32는 창 client다. 작업 영역은 host만 알기 때문에, Win32에서 창 밖으로 걸칠 수 있던 popup도 이 helper는 창 안에 둔다.

**열린 동안 필드를 누를 때.** 바깥 누름이 먼저 `set_open(false)`를 내고(`window_surface.cpp`) 클릭이 이어진다. 처음에는 클릭이 뗄 때의 tree에서 액션을 꺼내, 그 사이 `open == false` frame이 게시되면 `set_open(true)`를 내 되열렸다. `dropdown_element`도 같은 모양이었다. R3에서 고쳤다. 누를 때 펼쳐 있던 요소의 클릭은 누를 때의 tree의 액션이라 `set_open(false)`다([menu-focus-fixes-design.md](menu-focus-fixes-design.md)의 X-4). 게시가 있든 없든 같은 누름은 닫는다.

## 결정

**1. 접근성 구조.** 질문: 접근성 구조를 어떻게 세우는가.

- 길 a: 자식 둘, root는 구조. 되는 것: `uia_provider.cpp`·`accessibility.h`를 건드리지 않고 지금 코드로 선다. 대가: 읽기 프로그램에 UIA SplitButton이 아니라 단추 둘로 보인다. Narrator는 "저장, 단추"와 "저장 옵션, 단추, 접힘"을 따로 읽을 것이고 한 컨트롤이라는 말은 없을 것이다.
- 길 b: 한 노드 SplitButton. 되는 것: UIA 형식이 맞고 한 노드가 Invoke와 ExpandCollapse를 함께 낸다. 대가: `access_role::split_button`을 더하고 `control_type_for`·`invokable()`을 고친다. 공유 파일이고 모든 요소의 Invoke 판정에 닿는다. 한 element가 hit·hover·눌림·초점을 반으로 갈라야 하는데 hover 좌표는 툴팁이 있는 요소에만 기록된다(`hover_point`).
- 길 c: 자식 둘에 이름 있는 `group` root. 되는 것: 읽기 프로그램이 한 묶음으로 읽는다. 대가: 탐색이 한 단계 깊어지고 그룹 이름을 앱이 따로 준다.
- 권장: a. 눈에 보이는 구조와 읽히는 구조가 같고 공유 파일을 건드리지 않는다. 대가: 위의 읽기 방식이다. 나중에 b로 옮기면 접근 tree와 AutomationId가 바뀌어 자동화 스크립트가 깨진다.
- 승인: 길 a (2026-11-02).

**2. popup 앵커 helper.** 질문: popup을 필드에 앵커하는 길을 helper로 꺼내는가.

- 길 a: 기존 popup 앵커로 충분하다. 되는 것: 새 API가 없다. 대가: 앱이 `bounds()/scale`을 직접 계산한다. 위로 뒤집기가 없어 아래 공간이 모자라면 메뉴가 필드를 덮고, 정렬과 간격이 필드마다 되풀이된다.
- 길 b: 새 순수 함수 `place_field_popup`과 `menu_popup_config::below`. 되는 것: 뒤집기·정렬·간격이 한 곳에 서고 `tooltip_bounds`는 그대로다. 대가: 공개 함수와 설정 한 칸이 늘고, 뒤집기 산술이 tooltip과 비슷하게 두 곳에 생긴다.
- 길 c: `ui_tree.cpp`의 `tooltip_bounds`를 공용으로 꺼낸다. 되는 것: 산술이 한 곳이다. 대가: 그리기와 입력이 함께 쓰는 tooltip 위치(`tooltip_tests.cpp`가 잠근다)를 건드린다. 그 함수는 포인터 앵커와 창 전체 밀기를 가정해, 필드 아래 정렬이나 창 밖에 걸치는 popup과 인자가 갈린다.
- 길 d: helper는 정렬·간격만 정하고 뒤집기와 밀기는 host가 작업 영역(Win32)·내용 영역(Android)으로 한다. 되는 것: Win32에서 창 밖에 걸치는 메뉴가 그대로이고 앱이 `limit`을 주지 않는다. 모니터 경계를 host가 맡는다는 `ui_popup`의 원칙과, popup 위의 popup을 host가 왼쪽으로 뒤집는 지금 규칙(`popup_screen_position`, `update_overlays`)의 연장이다. 대가: `ui_popup`에 앵커 사각형 칸이 늘고 `popup_placement`와 `popup_surface::placement()`도 그것을 나른다(창이 움직이면 `surface_moved`가 그 placement로 다시 놓는다). 두 host의 배치 코드와 `clamp_popup_position`(앵커 높이를 받는다)을 고치고 `popup_reconcile_tests.cpp`가 따라온다. `win32_window.cpp`는 줄 4만 고치는 파일이라 이 노드의 Win32 몫을 줄 4 통합 때 얹어야 한다. 앱은 뒤집혔는지 모른다.
- 권장: b. 뒤집기 산술이 순수 함수 하나라 host 없이 test되고, `field_popup_position::above`로 앱이 뒤집혔는지 안다. 꼬리가 대상을 향해야 하는 teaching_tip이 같은 header에 방향을 더해 늘린다. 이 노드는 아래 한 방향만 만든다. 공유 층(`ui_popup`, host 두 곳)과 줄 4의 `win32_window.cpp`도 건드리지 않는다. 대가: 위에 적은 것과, Win32에서 창 밖에 걸치던 메뉴가 `limit`(창 client) 안에 머문다는 점이다. 창 아래·오른쪽 가장자리의 필드에서는 메뉴가 걸치는 대신 위로 뒤집히거나 왼쪽으로 밀린다. 위도 안 들어가면 지금처럼 아래에 두고 host가 민다. 창 밖 걸침을 지키는 쪽이 낫다고 보면 d다.
- 승인: 길 b (2026-11-02).

**3. Tab 자리.** 질문: Tab 자리를 어떻게 두는가.

- 길 a: 한 자리. 가로 focus group, 들어올 때 본체, ←/→로 이동. 되는 것: `choice_group_element`와 같은 모델이고 폼의 Tab 수가 늘지 않는다. 대가: 키보드로 화살표에 닿으려면 ←/→나 Alt+↓·F4가 필요하다.
- 길 b: 두 자리. 되는 것: Tab만으로 닿는다. 대가: 버튼 하나가 Tab 둘이다.
- 권장: a. 마우스·터치는 영향이 없다. 대가: 위의 도달 방법이다. 권장대로 진행.

**4. 메뉴를 여는 키.** 질문: 메뉴를 여는 키는 무엇인가.

- 길 a: Alt+↓·F4(어느 쪽이든)와 화살표 쪽의 Enter·Space·↓. 되는 것: Win32 콤보 상자의 Alt+↓·F4 관례와 메뉴 단추의 ↓ 관례를 모두 받는다. 대가: 앱이 화살표 쪽 ↓를 쓸 수 없다.
- 길 b: Alt+↓·F4만. 되는 것: 화살표 쪽 ↓가 앱으로 흐른다. 대가: 메뉴 단추에서 흔한 ↓가 먹지 않는다.
- 길 c: 본체에서도 ↓가 연다. 대가: 목록 안 버튼의 ↓를 가로챈다.
- 권장: a. 본체의 ↓는 가로채지 않는다. 대가: 위의 앱이 쓸 수 없는 ↓다. 권장대로 진행.

**5. 메뉴 정렬.** 질문: 메뉴를 어디에 정렬하는가.

- 길 a: 버튼 전체 폭 기준 왼쪽 정렬(`start`). 되는 것: 메뉴 왼쪽이 버튼 왼쪽과 맞아 메뉴 막대와 같은 규칙이다. 대가: 화살표 아래에서 메뉴가 왼쪽으로 멀다.
- 길 b: 오른쪽 끝 정렬(`end`). popup 오른쪽이 버튼 오른쪽에 맞아 화살표 아래에 붙는다. 앱이 `alignment`로 고른다.
- 권장: a. helper가 둘 다 내고 기본은 a다. 메뉴 왼쪽이 버튼 왼쪽과 맞아 메뉴 막대와 같은 규칙이다. 대가: 화살표 아래에서 메뉴가 왼쪽으로 멀다는 점이다. 권장대로 진행.

**6. 모양.** 질문: 모양은 어디까지 따르는가.

- 길 a: `text_button_visual`의 normal·accent·danger. 되는 것: 글자 버튼과 같은 채움 판정(`text_button_fill_for`)이라 한 dialog에 나란히 서도 색이 어긋나지 않는다. 대가: `link`는 상자가 없어 normal로 읽는다.
- 길 b: normal·accent만. 되는 것: 모양이 둘이라 단순하다. 대가: 되돌릴 수 없는 동작(삭제 ▾)을 앱이 `look.fill` 선택자로 직접 칠한다.
- 권장: a. 마지막으로 쓴 동작이 본체가 되는 동작은 어느 쪽이든 앱 몫이다. 라이브러리는 메시지만 내고 앱이 다음 frame에 `look`과 `invoke`를 바꾼다. 대가: 위의 `link` 처리다. 권장대로 진행.

## 플랫폼

**Win32.**

- 메뉴는 별도 popup 창이다. 앵커 창 client 기준으로 놓이고 host가 작업 영역으로 민다(`popup_screen_position`, `clamp_popup_position`). 위로 뒤집지 않으므로 뒤집기는 helper가 하고, 그 범위는 앱이 주는 `limit`(창 client)이다. 창 client 밖으로 걸치던 메뉴는 창 안에 머문다(결정 2).
- Alt+↓는 `WM_SYSKEYDOWN`이 키 이벤트로 나르고 Alt+F4만 시스템에 남는다. F4는 `WM_KEYDOWN`이다.
- UIA는 본체에 Invoke, 화살표에 ExpandCollapse를 낸다. `uia_provider.cpp`와 `win32_window.cpp`는 건드리지 않는다.

**Android.** Java 소스는 0줄이다. 그리기·hit·`ui_popup` overlay가 모두 공용 층이라 JNI도 unsupported도 없다.

- overlay는 popup을 내용 영역 안으로 들이기만 한다(`std::clamp`). 위로 뒤집는 것은 helper고 `limit`이 내용 영역이다.
- hover가 없고 눌림 강조만 선다. 터치 판정 영역을 넓히는 장치가 없어 화살표가 좁다. 기본 폭 28 논리 픽셀은 손가락에 작을 수 있어 터치 앱은 `arrow_width`와 슬롯 높이를 키운다.
- 외장 키보드의 Alt·F4는 `android_input.cpp`가 같은 키 이벤트로 옮겨 같은 처리기가 받는다. 소프트 키보드에는 Alt가 없다.
- 접근성 role·상태는 Android에서 소비되지 않는다([number-input-design.md](number-input-design.md)와 같은 까닭이다).

## 다른 노드와의 약속

- **date_picker**, **color_picker**(popup 조립을 고를 때)는 열림 규약을 그대로 따른다. `set_open` 절대 메시지, `field_popup_key_handler`, `place_field_popup`, 닫는 계기 표, 한 번에 하나만이다. **menu_bar**는 상태와 절대 메시지(`set_open`), 닫는 계기 표, 한 번에 하나만, `place_field_popup` 자리는 따른다. 여는 키와 열린 동안 막대 항목 사이 이동은 menu_bar가 정한다(Alt·F10·←/→). 이동 코드는 `process_menu_key`에 들어가므로 controller를 고치는 menu_bar(줄 3)가 맡는다. 항목 사이 hover 전환도 그 노드의 몫이다.
- **teaching_tip**은 `field_popup.h`에 방향을 더해 늘린다. 이 노드가 아래 한 방향의 시그니처를 세운다.
- `examples/demo/popups_page.*`와 `examples/mobile_demo/popups_page.cpp`는 date_picker·teaching_tip과 겹친다. 이 노드가 `open_popup_` 하나로 바꾸고 뒤 노드는 그 값을 쓴다.
- `ui_element_id.h`의 kind 추가는 병렬 노드와 같은 자리라 통합 때 줄 충돌이 난다. AutomationId는 kind의 수라 병합 순서가 번호를 정한다. test와 luil-uia 스크립트는 번호를 박지 않는다.
- 접근 이름 기본 문구(" 옵션")는 [default-text-design.md](default-text-design.md)의 헤더로 옮겼다. 분할 버튼 화살표의 이름은 `split_button_arrow_name`이 짓는다. `text_button_background`를 꺼내도 동작은 같고 기존 raster test가 지킨다.

## 확인

- `tests/split_button_element_tests.cpp`:
  - 자식 둘의 kind·owner·자리, 구분선이 화살표 안에 서는지, `invoke`가 없으면 화살표 하나인지;
  - 본체 클릭은 `invoke`, 화살표 클릭은 열린 상태의 반대를 담은 `set_open`, 비활성이면 막히고 `invoke_enabled`가 거짓이면 본체만 막히는지;
  - controller에 키를 넣어 Alt+↓·F4는 어느 쪽에서나, ↓는 화살표에서만 열고 본체의 ↓는 앱 정책까지 가는지;
  - 열려 있으면 Alt+↓가 삼켜지고 Ctrl·Shift 조합은 `nullopt`인지, Tab 한 자리와 ←/→ 순환, 본체가 꺼지면 화살표가 자리인지;
  - 열린 동안에만 초점 떠남 훅이 서고 초점이 떠나면 `set_open(false)`가 나가는지;
  - 열린 동안 화살표를 누르고 `open == false` frame이 게시된 뒤 떼도 `set_open(false)`로 닫히는지(R3에서 뒤집었다).
- `tests/field_popup_tests.cpp`: 아래 기본 자리, 아래가 넘칠 때 뒤집기, 위도 안 들어가면 아래 유지, `end` 정렬과 가로 밀기, 배율 2의 논리 좌표, 키 처리기가 받는 키와 받지 않는 키.
- `tests/popup_reconcile_tests.cpp`: `menu_popup_config::below`가 크기를 잰 뒤 `x`·`y`를 정하는지.
- `tests/accessibility_tests.cpp`: root가 접히고 두 절반이 형제인지, 본체와 화살표의 info(`expanded`), expand·collapse가 절대 메시지인지.
- `tests/uia_provider_tests.cpp`: 본체 노드는 Invoke만, 화살표 노드는 ExpandCollapse만 내고 Expand가 `set_open(true)`를 나르는지.
- `tests/raster_draw_tests.cpp`: 구분선 한 줄, 바깥 모서리만 둥근지, 열린 동안 화살표의 눌림 바탕, accent·danger 채움, 비활성.
- 데모(`examples/demo`, `examples/mobile_demo`의 popups 페이지): "저장" 분할 버튼(메뉴 두 항목)과 "보내기" 드롭다운 단추를 놓는다. "저장 메뉴 열림: 닫힘/열림", "보내기 메뉴 열림: 닫힘/열림", "마지막 동작: 없음/저장/…"을 label로 그려 UIA가 되읽는다. 격리 데스크톱에서 luil-uia로 두 노드의 Invoke·Expand와 메뉴 항목 선택을 돌린다.
  - 데모는 `dropdown_open_`·`menu_open_`을 열린 필드의 owner 하나(`open_popup_`)로 바꿔 한 번에 하나만 뜨게 한다. 이미 닫힌 필드의 늦은 닫기 메시지는 지금 떠 있는 popup을 닫지 않는다.
  - `examples/demo/popups_page`·`examples/mobile_demo/popups_page`의 test는 `tests/demo_gallery_tests.cpp`·`tests/mobile_demo_page_tests.cpp`가 읽는다.
- 사람 확인: 실기기에서 화살표 탭 크기, Narrator의 두 노드 읽기, 두 절반 경계의 hover 강조, 고대비에서 구분선과 열림 표시.

## 범위 밖

- 한 노드 SplitButton 역할(결정 1의 b). 승인이 나면 따로 설계한다.
- 마지막으로 쓴 동작이 본체가 되는 동작. 앱의 상태다.
- `dropdown_element`·`combo_box_element`에 Alt+↓ 달기. 같은 처리기를 쓰는 한 줄이지만 기존 요소의 동작이 바뀌어 따로 한다.
- 위·옆 방향 배치와 꼬리. teaching_tip의 몫이다. 터치 판정 영역을 넓히는 장치도 없다.
