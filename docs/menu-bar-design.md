# 메뉴 막대

창 위에 "파일·편집·보기" 같은 최상위 메뉴 줄을 세운다. 항목을 누르거나 키보드로 들어오면 그 아래에 기존 메뉴 popup이 뜨고, 열린 동안 ←/→와 hover로 이웃 메뉴로 옮긴다. 공개 형식은 [menu_bar_element.h](../include/luil/ui/menu_bar_element.h)의 `menu_bar_config`·`menu_bar_state`·`menu_bar_element`와 [menu_popup.h](../include/luil/app/menu_popup.h)의 `make_menu_bar_popup`이다.

키보드와 자리는 사용자가 정했다(G3, 2026-11-02). Alt를 눌렀다 다른 키 없이 떼면 막대 첫 항목에 서고, Alt+글자(접근 키), F10, 좌우 이동, Esc 단계가 된다. 막대가 있는 창에서는 Alt 뗌·Alt 글자 시스템 메시지를 OS에 넘기지 않는다. 막대는 제목 표시줄(caption) 아래 별도 줄이고 `caption_element`는 고치지 않는다. 앞서 고칠 controller 한계 둘(X-3·X-4)은 [menu-focus-fixes-design.md](menu-focus-fixes-design.md)에 나눴다.

## 문제

- 막대 요소가 없다. 앱이 글자 단추 줄과 `make_menu_popup`을 손으로 잇고, 열린 메뉴 사이 이동은 만들 수 없다. `process_menu_key`([ui_interaction.cpp](../src/ui/ui_interaction.cpp))는 →를 하위 메뉴 열기로, ←를 깊이가 0보다 클 때의 하위 메뉴 닫기로만 쓴다. 깊이 0의 ←/→로 이웃 최상위 메뉴에 갈 길이 없다. 메뉴가 열려 있으면 `process_key`가 메뉴가 갖지 않은 키를 삼켜 Alt+글자로 다른 메뉴에 갈 길도 없다.
- Alt 단독은 키가 아니다. `modified_key_from_virtual`([surface_input.cpp](../src/win32/surface_input.cpp))이 `VK_MENU`를 `key_code::none`으로 버리고, 규약도 "수정자 키 자체는 키가 아니다"다([keyboard-shortcut-design.md](keyboard-shortcut-design.md)).
- Alt 뗌은 OS의 메뉴 모드로 간다. [window_surface.cpp](../src/win32/window_surface.cpp)의 `WM_SYSKEYUP`·`WM_SYSCHAR`는 `key_capturing()`일 때만 삼키고 나머지는 `DefWindowProc`의 `SC_KEYMENU`로 간다. 그 뒤의 ↓가 시스템 메뉴를 열 것이다(추정, 사람 확인). F10은 `WM_SYSKEYDOWN`이 `key_code::f10`으로 나르지만 그 뗌도 같은 길이다. Alt+글자도 키 이벤트로 오지만 짝인 `WM_SYSCHAR`가 메뉴 없는 창의 경고음이 될 수 있다(추정, 사람 확인).
- 접근 키 칸이 없다. `menu_item_config`([menu_element.h](../include/luil/ui/menu_element.h))에는 key·label·icon·shortcut_text만 있다.
- 접근성 역할이 없다. `access_role`에 메뉴 막대가 없어 `control_type_for`([uia_provider.cpp](../src/win32/uia_provider.cpp))가 Pane으로 답한다.

## 공개 형식

