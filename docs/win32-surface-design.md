# Win32 window surface

`window_surface`는 main window, secondary top-level window, popup을 포함한 모든 drawable window의 공통 Win32 경계다. 하나의 `HWND`에 속해야 하는 resource를 소유하고 application 전체 coordination은 `surface_context`에 위임한다.

내부 interface는 [`src/win32/window_surface.h`](../src/win32/window_surface.h)에 선언되어 있다. Application API가 아니라 platform maintainer를 위한 구현 계약이다.

## Surface 책임

`window_surface`는 다음을 소유하거나 추적한다.

- `HWND`, 안정적인 surface id, 현재 DPI
- 현재 surface에 지정된 immutable tree
- renderer 하나와 window 크기의 presentation resource
- mouse-leave tracking과 pointer message 변환
- keyboard focus를 받을 수 있을 때 TSF session 하나
- UI Automation fragment root 하나
- 활성화된 경우 OLE file-drop target 하나

Main surface id는 빈 string이다. Secondary window와 popup은 application-defined id를 쓴다. 같은 id가 raw input에 붙어 downstream hit testing, focus, drag state, repaint timing, WebView routing이 올바른 tree를 선택하게 한다.

`attach_window`는 `HWND`를 결합하고 window별 service를 등록한다. `detach_window`는 UI Automation과 OLE object를 revoke하고, window 파괴 전에 TSF를 reset하며 renderer와 handle을 해제한다. Detach 뒤 platform object가 operational surface를 보유해서는 안 된다.

## `surface_context`

Surface끼리는 서로 알지 않으며 owner를 downcast하지 않는다. 다음 공유 기능은 `surface_context`를 통해 호출한다.

- `app_host`, immutable `window_config`, interaction policy
- process-wide DirectComposition device
- WebView synchronization, layout, pointer relay
- 현재 appearance와 typeface 선택
- popup dismissal과 anchor 이동
- action과 whole-window file-drop dispatch
- surface 간 repaint scheduling과 focus 알림
- pointer-capture ownership과 runtime error 보고

Application-window 구현이 process 또는 window-set concern을 소유하므로 popup과 secondary surface가 main window procedure state를 복사하지 않고 공통 기능을 쓸 수 있다.

## 공통 message 처리

`handle_surface_message`는 모든 surface에서 의미가 같은 message를 변환한다. Pointer movement와 button, wheel, capture loss, cursor, background erase, paint, focus transition, UI Automation object request, OLE drag state가 공통 경로를 쓴다.

반환값은 optional `LRESULT`다. 값이 있으면 공통 계층이 처리했으며 `nullopt`이면 concrete surface가 main-window, caption, popup, sizing 또는 lifecycle 동작을 계속 처리한다.

Pointer coordinate는 physical client pixel이다. Wheel의 screen coordinate는 수신 surface의 client coordinate로 바꾼다. Double-click message는 일반 두 번째 press가 되며 click count 해석은 interaction controller가 맡는다. Modifier 없는 printable character는 `WM_CHAR`로, named key와 modifier가 있는 character key는 공통 key vocabulary로 전달한다.

Pointer capture는 `surface_context`가 관리하는 process-global state다. `SetCapture`가 실제로 성공했을 때만 cancellation을 준비한다. 자체 `ReleaseCapture` 전에 해제 상태로 바꾸는데, Win32가 `WM_CAPTURECHANGED`를 동기적으로 보내기 때문이다. 다른 window가 capture를 가져가면 tree 밖 release를 합성해 click 없이 press와 drag state를 정리한다.

## Rendering

각 surface는 자체 `skia_renderer`를 생성하고 resize한다. Draw 전에 frame palette, high-contrast state, font setting, surface별 interaction, caption state, text-input state를 `frame_state`로 합친다.

Direct3D backend는 shared DirectComposition device를 받지만 window별 swap-chain과 visual resource를 만든다. CPU rendering은 window별 bitmap presentation path다. Main window는 application presentation 실패를 종료로 처리할 수 있고 auxiliary surface는 자신만 report하고 닫을 수 있다.

WebView hole은 arranged tree만 clipped rectangle을 알기 때문에 draw 직전에 결정한다. CPU surface에는 composition underlay가 없으므로 WebView를 숨기고 transparent hole을 만들지 않는다.

