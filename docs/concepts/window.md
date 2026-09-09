# Win32 창과 표면

[`run_application_window`](../../include/luil/win32/win32_window.h)는 Win32 메시지와 luil 스레드 모델 사이를 연결한다. UI thread는 메시지를 raw event로 번역하고, 게시된 frame/tree/snapshot을 모아 그리며, 창 조작·clipboard·TSF·파일 dialog처럼 UI thread가 필요한 작업을 실행한다.

## 표면

창 종류별 procedure를 복제하지 않고 공통 `window_surface`에 입력·cursor·focus·drop·render 동작을 둔다. `caption_surface`는 custom caption과 비클라이언트 hit test를 더하고, `main_surface`, `secondary_surface`, `popup_surface`가 각 창의 수명과 배치를 조립한다. 표면마다 HWND, renderer, tree, scale, id가 있다. 주 창 id는 비어 있다.

popup은 `ui_popup::anchor`가 가리키는 표면의 client 좌표·소유자·배율을 따른다. anchor가 비어 있으면 주 창이며, anchor 표면이 사라지면 popup도 만들지 않는다. popup은 `WS_EX_NOACTIVATE`라 keyboard focus를 받지 않으므로 anchor의 TSF session이 연결된 popup tree를 함께 본다.

파일 drop은 각 표면의 `IDropTarget`이 받는다. 수락과 drop 액션은 UI 스레드가 게시된 tree에 동기적으로 질의한다. 수락 element가 없을 때만 window delegate로 전달한다. 표면은 앱 소유 상태를 알지 못하고 `surface_context`로 host, frame, palette, popup 목록, wake와 action callback을 빌린다.

## Popup 닫힘

UI thread는 다음 계기를 `ui_popup::dismiss(reason)`으로 전달한다.

| 계기 | reason |
| --- | --- |
| popup 밖 pointer press | `pointer_press_outside` |
| anchor 내용의 wheel | `wheel_scrolled` |
| Esc | `escape_key` |
| 표면 이동 | `surface_moved` |
| 표면 resize | `surface_resized` |
| activation 변경 | `activation_changed` |

빈 dismiss callback은 해당 계기에 popup을 유지한다. 어떤 popup도 Esc를 닫지 않으면 키를 삼키지 않고 앱 정책으로 보낸다.

## Frame과 repaint

`WM_PAINT`가 `window_surface::render()`를 호출하는 유일한 그림 경로다. frame·interaction 게시나 `WM_SIZE`, `WM_DPICHANGED`, 테마 변경, timer는 표면을 무효화한다. render는 최신 frame/tree/interaction, client rect, maximized 상태, DPI, 고대비를 결합해 `frame_state`를 만들고 글꼴 설정을 반영한다. 실제 팔레트는 `draw_frame()`이 합성한다. surface별 interaction은 표면 경계에서 필터링하고 caption hover를 그 뒤에 합친다.

caption button은 `caption_config::buttons`, `caption_layout`, `window_style_for()`가 같은 목록을 사용해야 그림·hit test·Win32 style이 일치한다. 없는 버튼은 자리와 style을 모두 차지하지 않는다. `WS_THICKFRAME`과 `WS_SYSMENU`는 버튼 목록과 독립이다.

창 배치는 `WM_EXITSIZEMOVE`, maximized/restored 전환, 전체 화면 전환, `WM_CLOSE`에서 delegate에 보고한다. 앱이 복원할 배치는 `window_placement_revision`이 새로울 때만 적용한다. 창 또는 renderer 생성이 실패하면 `show_startup_error`가 디버그 출력과 메시지 상자를 내며 smoke 모드에서는 상자를 생략한다. 상자의 제목과 앞글은 `window_config::startup_error`로 앱이 정한다.

## 전체 화면

전체 화면은 **테두리 없는 창**이다. 제시가 DirectComposition 합성 swap chain을 지나므로 독점 전체 화면(exclusive fullscreen)은 설 수 없다 — 합성 swap chain은 `SetFullscreenState`를 받지 못한다. 그래서 유일하게 가능한 길인 "`MONITORINFO::rcMonitor`를 그대로 덮는 창"을 구현한다. rcWork를 쓰는 다른 계산(최대화 한계, popup 다듬기, 보조 창 초기 배치)과 달리 여기만 rcMonitor를 목표로 삼는다.

**주 창 전용이다.** 배치 저장·복원과 같은 범위이며 보조 창과 popup은 대상이 아니다. 앱은 [`ui_command::window_toggle_fullscreen`](../../include/luil/ui/ui_events.h)만 내고 어느 모니터를 덮을지, 나올 때 어디로 돌아갈지는 UI thread가 든다.