```cpp
// include/luil/ui/menu_bar_element.h
inline constexpr float menu_bar_height { 28.0f };        // 논리 픽셀
inline constexpr float menu_bar_item_padding { 10.0f };  // 글 양옆

struct menu_bar_item_config
{
    std::u8string key {};    // 항목 owner이자 열린 메뉴의 이름
    std::u8string label {};
    // 접근 키다. 영문자나 숫자 하나이고 대소문자를 가리지 않는다. 0이면 없다.
    //  - 글자가 라벨에 있으면 그 글자에 밑줄을, 없으면 라벨 뒤에 "(F)"를 붙여 그린다.
    char32_t access_key { 0 };
    bool enabled { true };
};

struct menu_bar_state
{
    std::u8string current {};  // 막대가 선 항목이다. 비면 막대가 쉰다
    bool open { false };       // current의 메뉴가 떠 있다
    bool keyboard { false };   // 키보드로 들어왔다. 밑줄을 보이고 연 메뉴의 첫 항목을 강조한다
    [[nodiscard]] bool operator==(const menu_bar_state&) const = default;
};

struct menu_bar_config
{
    std::u8string owner {};
    std::vector<menu_bar_item_config> items {};
    menu_bar_state state {};  // 앱 상태다
    // 막대를 이 상태로 하라는 절대 메시지다. 없으면 막대가 눌리지 않고 키도 받지 않는다.
    std::function<input_action(const menu_bar_state&)> set_state {};
    bool enabled { true };
    // 거짓이면 F10은 앱 단축키로 흐르고 Alt 단독만 막대를 세운다.
    bool f10_activates { true };
    std::u8string access_name {};  // 비면 기본 문구 (`menu_bar_name`)
};

class menu_bar_element final : public ui_element
{
public:
    explicit menu_bar_element(menu_bar_config config);
    [[nodiscard]] const menu_bar_config& config() const noexcept;
    [[nodiscard]] std::optional<rect_f> item_bounds(std::u8string_view key) const;  // bounds()와 같은 물리 픽셀
    // 막대가 쥔 키다 (controller가 부른다). 막대의 것이 아니면 nullopt다.
    [[nodiscard]] std::optional<std::vector<input_action>> handle_key(const key_pressed_event& event, bool menu_shown) const;
    // 항목 폭의 합이다 (논리 픽셀). `menu_element::width_for`와 같이 물리 글자 크기로 잰다.
    [[nodiscard]] static float width_for(const menu_bar_config& config, const text_measurer& measurer = default_text_measurer(), float font_size = 12.0f, float scale = 1.0f);
};

// tree에서 키를 받을 수 있는 막대다. 숨었거나 꺼졌거나 가둠 밖이면 nullptr다.
[[nodiscard]] const menu_bar_element* active_menu_bar(const ui_tree& tree);
// 막대 접근 키와 겹치는 Alt+글자 명령의 id다. 겹치면 막대가 이긴다.
[[nodiscard]] std::vector<std::u8string> menu_bar_access_conflicts(const menu_bar_config& config, std::span<const app_command> commands);
```

```cpp
// include/luil/app/menu_popup.h (끝에 더함)
// 막대 항목 아래에 그 메뉴를 띄운다. `popup.below`는 항목 자리와 `limit`으로 채운다.
//  - dismiss는 Esc면 { current, false, keyboard }, 그 밖의 계기면 {}다. `menu.after_select`가 비면 {}로 채운다.
//  - `menu.owner`는 항목 키다. 나머지는 `make_menu_popup`이다.
[[nodiscard]] ui_popup make_menu_bar_popup(const menu_bar_element& bar, menu_config menu, menu_popup_config popup, rect_f limit);
```

끝에 더하는 칸: `menu_item_config::access_key`(`char32_t`, `children` 뒤), `key_code::alt_tap`(`numpad_decimal` 뒤), `interaction_snapshot::menu_keys`(`pointer_control` 뒤), `access_role::menu_bar`(`navigation` 뒤), `access_info::access_key`(`error` 뒤). kind `menu_bar`·`menu_bar_item`은 `end_of_library_kinds` 위다.

## 규칙

**자리와 그리기.** 막대는 앱이 caption 아래 줄에 놓는 요소다. 높이는 `menu_bar_height`에 배율을 곱한 값이고 항목은 왼쪽부터 선다. 항목 폭은 글 폭(접근 키 덧붙임 포함)에 양옆 `menu_bar_item_padding`이다. 넘치면 오른쪽이 잘리고 키보드로는 닿는다. 글자 크기와 굵기는 메뉴 항목과 같다(`metrics.body_font_size`, `font_weights.body_text`).

- 줄 바탕은 칠하지 않는다. 항목 바탕은 hover·키보드로 선 항목·열린 항목에서만 서고 색은 `text_button_background`([text_button_paint.h](../src/ui/text_button_paint.h))가 정한다. hover와 키보드로 선 항목은 hover 바탕, 열린 항목은 눌림 바탕이다.
- 밑줄은 `state.keyboard`일 때만 1 논리 픽셀 선으로 긋는다. "(F)" 덧붙임은 늘 그린다. 폭이 상태마다 흔들리지 않는다.
- 색은 팔레트 역할만 써서 고대비가 따라온다. 움직임이 없어 움직임 줄이기와 무관하다.

**상태와 메시지.** 열림은 앱 상태다. `set_state`는 절대 상태 하나라 분할 버튼의 `set_open`([split-button-design.md](split-button-design.md))을 막대에 맞게 넓힌 것이다. 닫는 계기 표와 한 번에 하나만 규칙도 그대로 따른다.

