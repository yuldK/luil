# macOS 창 정책

macOS host([`macos_app.mm`](../src/macos/macos_app.mm))가 Windows host와 같은 동작을 내는 자리를 모은다. 기준으로 삼은 앱은 테두리 없는 작은 도구 창을 주 창으로 쓰는 데스크톱 앱이다. 그런 앱은 늘 위에 뜨고, 바탕이 비치고, 머릿줄을 직접 그리며, 창 곁에 세부 정보 카드를 띄운다. 예전에는 그런 앱이 `window_delegate::on_native_window_created`로 받은 `NSWindow`를 직접 고쳐서 우회해야 했다. 이제는 설정(`macos::application_config`), frame(`ui_frame`), 훅(`win32::window_delegate`)으로 같은 일을 한다.

예제 [`examples/tool_window`](../examples/tool_window/main.cpp)(`luil_tool_window`)가 이 앱의 모양이다. 아래 항목은 모두 그 예제를 띄우는 창 test로 확인한다 ([검증](#검증)).

## 설정

이름과 뜻은 Windows의 `window_config`와 맞춘다. Windows에 없는 것은 macOS 이름으로 둔다.

| macOS (`application_config`) | Windows | 뜻 |
| --- | --- | --- |
| `borderless` | 주 창의 기본 모양 (custom caption) | 제목 막대와 창 단추가 없고 앱이 caption을 그린다. 가장자리 크기 조절과 최소화는 남는다 |
| `presentation = layered` | `window_config::presentation` | 창이 불투명하지 않다. 그린 알파대로 뒤가 비친다 |
| `layered_click_through` | 같은 이름 | 알파 0인 자리의 누름을 뒤 창으로 보낸다. 기본은 이 창이 받는다 |
| `corner_radius` | DWM이 정한다 | 창 내용을 그 반지름으로 둥글게 자른다. 창 그림자도 그 모양이다 |
| `topmost`, `ui_frame::topmost` | (topmost) | 다른 앱의 창 위에 늘 뜬다 (`NSFloatingWindowLevel`). frame 값으로 실행 중에 바꾼다 |
| `all_spaces` | 없다 | 모든 공간(Space)과 다른 앱의 전체 화면 위에도 선다 |
| `activation` | 없다 | Dock과 앱 전환기에 서는가다 (`automatic`·`regular`·`accessory`) |
| `inactive_hover` | 기본 동작 | 앱이 뒤에 있어도 보이는 창이 hover를 받는다 |
| `initial_position`, `initial_placement` | 같은 이름 | 처음 자리, 저장해 둔 배치로 열기 |

## 1. popup 판

popup마다 테두리 없는 판(`NSPanel`) 하나가 선다. 예전에는 앵커 창의 내용 영역 안에 겹쳐 그리고 자리도 그 안으로 끌어들였다. 그래서 작은 창 곁에 붙이는 카드나 창 밖으로 펼쳐지는 목록이 잘렸다. 이제 Windows의 popup 창과 같은 규칙이다.

- **자리.** 앵커 표면 내용의 왼쪽 위에서 `ui_popup`의 x·y만큼 떨어진 화면 자리에 그대로 선다. 창 밖으로 나가고, 그 자리가 가장 많이 걸친 화면의 작업 영역(메뉴 막대·Dock을 뺀 곳) 안으로만 민다. 작업 영역보다 긴 판은 그 높이로 줄고 휠로 흘린다. 하위 메뉴는 오른쪽 끝을 넘으면 앵커 popup의 왼쪽에 선다. 이 계산은 AppKit을 모르는 [`place_popup_on_screen`](../src/macos/window_rules.h)이고, Win32 `popup_screen_bounds`와 같은 [`clamp_popup_position`](../src/host/popup_reconcile.h)을 쓴다. 보정한 최종 자리는 `ui_popup::placed`가 앵커 기준 논리 픽셀로 받는다.
- **창 관계.** 판은 앵커 창(주 창·보조 창·도킹 표면·앵커 popup)의 자식 창이다. 그래서 그 창 위에 서고, 함께 움직이고, 함께 숨는다. 전체 화면 공간에도 따라 선다. 층(level)은 앵커 창의 것을 따른다. 늘 위에 뜨는 주 창의 popup도 그 위에 선다.
- **활성.** 판은 비활성화 판(`NSWindowStyleMaskNonactivatingPanel`)이다. 키 창·주 창이 되지 않는다. 띄우거나 눌러도 앱이 활성화되지 않고 주 창의 키 상태를 빼앗지 않는다 (Win32 `WS_EX_NOACTIVATE`·`MA_NOACTIVATE`). 키보드는 앵커 쪽 키 창이 받고, 논리 초점이 popup 안의 칸으로 보낸다. 입력기의 후보 창은 판의 view 좌표로 선다 ([popup-ime](popup-ime-design.md)와 같은 길).
- **배율.** 판의 그림은 앵커 표면의 배율로 그린다. 판의 tree는 앱이 앵커의 배율로 배치했기 때문이다 (Win32와 같다).
- **모양.** `ui_popup::corner`·`border`는 [`resolve_popup_shape`](../src/host/popup_reconcile.h)로 푼다. 둥근 판은 판 그릇 layer의 `cornerRadius`(`popup_corner_logical_radius`)로 자른다. 경계는 그 layer의 1px 테두리를 팔레트의 `tooltip_border`로 긋는다 (Win32의 DWM 테두리 색과 같은 값). 직각 판은 표면이 1px 선을 긋는다. 그림자는 시스템 창 그림자이고, 그린 알파의 모양을 따른다. 크기나 모양이 바뀐 뒤 처음 그린 장으로 다시 잰다.
- **투명.** `ui_popup::transparent`면 바탕을 비우고 그림자·테두리·배경 효과를 두지 않는다. 알파 0인 자리의 누름은 시스템이 뒤 창으로 보낸다.
- **배경 효과.** 판은 활성화되지 않는 표면이라 `acrylic`(HUD 재질)만 선다. mica는 서지 않는다. 판의 상태는 알리지 않는다 (Win32와 같다).
- **입력.** 판의 사건에는 popup id가 붙는다 (흘린 만큼 옮긴 popup 좌표). 누른 판 밖으로 끌어도 이동·뗌은 그 판으로 간다. AppKit의 암묵적 캡처가 Win32의 `SetCapture`와 같다. 포인터가 판을 떠나면 그 판에 hover 이탈이 간다. 판 위의 커서 모양은 판의 tree가 정한다.
- **닫힘 계기.**

  | 계기 | macOS |
  | --- | --- |
  | `pointer_press_outside` | popup이 아닌 표면(주 창·보조 창·도킹 표면)의 누름. caption 띠를 누른 것도 들어간다. 누름 자체는 그 표면으로 간다 |
  | `wheel_scrolled` | popup이 아닌 표면의 휠 |
  | `escape_key` | 키 창에서 누른 Esc. 맨 위의 자식 popup 하나만 묻는다. 아무도 닫지 않으면 Esc는 앱의 키 라우팅으로 간다 |
  | `surface_moved` | 창 하나가 움직임. caption 끌기에서는 걸음마다 온다. 남는 판은 앵커를 따라 다시 선다 |
  | `surface_resized` | 창 하나의 크기가 바뀜 |
  | `activation_changed` | 창이 키 창을 잃음 (다른 앱이나 다른 창으로 넘어감). 비활성 앱에 띄운 판에는 오지 않는다. 이것도 Win32의 비활성 창과 같다 |
  | `anchor_closed` | 앵커가 사라짐 |

  한 번 내기 규칙은 [`take_popup_dismiss_action`](../src/host/popup_dismiss.h)이다. 새 frame이 오면 표식을 푼다.
- **접근성.** 판마다 VoiceOver 다리 하나가 서서 popup tree를 판 하나의 겹으로 건넨다 (흘린 양을 함께 본다).
- **웹뷰.** 판은 웹뷰 위의 따로 선 창이라 웹뷰를 가리지 않는다. 겹쳐 그리던 때의 "popup과 겹치면 웹뷰를 숨긴다"는 이제 없다.

## 2. 주 창의 모양

- **테두리 없는 창.** `NSWindowStyleMaskBorderless | Resizable | Miniaturizable`이다. 제목 막대와 창 단추가 없다. 가장자리를 끌어 크기를 바꾸는 것은 시스템이 한다. `ui_platform::window_caption`이 참이 되어 `caption_element`가 자리를 차지하고 그린다. caption을 그리는가는 플랫폼에 하나뿐이라, 보조 창도 같은 모양이 된다 (Windows처럼 창마다 앱이 caption을 그린다).
- **키 입력.** 주 창과 독립 창은 `canBecomeKeyWindow`·`canBecomeMainWindow`가 늘 참인 `LuilWindow`다. 테두리 없는 소유 보조 창도 키 창이 되는 판이다. 앱이 스스로 `styleMask`를 바꾼 창도 키 입력을 받는다.
- **투명 바탕.** `layered`면 창이 불투명하지 않고 Skia가 바탕을 알파 0으로 비운다 (`frame_state::transparent`). tree가 그린 알파대로 뒤가 비친다. 그래서 앱이 판을 칠한다 (예제는 둥근 `panel_element`). 둥근 모서리(`corner_radius`)만 주면 바탕은 불투명하고 모서리 바깥만 비친다. 배경 효과(`ui_frame::backdrop`)를 함께 주면 그 효과 view도 같은 모서리로 잘린다.
- **층·공간.** `topmost`는 시작 값이다. 실행 중에는 앱이 `ui_frame::topmost`를 싣고 바뀐 frame에서 옮긴다. 소유된 보조 창은 늘 주 창보다 한 층 위다. `all_spaces`는 `CanJoinAllSpaces | FullScreenAuxiliary`다.
- **활성화 정책.** 시작할 때 한 번 정한다. `automatic`은 번들의 `LSUIElement`를 따른다. 예전처럼 늘 `Regular`로 정해 Info.plist를 덮어쓰지 않으므로, 앱이 나중에 바꿀 때 Dock 아이콘이 잠깐 보였다 사라지는 일이 없다.

## 3. 직접 그린 caption의 끌기

테두리 없는 창의 왼쪽 단추 누름은 tree보다 host가 먼저 본다. Win32 비클라이언트 판정(`WM_NCHITTEST`)과 같은 규칙이고, 판정은 AppKit을 모르는 [`custom_caption_hit`](../src/macos/window_rules.h)이다.

1. 게시된 tree에서 `caption_element`가 선 띠와 그 설정(치수·창 단추)으로 [`caption_layout`](../src/host/caption_layout.h)을 짓는다 (`caption_band_in`, `make_caption_layout`, `hit_test_caption`).
2. 창 단추 자리는 그 창의 최소화·확대·닫기다. 누른 단추 위에서 뗄 때만 실행한다 (`caption_click_executes`와 같다).
3. 끌기 자리에 `caption_client` 표시를 단 element(앱 단추·검색 칸)가 서 있으면 그 자리는 tree의 것이다.
4. 나머지 띠는 창 끌기다. 두 번 누르면 시스템 설정 "윈도우 제목 막대를 이중 클릭하여"(`AppleActionOnDoubleClick`)를 따른다 (확대·최소화·없음).
5. tree에 `caption_element`가 없으면 끌 자리가 없다. Windows는 맨 위 띠를 기본으로 두지만, macOS의 테두리 없는 창은 앱이 그린 머릿줄의 단추를 끌기로 빼앗지 않게 caption을 둔 창만 끈다.

포인터 이동은 그대로 tree에 가므로 창 단추의 hover와 tooltip은 tree가 그린다. 띠를 누르는 것도 popup 밖의 누름이다.

**끌기.** 창 끌기는 host가 포인터를 따라 직접 옮긴다 (`performWindowDrag`가 아니다). 걸음마다 주 창이면 `win32::window_delegate::on_window_moving(host, bounds)`가 불린다. 이것이 Win32 `WM_MOVING`의 자리다. `bounds`는 창 바깥 사각형이 갈 자리이고, 앱이 고친 왼쪽 위로 간다 (크기는 그대로다). 화면 작업 영역 안으로 맞추기, 머릿줄 위치를 기준으로 맞추기에 쓴다. 배치는 끌기 끝에 한 번 알린다 (Windows와 같다). 확대된 창은 시스템 끌기 문턱(3점)만큼 움직인 뒤 돌아갈 크기로 돌아와 포인터 밑에 선다. 잡은 자리의 가로 비율을 지킨다. 누르기만 한 것(두 번 누르기의 첫 누름)은 끌기가 아니다.

## 4. 비활성 앱의 hover

Skia view의 포인터 추적(`NSTrackingArea`)은 `inactive_hover`(기본 참)면 `NSTrackingActiveAlways`다. 앱이 앞에 있지 않아도 보이는 창이면 포인터 이동·hover·`hover_dwell`이 온다 (Windows의 `WM_MOUSEMOVE`와 같다). 거짓이면 예전처럼 `NSTrackingActiveInActiveApp`이다. popup 판과 도킹 표면도 같은 값을 따른다. 그래서 늘 위의 도구 창이 뒤에 있는 동안에도 머무름 카드가 선다.

## 5·6. 처음 자리와 자리 보고

- **처음 자리**는 저장 배치(`initial_placement`), 왼쪽 위 자리(`initial_position`), 주 화면 가운데의 차례로 정한다. 저장 배치는 Windows와 같은 규칙으로 거른다 ([`accepted_initial_placement`](../src/macos/window_rules.h)). 크기가 0 이하이거나, 끝이 32-bit 화면 좌표를 넘거나(`placement_in_screen_range`와 같은 식), 최소 크기(점 × 주 화면 배율)보다 작거나, 어느 화면에도 걸치지 않으면 통째로 버리고 다음 차례를 따른다. 받은 배치가 확대였으면 창은 처음부터 확대로 서고, 전체 화면이었으면 창이 보인 뒤 들어간다. 값은 보고로 받은 그대로다 (x·y는 창 내용의 왼쪽 위, 크기는 내용의 픽셀, 확대·전체 화면이면 돌아갈 자리). `initial_position`은 창 바깥 사각형의 왼쪽 위이고 그 화면의 보이는 영역 안으로 민다.
- **보고.** 처음 자리를 정하는 동안의 이동·크기 변경은 알리지 않는다. 가운데 놓기의 `windowDidMove`, 저장 배치의 `setFrame:`, 앱이 `on_native_window_created`에서 고친 창이 여기에 든다. 창이 보인 뒤 실제 자리를 한 번 알린다. 그 전에 나간 가운데 자리를 앱이 저장해 두었던 배치에 덮어쓰지 않게 하려는 것이다. 첫 frame의 배치 요청(`window_placement_request`)도 그동안은 옮기기만 한다.

## 7. 확대 보고

제목 막대가 없는 창의 확대는 host가 든다. `NSWindow.isZoomed`는 그런 창에서 참을 돌려준다 (`styleMask`를 Borderless로 바꾼 창, 높이가 작은 창 포함). 그래서 예전에는 `maximized = true`에 옛 보통 자리를 실어 보고했다. 이제 그 판정에 기대지 않는다.

- 제목 막대가 있는 창은 시스템 확대(`zoom:`)와 `isZoomed`다.
- 제목 막대가 없는 창의 확대(`ui_command::window_toggle_maximize`, caption 확대 단추, 두 번 누르기)는 그 화면의 보이는 영역을 채우고 돌아갈 자리를 든다. `maximized`는 앱이 확대한 동안만 참이고, 나머지는 실제 사각형을 보고한다.

## 8. 시스템 제목 막대의 caption 앱 단추

제목 막대가 있는 창(`borderless`가 아닌 창)에서 `caption_element`는 자리를 차지하지 않는다. 그래서 예전에는 `caption_config::app_buttons`가 macOS에서 사라졌다. 이제 host가 게시된 tree의 `caption_element` 설정을 읽어, 그 창 제목 막대의 오른쪽 끝 부속 view(`NSTitlebarAccessoryViewController`)에 같은 단추를 세운다.

- 글리프는 codicon을 틀 그림(template)으로 그린다. 시스템이 제목 막대의 색으로 칠한다.
- tooltip과 접근성 이름은 `tooltip`이고, 켬 상태는 `enabled`다. 보조 기술이 읽는 요소는 단추의 셀이라 이름을 셀에도 단다.
- 누르면 지금 tree의 같은 key 단추의 액션을 낸다 (tree에서 누른 것과 같다).
- frame마다 읽는다. 단추를 더하거나, 빼거나, 바꾸거나, 끄면 다음 frame부터 선다.

`borderless`면 앱이 caption 전체(제목·창 단추·앱 단추)를 그린다. 어느 쪽이든 앱은 같은 `caption_element`를 tree에 둔다. 플랫폼마다 갈라 그리지 않는다.

## 아직 없는 것

- Win32는 `ui_frame::topmost`를 읽지 않고 `on_window_moving`을 부르지 않는다 (이 변경은 Windows 소스를 건드리지 않았다).
- popup 판의 위·아래 화살표 칸(Win32 `scroll_up_strip`)은 없다. 작업 영역보다 긴 판은 휠로만 흘린다.
- 실제 커서로는 확인하지 않은 것: 테두리 없는 창의 가장자리 크기 조절, 비활성 앱 위의 실제 포인터 hover(대본은 앱 큐에 넣은 사건이라 추적 영역을 거치지 않는다. 추적 옵션은 `tracking`으로 본다), 투명 창에서 알파 0인 자리의 누름 통과.

## 검증

- core test [`macos_window_rules_tests.cpp`](../tests/macos_window_rules_tests.cpp): 저장 배치 거르기, caption 판정(창 단추·앱 단추·끌기 띠·caption이 아래에 선 tree·caption 없는 tree), 두 번 누르기 설정, 끌기 걸음과 앱이 고친 자리, popup의 화면 자리(창 밖·화면 끝 밀기·하위 메뉴 뒤집기·긴 판).
- 창 test(CTest 표지 `window`): 예제 `luil_tool_window`를 띄워 [입력 대본](../tests/macos)을 돌린다. [`run_window_script.cmake`](../tests/macos/run_window_script.cmake)가 앱의 `luil tool:`·`luil script:` 줄을 차례대로 본다.

  | test | 항목 |
  | --- | --- |
  | `luil_macos_window_popups` | 1. 창 밖의 판, 키 창·활성 유지, Esc·바깥 누름 닫힘, 비활성 앱의 머무름 카드 |
  | `luil_macos_window_look`, `luil_macos_window_active_hover` | 2·4. 테두리 없음·비치는 창·늘 위·모든 공간·accessory, 추적 옵션 |
  | `luil_macos_window_caption` | 3·7. 끌기와 끌기 중 자리 고치기, 끝에 한 번 알림, 두 번 누르기·확대 단추와 그 보고, 앱 단추의 `topmost` |
  | `luil_macos_window_placement`, `_zoomed`, `_offscreen` | 5·6. 저장 배치로 열기, 처음 자리 한 번 알림, 화면 밖 배치 거르기 |
  | `luil_macos_window_titlebar` | 8. 제목 막대의 앱 단추(글리프·tooltip·이름·누름) |

- 기존 demo의 drop-down·카드 메뉴·메뉴 막대 메뉴를 대본으로 열어 판으로 서는 것, Esc·바깥 누름에 닫히는 것, 판 위 hover 강조를 `snapshot <png> <popup id>`로 확인했다.
