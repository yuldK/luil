# 묶음 안 Tab과 열린 popup 다시 누르기

메뉴 막대([menu-bar-design.md](menu-bar-design.md)) 앞에 controller 한계 둘을 고친다. X-3은 묶음의 대표가 아닌 구성원에 초점이 있을 때 Tab이 순서의 처음으로 가는 것, X-4는 popup이 열린 동안 그것을 연 요소를 다시 누르면 닫힌 frame이 게시된 뒤 뗌에서 다시 열리는 것이다. 사용자가 R3 줄 3에서 menu_bar 앞에 함께 고치기로 승인했다. 메뉴 막대가 같은 함수(초점 이동, 누름과 뗌의 클릭)를 쓰기 때문이다. 공개 형식은 [ui_tree.h](../include/luil/ui/ui_tree.h)의 `ui_tree::focus_stop_of` 하나이고 나머지는 controller 동작이다.

## 문제

**X-3. 묶음 안에서 Tab이 처음으로 간다.**

- `ui_tree::focus_order`는 `collect_focus_stops`로 훑으며 묶음(`focus_group() != none`)을 `group_entry` 하나로 접는다([ui_tree.cpp](../src/ui/ui_tree.cpp)). 대표는 `focus_entry`가 가리키는 항목이고, 없거나 받을 수 없으면 첫 항목이다.
- `interaction_controller::move_focus`([ui_interaction.cpp](../src/ui/ui_interaction.cpp))는 그 순서에서 `snapshot_.focused`를 `std::find`로 찾는다. 대표가 아닌 구성원은 순서에 없어 찾지 못하고, `next`가 처음(Tab) 또는 끝(Shift+Tab)에 남는다.
- 분할 버튼의 화살표, `choice_group_element`의 고르지 않은 칸, 탭 막대에서 화살표로 옮긴 탭이 모두 그렇다. [split-button-design.md](split-button-design.md)가 "controller의 몫이라 이 노드에서 고치지 않는다"로 남겼다.

**X-4. 열린 popup의 여는 요소를 다시 누르면 다시 열린다.**

- Win32 표면은 누름마다 `dismiss_popups(popup_dismiss_reason::pointer_press_outside)`를 내고 클릭은 그대로 진행한다([window_surface.cpp](../src/win32/window_surface.cpp)의 `WM_LBUTTONDOWN`). Android도 overlay 밖 누름에서 같은 계기를 낸다([android_app.cpp](../src/android/android_app.cpp)).
- 클릭은 뗄 때의 tree에서 액션을 꺼낸다. 마우스는 `process_release`가 `hit_test`로 찾은 요소의 `run_trigger`이고, 터치는 `finish_touch_tap`이 `live_touch_target`으로 찾은 요소다.
- 누름과 뗌 사이에 `open == false` frame이 게시되면 클릭이 build 때의 `open`(거짓)의 반대인 `set_open(true)`를 낸다. 닫으려던 누름이 도로 연다. `tests/split_button_element_tests.cpp`의 "Locks the current behaviour of re-pressing the arrow once the closed frame is published"가 지금 동작을 잠갔다.
- 드롭다운·분할 버튼·메뉴 막대 항목처럼 절대 메시지로 여닫는 요소에 공통이다.

## 공개 형식

```cpp
// include/luil/ui/ui_tree.h (focus_group_of 옆)
// 이 자리를 Tab 순서에서 대신하는 자리다 (`focus_order`가 접은 것과 같은 규칙).
//  - 묶음 안이면 그 자리를 접은 가장 바깥 묶음의 대표다. 묶음 밖이면 자리일 때 자신이고, 자리가 아니면 nullopt다.
//  - 가둠이 있으면 가둠 안의 묶음만 센다. 가둠 자신은 접히지 않고, 가둠 밖의 자리는 nullopt다 (`focus_order`와 같다).
//  - 보이지 않거나 tree에 없으면 nullopt다.
[[nodiscard]] std::optional<ui_element_id> focus_stop_of(const ui_element_id& id) const;
```

controller에는 공개 형식이 늘지 않는다. 비공개 상태에 두 칸이 더해진다. 마우스 누름의 `pressed_open_tree_`와 터치 접촉의 `touch_contact::open_tree`다(아래 X-4, 결정 3).

## 규칙

**X-3. Tab은 지금 자리를 대신하는 자리에서 센다.**