| 계기 | 보내는 상태 |
| --- | --- |
| 쉬는 막대에서 Alt 뗌·F10 | `{ 첫 활성 항목, false, true }` |
| Alt+글자(접근 키가 맞음) | `{ 그 항목, true, true }`. 열린 동안에도 그 메뉴로 옮긴다 |
| 선 막대에서 ←/→, Home/End | 이웃·처음·끝 항목. 끝에서 돌고 꺼진 항목은 건너뛴다 |
| 선 막대에서 ↓·↑·Enter·Space, 글자(접근 키) | `{ 그 항목, true, true }` |
| 열린 메뉴 깊이 0의 ←/→ | 이웃 항목의 `{ 이웃, true, true }`. →는 강조 항목에 하위 메뉴가 있으면 하위가 먼저다 |
| 열린 메뉴의 Esc | `{ current, false, keyboard }`. 막대는 선 채 남는다 |
| 선 막대에서 Esc, Alt 뗌·F10 | `{}`. 열린 동안의 Alt 뗌·F10도 `{}`다 |
| 항목 클릭 | 닫힌 항목은 `{ 그 항목, true, false }`, 열린 항목은 `{}` |
| 열린 동안 다른 항목 hover | `{ 그 항목, true, false }` |
| 항목 고름, 열린 메뉴의 바깥 누름(dismiss), 창 비활성, 열리지 않고 선 막대의 밖 누름 | `{}` |

- 열린 동안의 hover 전환은 항목의 `hover_region_target::on_enter`다([hover-dwell-design.md](hover-dwell-design.md)). 열린 동안 current가 아닌 항목에만 단다. 마우스·펜만이고 지연이 없다.
- 열린 항목을 다시 누르면 X-4 규칙으로 누를 때의 액션 `{}`가 나간다. 바깥 누름의 dismiss도 `{}`라 다시 열리지 않는다.
- 선 막대의 키보드 초점은 그대로다. 항목은 Tab 자리가 아니고(`set_tab_stop(false)`, `focusable`은 `tab_stop`을 요구한다) 강조는 상태가 그린다. Esc로 빠져나와도 초점이 돌아갈 일이 없다.

**controller.** `process_key`의 키 잡기 다음, 열린 메뉴 블록 앞에서 `active_menu_bar`를 찾는다. 키가 온 표면의 tree가 먼저이고 없으면 초점 표면이다.

1. `alt_tap`, 그리고 `f10_activates`일 때 수식 키 없는 F10: 막대가 쉬면 세우고, 서 있거나 열려 있으면 `{}`다. Shift+F10은 지금처럼 컨텍스트 메뉴다.
2. Alt만 눌린 글자·숫자 키가 접근 키와 맞으면 그 메뉴를 연다. 맞지 않으면 다음 단계로 흐른다. 키 처리기와 앱 정책보다 앞이라 같은 Alt+글자 명령보다 막대가 이긴다. `menu_bar_access_conflicts`가 그 겹침을 알린다.
3. 막대가 서 있고 열리지 않았으면 `handle_key(event, false)`가 모든 키를 받는다. 막대의 것이 아닌 키는 삼킨다. 열린 메뉴가 갖지 않은 키를 삼키는 지금 규칙과 같다.
4. 막대가 열려 있으면 메뉴 블록이 먼저다. `process_menu_key`가 깊이 0에서 쓰지 않은 ←/→는 `handle_key(event, true)`로 가고, 깊이 0의 Esc는 앱 정책의 `close_menu` 대신 그리로 간다. 막대가 열린 동안 깊이 0 메뉴는 막대의 것으로 본다(한 번에 하나만).
5. 키보드로 연 막대 메뉴(`state.keyboard`)가 서거나 그 owner가 바뀌면 `update_focus`가 첫 활성 항목을 강조한다. ←/→로 옮긴 메뉴도 새 메뉴다. 포인터로 연 메뉴는 지금처럼 강조 없이 선다.
6. 막대가 쉬지 않는 동안 `snapshot_.menu_keys`가 참이고 글자 입력(`character_typed_event`)은 버린다. 같은 키가 키로 온다(아래 Win32). 열리지 않고 선 막대(`open == false`)의 밖 누름과 `surface_focus_lost_event`는 그 액션 앞에 `{}`를 더한다. 열린 동안의 바깥 누름은 popup dismiss의 몫이고 popup 표면 안의 누름은 메뉴의 것이라 더하지 않는다. 더하면 하위 메뉴가 있는 항목의 누름이 메뉴를 닫는다.

