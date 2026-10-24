# Popup overlay 표면

`ui_popup`은 주 창이나 보조 창 위에 떠야 하는 메뉴, dropdown, 긴 tooltip을 별도 Win32 popup surface로 표현한다. Popup은 독립 tree와 렌더러를 가지지만 활성화되지 않으며 frame의 선언으로 수명이 결정된다.

공개 형식은 [app_host.h](../include/luil/app/app_host.h)에 있다. 플랫폼 표면은 [popup_surface.h](../src/win32/popup_surface.h)에 정의된다.

## Frame 선언 모델

앱의 logic thread는 `ui_frame::popups`에 원하는 popup 전체를 싣는다.

```cpp
frame->popups.push_back(luil::ui_popup {
    .id = u8"account-menu",
    .anchor = {},
    .x = button_left,
    .y = button_bottom,
    .width = 240.0f,
    .height = 320.0f,
    .tree = std::make_shared<luil::ui_tree>(std::move(menu_tree)),
    .dismiss = make_menu_dismiss(),
});
```

목록에 나타난 popup은 존재하고 목록에서 빠진 popup은 제거된다. UI thread가 popup 열림 상태를 따로 소유하지 않는다.

`id`는 비어 있지 않아야 한다. 빈 표면 id는 주 창을 뜻하며 popup과 보조 창은 같은 표면 이름 공간에서 서로 다른 id를 사용한다.

각 popup은 owner 관계로 앵커 창 위에 유지된다. Frame 배열은 기존 popup HWND의 Z-order를 재정렬하는 명령이 아니다. 동시에 겹치는 popup이 필요하면 앱은 열림 수명과 배치를 명확히 관리해야 한다.

## 별도 tree

Popup tree의 좌표계는 popup client 원점 `(0, 0)`에서 시작한다. 주 tree의 자손으로 popup 내용을 두지 않는다.

별도 tree를 사용하면 다음 경계가 명확해진다.

- popup 창 좌표로 직접 hit test한다.
- popup bounds 밖은 Win32 창 경계가 자른다.
- popup별 hover, press, focus를 surface id로 구분한다.
- popup만 다시 그릴 수 있다.
- popup 제거와 함께 해당 tree의 gesture와 focus를 정리할 수 있다.

Popup tree는 주 tree와 같이 게시 후 불변이다. `ui_tree::duplicate_ids()`와 `unarranged()`도 각 tree 안에서 검사한다.

## Win32 창 정책

Popup surface는 다음 창 정책을 사용한다.

- `WS_POPUP`
- `WS_EX_NOACTIVATE`
- `WS_EX_TOOLWINDOW`
- 앵커 최상위 창을 owner로 지정

`WM_MOUSEACTIVATE`에는 `MA_NOACTIVATE`를 반환한다. Popup을 눌러도 물리 keyboard focus는 앵커 창에 남고 논리 초점만 popup element로 이동한다.

Tool window 확장 스타일 때문에 taskbar와 Alt+Tab의 독립 창으로 나타나지 않는다. Owner가 파괴되면 운영체제 소유 관계에 따라 popup도 사라진다.

## 모서리·그림자·테두리

Popup 창은 지금 OS의 모양을 따른다. Windows 11처럼 DWM이 창 모서리 선택을 받는 판에서는 둥근 창이고, 그렇지 않으면 처음부터의 직각 창이다. 앱이 popup마다 고를 수 있고, 고르지 않으면 앱 스타일(`ui_style::popup.corner`, JSON의 `popup.corner`)이며 그 기본은 OS 기본이다.

| `popup_corner` | Windows 11 | 그 밖 |
| --- | --- | --- |
| `system` (기본) | 작게 둥글게 — OS의 메뉴·tooltip과 같다 | 직각 |
| `round` | `DWMWCP_ROUND` (8) | 직각 |
| `round_small` | `DWMWCP_ROUNDSMALL` (4) | 직각 |
| `square` | `DWMWCP_DONOTROUND` | 직각 |

규칙은 [`resolve_popup_shape`](../src/host/popup_reconcile.h) 하나에 모았다 — 플랫폼을 모르는 순수 함수라 조합을 test로 잠근다.