창이 지금 어떤 모습인지는 [`window_display_mode`](../../src/win32/window_mode.h) 한 값이다. 창 스타일, 비클라이언트 판정, `WM_GETMINMAXINFO`의 최대화 크기, DWM 프레임이 모두 이 값을 본다. 특히 **스타일은 버튼 집합과 모드를 함께 받는 하나의 계산**이다. `caption_surface::set_caption`이 버튼이 바뀔 때마다 `GWL_STYLE`을 통째로 다시 쓰기 때문에, 전체 화면이 스타일을 나중에 덧칠하는 방식이면 caption이 바뀌는 아무 frame에서나 그 덧칠이 지워진다.

전체 화면인 동안의 차이는 다음과 같다.

| 자리 | 통상·최대화 | 전체 화면 |
| --- | --- | --- |
| 창 스타일 | `WS_THICKFRAME`, 버튼에 따른 `WS_MAXIMIZEBOX` | 둘 다 없음 (`WS_SYSMENU`·`WS_MINIMIZEBOX`는 남는다) |
| 비클라이언트 판정 | 테두리·모서리·시스템 메뉴·버튼·끌기 | 전부 `HTCLIENT` |
| `ptMaxSize` | 모니터 작업 영역(rcWork)으로 제한 | 건드리지 않음 |
| DWM 프레임 | `MARGINS`의 위쪽 1px | 0 (그 한 줄이 화면 맨 위의 이음매가 된다) |

크기 조절을 판정만으로 막지 않는 이유는 Alt+Space의 시스템 메뉴와 Win+↑가 그 판정을 지나지 않기 때문이다. 스타일에서 함께 빼야 화면과 상태가 갈라지지 않는다.

들어갈 때 갈무리하는 것은 `GWL_EXSTYLE`와 `WINDOWPLACEMENT` 전부(`rcNormalPosition`과 `showCmd`)이고, 그 뒤 `SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE`로 모니터 사각형에 옮긴다. 갈무리에 실패하면 들어가지 않는다 — 돌아갈 자리를 모르는 전체 화면은 창을 되찾을 길이 없는 상태다. 나올 때 그 둘을 그대로 되돌리므로 최대화된 창에서 들어가면 최대화된 창으로 나온다.

**`GWL_STYLE`은 갈무리하지 않는다.** 들어갈 때가 버튼 집합과 모드로 계산해 세우는 값이므로 나올 때도 같은 계산이다(`custom_window_style_for(buttons, normal)`). 갈무리한 비트를 되돌리면 전체 화면인 동안 앱이 바꾼 버튼 집합이 그 순간 뒤집힌다 — 전체 화면에서 최대화 버튼을 접은 앱이 나올 때 `WS_MAXIMIZEBOX`를 되찾아, 캡션은 그 버튼을 그리지 않는데 Win+↑·캡션 더블클릭·시스템 메뉴의 최대화만 사는 창이 된다. `set_caption`은 버튼 집합이 바뀔 때만 `GWL_STYLE`을 다시 쓰므로 그 어긋남은 저절로 지워지지도 않는다.

다시 계산한 값에 `WS_MAXIMIZE`가 없다는 것이 나올 때의 **자리**도 정한다. 최대화된 창에서 전체 화면에 들어가면 그 표식은 내내 남고(자리를 직접 준 `SetWindowPos`는 지우지 않는다), 남은 채로 `SetWindowPlacement`에 `SW_SHOWMAXIMIZED`를 주면 OS가 이미 최대화된 창으로 보아 사각형을 다시 세우지 않을 수 있다 — 창이 화면을 덮은 크기 그대로 앉는다. 계산한 스타일을 통째로 쓰는 것이 곧 그 표식을 지우는 일이라, 나올 때의 자리는 OS의 그 모서리에 기대지 않는다.

## 전체 화면과 모니터 변화

덮는 사각형은 **들어갈 때 한 번 재고 마는 값이 아니다.** 모니터 사각형이 바뀌면 창도 따라가야 "rcMonitor를 덮는 창"이라는 말이 계속 참이다. 그러지 않은 창은 여전히 테두리가 없고 판정도 어디나 `HTCLIENT`라, 사용자가 크기를 되돌릴 길이 전체 화면 해제밖에 없다.

| 메시지 | 전체 화면 | 통상·최대화 |
| --- | --- | --- |
| `WM_DPICHANGED` | 제안 사각형을 **버리고** 지금 모니터의 rcMonitor를 다시 구해 앉힌다 | OS가 제안한 사각형을 적용한다 |
| `WM_DISPLAYCHANGE` | 지금 모니터의 rcMonitor를 다시 구해 앉힌다 | 아무것도 하지 않는다 |