**접근 키.** 막대 항목과 막대에서 연 메뉴의 항목(`menu_item_config::access_key`)이 받는다. 맞추기는 `key_code`(`key_a`…`key_z`, `key_0`…`key_9`)로 한다. 이름은 미국 배열 자리라 한국어 입력기가 켜져 있어도 같은 키가 맞는다. 메뉴 안에서 맞는 항목이 하나면 실행하고(하위 메뉴가 있으면 연다) 여럿이면 다음 것으로 강조만 옮긴다.

**접근성.** 막대는 `access_role::menu_bar`로 UIA MenuBar다. 이름은 `access_name`, 비면 기본 문구다. 항목은 `menu_item`이고 `expanded`가 `current == key && open`이다. `invokable()`이 `expanded`를 가진 요소의 Invoke를 빼므로 ExpandCollapse만 서고, expand는 `{ key, true, false }`, collapse는 `{}`다. `access_info::access_key`("Alt+F")를 UIA AccessKey 속성으로 낸다. 라벨의 "(F)"는 이름에 넣지 않는다.

**기본 문구.** 막대의 기본 접근 이름 `menu_bar_name()`("메뉴 막대")을 [default_text.h](../src/ui/default_text.h) 끝에 더한다. "(F)"와 "Alt+F"는 기호와 키 이름이라 기본 문구가 아니다.

## 결정

**1. 막대 상태를 어디에 두는가.** 질문: 키보드로 선 항목과 열린 메뉴를 누가 드는가.

- 길 a: 앱 상태 `menu_bar_state { current, open, keyboard }`와 절대 메시지 `set_state`. controller는 tree에서 막대를 찾아 키를 보낸다. 되는 것: 열림은 앱 상태라는 popup 규약 그대로이고 test가 상태 하나로 막대를 재현한다. 대가: 키보드로 서기만 해도 app 메시지가 오가고 frame이 다시 선다.
- 길 b: 선 항목은 controller가 snapshot 강조로 들고(`menu_highlight` 선례) 앱은 열린 메뉴 키만 든다. 되는 것: 메시지가 줄어든다. 대가: 열림은 앱, 선 자리는 controller로 상태가 둘로 갈려 Esc 단계와 hover 전환이 두 곳을 맞춘다. snapshot 칸이 하나 더 는다.
- 길 c: 막대 항목에 키보드 초점을 옮긴다. 되는 것: ←/→가 기존 가로 묶음(`process_group_key`)으로 돈다. 대가: `focusable`이 `tab_stop`을 요구해 항목이 Tab 자리가 되고, 텍스트 칸의 초점이 떠나 Esc 뒤 되돌릴 자리를 따로 든다. 초점 떠남 훅이 열린 popup을 닫는다.
- 권장: a. 대가: 위의 메시지 왕복이다. 공개 API 모양을 정하므로 승인 필요.

**2. Alt 단독을 무엇으로 나르는가.** 질문: Alt를 홀로 뗀 것을 controller에 어떻게 알리는가.

- 길 a: 새 이름 키 `key_code::alt_tap`을 `numpad_decimal` 뒤에 둔다. 막대가 있는 창만 `alt == false`인 `key_pressed_event`로 보낸다. 되는 것: F10과 갈려 `f10_activates`를 끌 수 있고 다른 창의 키는 그대로다. 라벨이 없어 `keyboard_shortcut_tests.cpp`의 89개 수가 그대로다. 대가: "수정자 키 자체는 키가 아니다" 규약에 예외 하나가 선다.
- 길 b: Alt 뗌을 F10 누름으로 보낸다. 되는 것: 어휘가 늘지 않는다. 대가: F10을 단축키로 쓰는 앱이 Alt 단독도 잃고 둘을 가를 수 없다.
- 길 c: `raw_input_event`에 새 변형을 더한다. 되는 것: 키와 섞이지 않는다. 대가: `std::visit`로 변형을 다루는 곳을 모두 고친다.
- 권장: a. 대가: 위의 규약 예외다. 권장대로 진행.

**3. 접근 키를 어떻게 적는가.** 질문: 접근 키를 라벨 안에 적는가, 칸을 따로 두는가.