- **둥근 모서리.** popup 창을 만든 직후, 보이기 전에 `DWMWA_WINDOW_CORNER_PREFERENCE`를 건다. 직각을 고른 창에도 `DWMWCP_DONOTROUND`를 건다 — 꼬리 달린 말풍선처럼 모양을 스스로 그리는 popup은 OS가 popup을 둥글게 하는 판이 와도 직각이어야 한다.
- **그림자.** 직각 창은 창 클래스의 `CS_DROPSHADOW`(메뉴·tooltip이 쓰는 그 그림자)다. 둥근 창에는 쓰지 않는다 — 둥글게 잘린 창 밑에 사각 그림자가 깔려 모서리 밖으로 보인다. 둥근 창에는 DWM이 같은 모양의 그림자를 얹는다. 그림자는 창 클래스의 스타일이라 popup마다 고를 수 없으므로 **클래스를 모양별로 둘** 둔다(`.Popup`·`.PopupRounded`). 그래서 같은 id의 popup이 둥근 창과 직각 창 사이를 오가면 대조(`reconcile_popups`)가 옮기지 않고 다시 만든다 — 앵커가 바뀐 popup과 같은 판정이다(`popup_placement::rounded`). 둥근 정도만 바뀐 것(둥글게 ↔ 작게)은 같은 창에 다시 건다.
- **테두리.** `ui_popup::border`는 "경계를 둔다"이다. 직각 창은 표면이 tree를 다 그린 뒤 1px `tooltip_border` 선을 긋는다(`frame_state::border`). 둥근 창은 그 사각 선을 긋지 않는다 — OS 테두리와 겹치고 모서리에서 사각으로 튀어나온다. 대신 OS 테두리 색을 `DWMWA_BORDER_COLOR`로 같은 역할색(창 바탕 위에 얹은 불투명 결과 — DWM은 알파를 받지 않는다)에 맞춘다. 경계를 끄면 둥근 창은 `DWMWA_COLOR_NONE`으로 OS 테두리도 없앤다. 직각 창에도 OS 테두리는 `DWMWA_COLOR_NONE`이다 — 경계는 표면의 선 하나다. 고대비는 OS 색(`DWMWA_COLOR_DEFAULT`)이 맞다.
- **명암.** popup에도 `DWMWA_USE_IMMERSIVE_DARK_MODE`를 건다. 어두운 바탕이면 OS 그림자와 테두리가 어두운 모드의 것이라 어울린다. 명암은 테마 이름이 아니라 바탕의 밝기(`is_dark_background`)가 정한다 — 주 창과 같은 규칙이다. 색과 명암은 표면이 그릴 때마다 frame의 팔레트로 견주고 바뀐 때만 DWM에 말한다.
- **tree의 반지름.** popup 안의 tree가 창과 같은 반지름으로 그리려면 [`popup_corner_radius`](../include/luil/app/app_host.h)가 고른 모서리의 실제 반지름(논리 픽셀)을 답한다. 둥근 모서리를 못 쓰면 0이다. 자기 둘레에 사각 선을 긋는 tree(메뉴)는 둥근 창에서 그 선을 끈다 — `make_menu_popup`이 popup 모양에 맞춰 `menu_config::border`를 정한다.

**물러서기.** 둥근 창은 `ui_platform::rounded_popup_corners`가 참일 때만이다. Win32 host는 주 창을 만들 때 `DWMWA_WINDOW_CORNER_PREFERENCE`에 기본값을 걸어 받는 판인지 묻고(Windows 10 이하는 실패를 돌려준다), 원격 세션(`SM_REMOTESESSION`)과 꺼진 composition이 아닌지를 popup을 대조할 때마다 다시 본다. 원격 데스크톱은 DWM 효과가 꺼져 둥근 창의 그림자와 테두리가 그려지지 않으므로, 그대로 두면 클래스 그림자도 사각 선도 없는 창이 되어 경계가 사라진다 — 그래서 원격 세션은 직각 창, 클래스 그림자, 1px 선으로 물러선다. 바뀌면 플랫폼 성질을 고쳐 적어 앱의 다음 frame이 반지름 0으로 그린다. 판정이 틀려 모서리 선택이 실패하면 그 창은 직각으로 물러서 표면의 선을 다시 켠다 (그림자 없는 클래스라도 선이 경계를 맡는다).

## 렌더링

각 popup surface는 자기 `window_surface`와 renderer를 갖는다. Popup renderer는 CPU 모드로 생성된다. Popup은 보통 작고 수명이 짧으므로 HWND마다 D3D swapchain과 실패 복구 수명을 추가하지 않는다.