- `move_focus`는 `std::find`가 실패하면 `tree->focus_stop_of(snapshot_.focused)`로 다시 찾는다. 찾으면 그 자리에서 한 칸 옮긴다. 그래도 순서에 없으면(앱 정책 `order_focus`가 걸렀다) 지금처럼 끝에서 시작한다.
- 훑기의 시작점은 `focus_order`와 같다. 가둠이 있으면 가둠에서, 없으면 root에서 내려간다. 가둠 밖의 자리는 순서에 없으므로 nullopt다.
- 가장 바깥 묶음인 까닭은 `collect_focus_stops`가 처음 만난(가장 바깥) 묶음에서 멈추고 안쪽 묶음까지 함께 접기 때문이다.
- 판정은 `ui_tree` 안에 둔다. 접기 규칙이 `collect_focus_stops`·`group_entry`와 한 파일에 서야 둘이 어긋나지 않는다. controller가 `ancestry_of`와 `within`으로 다시 짜면 이름 없는 컨테이너(빈 owner)가 여럿일 때 `find`가 엉뚱한 것을 답한다.
- Tab은 묶음을 떠나 다음 자리로, Shift+Tab은 앞 자리로 간다. 묶음 안의 다른 구성원으로 가지 않는다. 묶음은 Tab에서 한 자리라는 기존 규칙([focus-group-design.md](focus-group-design.md)) 그대로다.

**X-4. 펼친 요소의 클릭은 누를 때의 액션이다.**

- 누를 때 hit 요소가 `left_click` 액션을 갖고 `accessibility().expanded`가 참이면 누른 표면의 tree(`shared_ptr<const ui_tree>`)를 누름 상태에 쥔다. 마우스는 `process_press`의 왼쪽 누름(`pressed_open_tree_`), 터치는 접촉을 만들 때(`touch_contact::open_tree`)다. 액션이 없으면 접근 정보를 읽지 않는다.
- 액션만 떼어 쥐지 않는다. 드롭다운·묶음 상자의 클릭은 요소를 가리키는 람다(`[this, target]`)이고 메뉴 항목은 메뉴 설정을 가리키는 포인터를 쥔다. 누르는 사이 `set_tree`가 옛 tree를 놓으면 그 요소가 사라져 쥔 액션이 죽은 요소를 부른다(결정 3).
- 뗌의 판정은 지금과 같다. 뗌 tree에서 같은 id 위에서 뗐는지, 활성인지, 끌기·길게 누르기가 아닌지를 본다. 클릭이 서면 쥔 tree에서 같은 id의 요소를 찾아 그 `left_click` 액션을 낸다. 문맥(자리·수식 키)은 뗌의 것이다. 더블 클릭 판정은 그대로다.
- 터치에서 숨은 부품(`hover_only`) 대신 조상이 탭을 받으면 누른 대상이 아니므로 뗌 tree의 액션이다.
- 쥔 tree의 액션은 열린 tree가 지은 것이라 `set_open(false)`다. 바깥 누름의 `dismiss`도 `set_open(false)`이고 절대 메시지라 둘이 함께 와도 결과는 닫힘이다. frame이 게시되었든 아니든 같다.
- 마우스 쪽은 `clear_press`가 지운다(`cancel_pointer_gesture`·드롭 유실도 그것을 부른다). 뗌은 판정 앞에서 `clear_press`를 부르므로 그 전에 지역으로 옮긴다. 터치 쪽은 접촉과 함께 버려진다. 탭 확정 앞에서 `clear_press`가 불리므로 접촉에 둔다. 누름 하나에 요소 하나라 쥐는 tree도 하나다.
- 펼침을 알리지 않는 요소의 클릭은 지금처럼 뗌 tree의 액션이다. 누르는 동안 바뀐 상태로 실행하는 지금의 계약을 넓게 바꾸지 않는다.
- 하위 메뉴가 열린 메뉴 항목도 `expanded`를 알린다(`menu_element.cpp`). 그 항목의 클릭은 `open_submenu(key)`라 누를 때의 것이어도 같은 메시지다. 펼친 묶음 상자 머리(`group_element`)도 `expanded`를 알리고 클릭은 누를 때의 `set_collapsed(true)`다. 나무 행의 펼침 칸도 절대 메시지면 같다. 토글 메시지를 쓰는 앱 요소는 누를 때의 상태로 뒤집으므로 이것이 맞다.

## 결정