- 길 a: 칸 `access_key`(`char32_t`). 라벨에 글자가 없으면 "(F)"를 붙여 그린다. 되는 것: 번역 글에 표식이 섞이지 않고, 한글 라벨도 "파일(F)" 관례로 선다. 대가: 막대 항목과 메뉴 항목에 칸이 하나씩 는다.
- 길 b: 라벨 안 `&`(Windows 관례). 되는 것: 칸이 늘지 않는다. 대가: 접근 이름·검색 글·폭 재기가 모두 `&`를 벗겨야 하고, `&` 글자를 쓰려면 `&&`로 적는다. 한글 라벨에는 붙일 글자가 없다. 트레이 메뉴(`make_tray_menu`)가 `&`를 그대로 넘겨야 해 `tray-icon-design.md`의 `&&` 바꾸기 규칙이 뒤집힌다.
- 권장: a. 대가: 위의 칸 둘이다. 권장대로 진행(G3이 접근 키 칸을 공통 비용으로 셌다).

**4. 메뉴 안 접근 키를 어디까지 받는가.** 질문: 수식 키 없는 글자가 메뉴 항목을 고르는 범위는 어디인가.

- 길 a: 막대 항목만. 되는 것: 메뉴 키 경로가 그대로다. 대가: 메뉴를 연 뒤에는 화살표로만 고른다.
- 길 b: 막대에서 연 메뉴의 항목까지. 되는 것: Windows 메뉴 막대 관례 그대로다. `menu_keys` 동안 글자가 키로 와 입력기와 섞이지 않는다. 대가: `menu_item_config`에 칸이 하나 늘고 `process_menu_key`에 글자 맞추기가 든다.
- 길 c: 컨텍스트 메뉴까지 모든 메뉴. 되는 것: 규칙이 하나다. 대가: 검색 칸이 있는 메뉴(명령 팔레트)의 글자를 빼앗고, 그 밖의 메뉴는 글자가 문자 경로라 입력기와 섞인다.
- 권장: b. 대가: 위의 칸 하나와 키 경로 한 갈래다. 권장대로 진행.

**5. 열린 메뉴 사이를 어떻게 옮기는가.** 질문: 깊이 0의 ←/→를 누가 받는가.

- 길 a: controller가 깊이 0에서 쓰지 않은 ←/→를 활성 막대의 `handle_key`에 넘긴다. 되는 것: 막대가 이웃과 꺼진 항목을 알고 `menu_config`가 그대로다. 대가: controller가 `menu_bar_element`를 안다(`menu_element`를 아는 지금과 같은 결이다).
- 길 b: `menu_config`에 이전·다음 액션을 더한다. 되는 것: controller가 막대를 모른다. 대가: 메뉴마다 앱이 이웃 메시지를 채우고, Alt+글자 전환은 따로 남는다.
- 권장: a. 대가: 위의 결합이다. 권장대로 진행.

**6. 열린 동안 hover 전환을 어떻게 내는가.** 질문: 이웃 항목으로 포인터가 오면 무엇이 메뉴를 바꾸는가.

- 길 a: 항목의 `hover_region_target::on_enter`. 되는 것: controller를 고치지 않고 지연 없이 바뀐다. 대가: 열린 동안 tree마다 항목에 역할이 달린다.
- 길 b: `hover_dwell_target`에 짧은 지연. 되는 것: 지나가는 포인터에 덜 흔들린다. 대가: hit 대상이 되어 툴팁 규칙과 섞이고 Windows보다 굼뜨다.
- 길 c: controller가 포인터 이동에서 직접 낸다. 되는 것: 포인터 이동이 한 경로로 모여 항목마다 역할을 따로 달지 않는다. 대가: 이동 경로가 깊이 바뀐다.
- 권장: a. 대가: 위의 역할 한 칸이다. 권장대로 진행.

**7. Android에서 어디까지 서는가.** 질문: 두 번째 플랫폼에서 막대와 Alt를 어떻게 두는가.

- 길 a: 막대·클릭·Alt+글자·F10·←/→는 두 플랫폼, Alt 단독은 Win32만이다. 모바일 데모에는 두지 않는다. 되는 것: Java 0줄 그대로이고 휴대폰 화면에 데스크톱 chrome이 서지 않는다. 대가: Android의 하드웨어 키보드 사용자는 Alt 단독 대신 F10·Alt+글자를 쓴다.
- 길 b: `android_input.cpp`가 Alt 뗌을 보고 `alt_tap`을 합성한다. 되는 것: 두 플랫폼이 같다. 대가: 변환기가 누름만 보는 지금 규칙(뗌을 버림)을 바꾸고 Android 관례에 없는 동작이다.
- 길 c: Android에서 막대를 서지 않게 한다(높이 0). 되는 것: 모바일 회귀가 없다. 대가: 태블릿·데스크톱 모드 앱이 막대를 쓸 수 없다.
- 권장: a. 대가: 위의 Alt 단독 차이다. 권장대로 진행.