Draw 뒤 owner는 모든 surface의 가장 이른 `next_update`를 schedule한다. 각 tree에는 해당 surface로 filter한 interaction state를 제공한다. 다른 surface의 focus나 hover를 재사용하면 caret과 animation update를 잘못 억제할 수 있다.

## Text input

`surface_tsf_host`는 surface의 TSF document를 shared interaction 및 application pipeline에 연결한다. Focusable surface는 session 하나를 가진다. 해당 surface가 active이거나 여기에 anchor된 no-activate popup이 logical text focus를 가질 때만 text target을 보고한다.

Committed document와 text rectangle은 현재 published tree에서 온다. Composition과 edit request는 일반 input 또는 application action이 된다. TSF setup이 실패해도 기본 character path와 system composition UI를 통해 surface를 사용할 수 있다.

WebView가 Win32 focus를 소유하면 surface는 TSF를 WebView2에 양보한다. Logical focus는 계속 `webview_element`를 가리켜 focus visual과 keyboard traversal을 일관되게 유지한다.

## 접근성

Attached surface마다 UI Automation fragment root 하나를 공개할 수 있다. Provider는 surface의 현재 tree에서 element id를 찾아 frame 교체 뒤에도 element pointer 없이 살아남는다. Focus, bounding rectangle, navigation, action, text range, change event는 같은 surface-local tree와 physical-to-screen 변환을 쓴다.

접근성 action은 `access_dispatch_message`로 queue한 뒤 window의 정상 순서에서 dispatch한다. COM은 modal message loop나 drawing 중 재진입할 수 있으므로 deferral이 callback 중 window 파괴를 막는다.

## File drop

Main과 secondary caption surface는 `window_config::accept_file_drop`에 따라 file-drop을 등록하지만 popup은 등록하지 않는다. Drag hover는 input pump에 post하고 완료된 OLE drop은 현재 surface tree를 동기적으로 조회해 선택한 element action을 dispatch한다. 자세한 계약은 [OS 파일 drag & drop](os-dragdrop-design.md)에 있다.

## 창 모드

`caption_surface`는 custom caption과 함께 **창이 서 있는 모습**(`window_display_mode`)도 소유한다. 이미 창 style, 확장 style, `WM_GETMINMAXINFO`, DWM 프레임을 그 자리가 맡고 있어 전체 화면 진입·이탈이 손대야 하는 것이 전부 여기 모여 있기 때문이다.

판단은 창을 모르는 [`window_mode.h`](../src/win32/window_mode.h)에 있고 surface는 그 답을 Win32에 옮긴다. Style(버튼 집합 + 모드), 비클라이언트 hit, 모니터 목표 사각형, 앱에 보고할 배치가 전부 그쪽의 순수 함수다. `caption_layout`, `popup_reconcile`, `surface_invalidate`와 같은 규칙이며 창 없이 test가 선다.

전체 화면은 main surface만 사용한다. Secondary surface와 popup은 언제나 통상 모드다.

## Concrete surface 역할

Main application window는 `window_surface`에 `app_host`, popup과 secondary surface collection, shared DirectComposition device, WebView, system appearance, timer, process shutdown ownership을 더한다.

`secondary_surface`는 custom-caption command, live metrics 보고, application-controlled close request를 추가한다. `popup_surface`는 no-activate 동작, anchor 배치, popup dismissal rule을 추가한다. Input, rendering, TSF, 접근성, drag/drop은 `window_surface`의 공통 구현을 사용한다.

## 검증

[`tests/surface_input_tests.cpp`](../tests/surface_input_tests.cpp)는 message 변환과 surface tag를 검증하고 [`tests/surface_invalidate_tests.cpp`](../tests/surface_invalidate_tests.cpp)는 repaint 판단을 검증하며 [`tests/window_mode_tests.cpp`](../tests/window_mode_tests.cpp)는 창 모드의 style·hit·목표 사각형·보고 배치를 검증한다. Secondary, popup, accessibility, renderer, text-input, WebView, OLE test는 공통 surface 경계에 붙은 service를 반복 검증한다.
