# 여러 top-level window

Application은 `ui_frame::windows`에 secondary top-level window를 publish한다. 각 `ui_window`는 자체 tree, custom caption, renderer, DPI, input state, text-input session을 가진 창을 설명한다. 기본은 주 창이 소유하는 도구·문서 창(owned window)이고, `independent`를 켜면 주 창에 매이지 않는 창이 된다 — 브라우저나 코드 편집기의 창 하나처럼 작업 표시줄에 따로 서는 창이다.

내부 끌기는 창을 넘는다. 한 창에서 시작한 끌기를 다른 창의 tree에 놓을 수 있고, 받는 대상 없이 놓으면(창 밖) 끌기를 시작한 쪽이 화면 좌표와 함께 그 사실을 받는다. 이 둘과 화면 좌표로 창을 세우는 길을 합치면 탭을 창 밖으로 끌어내 새 창으로 떼어 내고, 다른 창의 탭 막대로 끌어 넣어 합칠 수 있다([탭을 떼어 내고 합치기](#탭을-떼어-내고-합치기)). 탭이 어느 창에 속하는지와 창마다 tree를 짓는 일은 앱의 몫이다.

공개 frame 계약은 [`include/luil/app/app_host.h`](../include/luil/app/app_host.h)에 있다. Platform 동작은 [`src/win32/secondary_surface.h`](../src/win32/secondary_surface.h)와 [`src/win32/win32_window.cpp`](../src/win32/win32_window.cpp)의 reconciliation code에 구현되어 있다.

## Window publish

```cpp
luil::ui_window tools {};
tools.id = u8"tools";
tools.caption.title = u8"Tools";
tools.x = 40.0f;
tools.y = 40.0f;
tools.width = 480.0f;
tools.height = 640.0f;
tools.minimum_width = 280.0f;
tools.minimum_height = 240.0f;
tools.tree = tools_tree;
tools.metrics = [](float width, float height, float scale) {
    return luil::app_message { tools_resized { width, height, scale } };
};
tools.close = [] {
    return luil::make_app_action(close_tools {});
};
frame.windows.push_back(std::move(tools));
```

`id`는 안정적인 application key이며 popup과 WebView anchor가 사용하는 surface namespace를 공유한다. 한 frame 안에서 고유해야 한다. Width 또는 height가 0 이하면 window가 없는 것으로 취급한다.

Platform은 frame마다 published id 목록과 live window를 맞춘다.

- 새 id는 window를 만든다.
- 기존 id는 최신 tree, caption, callback, minimum size를 적용한다.
- frame에서 사라진 id는 window를 파괴한다.

Id가 계속 publish되는 동안 creation failure를 기억해 frame마다 다시 시도하지 않는다. Id를 제거한 뒤 다시 publish하면 새 생성을 시도할 수 있다.

## 소유된 창과 독립 창

기본(`independent == false`) secondary window는 main window를 owner로 둔 Win32 owned window다. Owner 위에 유지되고, owner가 최소화되면 함께 숨고, owner와 함께 파괴되지만 독립적으로 이동하고 resize할 수 있다. 도구 창·검사 창처럼 주 창에 딸린 창이다.

`independent`를 켜면 owner가 없는 최상위 창으로 만든다(`WS_EX_APPWINDOW`).

- 작업 표시줄에 따로 서고 Alt+Tab에 따로 보인다.
- 따로 최소화된다. 주 창이 최소화돼도 그대로 보인다.
- 주 창 위에 붙어 있지 않다. Z 순서는 사용자가 정한다.
- 그 밖은 소유된 창과 같다. 열림·닫힘은 frame의 목록이 정하고(빼면 사라진다), 같은 입력 pump·renderer 정책·popup 앵커·파일 drop을 쓴다. 주 창이 실제로 파괴되면(앱이 끝나면) 함께 정리된다.

둘은 별도의 창 종류가 아니라 `ui_window`의 선택 필드 하나다. 갈리는 것이 생성 인자(owner와 확장 스타일) 둘뿐이고, 표면·입력·캡션·배치 보고는 한 벌이라 종류를 나누면 같은 코드가 두 벌이 된다. 값은 만들 때 정한다 — 살아 있는 창의 소유 관계를 바꾸지 않으며, 바꾸려면 id를 빼고 다시 싣는다.

## 초기 배치

`x`, `y`는 main window client origin 기준 logical offset이며, `width`, `height`는 요청한 logical client size다. 이 값들은 생성할 때만 사용한다. 이후 배치는 사용자 이동과 resize가 소유하며 같은 id를 다시 publish해도 원래 위치로 돌아가지 않는다.

초기 rectangle은 main window의 현재 scale로 변환하고 client bounds에서 window bounds로 조정한 뒤 가장 가까운 monitor work area에 제한한다. 생성 뒤에는 해당 window가 위치한 monitor의 DPI를 따른다. `minimum_width`, `minimum_height`는 live policy이며 이후 Win32 minimum-size query에 최신 값이 쓰인다.

자리를 정하는 길이 둘 더 있다. 우선순위는 `initial_placement` → `screen_anchor` → `x`·`y`다.

- **화면 닻(`screen_anchor`).** 새 창 client의 논리 점 (`client_x`, `client_y`)이 화면의 물리 점 `screen`에 오게 놓는다. 끌어 낸 탭을 놓은 자리에 그 탭이 그대로 오는 새 창이 이것이다. 논리 점과 크기를 물리로 바꾸는 배율은 **그 화면 점이 있는 모니터의 것**이라, 앱은 모니터 배치도 배율도 모른 채 "탭이 이 점에 오게"만 말한다. 그 뒤 window bounds로 조정해 그 모니터의 work area에 제한하는 것은 다른 초기 배치와 같다 — 밀린 만큼 점은 어긋난다. 화면 좌표는 끌기가 끝날 때 함께 온다(`drag_release::screen`).
- **저장 배치(`initial_placement`).** 앱이 `placement`로 받아 저장해 둔 값을 그대로 넣는다. 규약은 주 창의 `window_config::initial_placement`와 같다(x·y는 GetWindowPlacement의 work area 좌표, 크기는 물리 픽셀의 창 사각형). 잘못된 값, 32-bit 범위를 넘는 값, 어느 모니터에도 걸치지 않는 배치, 그 모니터 배율의 최소 크기보다 작은 배치는 통째로 무시하고 다음 길로 물러선다. `maximized`면 그 사각형 위에서 최대화해 돌아갈 자리가 그대로 남는다. `fullscreen`은 보지 않는다.

표면 좌표와 화면 좌표의 변환 함수를 따로 주지 않는다. 앱에 필요한 화면 좌표는 놓은 자리 하나이고 그것은 끌기가 실어 준다. 변환을 주면 앱이 모니터 배율을 알아야 하는데, 그 배율은 창을 세울 모니터가 정해진 뒤에야 UI thread가 안다.

## Metrics와 tree layout

Platform은 생성 뒤와 client size 또는 DPI 변경 뒤에 `metrics(width, height, scale)`을 호출한다. 크기는 logical client pixel이고 `scale`은 logical pixel당 physical pixel 수다. 반환한 application message는 정상 logic-thread inbox로 간다.

Application은 이 event로 surface용 새 tree를 arrange하고 이후 frame에 publish한다. 그때까지 이전 tree와 원래 scale은 일관된 쌍으로 유지된다. Platform은 published tree를 변경하거나 다시 layout하지 않는다.

Secondary tree는 해당 window의 client origin에서 시작한다. 일반 custom-caption interaction을 사용하면 위쪽에 `caption_element`를 포함해야 한다. `ui_window::caption`은 이에 맞는 native title, icon, tooltip, button policy, non-client metrics를 제공한다. 두 값은 같은 caption을 설명해야 한다.

## 닫기

Caption close button과 Alt+F4는 `ui_window::close`를 호출한다. UI thread는 반환된 action을 dispatch하지만 window를 직접 파괴하지 않는다. Application logic이 다음 frame에서 id를 빼면 닫힌다.

`close`가 비어 있으면 닫기 요청을 무시한다. Close callback은 published frame당 최대 한 번 발생하며 window를 다시 publish하면 재활성화된다. 반복 system message가 application을 flood하지 않으면서 열린 상태를 application이 결정하게 한다.

## 배치 보고와 복원

`ui_window::placement`를 달면 그 창의 배치를 앱 메시지로 받는다. 만든 직후, 이동·크기 조절이 끝났을 때(`WM_EXITSIZEMOVE`), 최대화 ↔ 복원이 바뀌었을 때다. 값은 주 창의 `window_delegate::make_window_placement_message`와 같은 `window_placement`이고(최대화·최소화 중에도 복원 사각형), 앱은 그것을 저장했다가 다음 실행의 `initial_placement`로 되돌린다. 비어 있으면 알리지 않는다.

소유된 창과 독립 창이 함께 받는다. 예전에는 "보조 창의 자리는 만들 때 한 번이고 그 뒤로는 사용자의 것이라 저장하고 되돌릴 주체가 없다"고 보아 주 창만 받았다. 창을 떼어 내고 합치는 앱에서는 앱이 바로 그 주체다 — 창마다 탭 목록과 배치를 저장하고 다음 실행에서 같은 창들을 같은 자리에 되살린다. 소유 여부는 이 질문과 상관이 없어 둘로 가르지 않는다.

**전체 화면은 여전히 주 창의 것이다.** `ui_command::window_toggle_fullscreen`은 주 창에만 듣고 보조 창의 배치에서 `fullscreen`은 늘 거짓이다. 화면을 덮는 창이 여럿이면 어느 것이 덮는지가 앱 상태 밖에서 갈린다. Secondary window의 style은 언제나 통상 모드의 계산이다([window_mode.h](../src/win32/window_mode.h)의 `window_style_for(buttons, normal)`). 전체 화면 계약은 [Win32 창과 표면](concepts/window.md)에 있다.

| 받는 것 | 주 창 | 소유된 창 | 독립 창 |
| --- | --- | --- | --- |
| 자기 캡션·크기 조절·최대화 | 예 | 예 | 예 |
| 배치 보고 | `window_delegate` | `ui_window::placement` | `ui_window::placement` |
| 저장 배치로 생성 | `window_config::initial_placement` | `ui_window::initial_placement` | `ui_window::initial_placement` |
| 실행 중 배치 적용 요청 | `ui_frame::window_placement_request` | 아니오 | 아니오 |
| 전체 화면 | 예 | 아니오 | 아니오 |
| 작업 표시줄·따로 최소화 | 예 | 아니오 | 예 |

## 주 창의 닫힘

주 창은 앱 하나의 수명을 쥔다 — 입력 pump, logic thread, composition device, 깨우기 신호가 거기 매여 있다. 그래서 독립 창이 남아 있어도 주 창을 파괴하지 않는다. 대신 주 창의 닫힘을 앱이 받아 정한다.

- `ui_frame::close`가 비어 있으면 지금처럼 앱을 끝낸다. 다른 창도 함께 사라진다.
- 있으면 주 창의 닫힘 요청(캡션 close, Alt+F4, 작업 표시줄 닫기, `ui_command::window_close`)에서 그 동작만 내고 아무것도 닫지 않는다. 보조 창의 `close`처럼 게시된 frame당 한 번이다.
- 앱은 나머지 창을 남기려면 `ui_frame::main_hidden`으로 주 창을 숨긴다. 숨은 주 창은 작업 표시줄에서도 빠지고, 앱은 독립 창만으로 계속 돈다. 주 창이 소유한 창과 주 창에 앵커한 popup은 함께 숨지 않으므로 같은 frame에서 뺀다. 다시 보이려면 값을 거짓으로 돌린다.
- 끝내려면 `ui_command::application_exit`를 낸다. 닫힘 훅을 거치지 않는 유일한 길이다. 숨은 주 창으로 돌다가 마지막 독립 창을 닫을 때 이것을 내는 것도 앱의 몫이다 — 내지 않으면 보이는 창 없이 프로세스가 남는다.
- 세션 종료(로그오프·재시작)는 훅을 거치지 않고 끝낸다. 저장은 지금처럼 종료 메시지가 한다.

닫힘을 "주 창을 파괴하고 다른 창을 주 창으로 올린다"로 풀지 않는 이유: 주 창의 HWND가 바뀌면 그 위에 선 깨우기 신호·타이머·클립보드 감시·composition target을 모두 옮겨야 하고, 그 사이에 오는 신호가 사라진다. 숨기기는 그 어느 것도 옮기지 않는다.

시간 예고(caret 깜빡임 등)의 사슬은 보이는 표면이 하나라도 있으면 잇는다. 예전에는 주 창이 최소화되면 놓았는데, 독립 창은 주 창과 따로 보이기 때문이다.

## Rendering과 DPI

각 top-level surface는 자체 swap chain 또는 CPU bitmap을 소유하고 `window_config`의 renderer policy를 사용한다. `automatic`은 해당 surface에서 Direct3D를 시도한 뒤 CPU로 fallback한다. Runtime renderer loss도 같은 fallback 경로를 쓴다.

Direct3D surface는 application의 DirectComposition device를 공유하지만 각 window는 자체 composition target과 visual을 가진다. Surface별 DPI는 input, caption metrics, tree arrangement, text geometry, WebView scale에 쓰인다.

## Input, focus, IME

Pointer, wheel, key, focus, file drag, accessibility event에는 surface id가 붙는다. Main-window event와 같은 input pump로 들어가며 해당 surface의 tree에서 hit testing한다.

Hover, pressed state, logical focus, menu highlight, drag origin, popup association은 surface별로 해석한다. Surface가 사라지면 그 surface의 gesture state는 다른 window로 옮기지 않고 취소한다. 내부 끌기만은 표면을 넘는다 — 아래 [창을 넘는 끌기](#창을-넘는-끌기)다.

Focusable top-level window마다 TSF session 하나를 소유한다. Active surface만 TSF에 text target을 알려 thread manager focus를 두고 session끼리 경쟁하지 않게 한다. Popup은 activate하지 않고 anchor surface의 text session을 빌린다.

시간 기반 paint도 surface identity를 지킨다. Platform은 surface별로 filter한 interaction state를 사용해 모든 tree의 `next_update`를 구하고 가장 이른 시점에 timer 하나를 건다.

## 창을 넘는 끌기

누른 채 끄는 포인터는 누른 창이 잡고 있다(Win32 마우스 캡처, 터치·펜의 암묵 캡처). 그래서 이벤트는 창 밖으로 나가도 언제나 그 창의 client 좌표로 온다. 좌표만으로는 포인터가 지금 무엇 위에 있는지 알 수 없으므로, 내부 끌기 동안에는 플랫폼이 화면에서 포인터 밑을 다시 찾아 이동·뗌 이벤트에 싣는다(`pointer_over`). 함께 화면 좌표(`screen`)도 싣는다.

- **찾는 규칙은 OS의 Z 순서다.** 그 화면 점에서 맨 위에 보이는 창(`WindowFromPoint`의 최상위 창)이 답이다. 그것이 이 앱의 표면(주 창·보조 창·popup)이면 그 표면과 그 표면의 client 좌표이고, 아니면(다른 앱이 덮고 있다, 바탕화면이다, 작업 표시줄이다) "창 밖"이다. 여러 창이 겹쳐 있으면 사용자가 보는 맨 위의 창이 이긴다 — 보조 창·popup은 그 아래의 주 창보다 먼저, 다른 앱이 덮은 우리 창은 그 다른 앱에 진다. 앱이 정한 순서나 frame의 순서를 쓰지 않는 것은 그것이 화면에 보이는 것과 어긋날 수 있어서다.
- 끌리는 그림 창(아래)은 클릭이 통과하는 창이라 이 탐색에 걸리지 않는다.
- 보이지 않는 창(test의 숨은 창)이 캡처를 쥐었으면 싣지 않는다. 화면의 점이 그 창을 가리키지 않아 엉뚱한 답이 된다. 싣지 않은 이벤트는 그 이벤트의 표면이 포인터 밑이고, 좌표가 그 표면 내용(root) 밖이면 창 밖으로 본다.

Controller는 그 자리를 끌기의 자리로 쓴다(`drag_visual`).

- `surface`는 끌기를 시작한 표면이고 그대로다. `pointer_surface`(읽을 때는 `pointer_on()`)가 포인터가 놓인 표면이며, `x`·`y`와 `hovered_drop_target`은 그 표면의 것이다. 놓을 자리는 그 표면의 tree에서 찾고, 놓으면 그 tree의 `on_drop`이 그 표면 좌표로 실행된다.
- `outside`는 포인터가 이 앱의 어느 표면 위에도 없다는 뜻이다. 그때 놓을 자리는 없고, 포인터 표면은 출발 표면이며 좌표는 그 표면 기준(창 밖의 값)이다. 가장자리 자동 스크롤이 창 밖으로 끌고 나간 목록을 계속 흘리는 것이 이 좌표다.
- 표시는 두 표면에 걸친다. `interaction_for_surface`는 포인터가 놓인 표면에 ghost와 강조를 남기고, 출발 표면에는 payload만 남겨 끌리는 항목의 흐린 표시를 유지한다(그 표면의 것은 `outside`로 걸러 ghost·강조가 서지 않는다). 둘 다 아닌 표면에는 없다.
- 커서 모양도 포인터가 놓인 창의 tree가 그 창 좌표로 답한다. 캡처를 쥔 창이 발행본이 바뀔 때 그 창의 tree에 묻는다. 창 밖이면 `drag_payload::outside_cursor`다(앱이 청하지 않으면 잡은 쪽의 모양이 이어진다).

**출발 표면이 끌기 도중 사라지면 취소다.** 그 창이 쥔 캡처가 창과 함께 풀려 뗌이 어디에 떨어질지 알 수 없고, 짐작한 자리에 놓으면 사용자가 하지 않은 놓기가 실행된다. Esc·캡처 상실과 같은 취소라 `on_drop`도 `on_unclaimed_drop`도 부르지 않는다. 마지막 탭을 끌어낸 창을 비우는 앱은 그 창을 **놓기의 메시지를 받은 뒤에** 닫는다 — 놓기가 끝난 뒤의 frame에서 빼면 아무것도 잃지 않는다. 끄는 동안 출발 창에서 그 탭을 흐리게 보이는 것(`dragged`)으로 "떠났다"는 표시는 이미 선다.

포인터가 가 있던 **다른** 표면이 사라지는 것은 끌기를 죽이지 않는다. 그 자리의 강조만 거두고 다음 이동이 새 자리를 찾는다.

### 창 밖에서 보이는 그림

포인터가 이 앱의 어느 창 위에도 없으면 창 안의 ghost는 그릴 곳이 없다. 그동안 플랫폼이 화면 위에 투명한 최상위 창 하나를 세워 끌리는 항목의 그림을 포인터 오른쪽 아래에 따라오게 한다([drag_ghost_window.h](../src/win32/drag_ghost_window.h)). 그림은 창 안의 ghost와 같은 상자에 "새 창" 글리프를 얹은 것이라(`draw_detached_drag_ghost`), 놓으면 새 창이 된다는 것을 알아볼 수 있다.

- 입력을 받지 않고(`WS_EX_TRANSPARENT`) 활성이 되지 않으며, 작업 표시줄·Alt+Tab에 서지 않는다.
- 배율은 포인터가 가리키는 모니터의 것이다. 내용·배율·외양이 바뀔 때만 다시 그리고 나머지는 옮기기만 한다.
- 출발 쪽이 자기 그림을 그리는 끌기(`custom_visual`)와 OS 파일 끌기에는 세우지 않는다.
- 끄는 동안 창이 실제로 따라오지는 않는다. 놓는 순간 앱이 새 창을 세운다.

## 받는 대상 없는 놓기

`drag_source::on_unclaimed_drop`은 받는 대상 없이 끝난 놓기를 끌기를 시작한 쪽에 알린다. 앱 메시지(이벤트)가 아니라 drag source의 콜백인 이유는 다음과 같다.

- 놓기의 의미는 **무엇을 끌었는가**가 정한다. 탭은 새 창으로 떼어 내고, 목록 행은 아무 일도 하지 않고, 파일 항목은 내보내기를 할 수 있다. payload를 짓는 자리가 이미 그 판단을 하는 자리다. 앱 전체에 한 이벤트로 주면 앱이 payload를 다시 갈라 그 판단을 되풀이한다.
- 짝인 `drop_target::on_drop`과 같은 꼴이다. 콜백은 `input_action`을 돌려주므로 app message·UI command의 thread 규칙이 그대로 선다.
- 끌기가 **시작될 때** 쥐어 둔다. 출발 element가 끄는 동안의 새 tree에서 사라져도(그 탭을 흐리게 다시 지은 tree) 값으로 담긴 factory라 그대로 산다.

인자는 payload와 `drag_release`다.

| 필드 | 뜻 |
| --- | --- |
| `surface`, `x`, `y` | 놓은 자리의 표면과 그 표면 client 좌표(물리 픽셀). 창 밖이면 출발 표면 기준 값이다. |
| `outside` | 이 앱의 어느 표면 위도 아니다 — 새 창으로 떼어 낼 자리. 거짓이면 이 앱의 창 위지만 받는 대상이 없는 자리(다른 창의 빈 곳)다. |
| `screen` | 놓은 자리의 화면 좌표. 새 창의 `screen_anchor`에 그대로 넘긴다. 모르는 플랫폼은 비운다. |
| `control`, `shift`, `alt` | 놓는 순간의 수식 키. |

부르지 않는 경우가 정해져 있다. 놓기가 아니라 **취소**인 것은 받는 대상이 없는 놓기와 구별된다.

- Esc, 캡처 상실(빼앗긴 마우스의 합성 뗌), 터치·펜의 시스템 취소, 입력 큐 유실, 출발 표면의 소멸.
- 불투명 표면(`hit_opaque` — modal scrim, 메뉴, 토스트)이 가린 자리에 놓은 것. 그 자리는 빈 것이 아니라 막힌 것이다. OS 파일 drop이 막힌 자리에서 창 전체 fallback을 부르지 않는 것과 같은 규칙이다([OS 파일 drag & drop](os-dragdrop-design.md)).
- 받는 대상 위에 놓은 것. 그것은 그 대상의 `on_drop`이다.

## 탭을 떼어 내고 합치기

`tab_bar_element`는 위의 길을 탭에 이어 둔다([tab_bar_element.h](../include/luil/ui/tab_bar_element.h)).

| 설정 | 하는 일 |
| --- | --- |
| `move` | 같은 막대 안의 순서 바꾸기. 대상 탭의 앞 반쪽이면 앞, 뒤 반쪽이면 뒤(`drop_side`)를 싣는다. 마지막 탭 뒤의 빈 곳은 마지막 탭의 뒤다. 순서가 그대로인 자리(자기 자신, 바로 앞 탭의 뒤 반쪽, 바로 뒤 탭의 앞 반쪽)는 받지 않아 강조도 서지 않는다. 받는 자리는 탭 테두리 대신 그 틈의 accent 세로 막대로 보인다. 있으면 예전의 `reorder`(moved, target) 대신 선다. |
| `receive` | 다른 막대(다른 창)에서 끌어 온 탭을 그 틈에 넣자는 메시지. payload(`dragged_owner`가 탭 키, `container`가 출발 막대)를 그대로 싣는다. 탭이 없는 막대는 막대 전체가 끝의 틈이다(`target`이 비어 있다). |
| `unclaimed_drop` | 탭을 받는 대상이 없는 자리에 놓았다는 메시지(payload + `drag_release`). 탭 키는 `payload.dragged_owner`이고 잡은 지점도 payload에 있다. 있으면 `reorder`·`move` 없이도 탭을 끌 수 있다. |
| `outside_cursor` | 창 밖으로 끌고 나갔을 때의 커서(`drag_payload::outside_cursor`). |

같은 막대의 탭인지는 막대 id와 **키가 그 막대에 있는가**로 가른다. 창마다 같은 코드로 막대를 지으면 막대 id(owner)가 같기 때문이다. 그래서 탭을 주고받는 막대들 사이에서 탭 키는 앱 전체에서 고유해야 한다.

`drop_target::accepts_at`(자리마다 받기를 가르는 판정)과 `custom_highlight`(대상이 자기 강조를 그린다)가 이 동작의 일반 부품이다. 다른 순서 있는 컨테이너도 같은 부품으로 앞·뒤 틈을 만들 수 있다.

앱의 흐름은 다음과 같다.

1. 탭을 창 밖에 놓으면 `unclaimed_drop`이 `outside == true`와 `screen`을 싣고 온다. 앱은 그 탭을 원래 창의 목록에서 빼고, 새 id의 `ui_window`(통상 `independent = true`)를 싣는다. `screen_anchor.screen`은 받은 화면 좌표이고, `client_x`·`client_y`는 새 창의 tree에서 그 탭이 설 자리에 payload의 잡은 지점(`grab_offset_x`·`grab_offset_y`, 출발 창 배율의 물리 픽셀이라 그 배율로 나눈 값)을 더한 논리 좌표다 — 그러면 잡았던 탭이 놓은 점에 그대로 온다.
2. 다른 창의 탭 막대에 놓으면 그 막대의 `receive`가 온다. 앱은 탭을 옮기고, 출발 창에 탭이 남지 않으면 그 창을 같은 frame에서 뺀다. 놓기는 이미 끝났으므로 창을 빼도 잃는 것이 없다.
3. 다른 창의 빈 곳(탭 막대 밖)에 놓으면 `unclaimed_drop`이 `outside == false`와 그 창의 표면 id를 싣고 온다. 그 창에 합칠지 무시할지는 앱이 정한다.
4. 주 창의 닫힘을 받아(`ui_frame::close`) 남은 독립 창을 살릴지, `application_exit`로 끝낼지 정한다.

```cpp
tabs.unclaimed_drop = [](const luil::drag_payload& payload, const luil::drag_release& release) {
    return luil::make_app_action(tab_released { payload.dragged_owner, payload.grab_offset_x, payload.grab_offset_y, release });
};

// logic: 창 밖에 놓인 탭을 새 독립 창으로 세운다.
if (released.release.outside && released.release.screen.has_value())
{
    luil::ui_window window {};
    window.id = new_window_id();
    window.independent = true;
    window.width = 960.0f;
    window.height = 640.0f;
    // 새 창 tree에서 첫 탭이 설 자리(논리) + 잡은 지점(출발 창 물리 ÷ 출발 창 배율)이다.
    window.screen_anchor = luil::window_screen_anchor {
        .screen = *released.release.screen,
        .client_x = first_tab_x + released.grab_offset_x / origin_scale,
        .client_y = first_tab_y + released.grab_offset_y / origin_scale,
    };
    window.placement = [id = window.id](const luil::window_placement& placement) {
        return luil::app_message { window_moved { id, placement } };
    };
    // tree·caption·metrics·close는 다른 창과 같다.
}
```

## Popup, WebView, file drop

Popup은 `ui_popup::anchor`에 secondary window id를 지정한다. Coordinate, owner, DPI, dismissal, text-input association이 해당 surface를 따른다.

WebView는 `ui_webview::anchor`에 같은 id를 지정하고 secondary tree에 대응하는 `webview_element`를 둔다. 기존 WebView의 anchor를 바꾸면 controller와 DirectComposition visual이 원래 window에 결합되어 있으므로 다시 만든다.

`window_config::accept_file_drop`이 true이면 secondary window도 자체 OLE drop target을 등록하고 자신의 tree에서 drop을 처리한다.

## 검증

[`tests/secondary_surface_tests.cpp`](../tests/secondary_surface_tests.cpp)는 live secondary surface가 최신 caption data, minimum size, callback, tree를 적용하는지 검증한다. 화면 닻이 잡은 점을 놓은 점에 놓는 계산(100%·150%·왼쪽 모니터의 음수 좌표), 독립 창의 확장 스타일, Win32 배치를 앱이 저장할 값으로 옮기는 규칙, 숨은 보조 창이 이동의 끝과 최대화 전환에 배치를 알리고 알릴 곳을 떼면 멈추는 것도 같은 파일에서 확인한다.

[`tests/ui_interaction_tests.cpp`](../tests/ui_interaction_tests.cpp)는 창 밖·창 안의 빈 자리·플랫폼이 "창 밖"이라고 한 자리에 놓은 끌기가 화면 좌표·수식 키와 함께 출발 쪽에 알려지는 것, 받는 대상 위와 불투명 판이 가린 자리에서는 알리지 않는 것, Esc·캡처 상실·입력 큐 유실이 알리지 않는 취소인 것을 확인한다. 다른 표면 위로 간 끌기가 그 표면에서 강조·놓기를 하고, 표면별 거름이 ghost와 흐린 표시를 두 창에 나누며, 포인터가 가 있던 창이 닫혀도 끌기는 남고, 출발 표면이 닫히면 놓기 없이 취소되는 것도 같은 파일이다. [`tests/tab_bar_element_tests.cpp`](../tests/tab_bar_element_tests.cpp)는 탭의 앞·뒤 자리, 순서가 그대로인 자리의 거절, 마지막 탭 뒤의 틈, 다른 막대의 탭 받기, 받는 대상 없는 놓기를 확인한다. Window reconciliation, focus routing, popup anchoring, surface input translation, renderer fallback, text-input 동작은 대응하는 Win32 component test에서 반복 검증한다.
