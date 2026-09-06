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

창 배치는 `WM_EXITSIZEMOVE`, maximized/restored 전환, `WM_CLOSE`에서 delegate에 보고한다. 앱이 복원할 배치는 `window_placement_revision`이 새로울 때만 적용한다. 창 또는 renderer 생성이 실패하면 `show_startup_error`가 디버그 출력과 메시지 상자를 내며 smoke 모드에서는 상자를 생략한다. 상자의 제목과 앞글은 `window_config::startup_error`로 앱이 정한다.