Tree가 바뀌면 해당 popup을 invalidate한다. 크기가 바뀌면 renderer도 새 client 크기에 맞춘다. 주 창과 popup은 같은 theme, font, interaction snapshot을 사용하지만 `interaction_for_surface()`로 각 표면의 상태만 남겨 그린다.

Popup은 caption chrome을 만들지 않는다. 포인터, 휠, 커서, tree draw 같은 공통 surface 입력과 렌더링만 사용한다.

`ui_popup::border`가 참이면(기본값) popup에 경계가 선다. 직각 창은 표면이 tree를 다 그린 뒤 둘레에 1px `tooltip_border`를 긋고(`frame_state::border`), 둥근 창은 OS 테두리가 같은 색으로 맡는다 (위 절). 다른 화면 위에 뜨는 판이라 경계가 있어야 아래 화면과 갈리고, tree 위에 긋기 때문에 가장자리까지 채운 내용에도 경계가 남는다. 경계를 일부러 지우는 popup(꼬리 달린 말풍선)만 끈다.

## 입력 합류

Popup window procedure는 포인터와 휠을 `raw_input_event`로 바꿔 같은 `app_host` input inbox에 게시한다. 이벤트에는 popup id와 popup client 좌표가 실린다.

Controller는 `surface_tree_list`에서 popup tree를 찾아 hit test하고, 결과 action을 주 tree 입력과 같은 경로로 dispatch한다. UI thread에서 앱 상태를 직접 바꾸지 않는다.

Popup은 keyboard focus를 받지 않으므로 key message는 앵커 최상위 창에서 온다. 논리 초점이 popup에 있으면 `focused_surface`가 popup tree로 라우팅한다.

## 닫힘 계약

`ui_popup::dismiss`는 `popup_dismiss_reason`을 `input_action`으로 바꾸는 factory다. 주요 reason은 바깥 포인터 누름, 휠 스크롤, Esc, 표면 이동과 크기 변경, 활성 전환이다.

UI thread는 reason을 감지해 action을 한 번 dispatch한다. Popup을 즉시 숨기지 않는다. 앱이 action을 처리하고 다음 frame에서 popup 선언을 제거하면 surface가 사라진다.

Factory가 없으면 모든 reason에서 popup을 유지한다. Factory가 빈 action을 반환하면 해당 reason만 무시한다. 어느 popup도 Esc action을 만들지 않으면 Esc는 일반 키 라우팅으로 흐른다.

같은 popup을 실은 새 frame을 받으면 dismiss 요청 표식을 풀어 이후 reason에서 다시 action을 낼 수 있다.

## DPI와 위치

Popup 좌표와 크기는 앵커 client 기준 논리 픽셀이다. UI thread는 앵커 DPI를 적용해 물리 픽셀로 만들고 화면 좌표와 작업 영역을 계산한다. Popup은 앵커 표면의 DPI를 그대로 사용한다.

앵커 이동과 DPI 변화는 popup placement를 다시 계산한다. 앵커 선택과 재생성 규칙은 [popup-anchor-design.md](popup-anchor-design.md)를 따른다.

## 창이 하나뿐인 플랫폼 (Android)

Android에는 popup마다 띄울 창이 없다. 같은 `ui_popup` 선언을 주 표면 안의 layer로 그린다. 데이터 모델(앵커 기준 논리 좌표, 분리 tree, `surface_tree_list`, 닫힘 factory)은 그대로이고 앱 코드도 같다.