**1. X-3을 어디서 푸는가.** 질문: 대표가 아닌 구성원을 순서의 자리로 옮기는 판정을 어디에 두는가.

- 길 a: `ui_tree::focus_stop_of` 공개 함수. 되는 것: 접기 규칙과 같은 파일이라 한 곳에서 바뀐다. 분할 버튼·탭 막대·검사기도 같은 답을 쓴다. 대가: `ui_tree.h`에 공개 함수 하나가 는다.
- 길 b: controller 안에서 `ancestry_of`로 가장 바깥 묶음을 찾고 `within`으로 순서의 항목을 맞춘다. 되는 것: 공개 형식이 늘지 않는다. 대가: 접기 규칙이 두 곳에 서고, 빈 owner 컨테이너에서 `within`의 `find`가 다른 요소를 답할 수 있다.
- 권장: a. 대가: 공개 함수 하나다. 권장대로 진행.

**2. X-4를 어떻게 막는가.** 질문: 닫으려던 누름이 다시 여는 경쟁을 어떤 규칙으로 막는가.

- 길 a: 누를 때 펼친 요소만 누를 때의 액션을 쓴다. 되는 것: 드롭다운·분할 버튼·메뉴 막대·색 고르개가 한 규칙으로 풀린다. 펼침을 알리지 않는 요소의 클릭은 그대로다. 대가: 누름 상태에 한 칸이 늘고(결정 3으로 tree 한 칸이다), 누를 때 `accessibility()`를 한 번 부른다.
- 길 b: 모든 클릭이 누를 때의 tree(`shared_ptr<const ui_tree>`)를 쥐고 그 요소의 액션을 쓴다. 되는 것: 규칙이 더 단순하다. 대가: 누르는 동안 바뀐 상태로 실행하던 모든 클릭의 뜻이 바뀐다. 누름 상태가 tree 한 벌을 붙잡는다.
- 길 c: 누를 때 펼쳐 있던 요소가 뗌 tree에서 접혀 있으면 클릭을 삼킨다. 되는 것: 액션을 쥐지 않는다. 대가: hover 시계로 하위 메뉴가 닫힌 뒤의 부모 항목 클릭처럼 열어야 할 누름까지 삼킨다.
- 길 d: 플랫폼이 popup을 닫은 누름은 클릭을 막는다. 되는 것: Win32 기본 메뉴의 관례와 같다. 대가: popup이 열린 동안 다른 단추를 누르면 그 단추도 먹지 않는다. "popup 닫힘은 클릭을 막지 않는다"는 지금 계약(window_surface.cpp 주석)을 뒤집고 두 host를 고친다.
- 권장: a. 대가: 위의 한 칸과 누름마다 펼친 요소의 접근 정보를 읽는 값이다. 권장대로 진행. 고침은 사용자가 승인했다(R3 줄 3). 길 a는 `expanded`를 알리는 요소의 클릭만 바꾸고 나머지 클릭은 그대로다.

**3. 누를 때의 액션을 어떻게 쥐는가.** 구현에서 정했다. 질문: 결정 2의 길 a가 쓸 "누를 때의 액션"을 누름과 뗌 사이에 무엇으로 들고 있는가.

- 길 a: 펼친 요소를 누른 동안만 누른 표면의 tree(`shared_ptr<const ui_tree>`)를 쥐고, 뗌에서 같은 id의 요소를 찾아 그 액션을 낸다. 되는 것: 요소를 가리키는 람다와 포인터가 tree와 함께 살아 있다. 누르는 동안 tree 한 벌을 쥐는 선례(`tooltip_pressed_content_`)가 있다. 대가: 펼친 요소를 누른 동안 옛 tree 한 벌이 메모리에 남는다.
- 길 b: `left_click` 액션(`ui_action`)만 복사해 쥔다(처음 문서의 모양). 되는 것: 쥐는 값이 작다. 대가: 드롭다운·묶음 상자의 람다가 `this`를, 메뉴 항목이 설정 포인터를 쥐어 옛 tree가 놓이면 죽은 요소를 부른다. 고칠 경쟁이 바로 옛 tree가 놓이는 경우다.
- 길 c: 누를 때 액션을 실행해 나온 `input_action`만 쥔다. 되는 것: 아무것도 가리키지 않는다. 대가: 앱 람다의 부작용이 누를 때 일어나고, 뗌이 다른 자리에서 끝나 클릭이 서지 않아도 이미 일어났다. 문맥(뗌의 자리·수식 키)도 누를 때의 것이 된다.
- 길 a로 했다. 결정 2의 길 b(모든 클릭이 tree를 쥔다)와 달리 펼친 요소의 누름만 쥐고 나머지 클릭의 뜻은 그대로다.