## 플랫폼

**Win32.** 키 경로는 `window_surface.cpp`·`surface_input.cpp`(이번 라운드 줄 3만)에 든다. 무장과 삼킴 판정은 순수 함수(`alt_tap_tracker`, `swallow_system_key`)로 `surface_input.h`에 두어 창 없이 test한다.

- 무장: `WM_SYSKEYDOWN`의 `VK_MENU`가 반복이 아니고(bit 30), Ctrl이 함께가 아니고(AltGr), 키 잡기 중이 아니고, 이 창의 게시된 tree에 `active_menu_bar`가 있을 때다. 풀기: 다른 키의 `WM_KEYDOWN`·`WM_SYSKEYDOWN`, 마우스 누름, `WM_KILLFOCUS`·비활성, 전역 단축키 눌림.
- 전역 단축키 눌림: `RegisterHotKey`로 건 Alt 조합은 OS가 글자 키를 가져가 창에 오지 않아 `VK_MENU`의 누름과 뗌만 오고, 풀지 않으면 `alt_tap`이 잘못 나간다. 풀기 입구는 공개 멤버 `window_surface::cancel_alt_tap()`([window_surface.h](../src/win32/window_surface.h))이고 안에서 `alt_tap_tracker::cancel`을 부른다. 무장은 키보드 초점 표면에 서는데 그 표면은 보조 창(`secondary_surface`)일 수 있고 `WM_HOTKEY`는 주 창으로만 온다. 그래서 `global-hotkey-design.md`의 `case WM_HOTKEY`가 주 창과 모든 보조 창 표면(`main_`·`windows_`)에 이 함수를 부른다. 그 호출은 줄 4가 통합 때 menu_bar 뒤에 한 줄로 낸다.
- `WM_SYSKEYUP`의 `VK_MENU`는 무장이었으면 `alt_tap`을 게시한다. 막대가 있으면 무장 여부와 상관없이 0을 돌려 메뉴 모드를 막는다. `VK_F10`의 뗌도 같다. `WM_SYSCHAR`도 막대가 있으면 삼키되 Space(Alt+Space 시스템 메뉴)는 넘긴다. Alt+F4는 `WM_SYSKEYDOWN`에서 그대로 시스템에 간다.
- `menu_keys` 동안 `WM_KEYDOWN`을 TIP(`text_input_->handle_key`)에 먼저 주지 않고, `post_key_event`의 글자 키 판정이 키 잡기와 같이 수식 키 없는 글자를 키로 보낸다.
- 막대를 세울 키(Alt 뗌, F10, Alt+글자)를 보내기 전에 조합 중이면 확정한다. 누름 전 확정(`commit_composition_before_press`)과 같다.
- 막대가 키보드로 선 동안 `announce_accessibility_focus`에 막대 항목 id를 준다. 열린 메뉴 안의 강조는 지금처럼 알리지 않는다.
- 막대가 있는지는 게시된 tree를 보고(`caption_band_in`처럼), 모드는 발행된 상호작용을 본다(`key_capturing`처럼).

**Android.** Java 소스는 0줄이다. 막대 요소·클릭·popup overlay는 공용 층이다.

- 키 변환(`android_input.cpp`의 `translate(const key_input&)`)은 누름만 보고, Alt는 `modifier_key`가 수식 키로 분류해 홀로는 키가 아니다. Alt 단독은 unsupported다(결정 7). Alt+글자는 `key_f`+alt, F10은 `key_code::f10`으로 와 같은 controller 길을 탄다.
- `menu_keys` 동안 글자를 키로 보내려면 [android_app.cpp](../src/android/android_app.cpp)가 `input_.set_key_capture`에 `menu_keys`를 함께 넘긴다(한 줄).
- 접근성 계층이 없어 역할·AccessKey는 소비되지 않는다.

## 다른 노드와의 약속