- **그리기.** 앱 host가 frame이 바뀔 때마다 popup의 자리를 물리 픽셀로 옮기고 내용 영역 안으로 들인다(앱 모델의 자리는 바꾸지 않는다). [`draw_frame`](../src/host/frame_state.cpp)이 주 tree를 그린 뒤 `frame_state::overlays`를 차례로 겹친다: 아래로 번지는 그림자, 창 배경, popup tree, 테두리. 창의 OS 그림자를 표면이 대신 그리는 것이다. 표면마다 `interaction_for_surface`로 자기 상호작용만 남긴다.
- **순서.** 주 tree의 tooltip·끌기 표시는 주 tree가 그리므로 popup이 그 위를 덮는다 (계획은 tooltip·끌기를 맨 위에 두려 했다). 터치 화면에는 주 tree의 hover tooltip이 서지 않아 겹칠 일이 드물다.
- **입력.** [`overlay_input_router`](../src/host/overlay_input.h)가 포인터 이벤트를 layer로 보낸다. 누름은 그 자리의 맨 위 layer로 가고 이벤트에 popup id와 popup 좌표가 실린다. 그 접촉의 이동·뗌·취소는 밖으로 나가도 같은 layer로 간다 (Win32의 암묵적 캡처와 같다). 마우스·펜 호버가 layer를 옮기면 앞 layer에 이탈을 낸다. 키는 지나가고 논리 초점이 라우팅한다.
- **닫힘 계기.**

  | 계기 | Android |
  | --- | --- |
  | `pointer_press_outside` | 어느 popup도 아닌 곳의 누름. 누름 자체는 주 tree로 간다 |
  | `wheel_scrolled` | 어느 popup도 아닌 곳의 휠 |
  | `escape_key` | Esc 키, 그리고 뒤로 가기. popup이 떠 있는 동안 뒤로 가기는 Activity가 아니라 앱이 받고, 아무 popup도 닫지 않으면 기본 동작(Activity 끝내기)을 한다 |
  | `surface_resized` | 내용 크기가 바뀜 (회전, 소프트 키보드) |
  | `activation_changed` | 창이 초점을 잃음 (알림 창, 다른 앱) |
  | `surface_moved` | 없다 (창이 움직이지 않는다) |

  한 번 내기 규칙은 Win32와 같은 [`take_popup_dismiss_action`](../src/host/popup_dismiss.h)이고, 새 frame이 오면 표식을 푼다.
- 앵커 사슬이 주 표면에서 끝나는 popup을 부모부터 그린다. 보조 창(`ui_frame::windows`)은 지원하지 않아 한 번 경고하고 무시한다.
- popup 안 텍스트 칸에 초점이 서면 IME가 그 칸에 붙는다. 글자 자리는 popup의 표면 원점으로 옮긴다.

## 반드시 유지할 불변식

- Popup 수명은 `ui_frame::popups`가 소유한다.
- Popup마다 독립 HWND, tree, renderer가 있다.
- Popup은 활성화되지 않으며 taskbar 창이 아니다.
- 모든 입력은 공통 input inbox와 controller를 거친다.
- Popup event 좌표는 popup client 좌표다.
- Dismiss는 action만 내고 앱이 다음 frame에서 제거한다.
- 렌더러는 CPU 모드이고 DPI는 anchor를 따른다.

## 검증 지침

[overlay_input_tests.cpp](../tests/overlay_input_tests.cpp)는 layer 라우팅(맨 위 layer, 접촉의 캡처, 호버 이탈, 바깥 누름·휠)을, [raster_draw_tests.cpp](../tests/raster_draw_tests.cpp)는 layer가 자리 안에만 그려지는 것을 확인한다. [popup_reconcile_tests.cpp](../tests/popup_reconcile_tests.cpp)는 frame 목록과 surface 수명의 대조를 검증한다. 모서리 넷 × 경계 켬·끔 × OS 지원 여부의 모양(그림자 클래스, 표면의 선, OS 테두리), 둥근 창과 직각 창 사이의 재생성, 실제 반지름, 메뉴 popup이 둥근 창과 경계 끔에서 자기 선을 끄는 것도 여기서 본다. [popup_dismiss_tests.cpp](../tests/popup_dismiss_tests.cpp)는 reason별 action과 중복 억제를 확인한다. [surface_input_tests.cpp](../tests/surface_input_tests.cpp)는 popup 표면 id와 좌표 정규화를 검증한다. [raster_draw_tests.cpp](../tests/raster_draw_tests.cpp)는 `frame_state::border`가 tree 없는 frame에서도 둘레 1px을 창 안에 긋는 것을 픽셀로 확인한다.

전체 조립은 [win32_window.cpp](../src/win32/win32_window.cpp), popup 메시지 처리는 [popup_surface.cpp](../src/win32/popup_surface.cpp)에 있다.

## 메뉴 항목의 종류

`menu_item_config::kind`는 action·check·radio다. 체크와 라디오의 `checked`는 앱 상태이며,
실행 메시지는 기존 `select`가 나른다. 체크는 Toggle, 라디오는 SelectionItem을 내고 둘 다
MenuItem 역할을 유지한다. 라디오가 있는 메뉴만 명시적 선택 container가 된다.

`shortcut_text`는 오른쪽 정렬된 표시 열이다. 실행 바인딩은 하지 않는다. `width_for`는
한 행에서 필요한 라벨과 단축키 폭을 함께 재고 가장 긴 행의 폭을 돌려준다.