## 플랫폼

- **Win32**: 고치는 곳은 controller와 `ui_tree`뿐이다. `window_surface.cpp`의 누름 닫힘은 그대로다.
- **Android**: 같은 controller라 터치 탭(`finish_touch_tap`)과 마우스 모두 받는다. overlay 밖 누름의 닫힘(`android_app.cpp`)은 그대로다. Java 소스는 없다.

## 다른 노드와의 약속

- **menu_bar(같은 줄, 뒤)**: 막대 항목이 `expanded`를 알리므로 열린 항목을 다시 누르면 X-4 규칙으로 닫힌다. 막대 항목은 Tab 자리가 아니라 X-3과는 무관하다.
- **split_button(R1)**: [split-button-design.md](split-button-design.md)의 Tab 문단과 "열린 동안 필드를 누를 때" 문단은 구현 커밋에서 "고쳤다"로 바꾼다. 잠근 test는 `set_open(false)`를 기대하도록 뒤집는다.
- **color_picker·date_picker(줄 1, R3·R4)**: 필드가 `expanded`를 알리면 따로 할 일이 없다. controller를 고치지 않는다.
- **teaching_tip(R4)**: 같은 controller 파일이다. 이 고침이 들어간 뒤에 시작한다.

## 확인

- `tests/ui_interaction_tests.cpp`(luil_core_tests):
  - 단추 A, 가로 묶음(대표는 첫 칸), 단추 B를 두고 화살표로 둘째 칸에 선 뒤 Tab이 B, Shift+Tab이 A에 서는지;
  - 묶음 안의 묶음에서도 바깥 묶음의 다음 자리로 가는지, 가둠 안에서도 같은지;
  - `order_focus`가 묶음 대표를 거른 앱에서는 지금처럼 끝에서 시작하는지.
- 같은 파일: `focus_stop_of`가 자리 자신, 대표, 바깥 묶음 대표, 숨은 가지의 nullopt를 답하는지.
- `tests/split_button_element_tests.cpp`(luil_core_tests): 잠근 test를 뒤집는다. `open == true` tree에서 누르고 `open == false` tree를 게시한 뒤 떼면 `set_open(false)`다. 게시가 없을 때의 `set_open(false)`는 기존 test가 잠근다. 닫힌 화살표를 누르는 사이 열린 tree가 게시되면 지금처럼 뗌 tree의 것인지도 본다. 화살표에서 Tab·Shift+Tab이 다음 단추로 가고, 열린 동안이면 떠남 훅이 닫는지를 더한다.
- `tests/dropdown_element_tests.cpp`(luil_core_tests): 드롭다운도 같은 경쟁에서 닫히는지, 게시가 없어도 닫히는지, 닫힌 칸은 여는지. 마우스와 터치 탭 둘 다다.
- `tests/submenu_tests.cpp`(luil_core_tests): 하위 메뉴를 연 항목의 클릭이 그대로 `open_submenu`인지.
- 데모: 기존 popups 페이지의 분할 버튼·드롭다운이 시연이다. "저장 메뉴 열림" label을 UIA로 되읽으며 격리 데스크톱에서 화살표를 Expand한 뒤 Collapse한다(화살표는 `expanded`를 알려 Invoke가 아니라 ExpandCollapse로 선다). 다시 누르기의 경쟁 자체는 합성 입력으로 재현하기 어려워 사람 확인이다.
- 사람 확인: 실제 마우스로 열린 드롭다운·분할 버튼 화살표를 빠르게 다시 눌러 닫히는지, 실기기 터치로 같은 동작, 키보드로 분할 버튼 화살표에서 Tab이 다음 단추로 가는지.

## 범위 밖

- Tab이 묶음 안 구성원을 차례로 도는 동작. 묶음은 한 자리라는 기존 규칙을 바꾸지 않는다.
- 펼침을 알리지 않는 앱 요소의 같은 경쟁. 앱이 `expanded`를 알리면 규칙을 받는다.
- popup 위의 popup을 누를 때의 닫힘 순서. 지금 host 규칙 그대로다.