`WM_DPICHANGED`의 제안 사각형은 지금 창 사각형에 배율 비를 곱한 값이다. 1920x1080 모니터를 100%에서 150%로 바꾸면 모니터는 그대로인데 창만 2880x1620이 되므로 전체 화면 창에는 쓸 수 없다. 배율 값 갱신·metrics 게시·popup 다시 맞추기는 두 길이 똑같이 지나고, 달라지는 것은 사각형을 어디서 얻는가뿐이다.

`WM_DISPLAYCHANGE`는 해상도 변경과 모니터 연결·해제다. 전체 화면이 아니면 아무것도 하지 않는다 — 통상 창을 화면이 바뀔 때마다 옮기는 것은 사용자의 몫이고, 화면 밖으로 나간 창을 다듬는 것은 이미 OS가 한다.

## 전체 화면과 저장되는 배치

`window_placement::fullscreen`이 참이면 나머지 값은 지금 창의 사각형이 아니라 **돌아갈 자리**다. `maximized`와는 겹쳐 쓴다: 둘 다 참이면 "지금은 전체 화면, 나오면 최대화된 창"이다. 그래서 한 줄을 저장했다가 그대로 다시 넣으면 전체 화면으로 열리면서 돌아갈 자리도 함께 산다.

전체 화면인 동안 OS의 관측값은 쓸 수 없다. 들어갈 때의 `SetWindowPos`가 `rcNormalPosition`을 모니터 사각형으로 덮어쓰고, 같은 순간의 `WM_SIZE`는 `SIZE_RESTORED`로 와서 최대화 표식까지 지운다. 무엇을 알릴지는 [`placement_to_report`](../../src/win32/window_mode.h)가 정한다 — 갈무리해 둔 값이 있으면 그것을, 없으면 관측값을 그대로 답한다. 전체 화면 전환의 `WM_SIZE`는 최대화 전환으로 세지 않고 명령이 직접 보고한다.

복원 요청이 전체 화면이면 **정상 배치를 먼저 놓고 창이 보인 뒤에** 들어간다. 순서가 뒤집히면 돌아갈 자리로 갈무리되는 것이 요청한 값이 아니라 지금 창의 자리가 된다.

앱에는 두 길로 알린다. 배치 메시지의 `fullscreen`은 tree를 다시 짓는 데 쓰고(캡션 줄을 접는다), `frame_state`에서 `draw_context::fullscreen`으로 흐르는 값은 그리기가 본다. `maximized`와 `fullscreen`은 함께 참이 되지 않는다 — 표면이 모드 하나에서 둘을 함께 세운다.

## 반드시 유지할 불변식

- 창의 모습은 `window_display_mode` 한 값이고, 스타일·hit test·크기 한계·DWM 프레임이 모두 그 값을 본다.
- 창 스타일은 버튼 집합과 모드를 함께 받는 하나의 계산이다. 스타일을 나중에 덧칠하지 않는다.
- 전체 화면에 들어갈 때와 나올 때가 그 계산 하나다. 나올 때 갈무리한 스타일 비트를 되돌리지 않는다.
- 전체 화면 창은 **지금** 모니터의 rcMonitor를 덮는다. 들어선 순간만이 아니라 배율(`WM_DPICHANGED`)과 화면(`WM_DISPLAYCHANGE`)이 바뀐 뒤에도 그렇다.
- 전체 화면인 창은 어느 자리도 비클라이언트가 아니다.
- 전체 화면 동안 보고되는 배치는 모니터 사각형이 아니라 돌아갈 자리다.
- 전체 화면은 주 창만의 것이다.
- caption button은 `caption_config::buttons`, `caption_layout`, `window_style_for()`가 같은 목록을 본다.

## 검증

[`tests/window_mode_tests.cpp`](../../tests/window_mode_tests.cpp)는 모드별 스타일과 hit 결과, 모니터 목표 사각형, 보고할 배치의 왕복을 창 없이 검증한다. 전체 화면에서 나올 때의 스타일도 여기서 못 박는다 — 전체 화면인 동안 버튼 집합이 바뀐 경우와, 그 계산이 `WS_MAXIMIZE`를 만들지 않는다는 것까지다. [`tests/win32_window_tests.cpp`](../../tests/win32_window_tests.cpp)는 실제 창에서 전체 화면의 hit이 전부 `HTCLIENT`인지와 창 수명을 검증하고, [`tests/caption_layout_tests.cpp`](../../tests/caption_layout_tests.cpp)는 caption 자리 계산이 두 판정에서 하나임을 검증한다.