- **X-3·X-4(같은 줄, 앞)**: [menu-focus-fixes-design.md](menu-focus-fixes-design.md)가 먼저 들어간다. 열린 항목 다시 누르기는 그 규칙에 기댄다.
- **split_button(R1)**: `set_open`·닫는 계기 표·한 번에 하나만·`place_field_popup`을 따른다. 그 문서가 menu_bar에 남긴 여는 키·항목 사이 이동·hover 전환을 여기서 닫는다.
- **R2 메뉴 폭·기본 문구·검사기**: `make_menu_bar_popup`은 `make_menu_popup`을 불러 배율을 받는 `width_for`를 그대로 쓴다. `menu_bar_name`은 `default_text.h/.cpp` 끝, kind 이름(`menu_bar`, `menu_bar_item`)은 `inspect_names.cpp`의 `library_kind_name`에 kind를 더하는 같은 커밋에서 더한다.
- **undo_stack(R2)**: 편집 메뉴는 `make_undo_commands`를 `menu_item_from`으로 짓는다. 접근 키는 앱이 `menu_item_config::access_key`에 채운다. `commands.h`는 고치지 않는다.
- **hotkey(줄 4)**: `alt_tap`에는 VK가 없다. `key_mapping.cpp`의 역표는 이 키를 0으로 답하고 단축키 잡기는 거절한다. `tests/surface_input_tests.cpp`는 menu_bar만 끝에 더한다. hotkey는 `alt_tap`이 `invalid_key`임을 `hotkey_sync_tests.cpp`에서 잠그고, 그 단언은 통합 커밋이 menu_bar 뒤에 낸다. 줄 4의 `case WM_HOTKEY`가 `cancel_alt_tap()`을 부른다(위 Win32 절).
- **tray(줄 4)**: 이 노드는 `win32_window.cpp`를 고치지 않는다. 트레이 메뉴는 시스템 `HMENU`라 X-4와 무관하다. 이 노드가 더한 `menu_item_config::access_key`는 `make_tray_menu`가 `&` 표기로 옮긴다. 그 구현은 통합 커밋이 menu_bar 뒤에 낸다(`tray-icon-design.md`).
- **rich_edit(줄 2)**: `menu_keys` 동안 텍스트 칸에 글자와 TIP 키가 가지 않는다. 서식 메뉴를 막대에 달면 이 문서의 접근 키를 쓴다. `rich-edit-exchange-design.md`가 요청한 갈래 하나를 줄 3이 별도 커밋으로 낸다. `process_text_input_key`의 읽기 전용 갈래 뒤, `multiline` 갈래 앞에서 snapshot의 `clipboard_keys_to_parent`가 참이고 `secret`이 아니면 Ctrl+C·Ctrl+X·Ctrl+V(Shift 포함), Ctrl+Insert, Shift+Insert, Shift+Delete에 `nullopt`를 돌려준다. 필드는 줄 2가 `text_input_snapshot` 끝과 `text_area_config` 끝에 더하므로, 이 갈래 커밋과 `tests/key_propagation_tests.cpp` 끝의 test는 통합 때 줄 2 병합 뒤에 쌓는다. `ui_interaction.cpp`는 줄 3만 고친다.
- **같은 라운드 공용 파일**: `ui_element.h`(`interaction_snapshot` 끝 칸), `accessibility.h`(enum 끝과 `access_info` 끝 칸), `uia_provider.cpp`(`control_type_for`의 case 한 줄과 AccessKey 속성 case), `ui_element_id.h`, `android_app.cpp` 한 줄은 모두 끝에 더하거나 한 줄이다.
- **데모**: 라이브러리 커밋과 데모 연결 커밋을 나눈다. `examples/demo_main.cpp`는 여러 줄이 고치므로 데모 연결은 통합 때 직렬로 쌓는다.
- 통합 때 [keyboard-shortcut-design.md](keyboard-shortcut-design.md)의 수정자 문장에 `alt_tap` 예외를, [popup-anchor-design.md](popup-anchor-design.md)에 막대 앵커 한 단락을, `docs/components.md`에 한 행을 더한다.

## 확인

- `tests/menu_bar_tests.cpp`(luil_core_tests):
  - 항목 폭과 "(F)" 덧붙임, 배율 2의 폭, `item_bounds`가 자식 bounds와 같은지, 넘친 항목이 잘리는지;
  - 클릭·hover_region·접근 키가 표의 상태를 내는지, `set_state`가 없으면 눌리지 않는지;
  - controller에 키를 넣어 `alt_tap`·F10·Alt+글자·←/→·Home/End·↓·Esc 단계·글자 키가 표대로인지, `f10_activates`가 거짓이면 F10이 앱 정책까지 가는지;
  - 열린 메뉴 깊이 0의 ←/→ 전환, →가 하위 메뉴를 먼저 여는지, 키보드로 연 메뉴만 첫 항목을 강조하는지, 메뉴 안 접근 키의 실행과 여럿일 때의 강조 이동;
  - 선 막대 밖 누름·창 비활성이 `{}`를 더하는지, 가둠 밖 막대가 키를 받지 않는지, `menu_keys`와 글자 입력 버림;
  - 접근 info(`menu_bar`, 항목 `expanded`, `access_key` "Alt+F")와 expand·collapse 절대 메시지, `menu_bar_access_conflicts`.
- `tests/surface_input_tests.cpp`(luil_tests): 무장·풀기(반복·Ctrl·다른 키·마우스), `alt_tap_tracker::cancel` 뒤 뗌에 `alt_tap`이 없는지, 뗌의 `alt_tap`, 막대가 있을 때의 `WM_SYSKEYUP`·`WM_SYSCHAR` 삼킴과 Space 넘김, `menu_keys`의 글자 키.
- `tests/key_propagation_tests.cpp`(luil_core_tests): `clipboard_keys_to_parent`가 참인 칸에서 Ctrl+C·Ctrl+X·Ctrl+V(Shift 포함), Ctrl+Insert, Shift+Insert, Shift+Delete가 `nullopt`로 칸 밖에 닿는지, `secret` 칸과 플래그가 꺼진 칸은 지금 길 그대로인지, 읽기 전용 칸의 Ctrl+X·Ctrl+V 삼킴이 그대로인지. 줄 2의 필드 커밋 뒤 통합 때 끝에 쌓는다.
- `tests/uia_provider_tests.cpp`(luil_tests): MenuBar ControlType, 항목의 ExpandCollapse와 Invoke 없음, AccessKey 속성.
- `tests/keyboard_shortcut_tests.cpp`: `alt_tap`의 라벨이 비어 이름 키 수가 89 그대로인지.
- `tests/raster_draw_tests.cpp`: 밑줄이 `keyboard`일 때만 그려지는지, 열린 항목의 눌림 바탕.
- 데모: `examples/demo/shell_menu.cpp·.h`(새 파일)가 파일(F)·편집(E)·보기(V)를 짓는다. 보기에는 사이드바 체크와 하위 메뉴 "테마"(밝게·시스템·어둡게)를 둔다. `demo_main.cpp`가 caption 아래(caption이 아래면 창 위)에 막대 줄을 놓고 본문을 그만큼 내린다. 막대 오른쪽 label에 "메뉴 막대: 쉼", "파일 · 키보드", "편집 열림 · 포인터"와 "마지막 메뉴 명령: …"을 그려 UIA가 되읽는다. `tests/demo_gallery_tests.cpp`가 막대 조립을, `tests/mobile_demo_page_tests.cpp`가 모바일 첫 화면에 `menu_bar` kind가 없음을 잠근다. 격리 데스크톱에서 luil-uia로 항목을 이름으로 Expand하고 메뉴 항목을 Invoke하며, PostMessage 키로 Alt 뗌·←/→·Esc를 보내 label을 되읽는다.
- 사람 확인: 실제 키보드로 Alt 단독·Alt+F·F10·방향키·Esc 단계, 경고음과 시스템 메뉴 모드가 없는지, 한국어 입력기를 켠 텍스트 칸에서 Alt+F와 메뉴 안 글자, 마우스로 열린 항목 다시 누르기와 hover 전환, 배율 100·150·200%와 고대비, Narrator가 "메뉴 막대"와 "파일, 메뉴 항목, 접힘, Alt+F"를 읽는지.

## 범위 밖

- caption 안 자리(G3의 다른 길). 항목에 `set_caption_client(true)`를 다는 길은 열어 둔다.
- 넘친 항목의 줄 바꿈·더 보기 단추, 항목 사용자 지정.
- 열린 메뉴 안 강조의 UIA 초점 알림. 컨텍스트 메뉴를 포함한 메뉴 전체의 일이다.
- 시스템 설정(SPI_GETKEYBOARDCUES)을 따른 상시 밑줄과 Alt를 누르는 동안의 밑줄 미리 보기.
- 데스크톱 명령표를 `app_bar_element` 넘침 메뉴로 접는 helper.
- 다른 프로그램이 건 Alt 조합. 그 글자 키는 OS가 가져가 이 창에 알림이 없어 Alt 단독으로 읽힐 수 있다. Win32 기본 메뉴도 같다고 알려져 있다(사람 확인). 이 앱의 전역 단축키는 위의 `cancel_alt_tap()`이 푼다.
